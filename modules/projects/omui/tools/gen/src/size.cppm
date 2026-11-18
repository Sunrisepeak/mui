// omui.gen.size — where a program's bytes go: a WebAssembly program's (W8,
// the Web's delivery: what a page downloads before its first frame, and what
// splitting the heavy pages would save, Q-38) and an ELF program's (B3: a
// desktop distribution's, its symbol table read). Its sections; its code by
// function, each function's body attributed to an owner — the C++ module
// it is attached to (omui.view), a namespace (std, xrender), a C
// library by its prefix (hb_ HarfBuzz, FT_ FreeType, md_ md4c) — from the
// name section an analysis build keeps (emscripten's --profiling-funcs: the
// same code, its functions' names kept; an ELF program's symbol table, which
// a build that is not stripped has). Without one, the sections only.
export module omui.gen.size;

import std;

export namespace omui::gen {

struct SizeEntry {
    std::string name;
    std::size_t bytes = 0;
    std::size_t functions = 0;
};

struct SizeReport {
    std::size_t                total = 0;     // the file
    std::size_t                shipped = 0;   // without the custom sections (names, features) a release build has none of
    std::vector<SizeEntry> sections;      // each, by its name (a custom one: "custom:<name>")
    std::size_t                code = 0, data = 0;
    std::size_t                functions = 0, named = 0;
    std::vector<SizeEntry> owners;        // the code by owner, largest first
    std::vector<SizeEntry> largest;       // the largest functions
};

// Reads a WebAssembly binary. `top`: how many of the largest functions to list.
[[nodiscard]] std::expected<SizeReport, std::string> wasm_size(std::span<const std::uint8_t> wasm, std::size_t top = 40);

// Reads an ELF program (64-bit, little-endian): `shipped` is what it loads
// (the allocated sections), `code` its executable sections, `data` the rest
// loaded; its functions and objects by owner from the symbol table.
[[nodiscard]] std::expected<SizeReport, std::string> elf_size(std::span<const std::uint8_t> elf, std::size_t top = 40);

// Either, by the file's first bytes.
[[nodiscard]] std::expected<SizeReport, std::string> program_size(std::span<const std::uint8_t> program, std::size_t top = 40);

// A symbol's owner: the module it is attached to ("omui.view"), else
// its outer namespaces (up to `depth`: "xrender", "std"), else a C
// library by its prefix ("harfbuzz", "freetype", "md4c", "libc"…).
[[nodiscard]] std::string owner_of(std::string_view symbol, int depth = 2);

// As JSON (for CI and agents) and as a Markdown table (for a person).
[[nodiscard]] std::string to_json(const SizeReport& report);
[[nodiscard]] std::string to_markdown(const SizeReport& report, std::size_t owners = 40);

}  // namespace omui::gen
