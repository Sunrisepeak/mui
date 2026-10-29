import std;
import openmui.image;
import openmui.testing;

using namespace omui::testing;
namespace image = omui::imageio;

namespace {

std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t at) {
    return (std::uint32_t{b[at]} << 24) | (std::uint32_t{b[at + 1]} << 16) | (std::uint32_t{b[at + 2]} << 8) | b[at + 3];
}

// An APNG played back: each shown frame composited over the one before (the
// test's own reader: chunks into single-frame PNGs that decode_png reads),
// with how many delays it lasts.
std::vector<std::pair<image::Image, std::uint32_t>> play(std::span<const std::uint8_t> apng) {
    std::vector<std::pair<image::Image, std::uint32_t>> shown;
    std::vector<std::uint8_t> ihdr;
    image::Image canvas;
    std::array<std::uint32_t, 5> fc{};  // w, h, x, y, delay numerator
    std::vector<std::uint8_t> data;
    auto flush = [&] {
        if (data.empty()) return;
        // A PNG of this frame alone: the IHDR with its size, its data as IDAT.
        std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
        auto put = [&](std::string_view type, std::vector<std::uint8_t> body) {
            for (int s = 24; s >= 0; s -= 8) png.push_back(static_cast<std::uint8_t>(body.size() >> s));
            std::vector<std::uint8_t> crc_in(type.begin(), type.end());
            crc_in.insert(crc_in.end(), body.begin(), body.end());
            png.insert(png.end(), crc_in.begin(), crc_in.end());
            std::uint32_t c = 0xffffffffu;
            for (std::uint8_t x : crc_in) {
                c ^= x;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            c ^= 0xffffffffu;
            for (int s = 24; s >= 0; s -= 8) png.push_back(static_cast<std::uint8_t>(c >> s));
        };
        std::vector<std::uint8_t> h = ihdr;
        for (int i = 0; i < 4; ++i) h[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(fc[0] >> (24 - 8 * i)), h[static_cast<std::size_t>(4 + i)] = static_cast<std::uint8_t>(fc[1] >> (24 - 8 * i));
        put("IHDR", h);
        put("IDAT", data);
        put("IEND", {});
        auto part = image::decode_png(png);
        check(part.has_value(), part ? "" : part.error());
        if (!part) return;
        for (std::uint32_t y = 0; y < fc[1]; ++y)
            std::memcpy(canvas.rgba.data() + ((fc[3] + y) * static_cast<std::uint32_t>(canvas.width) + fc[2]) * 4, part->rgba.data() + y * fc[0] * 4, fc[0] * 4);
        shown.emplace_back(canvas, fc[4]);
        data.clear();
    };
    for (std::size_t at = 8; at + 12 <= apng.size();) {
        const std::uint32_t n = be32(apng, at);
        const std::string_view type(reinterpret_cast<const char*>(apng.data() + at + 4), 4);
        const auto body = apng.subspan(at + 8, n);
        if (type == "IHDR") {
            ihdr.assign(body.begin(), body.end());
            canvas.width = static_cast<int>(be32(body, 0));
            canvas.height = static_cast<int>(be32(body, 4));
            canvas.rgba.assign(static_cast<std::size_t>(canvas.width) * canvas.height * 4, 0);
        } else if (type == "fcTL") {
            flush();
            fc = {be32(body, 4), be32(body, 8), be32(body, 12), be32(body, 16), (std::uint32_t{body[20]} << 8) | body[21]};
        } else if (type == "IDAT") {
            data.insert(data.end(), body.begin(), body.end());
        } else if (type == "fdAT") {
            data.insert(data.end(), body.begin() + 4, body.end());
        }
        at += 12 + n;
    }
    flush();
    return shown;
}

// FNV-1a over the pixels: a pin that names a whole picture.
std::uint64_t fnv(std::span<const std::uint8_t> b) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::uint8_t x : b) h = (h ^ x) * 0x100000001b3ull;
    return h;
}

// A picture like art or a photograph: smooth colour with a little grain
// (what lossy coding is for), alpha rising left to right, and a clear band
// on the left whose colours must survive (exact).
image::Image test_picture(int w, int h) {
    image::Image img;
    img.width = w, img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    std::uint32_t grain = 12345;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto px = img.rgba.begin() + (static_cast<std::ptrdiff_t>(y) * w + x) * 4;
            const std::array<int, 3> c{x * 255 / (w - 1), y * 255 / (h - 1), 255 - (x + y) * 255 / (w + h - 2)};
            for (std::size_t k = 0; k < 3; ++k) {
                grain = grain * 1664525u + 1013904223u;
                px[static_cast<std::ptrdiff_t>(k)] = static_cast<std::uint8_t>(std::clamp(c[k] + static_cast<int>(grain >> 29) - 4, 0, 255));
            }
            px[3] = x < w / 8 ? 0 : static_cast<std::uint8_t>(std::min(255, 64 + x * 255 / w));
        }
    return img;
}

