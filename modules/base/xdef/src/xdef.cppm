// mlibs.xdef — declared data's container (D-209): what every declared data file shares, whatever its kind.
//
//     auto name = mlibs::xdef::parse_name("rise.animation.xdef.json");   // {name "rise", kind "animation", …}
//     if (auto problem = mlibs::xdef::format_problem(root["$format"].text(), "matter", 1)) …  // a reader's first check
//
// A reader takes this alone (names, format identifiers); the registry, snapshots, the checker and the migration
// are the tool's (mlibs.xdef.tool, `xdef`). It knows no kind's fields: a kind's content is its owner's.
export module mlibs.xdef;

import std;

export namespace mlibs::xdef {

// ── names ──────────────────────────────────────────────────────────────────

// The suffix of the container, and the one a data file had before it (`.omui.`, until RC3.15's migration).
inline constexpr std::string_view container = "xdef";
inline constexpr std::string_view earlier_container = "omui";

// A data file's name: `<name>.<kind>.<container>.<extension>`.
struct FileName {
    std::string name;       // "rise"
    std::string kind;       // "animation"
    std::string container;  // "xdef" (or the earlier "omui")
    std::string extension;  // "ts", "json" or "jsonl"
};
// The parts of a file name, or nothing when it is no data file's.
[[nodiscard]] std::optional<FileName> parse_name(std::string_view file_name);

// A format identifier: `"<kind>@<major>"` (a snapshot's `$format`, the authority; a suffix is a hint).
struct Format {
    std::string kind;
    int major = 1;
    [[nodiscard]] bool same(const Format& other) const noexcept { return kind == other.kind && major == other.major; }
};
[[nodiscard]] std::optional<Format> parse_format(std::string_view text);
[[nodiscard]] std::string format_id(const Format& format);

// The key a snapshot's format identifier is under, at its root.
inline constexpr std::string_view format_key = "$format";

// What a reader checks first (D-209): the snapshot's `$format` (its text, or nothing when the root has none, or
// it is no string) names this kind at this major version. A problem's text, or nothing when it does. A reader
// passes the text whatever JSON library it reads with, and admits `$format` among the root's keys.
[[nodiscard]] std::optional<std::string> format_problem(std::optional<std::string_view> found, std::string_view kind, int major);

}  // namespace mlibs::xdef
