// omui.gen.part — a WebAssembly program cut in two by what a sibling program carries (a world in parts: the
// Web-program size design, 2026-11-27, ① "a part is what a capability adds").
//
// Two programs of one package are linked from the same units: the whole, and one built without a capability (its
// call to it left out, so the linker kept nothing only that capability reaches). The whole's first part is every
// function the sibling has too — by name, from the two symbol maps Emscripten writes (--emit-symbol-map) — and every
// function whose body the sibling has (the optimizer merges functions of one body under one name, not the same one in
// each link), and every function those call directly in the whole (what it left a function where the sibling's
// inlined it); the second part is the rest: what only the capability needs. wasm-split cuts the whole so; a call of
// the first part's into the second goes through a placeholder that brings the second in (omui-gen dist-toy's loader).
//
//     auto cut = omui::gen::cut_part({.whole = "bin/oweb-reader.wasm", .whole_names = "oweb-reader.js.symbols",
//                                     .first_names = "oweb-reader-reading.js.symbols", .primary = …, .deferred = …});
export module omui.gen.part;

import std;

export namespace omui::gen {

// How many functions a WebAssembly program imports (they come first in its function index space), or why it is
// not one.
[[nodiscard]] std::expected<std::size_t, std::string> function_imports(std::span<const std::uint8_t> wasm);

// The direct calls Binaryen's call graph states (`wasm-opt --print-call-graph`, a DOT graph whose nodes are the
// functions a program defines, by their index among those it defines — "12" — and its imports as `fimport$N`):
// each (caller, callee) between two functions the program defines.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> call_edges(std::string_view dot);

// A function's name as two links of the same code both have it: the uniquing suffix Emscripten's symbol map gives a
// name two functions share (`~__func()_17692`, by the function's index) dropped.
[[nodiscard]] std::string_view linked_name(std::string_view name) noexcept;

// What each function a program defines is, as two links of the same code have it alike (by its index among those it
// defines): its body as Binaryen prints it (`wasm-opt --print`), its callees by their linked names (`names`, the
// symbol map by function index, imports first; `imports` how many it imports), its constants and type indices —
// addresses, table slots, the order a link gave — left out, hashed. Binaryen's optimizer merges functions of one body
// under one name, the first it meets: the same code can be named for another function in each link.
[[nodiscard]] std::map<std::size_t, std::uint64_t> body_hashes(std::string_view printed, const std::map<std::size_t, std::string>& names, std::size_t imports);

// The functions of the first part, by their index among those the whole defines: each whose linked name (`whole`,
// its symbol map by function index, imports first) the sibling program has (`first`, its linked names) or whose body
// one of the sibling's has (`whole_bodies` against `first_bodies`: a function merged under another's name), and
// every function those reach by direct calls (`edges`). `imports`: how many functions the whole imports.
[[nodiscard]] std::set<std::size_t> first_part(const std::map<std::size_t, std::string>& whole, const std::set<std::string>& first, std::size_t imports,
                                               std::span<const std::pair<std::size_t, std::size_t>> edges,
                                               const std::map<std::size_t, std::uint64_t>& whole_bodies = {},
                                               const std::set<std::uint64_t>& first_bodies = {});

struct PartCut {
    std::filesystem::path whole;        // the program as packed (a release: its functions unnamed)
    std::filesystem::path whole_names;  // its symbol map
    std::filesystem::path first_names;  // the sibling's (the same package's program without the part)
    std::filesystem::path primary;      // where the first part is written
    std::filesystem::path deferred;     // and the second
    std::filesystem::path binaryen;     // the directory holding wasm-opt and wasm-split; empty: the newest emsdk's
};

struct PartReport {
    std::size_t functions = 0;  // the whole defines
    std::size_t named = 0;      // of those the sibling has by name
    std::size_t bodies = 0;     // more of them the sibling has by body (merged under another name)
    std::size_t first = 0;      // in the first part (those, and what they call directly)
};

// The whole cut in two, the halves written where asked; or why not (a tool missing, a map that names nothing).
[[nodiscard]] std::expected<PartReport, std::string> cut_part(const PartCut& cut);

// The newest emsdk mcpp or xlings installed whose bin/ holds wasm-split and wasm-opt, or none.
[[nodiscard]] std::optional<std::filesystem::path> binaryen_directory();

// The WebAssembly features Emscripten's wasm32 output uses, as Binaryen's flags: wasm-opt and wasm-split read and
// write the same ones (and none newer, as compact imports, that browsers reject).
[[nodiscard]] std::span<const std::string_view> emscripten_features() noexcept;

}  // namespace omui::gen
