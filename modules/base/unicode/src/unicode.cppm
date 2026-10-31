// openmui.unicode — text segmentation without ICU (D-47).
//
//   graphemes  UAX #29 extended grapheme clusters: what a caret steps over,
//              what backspace deletes
//   words      UAX #29 word boundaries: double-click selection, Ctrl+arrows
//   lines      UAX #14 line-break opportunities: where text wraps
//
// The properties come from the Unicode Character Database through generated
// tables (`omui-gen unicode`); the rules are the standards' own, checked
// against the UCD's conformance tests. Offsets are byte offsets into UTF-8;
// invalid UTF-8 is read as U+FFFD, one per maximal invalid subpart.
export module openmui.unicode;

import std;
export import :tables;

export namespace omui::unicode {

struct Decoded {
    char32_t      cp = 0;
    std::uint32_t length = 0;  // bytes consumed (≥ 1)
};

// One code point at `offset` (which must be < text.size()).
[[nodiscard]] Decoded decode(std::string_view text, std::size_t offset) noexcept;
// The offset of the code point before `offset` (which must be > 0).
[[nodiscard]] std::size_t previous(std::string_view text, std::size_t offset) noexcept;
void append_utf8(std::string& out, char32_t cp);
[[nodiscard]] std::u32string to_utf32(std::string_view text);

// Properties.
[[nodiscard]] Lb line_class(char32_t cp) noexcept;
[[nodiscard]] Gcb grapheme_class(char32_t cp) noexcept;
[[nodiscard]] Wb word_class(char32_t cp) noexcept;
[[nodiscard]] Ea east_asian_width(char32_t cp) noexcept;
[[nodiscard]] bool extended_pictographic(char32_t cp) noexcept;
[[nodiscard]] bool unassigned(char32_t cp) noexcept;

enum class Break : std::uint8_t { none, allowed, mandatory };

// Boundaries, as offsets into `text`: always 0 first and text.size() last
// (for non-empty text).
[[nodiscard]] std::vector<std::size_t> grapheme_boundaries(std::string_view text);
[[nodiscard]] std::vector<std::size_t> word_boundaries(std::string_view text);

struct LineBreak {
    std::size_t offset;  // a break before the code point at this offset
    bool        mandatory;
};
// Line-break opportunities, excluding offset 0; the last is text.size()
// (mandatory: the end of text, LB3).
[[nodiscard]] std::vector<LineBreak> line_breaks(std::string_view text);

// Grapheme navigation for carets: the boundary after / before `offset`.
[[nodiscard]] std::size_t next_grapheme(std::string_view text, std::size_t offset);
[[nodiscard]] std::size_t previous_grapheme(std::string_view text, std::size_t offset);
[[nodiscard]] std::size_t grapheme_count(std::string_view text);

// The word around `offset`: [begin, end) between the word boundaries that
// enclose it.
[[nodiscard]] std::pair<std::size_t, std::size_t> word_at(std::string_view text, std::size_t offset);

// Simple case mapping (UnicodeData.txt): one code point to one, the same in
// every locale; a code point without a mapping maps to itself.
[[nodiscard]] char32_t simple_upper(char32_t cp) noexcept;
[[nodiscard]] char32_t simple_lower(char32_t cp) noexcept;
// Full case mapping of a text, the same in every locale: SpecialCasing.txt's
// unconditional mappings (ß → SS, İ → i̇, ﬁ → FI) and Final_Sigma (Σ at the end
// of a word lowers to ς), the simple mapping otherwise. The language-specific
// rules (Turkish dotless i, Lithuanian dots) need a locale and are not applied.
// Invalid UTF-8 stays as it is.
[[nodiscard]] std::string upper(std::string_view text);
[[nodiscard]] std::string lower(std::string_view text);

}  // namespace omui::unicode
