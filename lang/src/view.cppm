// openmui.lang:view — what a script builds, as a view document.
//
// A script's view is data the runtime checks and runs (view-data-files §3):
// its constants are the document's; its expressions (`fmt`, `select` …) are
// CEL the runtime evaluates synchronously; its signals are runtime signals
// the document declares as provided, so every view and the script share one
// state; its handlers are the document's own events, forwarded to the script.
// So a view written in Lua, TypeScript or Python is the same tree as the C++
// builder's (CTS S level), and an agent sees no difference.
export module openmui.lang:view;

import std;
import nlohmann.json;
import openmui.abi;
import :host;

export namespace omui::lang {

// An expression: CEL text, and the signals it reads (spelled `s<handle>`).
struct Expr {
    std::string             text;
    std::vector<abi::OmuiSignal> signals;
};

[[nodiscard]] Expr literal(const Json& value);
[[nodiscard]] Expr signal_ref(abi::OmuiSignal s);
// A name in scope: a loop's row (`note`), its fields (`note.title`).
[[nodiscard]] Expr name_ref(std::string name);
[[nodiscard]] Expr call(std::string_view function, std::vector<Expr> args);
[[nodiscard]] Expr binary(std::string_view op, Expr a, Expr b);  // + - * / % == != < <= > >= && || in
[[nodiscard]] Expr unary(std::string_view op, Expr a);           // ! -
[[nodiscard]] Expr select(Expr cond, Expr then, Expr otherwise);
[[nodiscard]] Expr member(Expr object, std::string_view field);
[[nodiscard]] Expr fmt(std::string_view pattern, std::vector<Expr> args);

using Handler = std::function<void(const Json& event)>;

// A property's value as the script gave it.
struct Prop {
    enum class Kind : std::uint8_t { constant, expr, bind, tr };
    Kind              kind = Kind::constant;
    Json              value;   // constant; tr: the key
    Expr              expr;    // expr
    abi::OmuiSignal   signal = 0;  // bind
    std::vector<std::pair<std::string, Expr>> args;  // tr: the message's arguments by name
};

struct Element;

// `if`: children by a condition; `each`: a model's rows, one element per row
// (the body names the row `variable`).
struct Region {
    enum class Kind : std::uint8_t { when, each, slot };
    Kind                 kind = Kind::when;
    Expr                 condition;  // when; each: where (empty: every row)
    std::vector<Element> then, otherwise;  // when; each: then[0] is the body
    std::string          model, variable, slot;
};

struct Element {
    std::string                                type;
    std::string                                id;
    std::vector<std::pair<std::string, Prop>>  props;
    std::vector<std::pair<std::string, Handler>> on;
    std::vector<std::pair<std::string, Json>>  animate;  // transitions as documents write them
    std::vector<Element>                       children;
    std::shared_ptr<Region>                    region;   // set: this is a region, not a component
};

// An element tree as the bindings' own layers (the JavaScript prelude, the
// Python package) send it, as JSON:
//   {"type", "id", "props": {name: {"c": constant} | {"e": cel, "s": [signals]}
//                                  | {"b": signal} | {"t": key, "a": [{"e", "s"}]}},
//    "on": {event: handler index}, "animate": {prop: transition}, "children": […]}
//   {"region": {"kind": "when", "cond": {"e", "s"}, "then": […], "else": […]}
//              | {"kind": "each", "model", "variable", "where": {"e", "s"} | null, "body": {…}}
//              | {"kind": "slot", "name"}}
// `handler` gives the handler of an index; nothing for an index it has not.
[[nodiscard]] Result<Element> element_from_json(const Json& tree, const std::function<std::optional<Handler>(std::size_t)>& handler);

// "TextBox", "textBox" and "text-box" are the contract's `text_box`.
[[nodiscard]] std::string contract_name(std::string_view name);

// A script's signals, views and verbs over one Host (Script Binding Profile
// §6.1). Script-thread methods, unless stated.
class Session {
public:
    explicit Session(Host& host);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    [[nodiscard]] Host& host() noexcept { return host_; }

    // ── signals: runtime signals, mirrored here ──
    [[nodiscard]] Result<abi::OmuiSignal> signal(const Json& initial);
    [[nodiscard]] const Json& get(abi::OmuiSignal s) const;
    void set(abi::OmuiSignal s, Json value);
    // Runs `fn` (on the script thread) after each change, from either side;
    // answers a token for `unwatch` (a reload lets its last run's watches go).
    std::uint64_t watch(abi::OmuiSignal s, std::function<void(const Json&)> fn);
    void unwatch(std::uint64_t token);

