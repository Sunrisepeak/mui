// mlibs.text:shaper — the slot a text engine is provided at (D-238; terminal design §9).
//
// A FontCollection keeps its faces, their fonts, the fallback chain and what it could not cover; what a face is —
// its tables, its glyphs and their outlines, how a run of text becomes glyphs — is a shaper's. A program's root
// installs one at `shaper_slot`: HarfBuzz (package mlibs.text.harfbuzz) where pixels are drawn, the cell measure
// (`cells`, below) where text is a terminal's characters. The shaper not installed is not in the program: a
// terminal links no HarfBuzz. A collection made where none is installed has no faces, and says how to install one.
// A shaper is called per face, per font and per paragraph — never per pixel.
export module mlibs.text.api:shaper;

import std;
import mlibs.compose;
import :font;
import :paragraph;
import :shaped;

export namespace mlibs::text {

// A face as its shaper holds it: the characters it has, and its file's bytes (read when first needed).
class ShaperFace {
public:
    virtual ~ShaperFace() = default;
    [[nodiscard]] virtual bool covers(char32_t code_point) const = 0;
    [[nodiscard]] virtual std::span<const std::uint8_t> bytes() const = 0;
    [[nodiscard]] virtual std::size_t held() const = 0;  // its bytes in memory now: 0 until read
};

// A face at a weight and slant as its shaper holds it: its metrics at a size, and its glyphs as geometry.
class ShaperFont {
public:
    virtual ~ShaperFont() = default;
    [[nodiscard]] virtual Metrics metrics(float size) const = 0;
    [[nodiscard]] virtual GlyphOutline outline(std::uint32_t glyph) const = 0;
    [[nodiscard]] virtual std::vector<ColorLayer> color_layers(std::uint32_t glyph) const = 0;
};

// A face a shaper read: what it is (its names, em, axes, colour) and its object.
struct ShaperFaceMade {
    FaceInfo                    info;
    std::unique_ptr<ShaperFace> face;
};

// The slot's service: a table made when its provider is installed (never at program start).
struct Shaper {
    std::string_view name;
    // A face from a file — described from the few tables that say what it is, its bytes read when first needed —
    // or from bytes; a failure says what is wrong with it.
    std::expected<ShaperFaceMade, std::string> (*file)(const std::filesystem::path& file) = nullptr;
    std::expected<ShaperFaceMade, std::string> (*data)(std::vector<std::uint8_t> data) = nullptr;
    // A font of one of its faces at a weight (within the face's axis) and slant.
    std::unique_ptr<ShaperFont> (*font)(const ShaperFace& face, const FaceInfo& info, float weight, Slant slant) = nullptr;
    // A paragraph's runs (the shaped contract, :shaped), given each byte's bidi level and `shaped.base_level`: its
    // runs in logical order with their glyphs in visual order and metrics, the graphemes with their advances (their
    // words and line breaks are shape()'s), the ellipsis.
    void (*shape)(Paragraph::Shaped& shaped, std::span<const std::uint8_t> level_at) = nullptr;
    // What `add_defaults` adds; null: the default font files in their fallback order (`default_fonts`).
    std::expected<void, std::string> (*defaults)(FontCollection& fonts, const std::filesystem::path& directory) = nullptr;
};

inline constexpr compose::Slot<Shaper> shaper_slot{{
    .id = "mlibs.text.shaper",
    .summary = "how text becomes glyphs: fonts read, measured and shaped (HarfBuzz), or a terminal's character cells",
    .absent = "install a shaper at the program's root: mlibs::text::harfbuzz::install() (package mlibs.text.harfbuzz) where "
              "pixels are drawn, mlibs::text::cells::install() in a terminal (omui::run and the headless host install HarfBuzz)"}};

// The cell measure (terminal design §3 ①): text as a terminal shows it. A grapheme takes the cells it takes in a
// terminal (mlibs::unicode::columns: one, or two for a wide one), a line is a row, whatever the size, font,
// letter spacing or line height asked for; a glyph's id is its code point, a grapheme's code points its glyphs, all
// standing where the grapheme starts, so a cell renderer reads the characters back from the glyphs.
// Nothing has an outline. It depends on nothing beyond this package, so it is in it; a terminal host installs it.
namespace cells {

// The grid in the layout's units (logical pixels): a column's width, a row's height and the baseline's depth in a
// row. The host gives the ratio (D-249); the same numbers go to the cell renderer.
struct Grid {
    float column = 8;
    float row = 16;
    float baseline = 12;
};

// The cell measure: a face it makes measures by the grid of the time it was made (`grid`).
[[nodiscard]] const Shaper& shaper();
// Measures by `grid` from now on, and installs the cell measure at the process's `shaper_slot` (installed already:
// only the grid changes).
std::expected<void, compose::Error> install(Grid grid = {});
// The grid faces made now measure by.
[[nodiscard]] Grid grid() noexcept;

}  // namespace cells

}  // namespace mlibs::text
