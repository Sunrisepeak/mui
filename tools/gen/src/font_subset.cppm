// openmui.gen.font_subset — a font cut down to the characters a program
// draws (performance tiers §7: the page's Chinese font, 2.7 MB of the first
// frame's 7.7). Glyph ids are kept — what no kept character reaches is
// emptied (its outline in `glyf`, its variations in `gvar`), not removed —
// so layout tables (GSUB, GPOS) and metrics retain their glyph ids. The Unicode
// cmap contains only requested characters, allowing other fonts to provide
// the rest. TrueType outlines (`glyf`, `loca`; variable fonts' `gvar`);
// a composite glyph keeps its components.
export module openmui.gen.font_subset;

import std;

export namespace omui::gen {

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

// The font with every glyph no character of `keep` reaches emptied.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> subset_font(std::span<const std::uint8_t> font, const std::set<char32_t>& keep,
                                                                               FontSubsetReport* report = nullptr);

// The characters of UTF-8 text (malformed bytes skipped).
void characters_of(std::string_view text, std::set<char32_t>& into);

// What a subset keeps whatever the text: ASCII, Latin-1, general
// punctuation, CJK punctuation and the full-width forms.
void always_kept(std::set<char32_t>& into);

}  // namespace omui::gen
