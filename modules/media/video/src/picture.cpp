// RGB ↔ Y'CbCr 4:2:0 in integers, Y4M read back, PSNR.
module openmui.video;

import std;
import openmui.hash;
import hanim;
import openmui.image;

namespace omui::video {

namespace {

// BT.709 (Kr 0.2126, Kb 0.0722) at limited range, × 65536: Y' = 16 + 219/255
// · (Kr R + Kg G + Kb B); Cb, Cr = 128 + 224/255 · (B − Y) / 1.8556, (R − Y) /
// 1.5748. Each row of chroma sums to 0, so a grey has no colour; Y's sums to
// 219/255 · 65536, so white is 235.
constexpr std::int32_t yr = 11966, yg = 40254, yb = 4064;
constexpr std::int32_t ur = -6596, ug = -22188, ub = 28784;
constexpr std::int32_t vr = 28784, vg = -26145, vb = -2639;
static_assert(ur + ug + ub == 0 && vr + vg + vb == 0);
static_assert(((yr + yg + yb) * 255 + 16 * 65536 + 32768) >> 16 == 235);

// Back: R = 1.164383 (Y' − 16) + 1.792741 (Cr − 128), and so on, × 65536.
constexpr std::int32_t ky = 76309, krv = 117489, kgu = -13975, kgv = -34925, kbu = 138438;

// A pixel's colour over black: straight alpha multiplied in, rounded.
struct Rgb {
    std::int32_t r, g, b;
};
Rgb over_black(const std::uint8_t* p, imageio::Alpha alpha) noexcept {
    if (alpha == imageio::Alpha::premultiplied || p[3] == 255) return {p[0], p[1], p[2]};
    const std::int32_t a = p[3];
    return {(p[0] * a + 127) / 255, (p[1] * a + 127) / 255, (p[2] * a + 127) / 255};
}

std::uint8_t clamp8(std::int32_t x) noexcept { return static_cast<std::uint8_t>(std::clamp(x, 0, 255)); }

}  // namespace

void to_yuv420(const imageio::View& pixels, Yuv420& out) {
    const std::int32_t w = pixels.width, h = pixels.height;
    out.width = w;
    out.height = h;
    const std::int32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
    out.y.resize(static_cast<std::size_t>(w) * h);
    out.u.resize(static_cast<std::size_t>(cw) * ch);
    out.v.resize(static_cast<std::size_t>(cw) * ch);
    auto px = [&](std::int32_t x, std::int32_t y) { return over_black(pixels.rgba + static_cast<std::size_t>(y) * pixels.stride + static_cast<std::size_t>(x) * 4, pixels.alpha); };
    for (std::int32_t y = 0; y < h; ++y)
        for (std::int32_t x = 0; x < w; ++x) {
            const Rgb c = px(x, y);
            out.y[static_cast<std::size_t>(y) * w + x] = static_cast<std::uint8_t>((yr * c.r + yg * c.g + yb * c.b + 16 * 65536 + 32768) >> 16);
        }
    for (std::int32_t by = 0; by < ch; ++by)
        for (std::int32_t bx = 0; bx < cw; ++bx) {
            const std::int32_t x0 = 2 * bx, y0 = 2 * by;
            const std::int32_t x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            const Rgb a = px(x0, y0), b = px(x1, y0), c = px(x0, y1), d = px(x1, y1);
            const std::int32_t r = a.r + b.r + c.r + d.r, g = a.g + b.g + c.g + d.g, bl = a.b + b.b + c.b + d.b;
            // The sums are four pixels' (× 4), so 18 bits come off; the offset keeps it positive.
            out.u[static_cast<std::size_t>(by) * cw + bx] = static_cast<std::uint8_t>((ur * r + ug * g + ub * bl + 128 * 4 * 65536 + 2 * 65536) >> 18);
            out.v[static_cast<std::size_t>(by) * cw + bx] = static_cast<std::uint8_t>((vr * r + vg * g + vb * bl + 128 * 4 * 65536 + 2 * 65536) >> 18);
        }
}

Yuv420 to_yuv420(const imageio::View& pixels) {
    Yuv420 out;
    to_yuv420(pixels, out);
    return out;
}

imageio::Image to_rgba(const Yuv420& p) {
    imageio::Image img;
    img.width = p.width;
    img.height = p.height;
    img.rgba.resize(static_cast<std::size_t>(p.width) * p.height * 4);
    const std::int32_t cw = p.chroma_width();
    for (std::int32_t y = 0; y < p.height; ++y)
        for (std::int32_t x = 0; x < p.width; ++x) {
            const std::int32_t l = ky * (p.y[static_cast<std::size_t>(y) * p.width + x] - 16) + 32768;
            const std::int32_t u = p.u[static_cast<std::size_t>(y / 2) * cw + x / 2] - 128;
            const std::int32_t v = p.v[static_cast<std::size_t>(y / 2) * cw + x / 2] - 128;
            std::uint8_t* o = img.rgba.data() + (static_cast<std::size_t>(y) * p.width + x) * 4;
            o[0] = clamp8((l + krv * v) >> 16);
            o[1] = clamp8((l + kgu * u + kgv * v) >> 16);
            o[2] = clamp8((l + kbu * u) >> 16);
            o[3] = 255;
        }
    return img;
}

namespace {

double psnr_of(std::uint64_t squared, std::size_t count) {
    if (squared == 0) return std::numeric_limits<double>::infinity();
    const double mse = static_cast<double>(squared) / static_cast<double>(count);
    return 10 * hanim::dm::log(255.0 * 255.0 / mse) / 2.302585092994046;
}

std::uint64_t squared(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    std::uint64_t s = 0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const std::int64_t d = static_cast<std::int64_t>(a[i]) - b[i];
        s += static_cast<std::uint64_t>(d * d);
    }
    return s;
}

}  // namespace

