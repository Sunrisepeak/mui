// openmui.lang — the Script Binding Profile over openmui.h (component-binding
// §6): a script thread beside the runtime (:host), and what a script builds
// as view documents, its signals as mirrored runtime signals, its handlers
// as the document's events (:view). Every language binding is a thin layer
// over this: TypeScript (modules/projects/omui/lang/js).
export module omui.lang;

export import :host;
export import :view;
