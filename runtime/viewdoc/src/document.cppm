// openmui.viewdoc:document — a view document as data, read and checked.
//
//     auto r = omui::viewdoc::read(text, {.file = "counter.view.omui.json"});
//     for (const auto& d : r.diagnostics) std::println("{}", d.text());
//     if (r.ok()) use(*r.document);
//
// Checking resolves every name a document uses against what is registered
// when it runs (component types and their contracts, the runtime's own
// properties and events, the document's signals, models and interface): an
// unknown component, property, event or name, an expression that does not
// parse, a binding to a property that is not bindable, a constant of the
// wrong type, an invalid or repeated id. Each diagnostic has a JSON pointer,
// a line and a column, and a fix where one is known. Unknown fields are
// warnings, not errors (R7: readers skip what they do not know).
export module openmui.viewdoc:document;

import std;
import openmui.core;
import openmui.view;
import openmui.expr;

export namespace omui::viewdoc {

// The version of the format this reader writes and reads.
inline constexpr std::string_view format_version = "0.1";

struct Diagnostic {
    enum class Level : std::uint8_t { error, warning };
    Level         level = Level::error;
    std::string   code;     // "unknown-type", "unknown-property", "expression" …
    std::string   pointer;  // JSON pointer of what it is about
    std::uint32_t line = 0, column = 0;
    std::string   message;
    std::string   hint;     // how to fix it, when one is known

    // "counter.view.omui.json:12:7: error: … (hint) [code]"
    [[nodiscard]] std::string text(std::string_view file = {}) const;
    // The structured form of `omui check --json`.
    [[nodiscard]] Value to_value(std::string_view file = {}) const;
};

// A property's value: a constant, an expression (one-way), a two-way binding
// to a signal or a bindable interface property, or a message of the catalogue.
struct Binding {
    enum class Kind : std::uint8_t { constant, expr, bind, tr };
    Kind        kind = Kind::constant;
    Value       value;     // constant
    expr::Expr  expr;      // expr; tr: the call tr(key, args…)
    std::string name;      // bind: the signal or property
    std::string pointer;
};

// What an event does: set a signal, invoke a verb, emit an interface event.
struct Op {
    enum class Kind : std::uint8_t { set, verb, emit };
    Kind                                            kind = Kind::set;
    std::string                                     target;  // the signal, verb or event
    expr::Expr                                      value;   // set
    std::vector<std::pair<std::string, expr::Expr>> args;    // verb, emit (by name)
    std::string                                     pointer;
};

struct Reaction {
    std::string     event;
    std::vector<Op> ops;
};

struct Node {
    enum class Kind : std::uint8_t { element, when, each, slot };
    Kind                                            kind = Kind::element;
    std::string                                     type;                 // element
    const ComponentType*                            component = nullptr;  // element: resolved
    std::string                                     id;
    std::vector<std::pair<std::string, Binding>>    props;
    std::vector<Reaction>                           on;
    std::vector<std::pair<std::string, Transition>> animate;
    std::vector<Node>                               children;   // element; when: then; each: body
    std::vector<Node>                               otherwise;  // when: else
    expr::Expr                                      condition;  // when: if; each: where
    bool                                            filtered = false;  // each: has a where
    std::string                                     variable;   // each: the row's name
    std::string                                     model;      // each: in
    std::string                                     slot;       // slot
    std::string                                     pointer;
};

struct SignalDecl {
    std::string name;
    std::string type;  // bool, int, double, string, list, map, any
    Value       value;
    std::string pointer;
};

struct InterfaceProp {
    std::string name;
    std::string type;  // as a contract's: bool, i32, i64, f64, string, color, string_list, any, or enum (values)
    std::vector<std::string> values;
    Value       default_value;
    bool        bindable = false;
    std::string doc;
};

struct InterfaceEvent {
    std::string                                      name;
    std::vector<std::pair<std::string, std::string>> args;  // name, type
    std::string                                      doc;
};

struct Interface {
    std::vector<InterfaceProp>  props;
    std::vector<InterfaceEvent> events;
    Role                        role = Role::generic;
    std::string                 doc;

    [[nodiscard]] const InterfaceProp* prop(std::string_view name) const noexcept;
    [[nodiscard]] const InterfaceEvent* event(std::string_view name) const noexcept;
};

struct Document {
    std::string                                      version;  // "omui"
    std::vector<std::string>                         imports;
    std::string                                      name;
    bool                                             component = false;  // "component" rather than "view"
    Interface                                        interface;           // component documents
    std::vector<SignalDecl>                          signals;
    std::vector<std::pair<std::string, std::string>> models;  // name, the type of its rows
    Node                                             root;
    std::string                                      notes;

    [[nodiscard]] const SignalDecl* signal(std::string_view name) const noexcept;
};

struct ReadOptions {
    std::string file;  // for diagnostics
    // The component types names resolve to (default: the registered ones).
    std::function<const ComponentType*(std::string_view)> types;
    // The verbs `verb` operations name, when they are known when checking.
    const Verbs* verbs = nullptr;
};

struct ReadResult {
    std::optional<Document> document;  // set when there is no error
    std::vector<Diagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return document.has_value(); }
    [[nodiscard]] std::size_t errors() const noexcept;
};

[[nodiscard]] ReadResult read(std::string_view json, const ReadOptions& options = {});

// A signal's value converted to its declared type (an int to a double), or
// nothing when it cannot be: what `set` and two-way bindings store.
[[nodiscard]] std::optional<Value> coerce(std::string_view type, const Value& v);

}  // namespace omui::viewdoc
