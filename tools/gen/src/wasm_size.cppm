// openmui.gen.wasm_size — where a WebAssembly program's bytes go (W8, the
// Web's delivery: what a page downloads before its first frame, and what
// splitting the heavy pages would save, Q-38). Its sections; its code by
// function, each function's body attributed to an owner — the C++ module
// it is attached to (openmui.view), a namespace (std, xrender), a C
// library by its prefix (hb_ HarfBuzz, FT_ FreeType, md_ md4c) — from the
// name section an analysis build keeps (emscripten's --profiling-funcs: the
// same code, its functions' names kept). Without one, the sections only.
export module openmui.gen.wasm_size;

import std;

export namespace omui::gen {

struct WasmSizeEntry {
    std::string name;
    std::size_t bytes = 0;
    std::size_t functions = 0;
};

struct WasmSizeReport {
    std::size_t                total = 0;     // the file
    std::size_t                shipped = 0;   // without the custom sections (names, features) a release build has none of
    std::vector<WasmSizeEntry> sections;      // each, by its name (a custom one: "custom:<name>")
    std::size_t                code = 0, data = 0;
    std::size_t                functions = 0, named = 0;
    std::vector<WasmSizeEntry> owners;        // the code by owner, largest first
    std::vector<WasmSizeEntry> largest;       // the largest functions
};

// Reads a WebAssembly binary. `top`: how many of the largest functions to list.
[[nodiscard]] std::expected<WasmSizeReport, std::string> wasm_size(std::span<const std::uint8_t> wasm, std::size_t top = 40);

// A symbol's owner: the module it is attached to ("openmui.view"), else
// its outer namespaces (up to `depth`: "xrender", "std"), else a C
// library by its prefix ("harfbuzz", "freetype", "md4c", "libc"…).
[[nodiscard]] std::string owner_of(std::string_view symbol, int depth = 2);

// As JSON (for CI and agents) and as a Markdown table (for a person).
[[nodiscard]] std::string to_json(const WasmSizeReport& report);
[[nodiscard]] std::string to_markdown(const WasmSizeReport& report, std::size_t owners = 40);

}  // namespace omui::gen
