// openmui.view:element — the view IR (contract ②, spec §4).
//
// An Element is a component type, an explicit id or none, properties,
// event handlers, transitions and children. Builders in every language,
// immediate mode and view documents all produce Elements; the runtime turns
// them into nodes. Elements are built once: a property bound to a signal or
// an expression updates its node by itself (fine-grained), and only
// `when` / `each` regions rebuild their children.
export module openmui.view:element;

import std;
import openmui.core;
import openmui.layout;

export namespace omui {

struct ComponentType;  // :component

// ── values of properties ─────────────────────────────────────────────────

// Conversions between C++ property types and Values.
template <class T>
struct ValueOf;

template <>
struct ValueOf<Value> {
    static Value to(const Value& v) { return v; }
    static Value from(const Value& v) { return v; }
};
template <>
struct ValueOf<bool> {
    static Value to(bool v) { return Value(v); }
    static bool from(const Value& v) { return v.as_bool(); }
};
template <std::integral T>
    requires(!std::same_as<T, bool>)
struct ValueOf<T> {
    static Value to(T v) { return Value(static_cast<std::int64_t>(v)); }
    static T from(const Value& v) { return static_cast<T>(v.as_int(static_cast<std::int64_t>(v.as_double()))); }
};
template <std::floating_point T>
struct ValueOf<T> {
    static Value to(T v) { return Value(static_cast<double>(v)); }
    static T from(const Value& v) { return static_cast<T>(v.as_double()); }
};
template <>
struct ValueOf<std::string> {
    static Value to(const std::string& v) { return Value(v); }
    static std::string from(const Value& v) {
        if (v.type() == Value::Type::text) return std::string(v.as_string());
        if (v.is_null()) return {};
        return v.json();
    }
};
template <>
struct ValueOf<Color> {
    static Value to(const Color& c);
    static Color from(const Value& v);
};
template <>
struct ValueOf<std::vector<std::string>> {
    static Value to(const std::vector<std::string>& v) {
        Value::List l;
        for (const auto& s : v) l.emplace_back(s);
        return Value(std::move(l));
    }
    static std::vector<std::string> from(const Value& v) {
        std::vector<std::string> out;
        if (const Value::List* l = v.list())
            for (const Value& e : *l) out.emplace_back(e.as_string());
        return out;
    }
};

template <class T>
concept PropertyType = requires(const T& t, const Value& v) {
    { ValueOf<T>::to(t) } -> std::convertible_to<Value>;
    { ValueOf<T>::from(v) } -> std::convertible_to<T>;
};

// Where a property's value comes from: a constant, or a reactive reader
// (a signal, a computed value, an expression) re-read when what it reads
// changes, and for two-way bindings a writer.
struct Source {
    std::optional<Value>                     constant;
    std::function<Value()>                   read;   // reactive
    std::function<void(const Value&)>        write;  // two-way bindings only

    [[nodiscard]] bool reactive() const noexcept { return static_cast<bool>(read); }
    [[nodiscard]] Value current() const { return read ? read() : constant.value_or(Value{}); }
};

// A typed property of a builder option struct: a value, a signal (two-way
// where the property is bindable), a computed value, or any reactive source.
template <PropertyType T>
class Prop {
public:
    Prop() = default;
    Prop(T value) : source_{ValueOf<T>::to(value), {}, {}}, set_(true) {}  // NOLINT: implicit by design
    template <class U>
        requires std::constructible_from<T, const U&> && (!std::same_as<std::remove_cvref_t<U>, T>) &&
                 (!std::same_as<std::remove_cvref_t<U>, Prop>) && (!std::same_as<std::remove_cvref_t<U>, Source>) &&
                 (!requires { typename U::value_type; std::declval<const U&>().get(); })
    Prop(const U& value) : Prop(T(value)) {}  // NOLINT: "text" for std::string, 3 for float
    // A signal of T or of a type T converts from (a double for a float
    // property): bound two-way where the property is bindable.
    template <PropertyType U>
        requires std::convertible_to<U, T> || std::constructible_from<T, const U&>
    Prop(Signal<U> s)  // NOLINT
        : source_{std::nullopt, [s] { return ValueOf<U>::to(s.get()); }, [s](const Value& v) { s.set(ValueOf<U>::from(v)); }},
          set_(true) {}
    template <PropertyType U>
        requires std::convertible_to<U, T> || std::constructible_from<T, const U&>
    Prop(Computed<U> c)  // NOLINT
        : source_{std::nullopt, [c] { return ValueOf<U>::to(c.get()); }, {}}, set_(true) {}
    Prop(Source s) : source_(std::move(s)), set_(true) {}  // NOLINT: expressions and dynamic sources

    [[nodiscard]] bool is_set() const noexcept { return set_; }
    [[nodiscard]] const Source& source() const noexcept { return source_; }

private:
    Source source_;
    bool   set_ = false;
};

// ── transitions ──────────────────────────────────────────────────────────

// How a property moves to a new value: a tween or a spring (openmui.anim).
struct Transition {
    enum class Kind : std::uint8_t { tween, spring };
    Kind               kind = Kind::tween;
    anim::seconds      duration{0.2};
    anim::Ease         ease = anim::ease::standard;
    anim::SpringParams spring = anim::springs::standard;
    anim::seconds      delay{0};

