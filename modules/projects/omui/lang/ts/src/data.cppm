// omui.lang.ts:data — a TypeScript data module to the JSON it stands for
// (D-138, D-139: TS in, data out). Code, config and data are one language;
// the tools read the evaluated JSON, the canonical form.
//
//     // button.contract.omui.ts
//     import { defineContract } from "openmui/define";
//     export default defineContract({ button: { role: "button", … } });
//
// The module runs in a QuickJS of its own, made to give the same answer
// every time and to stop: no Date, no Math.random, no WeakRef, no I/O; its
// imports are "openmui/define" and its own files by path, inside `root`;
// memory and time are bounded. Its default export must be a define…()
// result holding data only (objects, arrays, strings, finite numbers,
// booleans, null): a function, undefined, NaN or a cycle is a problem at its
// JSON pointer.
export module omui.lang.ts:data;

import std;

export namespace omui::lang::ts {

struct DataOptions {
    std::filesystem::path root;                   // imports by path stay inside it (empty: the module's directory)
    std::size_t memory_bytes = 64 * 1024 * 1024;  // the heap it may use
    std::chrono::milliseconds time{2000};         // how long it may run
};

// What a data module evaluated to: the define function's kind ("contract",
// "native", "ops", "theme", "style", "motion", "view", "config") and the value
// as JSON text (keys in the module's order).
struct Data {
    std::string kind;
    std::string json;
};

// The kinds the define functions make: `define<Kind>` in "openmui/define".
inline constexpr std::array<std::string_view, 8> data_kinds{"contract", "native", "ops", "theme", "style", "motion", "view", "config"};

// A data module's file (TypeScript, or JavaScript); a problem as `file:line:column: text` where it has a place.
[[nodiscard]] std::expected<Data, std::string> evaluate(const std::filesystem::path& file, const DataOptions& options = {});
// A data module's source; `name` names it in problems and is where its imports by path are found from.
[[nodiscard]] std::expected<Data, std::string> evaluate_source(std::string_view source, std::string_view name, const DataOptions& options = {});

// The module "openmui/define": a define function per kind, each giving `{ kind, value }`.
[[nodiscard]] std::string define_module_source();

}  // namespace omui::lang::ts
