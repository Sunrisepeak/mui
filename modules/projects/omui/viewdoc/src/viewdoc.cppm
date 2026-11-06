// openmui.viewdoc — view documents (`*.view.omui.json`, view-data-files §3).
//
// One of the two ways to write a view, as data: for designers, agents and
// generative interfaces, for components shared by every language, and for
// C++ applications whose interface changes without a rebuild. A document is
// checked against what is registered, with located diagnostics; it becomes
// the Elements a builder makes, so its semantic tree is the builder's; it
// reloads in place, keeping its state.
//
//     auto view = omui::viewdoc::View::load_file(rt, "ui/counter.view.omui.json");
//     if (!view) for (auto& d : view.error()) std::println("{}", d.text());
//     view->mount(rt, "main");
export module openmui.viewdoc;

export import openmui.view;
export import openmui.expr;
export import :document;
export import :instance;
export import :schema;
