// openmui.ui.markdown — the Markdown component (canvas-markdown §2).
//
//     import openmui.ui.markdown;
//     omui::markdown({.id = "readme", .source = text, .base_url = "docs"});
//
// The component's interface is contract/markdown.contract.omui.toml (its
// base class, builder, stubs and schema are generated when the package
// builds); the document model is openmui.markdown's (`omui::md`).
export module openmui.ui.markdown;

export import openmui.core;
export import openmui.view;
export import openmui.markdown;
export import :markdown_contract;
export import :document;
