// omui.lang.ts — TypeScript for openmui (D-138, D-139).
//
//     auto js = omui::lang::ts::compile("logic/main.ts");          // JavaScript, lines and columns kept
//     auto data = omui::lang::ts::evaluate("button.contract.omui.ts");  // {kind: "contract", json: "{…}"}
//
// `:strip` blanks the types of the erasable subset; `:data` evaluates a
// data module — code, config and data in one language, its JSON the
// canonical form the tools read.
export module omui.lang.ts;

export import :strip;
export import :data;