// The frames of a moving box over a still background (frames 3 and 4 the
// same): what encode_apng's test draws, here for animated WebP too.
std::vector<image::Image> moving_box(int w = 64, int h = 40) {
    std::vector<image::Image> frames;
    for (int f = 0; f < 6; ++f) {
        image::Image img;
        img.width = w, img.height = h;
        img.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
        for (std::size_t i = 0; i < img.rgba.size(); i += 4) img.rgba[i] = 30, img.rgba[i + 1] = 40, img.rgba[i + 2] = 60, img.rgba[i + 3] = 255;
        const int x = 4 + 8 * std::min(f, 3) + (f >= 5 ? 8 : 0);
        for (int y = 10; y < 22; ++y)
            for (int k = x; k < x + 12; ++k) {
                auto px = img.rgba.begin() + (static_cast<std::ptrdiff_t>(y) * w + k) * 4;
                px[0] = 240, px[1] = 200, px[2] = 80;
            }
        frames.push_back(std::move(img));
    }
    return frames;
}

// A README-like loop: a soft shaded panel (a gradient with a little grain, as
// the renderer's antialiased UI and art are) and an antialiased disc moving
// over it, a caption bar that blinks.
std::vector<image::Image> readme_loop(int w, int h, int count) {
    std::vector<image::Image> frames;
    for (int f = 0; f < count; ++f) {
        image::Image img = test_picture(w, h);
        for (std::size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
        const double cx = 30 + (w - 60) * f / double(count - 1), cy = h / 2.0 + 30 * std::sin(f * 0.4), r = 22;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const double d = std::sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
                const double a = std::clamp(r - d + 0.5, 0.0, 1.0);
                auto px = img.rgba.begin() + (static_cast<std::ptrdiff_t>(y) * w + x) * 4;
                const std::array<double, 3> disc{250, 120, 40};
                for (std::size_t c = 0; c < 3; ++c) px[static_cast<std::ptrdiff_t>(c)] = static_cast<std::uint8_t>(std::lround(px[static_cast<std::ptrdiff_t>(c)] * (1 - a) + disc[c] * a));
                if (y >= h - 16 && (f / 4) % 2 == 0 && x >= 8 && x < w / 2) px[0] = px[1] = px[2] = 245;
            }
        frames.push_back(std::move(img));
    }
    return frames;
}

