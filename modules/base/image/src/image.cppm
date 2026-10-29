// openmui.image — images in memory, PNG, animated PNG, and WebP.
//
// An Image is 8-bit RGBA, rows top to bottom, either straight or
// premultiplied alpha (the renderer works premultiplied; files are straight).
// PNG goes through libpng's simplified API, WebP through libwebp (D-56).
// Decoding returns an error message rather than throwing; writing is
// deterministic for a given libpng, zlib and libwebp, which mcpp.lock pins.
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

// WebP (still images): lossy (VP8, alpha kept losslessly beside it) or
// lossless (VP8L). Colours under clear pixels are kept either way (libwebp's
// `exact`), so unpremultiplied art keeps its edges. Single-threaded, so the
// bytes are the same on every platform for a given libwebp.
struct WebpOptions {
    float quality = 90;     // lossy: 0–100, higher is better and larger; lossless: effort
    bool  lossless = false;
    int   method = 4;       // 0 (fast) – 6 (smallest)
};
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> encode_webp(const View& pixels, WebpOptions options = {});
[[nodiscard]] std::expected<Image, std::string> decode_webp(std::span<const std::uint8_t> bytes);  // straight alpha
[[nodiscard]] std::expected<void, std::string> save_webp(const std::filesystem::path& file, const View& pixels, WebpOptions options = {});
[[nodiscard]] std::expected<Image, std::string> load_webp(const std::filesystem::path& file);

// An animated WebP, shaped like encode_apng (so a caller switches format by
// name): every frame the size of the first, shown `delay` seconds (num / den)
// each, `loops` 0 forever (at most 65535). libwebp's WebPAnimEncoder stores
// each frame as what changed from the one before, merges a frame equal to the
// one before into it (it lasts longer), and codes them lossy (with alpha kept
// losslessly) or lossless. WebP keeps time in whole milliseconds: frame i
// starts at i × delay rounded to the nearest one, so a 1/12 s delay alternates
// 83 and 84 ms and never drifts. A key frame (one drawn without the one
// before, where a viewer can start) comes at most every `kmax` frames and no
// sooner than `kmin` after the last (kmax 0: only the first; 1: every frame).
// Colours under clear pixels kept (exact); single-threaded, so the bytes are
// the same on every platform for a given libwebp. What comes out as a single
// frame (one, or all the same) libwebp writes as a still WebP: no delay, no
// loop count.
struct AnimatedWebpOptions {
    std::uint16_t delay_num = 1, delay_den = 12;
    std::uint32_t loops = 0;
    float         quality = 80;  // lossy: 0–100, higher is better and larger; lossless: effort
    bool          lossless = false;
    int           method = 4;    // 0 (fast) – 6 (smallest)
    int           kmin = 33, kmax = 64;
};
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> encode_animated_webp(std::span<const View> frames,
                                                                                      AnimatedWebpOptions options = {});
[[nodiscard]] std::expected<void, std::string> save_animated_webp(const std::filesystem::path& file, std::span<const View> frames,
                                                                  AnimatedWebpOptions options = {});

// An animation played back: each frame the whole canvas as shown (straight
// alpha), how long it is shown. Frames the encoder merged come back as one.
struct AnimationFrame {
    Image         image;
    std::uint32_t delay_ms = 0;
};
struct Animation {
    int                         width = 0, height = 0;
    std::uint32_t               loops = 0;  // 0: forever
    std::vector<AnimationFrame> frames;
};
// Any WebP (a still one is an animation of one frame).
[[nodiscard]] std::expected<Animation, std::string> decode_animated_webp(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::expected<Animation, std::string> load_animated_webp(const std::filesystem::path& file);

// Either format, told by its signature (PNG's eight bytes, WebP's RIFF/WEBP).
[[nodiscard]] std::expected<Image, std::string> decode(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::expected<Image, std::string> load(const std::filesystem::path& file);

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
// A piece's long axis (its pixels' principal axis, alpha above `alpha`): the
// line through its middle it is drawn along, from end to end (the 3rd and
// 97th percentiles of its pixels along it, so a stray wisp does not stretch
// it) and its half thickness — where a limb's joint and tip lie.
struct Axis {
    double from_x = 0, from_y = 0, to_x = 0, to_y = 0;
    double half_width = 0;
};
[[nodiscard]] Axis principal_axis(const Image& img, std::uint8_t alpha = 96);
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

// ── a picture's volume ──────────────────────────────────────────────────

// A tangent-space normal map of a picture (a painted part, a UI picture),
// aligned with it and the same size, for render's Material: R is the
// normal's x (right), G its y (down, as the picture's rows), B its part out
// of the picture, each 128 + 127·n rounded; A is 255. The height it is the
// slope of is a rounded bevel — a quarter circle `bevel` pixels wide (and
// high) rising from the edge of the pixels whose alpha is at least `alpha`
// (the picture's border counts as an edge) — so a part reads as a volume,
// plus `detail` pixels of height per unit of luminance (darker is deeper:
// painted folds and creases), smoothed by a box of radius `smooth` twice,
// its slopes scaled by `strength`. Integers only (an exact distance
// transform, integer square roots): the same bytes everywhere.
struct NormalOptions {
    float        bevel = 12;       // pixels
    float        detail = 0;       // pixels of height from black to white; 0: the silhouette alone
    std::int32_t smooth = 2;       // pixels; 0: none
    float        strength = 1;     // the slopes' scale (< 1 flatter)
    std::uint8_t alpha = 128;      // inside: alpha at least this
};
[[nodiscard]] Image normal_map(const Image& img, NormalOptions options = {});

}  // namespace omui::imageio

namespace omui::imageio {

// For the codecs (not exported): the pixels straight, tightly packed — a
// copy unpremultiplied when they are premultiplied, else the view itself.
struct Straight {
    std::vector<std::uint8_t> copy;
    const std::uint8_t*       rgba = nullptr;
    int                       stride = 0;
};
[[nodiscard]] Straight straight(const View& pixels);

}  // namespace omui::imageio
