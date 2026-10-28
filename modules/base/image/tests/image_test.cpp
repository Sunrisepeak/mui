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

}  // namespace

int main(int argc, char** argv) {
    Suite s{"image"};

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
    });

    return s.run(argc, argv);
}
