// openmui.text:font — font files, faces, sized instances, fallback.
//
// A face is one font file (variable fonts have axes); a font is a face at a
// weight and slant, the unit shaping and rendering refer to (render::FontRef).
// Fallback is per grapheme cluster: the family list first, then the
// collection's fallback chain (CJK, Arabic, emoji, monospace), so mixed text
// renders with no tofu and the same face choice on every platform.
export module openmui.text:font;

import std;
import openmui.geom;
import openmui.render;

export namespace omui::typeset {

using FaceId = std::uint32_t;
using render::FontRef;

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
    // the executable, else the source tree's modules/text/fonts.
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

    [[nodiscard]] std::size_t face_count() const noexcept;
    [[nodiscard]] const FaceInfo& face(FaceId id) const;
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
    [[nodiscard]] Metrics metrics(FontRef font, float size) const;

    // The native objects, for the shaping and glyph units of this package.
    [[nodiscard]] void* hb_font(FontRef font) const;  // hb_font_t*, scaled to units per em

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::typeset
