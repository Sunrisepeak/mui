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
// memory and time are bounded; "omui/define" too. Its default export must be a define…()
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
// "native", "ops", "theme", "sound", "music" …) and the value
// as JSON text (keys in the module's order).
struct Data {
    std::string kind;
    std::string json;
};

// The kinds the define functions make (D-157): the spec's formats from
// "openmui/define", the implementation's from "omui/define".
inline constexpr std::array<std::string_view, 3> spec_kinds{"contract", "native", "view"};
inline constexpr std::array<std::string_view, 14> omui_kinds{"config",   "ops",       "theme", "style",  "motion",   "sound",    "music",
                                                             "timeline", "subtitles", "video", "effect", "behavior", "messages", "matter"};
inline constexpr std::array<std::string_view, 17> data_kinds{"contract", "native",   "view",      "config", "ops",    "theme",    "style",    "motion", "sound",
                                                             "music",    "timeline", "subtitles", "video",  "effect", "behavior", "messages", "matter"};

// A data module's file (TypeScript, or JavaScript); a problem as `file:line:column: text` where it has a place.
[[nodiscard]] std::expected<Data, std::string> evaluate(const std::filesystem::path& file, const DataOptions& options = {});
// A data module's source; `name` names it in problems and is where its imports by path are found from.
[[nodiscard]] std::expected<Data, std::string> evaluate_source(std::string_view source, std::string_view name, const DataOptions& options = {});

// The module "openmui/define" (`spec = true`: the spec's formats) or
// "omui/define" (the implementation's): a define function per kind, each
// giving `{ kind, value }`.
[[nodiscard]] std::string define_module_source(bool spec);

// The TypeScript declarations of "omui/define", for type-checking an
// application's configuration and catalogs ("openmui/define"'s are the
// spec's own, spec/stubs/define.d.ts).
[[nodiscard]] std::string_view omui_define_types();

}  // namespace omui::lang::ts
