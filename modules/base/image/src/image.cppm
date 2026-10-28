// openmui.image — images in memory, PNG, and animated PNG.
//
// An Image is 8-bit RGBA, rows top to bottom, either straight or
// premultiplied alpha (the renderer works premultiplied; files are straight).
// PNG goes through libpng's simplified API (D-56). Decoding returns an error
// message rather than throwing; writing is deterministic for a given libpng
// and zlib, which mcpp.lock pins.
export module openmui.image;

import std;

export namespace omui::imageio {

enum class Alpha : std::uint8_t { straight, premultiplied };

struct Image {
    int                       width = 0;
    int                       height = 0;
    Alpha                     alpha = Alpha::straight;
    std::vector<std::uint8_t> rgba;  // width × height × 4

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] std::span<std::uint8_t> row(int y) noexcept {
        return {rgba.data() + static_cast<std::size_t>(y) * width * 4, static_cast<std::size_t>(width) * 4};
    }
    [[nodiscard]] std::span<const std::uint8_t> row(int y) const noexcept {
        return {rgba.data() + static_cast<std::size_t>(y) * width * 4, static_cast<std::size_t>(width) * 4};
    }
};

// Pixels borrowed from elsewhere (a raster surface).
struct View {
    const std::uint8_t* rgba = nullptr;
    int                 width = 0;
    int                 height = 0;
    int                 stride = 0;  // bytes per row
    Alpha               alpha = Alpha::premultiplied;
};

[[nodiscard]] View view(const Image& img) noexcept;

// In place; exact inverses where alpha is 255 or 0, rounded to nearest elsewhere.
void premultiply(Image& img);
void unpremultiply(Image& img);

struct PngOptions {
    bool sixteen_bit = false;  // reserved: 8 bits per channel is written
};

[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> encode_png(const View& pixels, PngOptions options = {});
[[nodiscard]] std::expected<Image, std::string> decode_png(std::span<const std::uint8_t> bytes);

[[nodiscard]] std::expected<void, std::string> save_png(const std::filesystem::path& file, const View& pixels);

// An animated PNG (APNG): every frame the size of the first, shown `delay`
// seconds (num / den) each; `loops` 0 plays forever. Each frame after the
// first is stored as the rectangle that changed from the one before (the rest
// kept), and a frame equal to the one before lengthens it instead: a loop
// over a still background costs what moves. Viewers without APNG show the
// first frame. Deterministic like encode_png.
struct ApngOptions {
    std::uint16_t delay_num = 1, delay_den = 12;
    std::uint32_t loops = 0;
};
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> encode_apng(std::span<const View> frames, ApngOptions options = {});
[[nodiscard]] std::expected<void, std::string> save_apng(const std::filesystem::path& file, std::span<const View> frames, ApngOptions options = {});
[[nodiscard]] std::expected<Image, std::string> load_png(const std::filesystem::path& file);

}  // namespace omui::imageio
