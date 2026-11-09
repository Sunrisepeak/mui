// openmui.ui.markdown — the Markdown component (canvas-markdown §2).
//
//     import omui.ui.markdown;
//     omui::markdown({.id = "readme", .source = text, .base_url = "docs"});
//
// The component's interface is contract/markdown.contract.omui.ts (its
// base class, builder, stubs and schema are generated when the package
// builds); the document model is openmui.markdown's (`mlibs::markdown`).
export module omui.ui.markdown;

export import omui.core;
export import omui.view;
export import mlibs.markdown;
export import :markdown_contract;
export import :document;