    [[nodiscard]] static Transition tween(anim::seconds d, anim::Ease e = anim::ease::standard) {
        return {Kind::tween, d, e, {}, {}};
    }
    [[nodiscard]] static Transition spring_with(anim::SpringParams p = anim::springs::standard) {
        Transition t;
        t.kind = Kind::spring;
        t.spring = p;
        return t;
    }
    // A motion token of the registry ("motion.fast", "motion.spring.snappy").
    [[nodiscard]] static std::optional<Transition> token(std::string_view name);
};

// ── elements ──────────────────────────────────────────────────────────────

using Handler = std::function<void(const Value& args)>;

struct Element;

// Children that are rebuilt when what their builder reads changes: `when`
// and `each` (keyed, reconciled by key).
struct Region {
    std::function<std::vector<Element>()> build;
};

struct Element {
    const ComponentType*                             type = nullptr;
    std::string                                      id;   // explicit id (S-8: never display text)
    bool                                             generated_id = false;  // an immediate-mode id (`type:i…`)
    std::string                                      key;  // identity among siblings of a region (defaults to id)
    std::vector<std::pair<std::string, Source>>      props;
    std::vector<std::pair<std::string, Handler>>     handlers;
    std::vector<std::pair<std::string, Transition>>  transitions;
    std::vector<Element>                             children;
    std::shared_ptr<Region>                          region;  // children from a reactive builder
    // Configures the component instance once, before it mounts: what is not
    // a Value (a painter's function, a model object) reaches it this way.
    std::function<void(void* instance)>              configure;
    std::source_location                             where = std::source_location::current();

    Element& prop(std::string name, Source s);
    template <PropertyType T>
    Element& prop(std::string name, const Prop<T>& p) {
        if (p.is_set()) prop(std::move(name), p.source());
        return *this;
    }
    Element& on(std::string event, Handler h);
    Element& animate(std::string property, Transition t);
    Element& child(Element e);
};

// The view type of builders: an element tree.
using View = Element;

// What a reactive argument reads: a signal's or a computed value's value, or
// a plain value as it is.
template <class A>
struct Read {
    using type = A;
    static const A& get(const A& a) { return a; }
};
template <class T>
struct Read<Signal<T>> {
    using type = T;
    static const T& get(const Signal<T>& s) { return s.get(); }
};
template <class T>
struct Read<Computed<T>> {
    using type = T;
    static const T& get(const Computed<T>& c) { return c.get(); }
};

// A text property formatted from signals, checked at compile time like
// std::format: `omui::fmt("clicked {} times", count)` re-formats when
// `count` changes.
template <class... A>
[[nodiscard]] Source fmt(std::format_string<const typename Read<A>::type&...> format, A... args) {
    return Source{std::nullopt,
                  [f = format.get(), sources = std::tuple<A...>(std::move(args)...)] {
                      return std::apply(
                          [&](const A&... a) {
                              std::tuple<typename Read<A>::type...> values{Read<A>::get(a)...};
                              return Value(std::apply([&](auto&... v) { return std::vformat(f, std::make_format_args(v...)); }, values));
                          },
                          sources);
                  },
                  {}};
}

// A property computed from signals by a function, re-read when they change.
template <class F>
    requires std::invocable<F&>
[[nodiscard]] Source derive(F f) {
    using R = std::remove_cvref_t<std::invoke_result_t<F&>>;
    return Source{std::nullopt, [f = std::move(f)]() mutable { return ValueOf<R>::to(f()); }, {}};
}

// A region of children rebuilt when what `build` reads changes.
[[nodiscard]] Element dynamic(std::function<std::vector<Element>()> build,
                              std::source_location where = std::source_location::current());

// `then` when cond() is true, else `otherwise`.
[[nodiscard]] Element when(std::function<bool()> cond, std::function<View()> then,
                           std::function<View()> otherwise = {},
                           std::source_location where = std::source_location::current());

// One element per item of a list signal, reconciled by `key`.
template <class T, class KeyFn, class ItemFn>
[[nodiscard]] Element each(Signal<std::vector<T>> items, KeyFn key, ItemFn item,
                           std::source_location where = std::source_location::current()) {
    return dynamic(
        [items, key, item] {
            std::vector<Element> out;
            const auto& list = items.get();
            // Only the list is a dependency: what an item reads binds its own
            // properties, so changing one item's text does not rebuild the list.
            untracked([&] {
                for (std::size_t i = 0; i < list.size(); ++i) {
                    Element e = item(list[i], i);
                    if (e.key.empty()) e.key = std::format("{}", key(list[i]));
                    out.push_back(std::move(e));
                }
            });
            return out;
        },
        where);
}

}  // namespace omui
