// openmui.text:font — font files, faces, sized instances, fallback.
//
// A face is one font file (variable fonts have axes); a font is a face at a
// weight and slant, the unit shaping and drawing refer to (FontRef). Glyphs
// leave as geometry (outline, colour layers): rasterising and drawing them is
// a renderer's (xrender.text), so text knows no renderer.
// Fallback is per grapheme cluster: the family list first, then the
// collection's fallback chain (CJK, Arabic, emoji, monospace), so mixed text
// renders with no tofu and the same face choice on every platform.
export module mlibs.text.api:font;

import std;
import mlibs.geom;

export namespace mlibs::text {

using FaceId = std::uint32_t;

// A face at a weight and slant, interned by its collection: what shaping and
// drawing refer to (a renderer's font reference carries the same id).
struct FontRef {
    std::uint32_t id = 0;
    friend constexpr bool operator==(FontRef a, FontRef b) noexcept { return a.id == b.id; }
};

// A glyph's outline in ems (1 em = 1.0), y down, in mlibs.geom's path
// encoding: what a renderer rasterises. Empty for a glyph with no outline.
struct GlyphOutline {
    std::vector<geom::PathVerb> verbs;
    std::vector<geom::Point>    points;

    [[nodiscard]] geom::PathView view() const noexcept { return {verbs, points}; }
};

// One layer of a colour glyph (COLR v0): a glyph of the same font, filled with
// a palette colour (black where the palette has none).
struct ColorLayer {
    std::uint32_t glyph = 0;
    geom::Color   color;
};

enum class Slant : std::uint8_t { normal, italic };

struct FaceInfo {
    std::string family;
    std::string style;
    float       weight = 400;   // default instance
    Slant       slant = Slant::normal;
    bool        variable = false;
    float       min_weight = 400, max_weight = 400;
    bool        color = false;  // COLR glyphs (emoji)
    std::uint32_t units_per_em = 1000;
};

// Vertical metrics of a font at a size (pixels per em), positive values.
struct Metrics {
    float ascent = 0;
    float descent = 0;
    float line_gap = 0;
    float x_height = 0;
    float cap_height = 0;
    float underline_offset = 0;     // below the baseline
    float underline_thickness = 0;

    [[nodiscard]] float line_height() const noexcept { return ascent + descent + line_gap; }
};

// Fonts may be made (a weight not seen yet) on the thread that shapes while
// glyph caches on other threads read them (a canvas's textures, rasterised in
// the background); faces and the fallback chain are set up before that.
class FontCollection {
public:
    FontCollection();
    ~FontCollection();
    FontCollection(FontCollection&&) noexcept;
    FontCollection& operator=(FontCollection&&) noexcept;
    FontCollection(const FontCollection&) = delete;
    FontCollection& operator=(const FontCollection&) = delete;

    // The directory of the default fonts: $OMUI_FONTS, else `fonts/` beside
    // the executable, else the source tree's modules/content/text/fonts.
    [[nodiscard]] static std::filesystem::path default_directory();

    // This collection's identity, never reused (an address is: a collection
    // made where a freed one was would find caches keyed by the address and
    // their fonts' ids, which mean other fonts there). Whether one is alive.
    [[nodiscard]] std::uint64_t id() const noexcept;
    [[nodiscard]] static bool alive(std::uint64_t id);

    // Loads the five default fonts (Inter, JetBrains Mono, Noto Sans SC,
    // Noto Sans Arabic, Twemoji) and makes them the fallback chain.
    std::expected<void, std::string> add_defaults(const std::filesystem::path& directory = default_directory());

    std::expected<FaceId, std::string> add_file(const std::filesystem::path& file);
    std::expected<FaceId, std::string> add_data(std::vector<std::uint8_t> data);
    // A font that arrives after text was first shaped (a page's later batch:
    // the Chinese font a page fetches once it is drawn): added, and tried in
    // the fallback chain at `at` (after Inter, where the defaults have it).
    std::expected<FaceId, std::string> add_fallback(const std::filesystem::path& file, std::size_t at = 1);
    // Moves whenever a face is added or the fallback chain changes: text
    // shaped at an older revision shapes again (its glyphs may be others now).
    [[nodiscard]] std::uint64_t revision() const noexcept;
    // How many clusters select() found no face for so far: a host that
    // deferred a face (another language's) fetches it once this grows.
    [[nodiscard]] std::uint64_t uncovered() const noexcept;
    // The code points it found no face for (each cluster's first; distinct, the first 64).
    [[nodiscard]] std::vector<char32_t> uncovered_points() const;

    [[nodiscard]] std::size_t face_count() const noexcept;
    [[nodiscard]] const FaceInfo& face(FaceId id) const;
    // The face's file as it was added: what another process loads the same face from.
    [[nodiscard]] std::span<const std::uint8_t> face_data(FaceId id) const;
    [[nodiscard]] std::optional<FaceId> find_family(std::string_view family) const;
    [[nodiscard]] bool covers(FaceId id, char32_t cp) const;

    // Faces tried after a style's own families, in order.
    void set_fallback(std::vector<FaceId> chain);
    [[nodiscard]] std::span<const FaceId> fallback() const noexcept;

    // The face for a cluster: the first of `families`, then of the fallback
    // chain, that has a glyph for every code point of the cluster (emoji
    // presentation sequences prefer a colour face).
    [[nodiscard]] FaceId select(std::span<const std::string> families, std::u32string_view cluster) const;

    // A face at a weight and slant (interned: equal requests give equal refs).
    [[nodiscard]] FontRef font(FaceId face, float weight, Slant slant = Slant::normal);
    [[nodiscard]] FaceId face_of(FontRef font) const;
    [[nodiscard]] float weight_of(FontRef font) const;
    [[nodiscard]] Slant slant_of(FontRef font) const;
    [[nodiscard]] Metrics metrics(FontRef font, float size) const;

    // A glyph as geometry, for renderers: its outline, and its colour layers
    // in drawing order (none for a glyph that is not a colour glyph).
    [[nodiscard]] GlyphOutline outline(FontRef font, std::uint32_t glyph) const;
    [[nodiscard]] std::vector<ColorLayer> color_layers(FontRef font, std::uint32_t glyph) const;

    // The native object, for the shaping and outline units of this package.
    [[nodiscard]] void* hb_font(FontRef font) const;  // hb_font_t*, scaled to units per em

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mlibs::text