Psnr psnr(const Yuv420& a, const Yuv420& b) {
    if (a.width != b.width || a.height != b.height) return {};
    const std::uint64_t y = squared(a.y, b.y);
    const std::uint64_t all = y + squared(a.u, b.u) + squared(a.v, b.v);
    return {psnr_of(y, a.y.size()), psnr_of(all, a.bytes())};
}

std::string y4m_header(std::int32_t width, std::int32_t height, hanim::FrameRate fps) {
    return std::format("YUV4MPEG2 W{} H{} F{}:{} Ip A1:1 C420jpeg XYSCSS=420JPEG XCOLORRANGE=LIMITED\n", width, height, fps.num, fps.den);
}

std::expected<Y4m, std::string> read_y4m(std::span<const std::byte> file) {
    const std::string_view text(reinterpret_cast<const char*>(file.data()), file.size());
    const auto eol = text.find('\n');
    if (!text.starts_with("YUV4MPEG2 ") || eol == std::string_view::npos) return std::unexpected("not a Y4M file (no YUV4MPEG2 header)");
    Y4m out;
    out.colour = "420jpeg";
    auto number = [](std::string_view s) -> std::optional<std::int64_t> {
        std::int64_t v = 0;
        const auto [p, e] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (e != std::errc{} || p != s.data() + s.size()) return std::nullopt;
        return v;
    };
    for (auto part : std::views::split(text.substr(10, eol - 10), ' ')) {
        const std::string_view tag(part.begin(), part.end());
        if (tag.empty()) continue;
        const std::string_view value = tag.substr(1);
        switch (tag[0]) {
            case 'W':
                if (auto v = number(value)) out.width = static_cast<std::int32_t>(*v);
                break;
            case 'H':
                if (auto v = number(value)) out.height = static_cast<std::int32_t>(*v);
                break;
            case 'F': {
                const auto colon = value.find(':');
                const auto n = number(value.substr(0, colon));
                const auto d = colon == std::string_view::npos ? std::optional<std::int64_t>{} : number(value.substr(colon + 1));
                if (!n || !d || *n <= 0 || *d <= 0) return std::unexpected(std::format("a frame rate of `{}`", value));
                out.fps = {*n, *d};
                break;
            }
            case 'I':
                if (value != "p") return std::unexpected("interlaced Y4M is not read");
                break;
            case 'C':
                out.colour = std::string(value);
                if (!value.starts_with("420")) return std::unexpected(std::format("colour `{}`: only 4:2:0 is read", value));
                if (value.size() > 4 && value.starts_with("420p") && value[4] >= '0' && value[4] <= '9') return std::unexpected(std::format("colour `{}`: only 8 bits are read", value));
                break;
            default: break;
        }
    }
    if (out.width <= 0 || out.height <= 0) return std::unexpected("a Y4M file without its size");
    const std::size_t luma = static_cast<std::size_t>(out.width) * out.height;
    const std::size_t chroma = static_cast<std::size_t>((out.width + 1) / 2) * ((out.height + 1) / 2);
    std::size_t at = eol + 1;
    while (at < text.size()) {
        const auto end = text.find('\n', at);
        if (end == std::string_view::npos || !text.substr(at).starts_with("FRAME")) return std::unexpected(std::format("frame {}: no FRAME line", out.frames.size()));
        at = end + 1;
        if (text.size() - at < luma + 2 * chroma) return std::unexpected(std::format("frame {} is cut short", out.frames.size()));
        Yuv420 f;
        f.width = out.width;
        f.height = out.height;
        const auto* b = reinterpret_cast<const std::uint8_t*>(text.data()) + at;
        f.y.assign(b, b + luma);
        f.u.assign(b + luma, b + luma + chroma);
        f.v.assign(b + luma + chroma, b + luma + 2 * chroma);
        out.frames.push_back(std::move(f));
        at += luma + 2 * chroma;
    }
    return out;
}

std::uint64_t hash(std::span<const std::byte> bytes) noexcept {
    return hash::fnv1a(bytes);
}

std::uint64_t hash(std::span<const std::uint8_t> bytes) noexcept { return hash(std::as_bytes(bytes)); }

std::optional<hanim::FrameRate> parse_rate(std::string_view text) noexcept {
    auto whole = [](std::string_view s) -> std::optional<std::int64_t> {
        std::int64_t v = 0;
        const auto [p, e] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (e != std::errc{} || p != s.data() + s.size() || v <= 0) return std::nullopt;
        return v;
    };
    if (const auto slash = text.find('/'); slash != std::string_view::npos) {
        const auto n = whole(text.substr(0, slash)), d = whole(text.substr(slash + 1));
        if (!n || !d) return std::nullopt;
        return hanim::FrameRate{*n, *d};
    }
    if (const auto n = whole(text)) return hanim::FrameRate{*n, 1};
    // The NTSC rates by their usual names.
    if (text == "23.976" || text == "23.98") return hanim::FrameRate{24000, 1001};
    if (text == "29.97") return hanim::FrameRate{30000, 1001};
    if (text == "59.94") return hanim::FrameRate{60000, 1001};
    return std::nullopt;
}

std::string rate_text(hanim::FrameRate rate) { return rate.den == 1 ? std::format("{}", rate.num) : std::format("{}/{}", rate.num, rate.den); }

}  // namespace omui::video
