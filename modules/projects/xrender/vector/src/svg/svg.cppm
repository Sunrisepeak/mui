// xrender.vector.svg — a display list as SVG: the picture as vectors, at any
// size.
//
// Shapes, paths, strokes, clips, transforms, layers' opacity and blending,
// solid and gradient paints, images and glyphs (as their outlines, each
// defined once) are SVG's own elements. What SVG cannot say the same way — a
// pixel effect (but opacity, which is SVG's group opacity), a conic or mixed paint, an inset shadow, a stroke both
// trimmed and dashed, a colour glyph — is drawn by the CPU reference renderer and kept as an
// island of pixels in its place, marked `data-island` with why; the
// report lists every one. Node and part markers become groups marked
// `data-node` and `data-part`: what drew each piece, for an agent
// reading the picture. The text is the same bytes on every platform.
//
//     auto picture = xrender::svg::write(list, resources, {.view = {0, 0, 800, 600}, .encode_png = png});
//     if (picture) save(picture->text);   // picture->report.islands: what is pixels
export module xrender.vector.svg;

import std;
import mlibs.geom;
import xrender.vector;

export namespace xrender::svg {

struct Options {
    Rect view;                  // what the picture shows, in the list's units: its size
    float island_scale = 2;     // device pixels per unit where pixels stand in for vectors
    std::optional<Color> background;
    // A node marker's name in the picture (data-node); none: its number.
    std::function<std::string(std::uint32_t node)> node_name;
    // Premultiplied RGBA (width × height, rows of width × 4 bytes) as PNG, for
    // images and islands: the caller's encoder (mlibs::image::encode_png), so
    // this package keeps no image codec. None: they are left out, counted.
    std::function<std::expected<std::vector<std::uint8_t>, std::string>(std::span<const std::uint8_t> rgba, int width, int height)> encode_png;
};

// Why a piece is pixels.
enum class Why : std::uint8_t {
    effect,           // a layer's pixel effect
    backdrop_effect,  // one that reads what lies under it: drawn with it
    paint,            // conic gradients, image and mixed paints
    blend,            // a blend mode SVG has not (erase)
    inset_shadow,
    trimmed_stroke,   // trimmed and dashed at once
    colour_glyph,     // an emoji: a picture, not an outline
};
[[nodiscard]] std::string_view describe(Why why) noexcept;

struct Island {
    std::size_t command = 0;  // where it begins in the list
    Why         why = Why::effect;
    Rect        bounds;       // in the picture's units
    std::string effect;       // the effect's name, for an effect layer
};

struct Report {
    std::size_t commands = 0;  // commands written
    std::size_t shapes = 0;    // rectangles, rounded rectangles, paths, shadows
    std::size_t glyphs = 0;    // glyphs placed (their outlines defined once each)
    std::size_t images = 0;
    std::vector<Island> islands;
    std::size_t external = 0;  // GPU textures: only their place is kept
    std::size_t missing_images = 0;
    std::size_t missing_glyphs = 0;
    std::size_t unencoded = 0;  // images and islands left out: no encoder, or it failed
};

struct Picture {
    std::string text;
    Report      report;
};

// Fails only when the list's pushes and pops do not nest.
[[nodiscard]] std::expected<Picture, std::string> write(const DisplayList& list, const Resources& resources, const Options& options);

}  // namespace xrender::svg
