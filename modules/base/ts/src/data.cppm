// mlibs.ts:data — a declared data module to the JSON it stands for (D-139, D-209: TS in, data out). Code,
// config and data are one language; the tools read the evaluated JSON, the canonical form.
//
//     // button.contract.xdef.ts
//     import { defineContract } from "openmui/define";
//     export default defineContract({ button: { role: "button", … } });
//
// The module runs in a QuickJS of its own, made to give the same answer every time and to stop: no Date, no
// Math.random, no WeakRef, no I/O; memory and time are bounded. It imports the named modules its options give
// (a kind's define module, written by the kind's owner: this package knows no kind) and its own files by path,
// inside `root`. Its default export must be a `{ kind, value }` — what a define function gives — holding data
// only (objects, arrays, strings, finite numbers, booleans, null): a function, undefined, NaN or a cycle is a
// problem at its JSON pointer.
export module mlibs.ts:data;

import std;
import :error;

export namespace mlibs::ts {

// A module a data module imports by name (`"openmui/define"`): its JavaScript.
struct NamedModule {
    std::string name;
    std::string source;
};

struct DataOptions {
    std::filesystem::path root;                   // imports by path stay inside it (empty: the module's directory)
    std::size_t memory_bytes = 64 * 1024 * 1024;  // the heap it may use
    std::chrono::milliseconds time{2000};         // how long it may run
    std::vector<NamedModule> modules;             // what it may import by name
};

// What a data module evaluated to: the define function's kind ("contract", "ops", "matter" …), its format
// identifier ("ops@1", D-209: the kind's major version, which its define function writes) and the value as
// JSON text (keys in the module's order).
struct Data {
    std::string kind;
    std::string json;
    std::string format;

    // The snapshot in its canonical form: `{"$format":"<kind>@<major>","<kind>":value}` and a line break.
    [[nodiscard]] std::string snapshot() const;
};

// A kind a define module defines, and its major version.
struct DefinedKind {
    std::string_view name;
    int major = 1;
};

// A data module's file (TypeScript, or JavaScript); a problem as `file:line:column: text` where it has a place.
[[nodiscard]] std::expected<Data, TsFailure> evaluate(const std::filesystem::path& file, const DataOptions& options = {});
// A data module's source; `name` names it in problems and is where its imports by path are found from.
[[nodiscard]] std::expected<Data, TsFailure> evaluate_source(std::string_view source, std::string_view name, const DataOptions& options = {});

// A define module's JavaScript: `export const define<Kind> = …` for each kind, each giving
// `{ kind, format, value }` (the format `<kind>@<major>`).
[[nodiscard]] std::string define_module(std::span<const DefinedKind> kinds);

}  // namespace mlibs::ts
