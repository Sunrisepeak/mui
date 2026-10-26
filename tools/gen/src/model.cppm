// openmui.gen.model — the registry and contracts as the generator sees them.
//
// One intermediate representation feeds every emitter (C header, spec
// partitions, component partitions, script stubs, schemas, documentation), so
// what two outputs say about a role or a property cannot disagree.
export module openmui.gen.model;

import std;

export namespace omui::gen {

struct Loc {
    std::string file;
    int         line   = 0;
    int         column = 0;
};

struct Diagnostic {
    enum class Severity : std::uint8_t { error, warning };
    Severity    severity = Severity::error;
    Loc         loc;
    std::string message;
    std::string hint;
};

struct Named {
    std::string name;
    std::string doc;
    Loc         loc;
};

struct Arg {
    std::string name;
    std::string type;
};

struct Role : Named {
    std::string aria;
    std::string accesskit;
    bool        interactive = false;
};

struct State : Named {};

struct Effect : Named {
    int  tier   = 0;
    bool answer = false;
};

struct Origin : Named {};

struct Action : Named {
    std::string      effect;
    std::vector<Arg> args;
};

struct Extension : Named {
    int                      version = 1;
    std::vector<std::string> requires_;
};

struct Profile : Named {
    std::vector<std::string> scripts;
    int                      min_scripts = 0;
};

struct MotionToken : Named {
    std::optional<double> duration_ms;
    std::string           easing;
    std::optional<double> stiffness, damping, mass;
};

struct ExprOperator : Named {
    std::string                        text;
    int                                arity = 0;
    std::map<std::string, std::string> combinators;
};

struct ExprFunction : Named {
    std::string                        signature;
    std::map<std::string, std::string> combinators;
};

// ── C ABI ──────────────────────────────────────────────────────────────────

struct AbiField {
    std::string name;
    std::string type;
    std::string doc;
    int         array = 0;
};

struct AbiParam {
    std::string name;
    std::string type;
};

struct AbiHandle : Named {
    std::string kind;  // pointer | u64
};
struct AbiResult : Named {
    int value = 0;
};
struct AbiEnum : Named {
    std::vector<std::string> values;
};
struct AbiStruct : Named {
    bool                  extensible = false;
    std::vector<AbiField> fields;
};
struct AbiCallback : Named {
    std::string           returns;
    std::vector<AbiParam> params;
};
struct AbiFunction : Named {
    std::vector<AbiParam> params;
};

struct Abi {
    std::string              prefix = "omui";
    std::vector<AbiHandle>   handles;
    std::vector<AbiResult>   results;
    std::vector<AbiEnum>     enums;
    std::vector<AbiStruct>   structs;
    std::vector<AbiCallback> callbacks;
    std::vector<AbiFunction> functions;
};

// ── contracts ──────────────────────────────────────────────────────────────

// A property default as written in the contract.
using Default = std::variant<std::monostate, bool, std::int64_t, double, std::string, std::vector<std::string>>;

struct Prop : Named {
    std::string              type;    // bool i32 i64 f64 string color enum list<string>
    std::vector<std::string> values;  // enum values
    Default                  default_value;
    bool                     bindable = false;
};

struct Event : Named {
    std::vector<Arg> args;
    std::string      coalesce;  // "" or "frame"
};

struct ContractAction : Named {
    std::string      effect;
    std::vector<Arg> args;
};

struct Key {
    std::string chord;   // "Right|Up"
    std::string target;  // "increment", "set_value(min)"
    Loc         loc;
};

struct Command : Named {};

struct Part : Named {};

struct Motion {
    std::string part;
    std::string token;
    Loc         loc;
};

struct Contract : Named {
    std::string                 role;
    std::string                 spec;
    std::string                 children = "none";  // none, any, or the contract its children are (tabs: tab)
    std::vector<Prop>           props;
    std::vector<Event>          events;
    std::vector<ContractAction> actions;
    std::vector<Key>            keys;
    std::vector<Command>        commands;
    std::vector<Part>           parts;
    std::vector<Motion>         motion;
};

struct Registry {
    std::string                spec;
    std::vector<Role>          roles;
    std::vector<State>         states;
    std::vector<Effect>        effects;
    std::vector<Origin>        origins;
    std::vector<Action>        actions;
    std::vector<Extension>     extensions;
    std::vector<Profile>       profiles;
    std::vector<MotionToken>   motion_tokens;
    std::map<std::string, std::string> easing;   // name -> formula
    std::map<std::string, std::string> spring;   // clause -> formula
    std::vector<ExprOperator>  operators;
    std::vector<ExprFunction>  functions;
    std::vector<std::string>   expr_types;
    Abi                        abi;
    std::vector<Contract>      contracts;        // the Core contracts (registry/components)

    template <class T>
    [[nodiscard]] static const T* find(const std::vector<T>& list, std::string_view name) {
        for (const T& item : list)
            if (item.name == name) return &item;
        return nullptr;
    }
};

// Text helpers every emitter uses.
[[nodiscard]] std::string pascal(std::string_view snake);   // text_box -> TextBox
[[nodiscard]] std::string camel(std::string_view snake);    // text_box -> textBox
[[nodiscard]] std::string upper(std::string_view s);        // text_box -> TEXT_BOX
[[nodiscard]] std::string cpp_string(std::string_view s);   // a C++ string literal, escaped
[[nodiscard]] std::string format_double(double v);          // shortest round-trip, always with a '.'
[[nodiscard]] std::string ident(std::string_view name);     // a C++ identifier: keywords get a trailing '_'

}  // namespace omui::gen
