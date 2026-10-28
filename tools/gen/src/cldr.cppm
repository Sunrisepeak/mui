// openmui.gen.cldr — CLDR's JSON release to openmui.i18n's locale packs
// (D-74): per locale, `<id>.locale.omui.json` with its plural rules, number
// symbols, patterns and compact forms, Gregorian calendar names, patterns,
// skeletons and day periods, localised offsets, relative times and lists;
// and `likely.locale.omui.json`, the likely subtags of the languages packed.
// The packs are data the engine reads, committed beside their CLDR version.
export module openmui.gen.cldr;

import std;

export namespace omui::gen {

struct CldrPack {
    std::string file;  // en.locale.omui.json
    std::string text;
};

// From an unpacked cldr-json release (cldr-core, cldr-numbers-full,
// cldr-dates-full, cldr-misc-full beside each other) for the CLDR locale
// ids given (`en`, `zh`, `zh-Hant`); an error names what was missing.
[[nodiscard]] std::expected<std::vector<CldrPack>, std::string> emit_cldr_packs(const std::filesystem::path& cldr,
                                                                                  std::span<const std::string> locales,
                                                                                  std::string_view version);

}  // namespace omui::gen
