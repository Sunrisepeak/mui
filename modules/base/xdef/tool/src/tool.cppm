// mlibs.xdef.tool — declared data's tool kernel (D-209, D-205 stage 0): the kinds' registry each owner keeps in
// its manifest, a data module's canonical snapshot, the checker, and the migration from the earlier suffix.
//
//     auto registry = mlibs::xdef::Registry::read(workspace);            // each owner's kinds, from its manifest
//     auto text = mlibs::xdef::snapshot(file, *registry);                // a data module's canonical snapshot
//     auto problems = mlibs::xdef::check(workspace, *registry);          // the container's rules over a tree
export module mlibs.xdef.tool;

import std;
export import mlibs.xdef;
// Re-exported: snapshots fail as mlibs.ts's (TsFailure), whose codes describe themselves there (Clang looks
// `describe(TsError)` up only where mlibs.ts is imported).
export import mlibs.ts;

export namespace mlibs::xdef {

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

// ── embedding: data files a program carries (D-73) ─────────────────────────

// Who wrote a generated unit, as its header says: the program that makes it again, a note, and the build plugin
// whose embed() found the files of an index.
struct Generator {
    std::string_view program = "xdef";
    std::string_view note;
    std::string_view plugin = "mlibs.build.embed";
};
// A file's text as the partition `<module>:<name>`: an exported `std::string_view <name>` in namespace `ns`, the
// text as a raw string, or, when it is not UTF-8 text (a picture, a sound), every byte escaped with its length.
// `source` is the file's name as the header gives it.
[[nodiscard]] std::string emit_embed(std::string_view text, std::string_view source, std::string_view module, std::string_view ns,
                                     std::string_view name, const Generator& by = {});
// The files embedded under one tag as one table, partition `<module>:<name>`: it re-exports each file's partition
// and an array `<name>` of (the file's name, its text), sorted by name; `files` pairs a name with its partition.
[[nodiscard]] std::string emit_embed_index(std::span<const std::pair<std::string, std::string>> files, std::string_view module,
                                           std::string_view ns, std::string_view name, const Generator& by = {});

}  // namespace mlibs::xdef
