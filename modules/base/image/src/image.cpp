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

std::expected<Image, std::string> load_png(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot read {}", generic_utf8(file)));
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decode_png(bytes);
}

}  // namespace omui::imageio
