// omui.devtools.platforms — the verification table (release plan §2.5, D-201): docs/platforms.omui.json, each
// capability on each platform real, software, unverified or unsupported, written as the user documents' page.
export module omui.devtools.platforms;

import std;

export namespace omui::devtools::platforms {

// The page in a language ("en", "zh") from the table's JSON, or why the table cannot be read.
std::expected<std::string, std::string> page(std::string_view table, std::string_view language);

}  // namespace omui::devtools::platforms
