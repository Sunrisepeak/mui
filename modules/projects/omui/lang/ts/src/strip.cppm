// omui.lang.ts:strip — TypeScript to JavaScript with no tool (D-139):
// what is only types becomes blank, a space for each character and every line
// break kept, so the JavaScript has the TypeScript's lines and columns and an
// error points at the source as written. Only erasable syntax is TypeScript
// here (annotations, `interface`, `type`, `declare`, `as`, `satisfies`,
// generics, `!`, `implements`, access modifiers, overload signatures, `import
// type`); what TypeScript would have to turn into other code — `enum`,
// `namespace` with code, parameter properties, decorators, `import x =
// require(...)`, `export =`, `<T>x` — is a problem, with where it is and what
// to write instead. The same function serves the runtime (the Web too: no
// esbuild in a page), hot reload, Live Design and omui-gen.
export module omui.lang.ts:strip;

import std;

export namespace omui::lang::ts {

// A place in the source (1-based line and column, in bytes) and what is wrong there.
struct TypeScriptProblem {
    std::size_t line = 0, column = 0;
    std::string text;
};

// The JavaScript of `source`: the same text with every type blanked, or every
// problem found (the source is not erasable, or does not scan).
[[nodiscard]] std::expected<std::string, std::vector<TypeScriptProblem>> strip_types(std::string_view source);

// Each problem as `line:column: text`, one a line.
[[nodiscard]] std::string to_text(const std::vector<TypeScriptProblem>& problems);

// A TypeScript file as JavaScript; its problems as `file:line:column: text`, one a line.
[[nodiscard]] std::expected<std::string, std::string> compile(const std::filesystem::path& file);
// TypeScript source (an editor's text, `name` in its problems) as JavaScript.
[[nodiscard]] std::expected<std::string, std::string> compile_source(std::string_view source, std::string_view name = "main.ts");

}  // namespace omui::lang::ts
