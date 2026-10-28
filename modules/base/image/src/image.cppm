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

// ── cutting art out of a sheet (drawn or generated on a flat background) ──

// Keys out a background colour (a green or blue screen): each pixel's alpha
// from how far its chroma (YCbCr's Cb, Cr) is from the key's — clear within
// `inner`, opaque beyond `outer` — and the key's colour taken out of what is
// left (despill), so edges carry no fringe. Straight alpha in and out.
struct Key {
    std::uint8_t r = 0, g = 255, b = 0;
    float        inner = 30, outer = 70;  // chroma distances (0–255 scale)
};
void key_out(Image& img, Key key = {});

// The pieces of a picture: the bounds of each connected region of pixels with
// alpha above `alpha`, pieces closer than `gap` pixels joined (a tassel to
// its sleeve), smaller than `min_area` pixels left out; in reading order
// (top to bottom by rows of pieces, then left to right).
struct Piece {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    std::int64_t area = 0;  // its pixels
};
// `labels` (when given): each pixel's piece, an index into the answer (−1: none).
[[nodiscard]] std::vector<Piece> pieces(const Image& img, std::uint8_t alpha = 8, std::int32_t gap = 6, std::int64_t min_area = 64,
                                        std::vector<std::int32_t>* labels = nullptr);
// A picture made smaller (each new pixel the area-weighted mean of the old
// ones it covers, alpha-weighted so edges keep no dark fringe): art drawn
// large, carried at the size it is shown.
[[nodiscard]] Image scaled_down(const Image& img, std::int32_t width, std::int32_t height);
// Pictures packed into one (an atlas): placed in shelves, tallest first,
// `padding` clear pixels around each, no wider than `width`; where each went.
struct Packed {
    Image                        atlas;
    std::vector<Piece>           at;  // by input order: its place (x, y, width, height)
};
[[nodiscard]] Packed pack(std::span<const Image> images, std::int32_t width, std::int32_t padding = 2);
// A region of a picture as a picture of its own.
[[nodiscard]] Image crop(const Image& img, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
[[nodiscard]] std::expected<Image, std::string> load_png(const std::filesystem::path& file);

}  // namespace omui::imageio
