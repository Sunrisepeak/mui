// openmui.lang — the Script Binding Profile over openmui.h (component-binding
// §6): a script thread beside the runtime (:host), and what a script builds
// as view documents, its signals as mirrored runtime signals, its handlers
// as the document's events (:view). Every language binding is a thin layer
// over this: Lua (modules/projects/omui/lang/lua), TypeScript (modules/projects/omui/lang/js), Python (modules/projects/omui/lang/python).
export module omui.lang;

export import :host;
export import :view;
