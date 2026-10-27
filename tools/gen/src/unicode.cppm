// openmui.gen.unicode — the Unicode Character Database to lookup tables.
//
// D-47: line breaking (UAX #14) and text segmentation (UAX #29) need a few
// properties of every code point; ICU is not in the dependency graph, so the
// generator reads the UCD files and writes compact two-stage tables (a block
// index per 256 code points, shared 256-entry blocks) as a C++ partition of
// openmui.unicode, together with the enumerations of the property values.
export module openmui.gen.unicode;

import std;

export namespace omui::gen {

// Reads LineBreak.txt, EastAsianWidth.txt, GraphemeBreakProperty.txt,
// WordBreakProperty.txt, emoji-data.txt, DerivedCoreProperties.txt and
// DerivedGeneralCategory.txt from `ucd`; answers the text of
// modules/unicode/src/generated/tables.cppm, or what was missing.
[[nodiscard]] std::expected<std::string, std::string> emit_unicode_tables(const std::filesystem::path& ucd,
                                                                          std::string_view version);

// A deterministic subset of a UCD test file (every `step`-th test line), so
// the conformance data stays small enough to commit.
[[nodiscard]] std::expected<std::string, std::string> unicode_test_subset(const std::filesystem::path& file, int step);

}  // namespace omui::gen
