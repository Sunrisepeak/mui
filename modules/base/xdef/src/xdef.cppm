// mlibs.xdef — declared data's container (D-209): what every declared data file shares, whatever its kind.
//
//     auto name = mlibs::xdef::parse_name("rise.animation.xdef.json");   // {name "rise", kind "animation", …}
//     auto registry = mlibs::xdef::Registry::read(workspace);            // each owner's kinds, from its manifest
//     auto text = mlibs::xdef::snapshot(file, *registry);                // a data module's canonical snapshot
//     auto problems = mlibs::xdef::check(workspace, *registry);          // the container's rules over a tree
//
// It knows no kind's fields: a kind's content is its owner's (its define module, its reader, its CTS).
export module mlibs.xdef;

import std;
import mlibs.ts;

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

// ── the registry ───────────────────────────────────────────────────────────

// A kind as its owner registers it, in its manifest:
//
//     [package.metadata.openmui.xdef]
//     kinds   = { config = 1, ops = 1, report = 1 }     # each kind and its major version
//     modules = { "omui/define" = ["config", "ops"] }  # where a data module imports define<Kind> from
struct Kind {
    std::string name;
    int major = 1;
    std::string owner;   // the owning package ("omui.lang.ts")
    std::string module;  // its define module ("omui/define"), empty when only tools write it
};

struct Registry {
    std::vector<Kind> kinds;
    std::vector<std::string> problems;  // a kind with two owners, a registration that is not one

    // The registries of a workspace's members (the root manifest's `[workspace] members`).
    [[nodiscard]] static std::expected<Registry, std::string> read(const std::filesystem::path& workspace);
    [[nodiscard]] const Kind* find(std::string_view kind) const;
    // A define module for each registered one: `define<Kind>` giving `{ kind, value }`.
    [[nodiscard]] std::vector<mlibs::ts::NamedModule> define_modules() const;
};

// The workspace a path is in: the nearest directory above it whose mcpp.toml has `[workspace]`.
[[nodiscard]] std::optional<std::filesystem::path> workspace_of(const std::filesystem::path& path);

// ── snapshots ──────────────────────────────────────────────────────────────

// A data module evaluated (its define modules the registry's) to its snapshot in the canonical form: one
// object `{"<kind>": value}`, the module's key order, UTF-8, compact, a line break at its end. The same
// declaration gives the same bytes.
[[nodiscard]] std::expected<std::string, mlibs::ts::TsFailure> snapshot(const std::filesystem::path& file, const Registry& registry);

// ── the checker ────────────────────────────────────────────────────────────

struct Problem {
    std::string file;  // relative to the root, `/`-separated
    std::string text;
};
// The container's rules over a tree's data files: the container's suffix (not the earlier one), a kind someone
// registers, and a committed snapshot that is what its module evaluates to now. Build trees, a test's fixtures
// and another repository (a submodule) are skipped.
[[nodiscard]] std::vector<Problem> check(const std::filesystem::path& root, const Registry& registry);

// ── migration ──────────────────────────────────────────────────────────────

// The earlier container's files moved to this one (D-208: a persisted name renamed comes with its migration):
// every `<name>.<kind>.omui.<ts|json|jsonl>` of a registered kind renamed `.xdef.`, and every text under the
// root that names such a file or suffix (`.xdef.ts`, `.xdef.json`, `.xdef.jsonl`) rewritten. Notes kept as
// written: a Markdown file under one of `notes` (relative prefixes) is not edited. Build trees and version
// control are skipped; with `write` false nothing changes.
struct Migration {
    std::vector<std::pair<std::string, std::string>> renamed;  // relative paths, from → to
    std::vector<std::string> edited;                           // relative paths of the texts rewritten
    std::size_t replacements = 0;
};
[[nodiscard]] Migration migrate(const std::filesystem::path& root, const Registry& registry, bool write, std::span<const std::string> notes = {});

}  // namespace mlibs::xdef
