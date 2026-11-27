// mlibs.ts:strip — TypeScript to JavaScript with no tool (D-139):
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
export module mlibs.ts:strip;

import std;
import :error;

export namespace mlibs::ts {

// A place in the source (1-based line and column, in bytes) and what is wrong there.
struct TypeScriptProblem {
    std::size_t line = 0, column = 0;
    std::string text;
};

// The JavaScript of `source`: the same text with every type blanked, or every
// problem found (the source is not erasable, or does not scan).
[[nodiscard]] std::expected<std::string, std::vector<TypeScriptProblem>> strip_types(std::string_view source);

// What a module imports from another, read without running it (the closure an application is built with is
// derived from these, B7b): the names it takes, as the other module exports them (`b` of `b as c`, `default` of a
// default import); a type-only name is not taken. `everything`: it may use any of the module's names — `import *
// as x`, `export * from`, or an `import()` of it — so a reader keeps all of that module. An `import()` of a
// specifier that is not a string is a module of unknown name (`from` empty), everything of it.
struct Import {
    std::string              from;
    std::vector<std::string> names;
    bool                     everything = false;
    std::size_t              line = 0;  // 1-based, where its statement or call begins
};

// Every import of `source` (statements, `export … from`, and `import()` calls), in order; or its problems, as
// strip_types would say them.
[[nodiscard]] std::expected<std::vector<Import>, std::vector<TypeScriptProblem>> imports(std::string_view source);

// Each problem as `line:column: text`, one a line.
[[nodiscard]] std::string to_text(const std::vector<TypeScriptProblem>& problems);

// A TypeScript file as JavaScript; its problems as `file:line:column: text`, one a line.
[[nodiscard]] std::expected<std::string, TsFailure> compile(const std::filesystem::path& file);
// TypeScript source (an editor's text, `name` in its problems) as JavaScript.
[[nodiscard]] std::expected<std::string, TsFailure> compile_source(std::string_view source, std::string_view name = "main.ts");

}  // namespace mlibs::ts
