// omui.lang.ts:define — omui's declared data formats (D-139, D-157): the kinds, the define modules a data module
// imports them from ("openmui/define": the spec's formats; "omui/define": the implementation's), and their
// TypeScript declarations. The evaluation is the tool kernel's (mlibs.ts:data); this is the owner's part.
//
//     // button.contract.xdef.ts
//     import { defineContract } from "openmui/define";
//     export default defineContract({ button: { role: "button", … } });
export module omui.lang.ts:define;

import std;
import mlibs.ts;

export namespace omui::lang::ts {

// The kinds the define functions make (D-157): the spec's formats from "openmui/define", the implementation's
// from "omui/define".
inline constexpr std::array<std::string_view, 3> spec_kinds{"contract", "native", "view"};
inline constexpr std::array<std::string_view, 17> omui_kinds{"config",   "ops",       "theme", "style",  "motion",   "sound",    "music",  "timeline",
                                                             "subtitles", "video",    "effect", "behavior", "messages", "matter", "appearance", "world",
                                                             "project"};
inline constexpr std::array<std::string_view, 20> data_kinds{"contract", "native",    "view",  "config", "ops",      "theme",    "style",  "motion",     "sound",
                                                             "music",    "timeline",  "subtitles", "video", "effect", "behavior", "messages", "matter", "appearance", "world",
                                                             "project"};

// The module "openmui/define" (`spec = true`: the spec's formats) or "omui/define" (the implementation's): a
// define function per kind, each giving `{ kind, value }`.
[[nodiscard]] std::string define_module_source(bool spec);

// The options that let a data module import both define modules (mlibs.ts evaluates it).
[[nodiscard]] mlibs::ts::DataOptions data_options(mlibs::ts::DataOptions options = {});

// A data module of omui's formats evaluated: mlibs::ts::evaluate with both define modules.
[[nodiscard]] std::expected<mlibs::ts::Data, mlibs::ts::TsFailure> evaluate(const std::filesystem::path& file, mlibs::ts::DataOptions options = {});
[[nodiscard]] std::expected<mlibs::ts::Data, mlibs::ts::TsFailure> evaluate_source(std::string_view source, std::string_view name,
                                                                                   mlibs::ts::DataOptions options = {});

// The TypeScript declarations of "omui/define", for type-checking an application's configuration and catalogs
// ("openmui/define"'s are the spec's own, spec/stubs/define.d.ts).
[[nodiscard]] std::string_view omui_define_types();

}  // namespace omui::lang::ts
