module openmui.image;

import std;

namespace omui::imageio {

namespace {

// UTF-8 text of a path (generic separators for `generic_utf8`), never
// through the process's code page: std::filesystem::path::string() throws on
// Windows for a name the ANSI code page cannot spell (lint rule `paths`).
[[maybe_unused]] std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
[[maybe_unused]] std::string generic_utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

}  // namespace

View view(const Image& img) noexcept { return {img.rgba.data(), img.width, img.height, img.width * 4, img.alpha}; }

void premultiply(Image& img) {
    if (img.alpha == Alpha::premultiplied) return;
    for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        std::uint32_t a = img.rgba[i + 3];
        for (int c = 0; c < 3; ++c) {
            std::uint32_t t = img.rgba[i + static_cast<std::size_t>(c)] * a + 128;
            img.rgba[i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>((t + (t >> 8)) >> 8);
        }
    }
    img.alpha = Alpha::premultiplied;
}

void unpremultiply(Image& img) {
    if (img.alpha == Alpha::straight) return;
    for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        std::uint32_t a = img.rgba[i + 3];
        for (int c = 0; c < 3; ++c) {
            std::uint32_t v = img.rgba[i + static_cast<std::size_t>(c)];
            img.rgba[i + static_cast<std::size_t>(c)] = a == 0 ? 0 : static_cast<std::uint8_t>(std::min<std::uint32_t>(255, (v * 255 + a / 2) / a));
        }
    }
    img.alpha = Alpha::straight;
}

std::expected<void, std::string> save_png(const std::filesystem::path& file, const View& pixels) {
    auto bytes = encode_png(pixels);
    if (!bytes) return std::unexpected(bytes.error());
    std::ofstream out(file, std::ios::binary);
    if (!out) return std::unexpected(std::format("cannot write {}", generic_utf8(file)));
    out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!out) return std::unexpected(std::format("cannot write {}", generic_utf8(file)));
    return {};
}

std::expected<void, std::string> save_apng(const std::filesystem::path& file, std::span<const View> frames, ApngOptions options) {
    auto bytes = encode_apng(frames, options);
    if (!bytes) return std::unexpected(bytes.error());
    std::ofstream out(file, std::ios::binary);
    if (!out) return std::unexpected(std::format("cannot write {}", generic_utf8(file)));
    out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!out) return std::unexpected(std::format("cannot write {}", generic_utf8(file)));
    return {};
}

std::expected<Image, std::string> load_png(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot read {}", generic_utf8(file)));
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decode_png(bytes);
}

namespace {

std::uint32_t crc32(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b = {}) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xffffffffu;
    for (const auto part : {a, b})
        for (const std::uint8_t x : part) c = table[(c ^ x) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>(v >> s));
}
void be16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, std::string_view type, std::span<const std::uint8_t> data) {
    be32(out, static_cast<std::uint32_t>(data.size()));
    const std::array<std::uint8_t, 4> t{static_cast<std::uint8_t>(type[0]), static_cast<std::uint8_t>(type[1]), static_cast<std::uint8_t>(type[2]),
                                        static_cast<std::uint8_t>(type[3])};
    out.insert(out.end(), t.begin(), t.end());
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc32(t, data));
}

// A PNG's chunks of one type, their data in order.
std::vector<std::span<const std::uint8_t>> chunks(std::span<const std::uint8_t> png, std::string_view type) {
    std::vector<std::span<const std::uint8_t>> found;
    for (std::size_t at = 8; at + 12 <= png.size();) {
        const std::uint32_t n = (std::uint32_t{png[at]} << 24) | (std::uint32_t{png[at + 1]} << 16) | (std::uint32_t{png[at + 2]} << 8) | png[at + 3];
        if (at + 12 + n > png.size()) break;
        if (std::string_view(reinterpret_cast<const char*>(png.data() + at + 4), 4) == type) found.push_back(png.subspan(at + 8, n));
        at += 12 + n;
    }
    return found;
}

struct Box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // [x0, x1) × [y0, y1); empty: nothing changed
    [[nodiscard]] bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
};

