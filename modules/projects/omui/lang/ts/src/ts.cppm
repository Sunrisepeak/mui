// omui.lang.ts — omui's TypeScript (D-138, D-139, D-157), over the tool kernel's (mlibs.ts: types blanked,
// data evaluated).
//
//     auto data = omui::lang::ts::evaluate("button.contract.omui.ts");  // {kind: "contract", json: "{…}"}
//     auto types = omui::lang::ts::declare_components(shapes, {});      // builders' declarations
//
// `:define` holds omui's data formats (the kinds, their define modules and declarations); `:declare` writes the
// TypeScript declarations of components and native modules, for every tool alike; `:edit` finds and edits the
// builder calls of a view's source (Live Design).
export module omui.lang.ts;

export import :error;
export import :define;
export import :declare;
export import :edit;