std::vector<image::View> views_of(const std::vector<image::Image>& frames) {
    std::vector<image::View> v;
    for (const auto& f : frames) v.push_back(image::view(f));
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"image"};

    s.test("a piece's long axis: a slanted bar's ends and its half thickness", [] {
        omui::imageio::Image img{.width = 100, .height = 100, .alpha = omui::imageio::Alpha::straight, .rgba = std::vector<std::uint8_t>(100 * 100 * 4, 0)};
        // A bar 60 long and 10 thick, from (20, 20) toward (62, 62) (45°).
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x) {
                const double u = ((x + 0.5 - 20) + (y + 0.5 - 20)) / std::sqrt(2.0), v = std::abs((x + 0.5 - 20) - (y + 0.5 - 20)) / std::sqrt(2.0);
                if (u >= 0 && u <= 60 && v <= 5) img.rgba[(static_cast<std::size_t>(y) * 100 + x) * 4 + 3] = 255;
            }
        const auto a = omui::imageio::principal_axis(img);
        const bool forward = a.from_x < a.to_x;
        const double fx = forward ? a.from_x : a.to_x, fy = forward ? a.from_y : a.to_y, tx = forward ? a.to_x : a.from_x, ty = forward ? a.to_y : a.from_y;
        check(std::hypot(fx - 21.3, fy - 21.3) < 3 && std::hypot(tx - 60.7, ty - 60.7) < 3, std::format("ends ({:.1f}, {:.1f}) and ({:.1f}, {:.1f})", fx, fy, tx, ty));
        check(a.half_width > 3.5 && a.half_width < 5.5, std::format("half width {:.1f}", a.half_width));
    });

    s.test("PNG round trip keeps every pixel", [] {
        image::Image img;
        img.width = 37;
        img.height = 19;
        img.rgba.resize(37 * 19 * 4);
        for (std::size_t i = 0; i < img.rgba.size(); ++i) img.rgba[i] = static_cast<std::uint8_t>((i * 131) ^ (i >> 3));
        auto png = image::encode_png(image::view(img));
        check(png.has_value());
        check(png->size() > 8 && (*png)[1] == 'P' && (*png)[2] == 'N' && (*png)[3] == 'G');
        auto back = image::decode_png(*png);
        check(back.has_value());
        check(back->width == 37 && back->height == 19);
        check(back->rgba == img.rgba);
    });

    s.test("premultiplied pixels are written straight", [] {
        image::Image img;
        img.width = 1;
        img.height = 1;
        img.alpha = image::Alpha::premultiplied;
        img.rgba = {64, 0, 0, 128};  // 50% of #800000 premultiplied
        auto back = image::decode_png(*image::encode_png(image::view(img)));
        check(back->rgba == std::vector<std::uint8_t>{128, 0, 0, 128});
        image::premultiply(*back);
        check(back->rgba == std::vector<std::uint8_t>{64, 0, 0, 128});
    });

    s.test("an animated PNG: frames as the rectangles that changed, still frames merged, played back to the same pixels", [] {
        // A square moving across a still background, standing for two frames in the middle.
        std::vector<image::Image> frames;
        for (int f = 0; f < 6; ++f) {
            image::Image img;
            img.width = 64;
            img.height = 40;
            img.rgba.assign(64 * 40 * 4, 0);
            for (std::size_t i = 0; i < img.rgba.size(); i += 4) img.rgba[i] = 30, img.rgba[i + 1] = 40, img.rgba[i + 2] = 60, img.rgba[i + 3] = 255;
            const int x = 4 + 8 * std::min(f, 3) + (f >= 5 ? 8 : 0);  // frames 3 and 4 the same
            for (int y = 10; y < 22; ++y)
                for (int k = x; k < x + 12; ++k) {
                    auto px = img.rgba.begin() + (y * 64 + k) * 4;
                    px[0] = 240, px[1] = 200, px[2] = 80;
                }
            frames.push_back(std::move(img));
        }
        std::vector<image::View> views;
        for (const auto& f : frames) views.push_back(image::view(f));
        auto apng = image::encode_apng(views, {.delay_num = 1, .delay_den = 10});
        check(apng.has_value(), apng ? "" : apng.error());
        if (!apng) return;
        // To a viewer without APNG it is the first frame.
        auto still = image::decode_png(*apng);
        check(still.has_value() && still->rgba == frames[0].rgba, "the first frame");
        const auto shown = play(*apng);
        check_eq(shown.size(), std::size_t{5});  // frames 3 and 4 are one, shown twice as long
        const std::array<std::size_t, 5> which{0, 1, 2, 3, 5};
        const std::array<std::uint32_t, 5> lasts{1, 1, 1, 2, 1};
        for (std::size_t k = 0; k < shown.size() && k < 5; ++k) {
            check(shown[k].first.rgba == frames[which[k]].rgba, std::format("frame {}", k));
            check_eq(shown[k].second, lasts[k]);
        }
        auto one = image::encode_apng(std::span(views).first(1));
        check(one.has_value() && play(*one).size() == 1, "one frame");
        image::Image other;
        other.width = 8, other.height = 8, other.rgba.assign(8 * 8 * 4, 0);
        const std::array<image::View, 2> mixed{views[0], image::view(other)};
        check(!image::encode_apng(mixed).has_value(), "every frame the size of the first");
    });

    s.test("cutting art out of a green sheet: keyed out without a fringe, its pieces found, joined across small gaps, in reading order", [] {
        image::Image sheet;
        sheet.width = 120, sheet.height = 80;
        sheet.rgba.resize(120 * 80 * 4);
        auto put = [&](int x, int y, std::array<std::uint8_t, 4> c) { std::copy(c.begin(), c.end(), sheet.rgba.begin() + (y * 120 + x) * 4); };
        for (int y = 0; y < 80; ++y)
            for (int x = 0; x < 120; ++x) put(x, y, {12, 250, 8, 255});  // a green screen, not quite pure
        for (int y = 10; y < 40; ++y)
            for (int x = 10; x < 40; ++x) put(x, y, {200, 30, 30, 255});  // a red piece
        for (int y = 44; y < 46; ++y)
            for (int x = 20; x < 24; ++x) put(x, y, {200, 30, 30, 255});  // its tassel, 4 px below
        for (int y = 12; y < 30; ++y)
            for (int x = 70; x < 110; ++x) put(x, y, {240, 240, 240, 255});  // a white piece
        put(5, 70, {0, 0, 0, 255});  // a speck
        for (int x = 70; x < 110; ++x) put(x, 30, {120, 200, 110, 255});  // an edge half green: keyed partly, despilled
        image::key_out(sheet);
        check(sheet.rgba[(0 * 120 + 0) * 4 + 3] == 0, "the screen clear");
        check(sheet.rgba[(20 * 120 + 20) * 4 + 3] == 255 && sheet.rgba[(20 * 120 + 20) * 4] == 200, "a piece kept as it was");
        const auto edge = std::span(sheet.rgba).subspan((30 * 120 + 80) * 4, 4);
        check(edge[1] <= std::max(edge[0], edge[2]), std::format("despilled: {} {} {}", edge[0], edge[1], edge[2]));
        const auto found = image::pieces(sheet, 8, 6, 20);
        check_eq(found.size(), std::size_t{2});  // the tassel joined to its piece, the speck left out
        if (found.size() == 2) {
            check(found[0].x == 10 && found[0].y == 10 && found[0].height == 36, std::format("{} {} {} {}", found[0].x, found[0].y, found[0].width, found[0].height));
            check(found[1].x == 70, "then the white one, right of it");
            const auto part = image::crop(sheet, found[0].x, found[0].y, found[0].width, found[0].height);
            check(part.width == 30 && part.height == 36 && part.rgba[3] == 255, "cropped");
        }
    });

    s.test("WebP: the first use from many threads at once (libwebp's setup made once)", [] {
        const auto img = test_picture(48, 32);
        std::array<std::uint64_t, 8> got{};
        auto one = [&](std::size_t t) {
            auto bytes = image::encode_webp(image::view(img), {.lossless = t % 2 == 1});
            auto back = bytes ? image::decode_webp(*bytes) : std::unexpected(bytes.error());
            got[t] = back ? fnv(back->rgba) : 0;
        };
        if constexpr (omui::testing::threads) {
            std::vector<std::jthread> threads;
            for (std::size_t t = 0; t < got.size(); ++t) threads.emplace_back(one, t);
        } else {
            for (std::size_t t = 0; t < got.size(); ++t) one(t);  // the Web row starts no threads
        }
        for (std::size_t t = 2; t < got.size(); ++t) check(got[t] == got[t % 2], std::format("thread {}", t));
        check(got[0] != 0 && got[1] != 0);
    });

    s.test("WebP lossless round trip keeps every pixel, colours under clear pixels too", [] {
        image::Image img;
        img.width = 37;
        img.height = 19;
        img.rgba.resize(37 * 19 * 4);
        for (std::size_t i = 0; i < img.rgba.size(); ++i) img.rgba[i] = static_cast<std::uint8_t>((i * 131) ^ (i >> 3));
        for (std::size_t i = 3; i < img.rgba.size(); i += 4 * 5) img.rgba[i] = 0;  // every fifth pixel clear, its colour kept
        auto webp = image::encode_webp(image::view(img), {.lossless = true});
        check(webp.has_value(), webp ? "" : webp.error());
        if (!webp) return;
        check(webp->size() > 12 && std::string_view(reinterpret_cast<const char*>(webp->data()), 4) == "RIFF" &&
              std::string_view(reinterpret_cast<const char*>(webp->data() + 8), 4) == "WEBP");
        auto back = image::decode_webp(*webp);
        check(back.has_value());
        check(back->width == 37 && back->height == 19 && back->alpha == image::Alpha::straight);
        check(back->rgba == img.rgba);
    });

    s.test("WebP lossy: close to the picture, smaller, its alpha exact", [] {
        const auto img = test_picture(96, 64);
        auto webp = image::encode_webp(image::view(img), {.quality = 90});
        check(webp.has_value(), webp ? "" : webp.error());
        if (!webp) return;
        check(webp->size() < image::encode_webp(image::view(img), {.lossless = true})->size(), "smaller than lossless");
        auto back = image::decode_webp(*webp);
        check(back.has_value() && back->width == 96 && back->height == 64);
        if (!back) return;
        double sum = 0;
        int worst = 0, alpha_off = 0;
        for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
            if (back->rgba[i + 3] != img.rgba[i + 3]) ++alpha_off;
            for (std::size_t c = 0; c < 3; ++c) {
                const int d = std::abs(int{back->rgba[i + c]} - int{img.rgba[i + c]});
                sum += d, worst = std::max(worst, d);
            }
        }
        const double mean = sum / (96.0 * 64 * 3);
        check(mean < 3.0, std::format("mean difference {:.2f}", mean));
        check(worst <= 24, std::format("worst difference {}", worst));
        check_eq(alpha_off, 0);  // alpha is coded losslessly beside lossy colour
    });

    s.test("WebP is deterministic: a file's pixels and an encoding's, pinned", [] {
        // A 16 × 16 lossy WebP with alpha, as this libwebp writes test_picture(16, 16) at quality 75.
        static constexpr std::array<std::uint8_t, 152> file{
            0x52, 0x49, 0x46, 0x46, 0x90, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x58,
            0x0a, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x41, 0x4c,
            0x50, 0x48, 0x22, 0x00, 0x00, 0x00, 0x01, 0x5f, 0x20, 0x10, 0x20, 0x24, 0x38, 0xf4, 0x3f, 0xa2,
            0x21, 0x22, 0x92, 0x1c, 0x10, 0x09, 0x40, 0xb8, 0x62, 0x8b, 0x2d, 0xb6, 0xd8, 0x62, 0x83, 0x8d,
            0x7a, 0x44, 0xff, 0x03, 0x94, 0xd3, 0xed, 0x17, 0x56, 0x50, 0x38, 0x20, 0x48, 0x00, 0x00, 0x00,
            0xb0, 0x02, 0x00, 0x9d, 0x01, 0x2a, 0x10, 0x00, 0x10, 0x00, 0x02, 0x00, 0x34, 0x25, 0xb0, 0x02,
            0x74, 0x73, 0x01, 0x78, 0x03, 0x6c, 0x03, 0xf4, 0x00, 0x0d, 0x07, 0x18, 0x7c, 0xd8, 0x00, 0xfe,
            0xfa, 0x93, 0x32, 0x41, 0x31, 0xb7, 0x44, 0x26, 0x8a, 0xb1, 0xf6, 0x88, 0x18, 0x93, 0xfd, 0x7f,
            0xf5, 0x11, 0x3d, 0x80, 0x10, 0x4c, 0x48, 0x2f, 0xb0, 0xce, 0x86, 0x9d, 0x33, 0xff, 0xe8, 0x76,
            0xfd, 0xff, 0xfa, 0x3c, 0x3d, 0x0a, 0x78, 0x00,
        };
        auto from_file = image::decode_webp(file);
        check(from_file.has_value() && from_file->width == 16 && from_file->height == 16);
        if (from_file) check_eq(fnv(from_file->rgba), 0x28d81b35ae3737a6ull);
        const auto img = test_picture(16, 16);
        auto bytes = image::encode_webp(image::view(img), {.quality = 75});
        check(bytes.has_value() && std::ranges::equal(*bytes, file), "the encoder writes the same bytes");
        const auto big = test_picture(96, 64);
        auto lossy = image::decode_webp(*image::encode_webp(image::view(big), {.quality = 80, .method = 6}));
        check(lossy.has_value());
        if (lossy) check_eq(fnv(lossy->rgba), 0xbdf44cf8f79c0179ull);
    });

    s.test("WebP: premultiplied pixels are written straight", [] {
        image::Image img;
        img.width = 1;
        img.height = 1;
        img.alpha = image::Alpha::premultiplied;
        img.rgba = {64, 0, 0, 128};  // 50% of #800000 premultiplied
        auto back = image::decode_webp(*image::encode_webp(image::view(img), {.lossless = true}));
        check(back.has_value() && back->rgba == std::vector<std::uint8_t>{128, 0, 0, 128});
    });

    s.test("WebP: bad input is an error, not a crash", [] {
        std::array<std::uint8_t, 4> junk{1, 2, 3, 4};
        auto bad = image::decode_webp(junk);
        check(!bad.has_value() && bad.error().starts_with("webp:"));
        check(!image::decode_webp({}).has_value(), "nothing");
        const auto img = test_picture(32, 32);
        for (const bool lossless : {false, true}) {
            auto good = *image::encode_webp(image::view(img), {.lossless = lossless});
            for (const std::size_t keep : {std::size_t{12}, std::size_t{30}, good.size() / 2, good.size() - 1})
                check(!image::decode_webp(std::span(good).first(keep)).has_value(), std::format("cut to {} of {}", keep, good.size()));
            auto scrambled = good;
            for (std::size_t i = 20; i < scrambled.size(); i += 3) scrambled[i] ^= 0x5a;
            (void)image::decode_webp(scrambled);  // an error or some pixels; only not a crash
        }
        check(!image::encode_webp(image::View{}).has_value(), "an empty image");
        image::Image huge;
        huge.width = 20000, huge.height = 1, huge.rgba.assign(20000 * 4, 255);
        auto too_wide = image::encode_webp(image::view(huge));
        check(!too_wide.has_value() && too_wide.error().starts_with("webp:"), too_wide ? "" : too_wide.error());
    });

    s.test("an animated WebP: shaped like an APNG, played back with its frames, delays, size and loops (a still frame merged)", [] {
        const auto frames = moving_box();
        const auto views = views_of(frames);
        auto webp = image::encode_animated_webp(views, {.delay_num = 1, .delay_den = 10, .loops = 3, .lossless = true});
        check(webp.has_value(), webp ? "" : webp.error());
        if (!webp) return;
        check(webp->size() > 12 && std::string_view(reinterpret_cast<const char*>(webp->data() + 8), 4) == "WEBP");
        auto back = image::decode_animated_webp(*webp);
        check(back.has_value(), back ? "" : back.error());
        if (!back) return;
        check(back->width == 64 && back->height == 40, std::format("{} × {}", back->width, back->height));
        check_eq(back->loops, std::uint32_t{3});
        check_eq(back->frames.size(), std::size_t{5});  // frames 3 and 4 are one, shown twice as long
        const std::array<std::size_t, 5> which{0, 1, 2, 3, 5};
        const std::array<std::uint32_t, 5> lasts{100, 100, 100, 200, 100};
        for (std::size_t k = 0; k < back->frames.size() && k < 5; ++k) {
            check(back->frames[k].image.rgba == frames[which[k]].rgba, std::format("frame {} exact", k));
            check_eq(back->frames[k].delay_ms, lasts[k]);
        }
        // A delay that is not whole milliseconds: the starts rounded, never drifting (1/12 s: 83, 84, 83, ...).
        auto twelfth = image::decode_animated_webp(*image::encode_animated_webp(std::span(views).first(4), {.lossless = true}));
        check(twelfth.has_value() && twelfth->frames.size() == 4);
        if (twelfth && twelfth->frames.size() == 4) {
            std::uint32_t total = 0;
            for (const auto& f : twelfth->frames) total += f.delay_ms, check(f.delay_ms == 83 || f.delay_ms == 84, std::format("{} ms", f.delay_ms));
            check_eq(total, std::uint32_t{333});  // 4/12 s
            check_eq(twelfth->loops, std::uint32_t{0});
        }
        auto one = image::decode_animated_webp(*image::encode_animated_webp(std::span(views).first(1)));
        check(one.has_value() && one->frames.size() == 1, "one frame");
        // A still WebP reads as an animation of one frame; an animated one is not a still.
        auto still = image::decode_animated_webp(*image::encode_webp(views[0], {.lossless = true}));
        check(still.has_value() && still->frames.size() == 1 && still->frames[0].image.rgba == frames[0].rgba, "a still WebP");
        auto not_still = image::decode_webp(*webp);
        check(!not_still.has_value() && not_still.error().contains("decode_animated_webp"), not_still ? "" : not_still.error());
    });

    s.test("an animated WebP lossless keeps every pixel: alpha, and colours under clear pixels", [] {
        std::vector<image::Image> frames;
        for (int f = 0; f < 5; ++f) {
            image::Image img;
            img.width = 37, img.height = 19;
            img.rgba.resize(37 * 19 * 4);
            for (std::size_t i = 0; i < img.rgba.size(); ++i) img.rgba[i] = static_cast<std::uint8_t>((i * 131) ^ (i >> 3));
            for (std::size_t i = 3; i < img.rgba.size(); i += 4 * 5) img.rgba[i] = 0;  // every fifth pixel clear, its colour kept
            for (int y = 4; y < 12; ++y)  // a band that moves, part clear
                for (int x = f * 5; x < f * 5 + 9; ++x) {
                    auto px = img.rgba.begin() + (y * 37 + x) * 4;
                    px[0] = 10, px[1] = static_cast<std::uint8_t>(40 * f), px[2] = 200, px[3] = static_cast<std::uint8_t>(x % 3 == 0 ? 0 : 60 * x % 256);
                }
            frames.push_back(std::move(img));
        }
        const auto views = views_of(frames);
        auto webp = image::encode_animated_webp(views, {.lossless = true, .kmin = 2, .kmax = 3});  // key frames among them
        check(webp.has_value(), webp ? "" : webp.error());
        if (!webp) return;
        auto back = image::decode_animated_webp(*webp);
        check(back.has_value() && back->frames.size() == frames.size());
        if (!back || back->frames.size() != frames.size()) return;
        for (std::size_t k = 0; k < frames.size(); ++k) {
            check(back->frames[k].image.alpha == image::Alpha::straight);
            int off = 0;
            for (std::size_t i = 0; i < frames[k].rgba.size(); ++i) off += back->frames[k].image.rgba[i] != frames[k].rgba[i];
            check_eq(off, 0);
        }
        // Premultiplied frames are written straight.
        image::Image pm;
        pm.width = 1, pm.height = 1, pm.alpha = image::Alpha::premultiplied, pm.rgba = {64, 0, 0, 128};
        image::Image pm2 = pm;
        pm2.rgba = {0, 64, 0, 128};
        const std::array<image::View, 2> two{image::view(pm), image::view(pm2)};
        auto straight = image::decode_animated_webp(*image::encode_animated_webp(two, {.lossless = true}));
        check(straight.has_value() && straight->frames.size() == 2 && straight->frames[0].image.rgba == std::vector<std::uint8_t>{128, 0, 0, 128} &&
              straight->frames[1].image.rgba == std::vector<std::uint8_t>{0, 128, 0, 128});
    });

    s.test("an animated WebP lossy: every frame close to its picture, its alpha exact", [] {
        std::vector<image::Image> frames;
        for (int f = 0; f < 6; ++f) {
            image::Image img = test_picture(96, 64);  // alpha rising, a clear band
            for (int y = 20; y < 40; ++y)
                for (int x = 10 + f * 12; x < 30 + f * 12; ++x) {
                    auto px = img.rgba.begin() + (y * 96 + x) * 4;
                    px[0] = 250, px[1] = 250, px[2] = 250, px[3] = 255;
                }
            frames.push_back(std::move(img));
        }
        const auto views = views_of(frames);
        auto webp = image::encode_animated_webp(views, {.quality = 90});
        check(webp.has_value(), webp ? "" : webp.error());
        if (!webp) return;
        check(webp->size() < image::encode_animated_webp(views, {.lossless = true})->size(), "smaller than lossless");
        auto back = image::decode_animated_webp(*webp);
        check(back.has_value() && back->frames.size() == frames.size());
        if (!back || back->frames.size() != frames.size()) return;
        for (std::size_t k = 0; k < frames.size(); ++k) {
            const auto& got = back->frames[k].image.rgba;
            const auto& want = frames[k].rgba;
            double sum = 0;
            int worst = 0, alpha_off = 0;
            std::size_t counted = 0;
            for (std::size_t i = 0; i < want.size(); i += 4) {
                if (got[i + 3] != want[i + 3]) ++alpha_off;
                if (want[i + 3] == 0) continue;  // a clear pixel's colour is not seen
                ++counted;
                for (std::size_t c = 0; c < 3; ++c) {
                    const int d = std::abs(int{got[i + c]} - int{want[i + c]});
                    sum += d, worst = std::max(worst, d);
                }
            }
            const double mean = sum / (3.0 * static_cast<double>(counted));
            check(mean < 3.5, std::format("frame {}: mean difference {:.2f}", k, mean));
            check(worst <= 40, std::format("frame {}: worst difference {}", k, worst));
            check_eq(alpha_off, 0);
        }
    });

    s.test("an animated WebP is deterministic: an encoding's bytes and its played-back pixels, pinned", [] {
        const auto frames = moving_box(32, 24);
        const auto views = views_of(frames);
        auto lossy = image::encode_animated_webp(views, {.delay_num = 1, .delay_den = 10, .quality = 75});
        auto lossless = image::encode_animated_webp(views, {.delay_num = 1, .delay_den = 10, .lossless = true});
        check(lossy.has_value() && lossless.has_value());
        if (!lossy || !lossless) return;
        std::println("animated webp pins: lossy {} bytes {:#018x}, lossless {} bytes {:#018x}", lossy->size(), fnv(*lossy), lossless->size(), fnv(*lossless));
        check_eq(lossy->size(), std::size_t{578});
        check_eq(fnv(*lossy), 0x714e5b4e9b8c1e08ull);
        check_eq(lossless->size(), std::size_t{424});
        check_eq(fnv(*lossless), 0x14e8478fd980fb93ull);
        auto back = image::decode_animated_webp(*lossy);
        check(back.has_value());
        if (!back) return;
        std::vector<std::uint8_t> all;
        for (const auto& f : back->frames) all.insert(all.end(), f.image.rgba.begin(), f.image.rgba.end());
        std::println("animated webp pins: lossy played back {:#018x}", fnv(all));
        check_eq(fnv(all), 0x77dadea3c94e0960ull);
        check(image::encode_animated_webp(views, {.delay_num = 1, .delay_den = 10, .quality = 75}) == lossy, "the same again");
    });

    s.test("an animated WebP is much smaller than the APNG of the same loop", [] {
        const auto frames = readme_loop(320, 200, 36);
        const auto views = views_of(frames);
        auto apng = image::encode_apng(views, {.delay_num = 1, .delay_den = 12});
        auto lossy = image::encode_animated_webp(views, {.delay_num = 1, .delay_den = 12});
        auto lossless = image::encode_animated_webp(views, {.delay_num = 1, .delay_den = 12, .lossless = true});
        check(apng.has_value() && lossy.has_value() && lossless.has_value());
        if (!apng || !lossy || !lossless) return;
        std::println("320 × 200 × 36 frames: APNG {} bytes, animated WebP lossy (quality 80) {} bytes ({:.1f}×smaller), lossless {} bytes",
                     apng->size(), lossy->size(), double(apng->size()) / double(lossy->size()), lossless->size());
        const auto box = moving_box();
        auto box_apng = image::encode_apng(views_of(box));
        auto box_webp = image::encode_animated_webp(views_of(box));
        if (box_apng && box_webp) std::println("moving box 64 × 40 × 6: APNG {} bytes, animated WebP lossy {} bytes", box_apng->size(), box_webp->size());
        check(lossy->size() * 3 < apng->size(), "lossy WebP under a third of the APNG");
        auto back = image::decode_animated_webp(*lossy);
        check(back.has_value() && back->frames.size() == frames.size());
    });

    s.test("an animated WebP: bad input is an error, not a crash", [] {
        const auto frames = moving_box();
        const auto views = views_of(frames);
        check(!image::encode_animated_webp({}).has_value(), "no frames");
        image::Image other;
        other.width = 8, other.height = 8, other.rgba.assign(8 * 8 * 4, 0);
        const std::array<image::View, 2> mixed{views[0], image::view(other)};
        check(!image::encode_animated_webp(mixed).has_value(), "every frame the size of the first");
        check(!image::encode_animated_webp(views, {.delay_den = 0}).has_value(), "a zero denominator");
        check(!image::encode_animated_webp(views, {.delay_num = 1, .delay_den = 2000}).has_value(), "under a millisecond");
        check(!image::encode_animated_webp(views, {.loops = 70000}).has_value(), "too many loops");
        const std::array<image::View, 1> empty{image::View{}};
        check(!image::encode_animated_webp(empty).has_value(), "an empty image");
        image::Image huge;
        huge.width = 20000, huge.height = 1, huge.rgba.assign(20000 * 4, 255);
        const std::array<image::View, 1> wide{image::view(huge)};
        auto too_wide = image::encode_animated_webp(wide);
        check(!too_wide.has_value() && too_wide.error().starts_with("webp:"), too_wide ? "" : too_wide.error());
        std::array<std::uint8_t, 4> junk{1, 2, 3, 4};
        auto bad = image::decode_animated_webp(junk);
        check(!bad.has_value() && bad.error().starts_with("webp:"));
        check(!image::decode_animated_webp({}).has_value(), "nothing");
        for (const bool lossless : {false, true}) {
            auto good = *image::encode_animated_webp(views, {.lossless = lossless});
            for (const std::size_t keep : {std::size_t{12}, std::size_t{30}, good.size() / 2, good.size() - 1})
                check(!image::decode_animated_webp(std::span(good).first(keep)).has_value(), std::format("cut to {} of {}", keep, good.size()));
            auto scrambled = good;
            for (std::size_t i = 40; i < scrambled.size(); i += 3) scrambled[i] ^= 0x5a;
            (void)image::decode_animated_webp(scrambled);  // an error or some pixels; only not a crash
        }
    });

    s.test("files and errors", [] {
        auto dir = scratch_dir("image");
        image::Image img;
        img.width = 2;
        img.height = 2;
        img.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        check(image::save_png(dir / "a.png", image::view(img)).has_value());
        auto back = image::load_png(dir / "a.png");
        check(back && back->rgba == img.rgba);
        std::array<std::uint8_t, 4> junk{1, 2, 3, 4};
        auto bad = image::decode_png(junk);
        check(!bad.has_value() && bad.error().starts_with("png:"));
        check(!image::load_png(dir / "missing.png").has_value());
        check(image::save_webp(dir / "a.webp", image::view(img), {.lossless = true}).has_value());
        auto webp = image::load_webp(dir / "a.webp");
        check(webp && webp->rgba == img.rgba);
        // Either format, told by its signature.
        auto sniffed_png = image::load(dir / "a.png");
        auto sniffed_webp = image::load(dir / "a.webp");
        check(sniffed_png && sniffed_png->rgba == img.rgba, "load: a PNG");
        check(sniffed_webp && sniffed_webp->rgba == img.rgba, "load: a WebP");
        auto neither = image::decode(junk);
        check(!neither.has_value() && neither.error() == "not a PNG or WebP image");
        check(!image::load(dir / "missing.webp").has_value());
        const std::array<image::View, 2> two{image::view(img), image::view(*back)};
        check(image::save_animated_webp(dir / "anim.webp", two, {.lossless = true}).has_value());
        auto anim = image::load_animated_webp(dir / "anim.webp");
        // The same frame twice is one frame, and libwebp writes a single frame as a still WebP (no delay: 0).
        check(anim && anim->frames.size() == 1 && anim->frames[0].image.rgba == img.rgba && anim->frames[0].delay_ms == 0, "the same frame twice: one");
        auto as_still = image::load_webp(dir / "anim.webp");
        check(as_still && as_still->rgba == img.rgba, "a still");
        check(!image::load_animated_webp(dir / "missing.webp").has_value());
    });

    return s.run(argc, argv);
}