// Where `b` differs from `a`.
Box changed(const View& a, const View& b) {
    Box box{b.width, b.height, 0, 0};
    for (int y = 0; y < b.height; ++y) {
        const std::uint8_t* ra = a.rgba + static_cast<std::size_t>(y) * a.stride;
        const std::uint8_t* rb = b.rgba + static_cast<std::size_t>(y) * b.stride;
        if (std::memcmp(ra, rb, static_cast<std::size_t>(b.width) * 4) == 0) continue;
        int l = 0, r = b.width;
        while (l < r && std::memcmp(ra + l * 4, rb + l * 4, 4) == 0) ++l;
        while (r > l && std::memcmp(ra + (r - 1) * 4, rb + (r - 1) * 4, 4) == 0) --r;
        box.x0 = std::min(box.x0, l), box.x1 = std::max(box.x1, r);
        box.y0 = std::min(box.y0, y), box.y1 = std::max(box.y1, y + 1);
    }
    return box;
}

}  // namespace

std::expected<std::vector<std::uint8_t>, std::string> encode_apng(std::span<const View> frames, ApngOptions options) {
    if (frames.empty()) return std::unexpected("no frames");
    const View& first = frames.front();
    for (const View& f : frames)
        if (f.width != first.width || f.height != first.height || !f.rgba) return std::unexpected("every frame is the size of the first");
    if (options.delay_den == 0) return std::unexpected("a delay's denominator is not 0");
    // The frames as shown: each with the rectangle it changes and how many delays it lasts.
    struct Shown {
        std::size_t   frame;
        Box           box;
        std::uint32_t length;
    };
    std::vector<Shown> shown{{0, Box{0, 0, first.width, first.height}, 1}};
    for (std::size_t i = 1; i < frames.size(); ++i) {
        const Box b = changed(frames[i - 1], frames[i]);
        if (b.empty()) ++shown.back().length;
        else shown.push_back({i, b, 1});
    }
    auto png = encode_png(first);
    if (!png) return std::unexpected(png.error());
    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    const auto ihdr = chunks(*png, "IHDR");
    if (ihdr.empty()) return std::unexpected("png: no IHDR");
    chunk(out, "IHDR", ihdr.front());
    std::vector<std::uint8_t> data;
    be32(data, static_cast<std::uint32_t>(shown.size()));
    be32(data, options.loops);
    chunk(out, "acTL", data);
    std::uint32_t sequence = 0;
    for (std::size_t k = 0; k < shown.size(); ++k) {
        const Shown& s = shown[k];
        const View& f = frames[s.frame];
        const View part{f.rgba + static_cast<std::size_t>(s.box.y0) * f.stride + static_cast<std::size_t>(s.box.x0) * 4, s.box.x1 - s.box.x0,
                        s.box.y1 - s.box.y0, f.stride, f.alpha};
        if (k > 0) {
            png = encode_png(part);
            if (!png) return std::unexpected(png.error());
        }
        const std::uint32_t num = static_cast<std::uint32_t>(options.delay_num) * s.length;
        if (num > 0xffff) return std::unexpected("a frame lasts too long for its delay's numerator");
        data.clear();
        be32(data, sequence++);
        be32(data, static_cast<std::uint32_t>(part.width));
        be32(data, static_cast<std::uint32_t>(part.height));
        be32(data, static_cast<std::uint32_t>(s.box.x0));
        be32(data, static_cast<std::uint32_t>(s.box.y0));
        be16(data, static_cast<std::uint16_t>(num));
        be16(data, options.delay_den);
        data.push_back(0);  // dispose: none (the next frame draws over this one)
        data.push_back(0);  // blend: source (its pixels replace what is there)
        chunk(out, "fcTL", data);
        for (const auto idat : chunks(*png, "IDAT")) {
            if (k == 0) {
                chunk(out, "IDAT", idat);
                continue;
            }
            data.clear();
            be32(data, sequence++);
            data.insert(data.end(), idat.begin(), idat.end());
            chunk(out, "fdAT", data);
        }
    }
    chunk(out, "IEND", {});
    return out;
}

}  // namespace omui::imageio
