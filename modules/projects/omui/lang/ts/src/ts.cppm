// omui.lang.ts — TypeScript for openmui (D-138, D-139).
//
//     auto js = omui::lang::ts::compile("logic/main.ts");          // JavaScript, lines and columns kept
//     auto data = omui::lang::ts::evaluate("button.contract.omui.ts");  // {kind: "contract", json: "{…}"}
//     auto types = omui::lang::ts::declare_components(shapes, {});        // builders' declarations
//
// `:strip` blanks the types of the erasable subset; `:data` evaluates a
// data module — code, config and data in one language, its JSON the
// canonical form the tools read; `:declare` writes the TypeScript
// declarations of components and native modules, for every tool alike.
export module omui.lang.ts;

export import :strip;
export import :data;
export import :declare;
export import :edit;
