// mlibs.text.subset — a font cut down to the characters a program
// draws (performance tiers §7: the page's Chinese font, 2.7 MB of the first
// frame's 7.7). Glyph ids are kept — what no kept character reaches is
// emptied (its outline in `glyf`, its variations in `gvar`), not removed —
// so GSUB and the metrics retain their glyph ids, and GPOS (kerning above all)
// is written again for the glyphs that stay. The Unicode
// cmap contains only requested characters, allowing other fonts to provide
// the rest. TrueType outlines (`glyf`, `loca`; variable fonts' `gvar`);
// a composite glyph keeps its components.
export module mlibs.text.subset;

import std;

export namespace mlibs::text::subset {

struct FontSubsetReport {
    std::size_t glyphs = 0, kept = 0;
    std::size_t bytes_before = 0, bytes_after = 0;
};

struct FontDeliveryReport {
    std::size_t bytes_before = 0, bytes_after = 0;
    bool names_removed = false;
};

// A derived standalone TrueType delivery file, without stored PostScript
// glyph names. The post header's other 28 bytes and every other table's
// payload survive exactly; only head.checkSumAdjustment is recalculated.
// No post, or post 3, returns the original bytes. Bad SFNT/post structures,
// unsupported post versions and changes to a digitally signed face fail.
// This checks delivery structure/checksums, not every outline's validity.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> compact_font_post(
    std::span<const std::uint8_t> font, FontDeliveryReport* report = nullptr);

// The layout features omui's text turns on: the shaper's own for a line of text (the common and the horizontal ones,
// the direction's forms, variable fonts' substitutions) and tabular figures (`tnum`) — what a face cut for the first
// frame keeps the glyphs of (below).
[[nodiscard]] std::span<const std::string_view> shaping_features() noexcept;
// The variation axes omui's text moves: the weight (`wght`) — no style sets another, so a variable face's `opsz`, `wdth`,
// `slnt` stay at their defaults.
[[nodiscard]] std::span<const std::string_view> shaping_axes() noexcept;

// The font with every glyph no character of `keep` reaches emptied. With `features` (shaping_features(), say), only the
// glyphs those layout features make are kept besides, and every other feature is left without lookups, so a face that
// holds the alternates of ss01 or zero no more than the page asks for does not draw one blank where a style asks for
// them: it applies none. The default, no features: every feature of the face, every glyph they can make. With `move_axes`
// (shaping_axes(), say), a variable face's other axes are held at their defaults: the variation data of regions that move
// them is dropped from `gvar` (the glyph at any weight is the same, the data for an `opsz` nothing sets is gone). The
// default, no axes: every axis stays.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> subset_font(std::span<const std::uint8_t> font, const std::set<char32_t>& keep,
                                                                               FontSubsetReport* report = nullptr,
                                                                               std::span<const std::string_view> features = {},
                                                                               std::span<const std::string_view> move_axes = {});

// What a font covers, coarsely: the 256-code-point blocks its Unicode cmap
// maps any character of, merged into ranges of blocks [first, last] — what a
// page needs to know whether a face it deferred could draw a character.
[[nodiscard]] std::expected<std::vector<std::pair<std::uint32_t, std::uint32_t>>, std::string> font_blocks(std::span<const std::uint8_t> font);

// The characters of UTF-8 text (malformed bytes skipped).
void characters_of(std::string_view text, std::set<char32_t>& into);

// What a subset keeps whatever the text: ASCII, Latin-1, general
// punctuation, CJK punctuation and the full-width forms.
void always_kept(std::set<char32_t>& into);

}  // namespace mlibs::text::subset
