// openmui.viewdoc:instance — a view document running.
//
//     auto view = omui::viewdoc::View::load(rt, text, {.file = "counter.view.omui.json"});
//     view->slot("toolbar", my_toolbar());
//     view->mount(rt, "main");                 // the same tree a builder makes
//     view->reload(new_text);                  // in place: signals keep their values
//
// A document becomes Elements, the view IR every builder produces: a
// constant is a property's value, an expression a reactive source (it reads
// signals, models and interface properties, and is evaluated again when they
// change), `bind` a two-way binding, `if` and `for` regions reconciled by key.
// An event runs its operations in one batch: `set` writes a signal, `verb`
// invokes one of the application's verbs, `emit` raises an event. A verb on
// an activation becomes the node's `verb` (with `verb_args`), so the action's
// effect and answer rules (S-1, S-2) hold for it as for any button.
//
// A document with an interface defines a component (`define_component`): its
// properties and events are a contract generated from the interface, so code
// in every language and other documents use it like any component.
export module openmui.viewdoc:instance;

import std;
import openmui.core;
import openmui.view;
import openmui.expr;
import :document;

export namespace omui::viewdoc {

struct LoadOptions {
    std::string file;  // for diagnostics
    // The host's signals, for the document's `"provided": true` ones.
    std::function<std::optional<Signal<Value>>(std::string_view name)> provide;
};

class View {
public:
    [[nodiscard]] static std::expected<View, std::vector<Diagnostic>> load(Services& services, std::string_view json,
                                                                           LoadOptions options = {});
    [[nodiscard]] static std::expected<View, std::vector<Diagnostic>> load_file(Services& services, const std::filesystem::path& file,
                                                                                LoadOptions options = {});

    // What code puts into a slot, before or after the view is mounted.
    View& slot(std::string_view name, Element content);
    // The document's events that are not a component's (`emit` in a view).
    View& on(std::string event, std::function<void(const Value& args)> handler);
    // Every such event, with its name (a binding forwards them to its code).
    View& on_any(std::function<void(std::string_view event, const Value& args)> handler);

    // The view, to embed among other elements: it adds no node of its own,
    // and follows reloads.
    [[nodiscard]] Element element() const;
    // Mounts the root at a slot of the runtime; reload() updates it in place.
    NodeId mount(Runtime& runtime, std::string_view slot);

    // A new text for the document: checked first (nothing changes when it has
    // errors); signals of the same name and type keep their values; nodes that
    // stay keep their state, focus and animations.
    std::expected<void, std::vector<Diagnostic>> reload(std::string_view json);
    std::expected<void, std::vector<Diagnostic>> reload_file(const std::filesystem::path& file);

    // The state of the document, for code: its signals by name.
    [[nodiscard]] std::optional<Signal<Value>> signal(std::string_view name) const;
    [[nodiscard]] const Document& document() const noexcept;
    // The warnings of the last load or reload.
    [[nodiscard]] std::span<const Diagnostic> warnings() const noexcept;

    struct State;

private:
    std::shared_ptr<State> state_;
};

// Defines (or, again, redefines) the component of a document with an
// interface. A redefinition rebuilds every instance in place.
[[nodiscard]] std::expected<const ComponentType*, std::vector<Diagnostic>> define_component(std::string_view json,
                                                                                            LoadOptions options = {});
[[nodiscard]] std::expected<const ComponentType*, std::vector<Diagnostic>> define_component_file(const std::filesystem::path& file,
                                                                                                 LoadOptions options = {});

// Builds an element of a component by name, with properties and handlers,
// as a generic builder does for any registered type (a document's component
// from C++).
[[nodiscard]] Element make(std::string_view type, std::vector<std::pair<std::string, Source>> props = {},
                           std::vector<std::pair<std::string, Handler>> handlers = {}, std::string id = {});

}  // namespace omui::viewdoc