    // ── views ──
    // The view document of an element tree (what mount sends; tests and
    // `omui check` read it).
    [[nodiscard]] Json document(const Element& root) const;
    Result<void> mount(std::string slot, const Element& root);
    Result<void> unmount(std::string slot);

    // ── models: rows the application or the script pushes ──
    // Replaces `remove` rows at `from` with `rows` ([{"key": …, fields…}]),
    // as one change (push, insert, patch, remove, clear).
    Result<void> splice(std::string_view model, std::size_t from, std::size_t remove, const Json& rows);
    [[nodiscard]] Result<Json> rows(std::string_view model, std::size_t from = 0,
                                    std::size_t count = std::numeric_limits<std::uint32_t>::max());

    // ── the rest of the profile, as calls into the runtime ──
    [[nodiscard]] Result<abi::OmuiNode> find(std::string_view selector);
    [[nodiscard]] Result<Json> get(abi::OmuiNode node, std::string_view prop);
    Result<void> set(abi::OmuiNode node, const std::vector<std::pair<std::string, Json>>& props);
    // The bus interface: records as JSON values; the result as the C ABI's.
    [[nodiscard]] Result<std::vector<Json>> snapshot(const Json& query = Json::object());
    [[nodiscard]] Result<std::vector<Json>> act(const Json& request);
    [[nodiscard]] Result<std::vector<Json>> invoke(std::string_view verb, const Json& args);
    // A verb implemented by the script: it runs on the script thread while
    // the runtime waits (the runtime keeps answering the script meanwhile).
    // Registered again by this session (a reload), the new function replaces
    // the old under the same effect and answer.
    struct VerbSpec {
        std::string name, summary;
        abi::OmuiEffect effect = abi::OMUI_EFFECT_REVERSIBLE;
        std::string answer;
        bool        person = false;  // the person's own: refused over the bus (S-10)
    };
    Result<void> verb(VerbSpec spec, std::function<Result<Json>(const Json& args)> fn);

    // ── themes (openmui.theme) ──
    // Composes the runtime's theme: a theme, then overlays ({"dark", "compact"}).
    // Why not, as "not-found: …" or "invalid-argument: …"; the theme stays.
    Result<void> theme(const std::vector<std::string>& layers);
    // {"themes": […], "overlays": […], "current": "dark+compact", "dark": true}
    [[nodiscard]] Json themes();

    // ── native modules (component-binding §8) ──
    // The catalogue (omuiNativeCatalog): {"modules": {name: {types, functions}}}.
    [[nodiscard]] Json natives();
    // A call on the calling thread (a native touches no runtime state): its
    // result, or why not ("invalid-argument: …", "not-found: …", "failed: …").
    [[nodiscard]] Result<Json> native(std::string_view module, std::string_view function, const Json& args);
    // An `async` one: it runs on a worker thread, and `done` runs on the
    // script thread with the answer (while this session lives).
    void native_async(std::string module, std::string function, Json args, std::function<void(Result<Json>)> done);

    // Script-thread work done, no native call in flight, and the runtime has
    // everything sent: for frame loops and tests (runtime thread).
    [[nodiscard]] bool idle() const noexcept { return host_.idle() && in_flight_->load() == 0; }

private:
    struct Mirror {
        Json                                                                  value;
        std::vector<std::pair<std::uint64_t, std::function<void(const Json&)>>> watchers;
        abi::OmuiSubscription                                                 watch = 0;
        // The script's writes on their way to the runtime: the runtime's echo of
        // each is the script's own value coming back, not a change (two writes
        // in a row must not see the first come back over the second).
        std::deque<Json>                                                      inflight;
    };
    struct Mounted {
        std::vector<abi::OmuiSubscription> subscriptions;
        std::map<std::string, Handler>      handlers;  // by the event the document emits
        std::vector<std::shared_ptr<void>>  users;     // what the subscriptions call with
    };
    struct VerbBox;
    struct Workers;  // the threads async natives run on

    void changed(abi::OmuiSignal s, Json value);  // from the runtime: its echo of the script's write, or a change

    Host&                                    host_;
    std::map<abi::OmuiSignal, Mirror>        signals_;
    std::map<std::string, Mounted>           mounted_;
    std::vector<std::unique_ptr<VerbBox>>    verbs_;
    std::vector<std::shared_ptr<void>>       watchers_;  // what signal watches call with
    std::shared_ptr<bool>                    alive_ = std::make_shared<bool>(true);
    std::shared_ptr<std::atomic<int>>        in_flight_ = std::make_shared<std::atomic<int>>(0);
    std::unique_ptr<Workers>                 workers_;  // started by the first async call
};

}  // namespace omui::lang
