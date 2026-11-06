// openmui.reflect — C++26 reflection's convenience layer (D-39; GCC 16,
// -freflection; nothing else in openmui depends on it).
//
//     struct Settings {
//         std::string name;
//         [[=omui::reflect::range{0, 100}]] int volume = 30;
//         bool dark = false;
//         Size size = Size::medium;          // an enum: a select of its names
//     };
//     auto settings = omui::signal(Settings{});
//     rt.mount("main", omui::reflect::form(settings));   // ids: name, volume, dark, size
//
// A form has a control for each public member, by its type: a text box for a
// string, a toggle for a bool, a slider for a number, a select for an enum,
// a group for a struct. Every control is bound both ways to the signal; the
// members' names are the semantic ids (S-8) and, as people read them
// ("dark_mode" → "Dark mode"), the labels. Annotations say the rest:
// `range{min, max, step}`, `label{"…"}`, `hidden{}`.
//
// to_value / from_value convert a struct to openmui's values and back (a
// model's rows, a verb's arguments, a script's data); fields<T>() describes
// its members as the rows declare them.
export module openmui.reflect;

import std;
import openmui.core;
import openmui.view;

export namespace omui::reflect {

// ── annotations ─────────────────────────────────────────────────────────────

// A number's range in a form: [[=omui::reflect::range{0, 100}]] int volume;
struct range {
    double min = 0;
    double max = 100;
    double step = 1;
};

// The name people read, when the member's own is not it. An array, not a
// pointer: an annotation is a constant, and a string literal's address is not
// one.
struct label {
    char text[64]{};
};

// Not in the form.
struct hidden {};

// A member's name as people read it: "dark_mode" → "Dark mode".
[[nodiscard]] std::string human(std::string_view identifier);

namespace detail {

template <class T>
concept Text = std::same_as<T, std::string>;

template <class T>
concept Number = (std::integral<T> || std::floating_point<T>) && !std::same_as<T, bool>;

template <class T>
concept Enum = std::is_enum_v<T>;

template <class T>
concept Record = std::is_class_v<T> && std::is_aggregate_v<T> && !Text<T>;

template <class T>
struct is_vector : std::false_type {};
template <class U>
struct is_vector<std::vector<U>> : std::true_type {
    using item = U;
};

template <class T>
struct is_optional : std::false_type {};
template <class U>
struct is_optional<std::optional<U>> : std::true_type {
    using item = U;
};

consteval std::meta::access_context access() { return std::meta::access_context::unchecked(); }

template <class T>
consteval auto members() {
    return std::define_static_array(std::meta::nonstatic_data_members_of(^^T, access()));
}

template <class A>
consteval std::optional<A> annotation(std::meta::info member) {
    for (std::meta::info a : std::meta::annotations_of(member))
        if (std::meta::type_of(a) == ^^A || std::meta::type_of(a) == ^^const A) return std::meta::extract<A>(a);
    return std::nullopt;
}

template <class A>
consteval bool annotated(std::meta::info member) {
    return annotation<A>(member).has_value();
}

template <Enum E>
std::string_view name_of(E v) {
    template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E)))
        if (v == [:e:]) return std::meta::identifier_of(e);
    return {};
}

template <Enum E>
std::optional<E> parse(std::string_view name) {
    template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E)))
        if (name == std::meta::identifier_of(e)) return [:e:];
    return std::nullopt;
}

template <Enum E>
Value names() {
    Value::List out;
    template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) out.emplace_back(std::meta::identifier_of(e));
    return Value(std::move(out));
}

template <Enum E>
Value labels() {
    Value::List out;
    template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) out.emplace_back(human(std::meta::identifier_of(e)));
    return Value(std::move(out));
}

// A part of a value the form edits: how to read it, how to write it.
template <class T>
struct Lens {
    std::function<T()>          get;
    std::function<void(T)>      set;
};

template <class R, std::meta::info M>
auto member_lens(const Lens<R>& outer) {
    using F = [:std::meta::type_of(M):];
    return Lens<F>{[outer] { return outer.get().[:M:]; }, [outer](F v) {
                       R r = outer.get();
                       r.[:M:] = std::move(v);
                       outer.set(std::move(r));
                   }};
}

inline Element make(std::string_view type) {
    Element e;
    e.type = find_type(type);
    return e;
}

inline Source constant(Value v) { return Source{std::move(v), {}, {}}; }

template <class T>
Element form_of(const Lens<T>& lens, std::string id, std::string name);

template <class F>
Element control(const Lens<F>& lens, std::string id, std::string name, std::optional<range> r) {
    if constexpr (Text<F>) {
        Element e = make("text_box");
        e.id = std::move(id);
        e.prop("label", constant(Value(name)));
        e.prop("text", Source{std::nullopt, [lens] { return Value(lens.get()); }, [lens](const Value& v) { lens.set(std::string(v.as_string())); }});
        return e;
    } else if constexpr (std::same_as<F, bool>) {
        Element e = make("toggle");
        e.id = std::move(id);
        e.prop("label", constant(Value(name)));
        e.prop("checked", Source{std::nullopt, [lens] { return Value(lens.get()); }, [lens](const Value& v) { lens.set(v.as_bool()); }});
        return e;
    } else if constexpr (Number<F>) {
        const range x = r.value_or(range{0, 100, std::integral<F> ? 1.0 : 0.0});
        Element e = make("slider");
        e.id = std::move(id);
        e.prop("label", constant(Value(name)));
        e.prop("min", constant(Value(x.min)));
        e.prop("max", constant(Value(x.max)));
        e.prop("step", constant(Value(x.step)));
        e.prop("value", Source{std::nullopt, [lens] { return Value(static_cast<double>(lens.get())); },
                               [lens](const Value& v) {
                                   const double d = v.as_double();
                                   lens.set(std::integral<F> ? static_cast<F>(std::llround(d)) : static_cast<F>(d));
                               }});
        return e;
    } else if constexpr (Enum<F>) {
        Element e = make("select");
        e.id = std::move(id);
        e.prop("label", constant(Value(name)));
        e.prop("options", constant(names<F>()));
        e.prop("labels", constant(labels<F>()));
        e.prop("value", Source{std::nullopt, [lens] { return Value(std::string(name_of(lens.get()))); },
                               [lens](const Value& v) {
                                   if (auto x = parse<F>(v.as_string())) lens.set(*x);
                               }});
        return e;
    } else if constexpr (Record<F>) {
        return form_of(lens, std::move(id), std::move(name));
    } else {
        static_assert(Record<F>, "a form edits strings, bools, numbers, enums and structs of them");
    }
}

template <class T>
Element form_of(const Lens<T>& lens, std::string id, std::string name) {
    Element col = make("column");
    col.id = std::move(id);
    col.prop("gap", constant(Value("s")));
    if (!name.empty()) {
        Element heading = make("text");
        heading.prop("text", constant(Value(name)));
        heading.prop("style", constant(Value("label")));
        col.child(std::move(heading));
    }
    template for (constexpr auto m : members<T>()) {
        if constexpr (!annotated<hidden>(m)) {
            constexpr auto l = annotation<label>(m);
            const std::string_view ident = std::meta::identifier_of(m);
            col.child(control(member_lens<T, m>(lens), std::string(ident), l ? std::string(l->text) : human(ident), annotation<range>(m)));
        }
    }
    return col;
}

}  // namespace detail

// A form for T's members, bound both ways to `value`.
template <detail::Record T>
[[nodiscard]] Element form(Signal<T> value) {
    detail::Lens<T> lens{[value] { return value.get(); }, [value](T v) { value.set(std::move(v)); }};
    return detail::form_of(lens, {}, {});
}

// ── values ──────────────────────────────────────────────────────────────────

// A value of T as openmui's values: a map of its members (enums by their
// names, vectors as lists, optionals as the value or null).
template <class T>
[[nodiscard]] Value to_value(const T& v) {
    if constexpr (detail::Text<T>) return Value(v);
    else if constexpr (std::same_as<T, bool>) return Value(v);
    else if constexpr (std::integral<T>) return Value(static_cast<std::int64_t>(v));
    else if constexpr (std::floating_point<T>) return Value(static_cast<double>(v));
    else if constexpr (detail::Enum<T>) return Value(std::string(detail::name_of(v)));
    else if constexpr (detail::is_vector<T>::value) {
        Value::List out;
        for (const auto& x : v) out.push_back(to_value(x));
        return Value(std::move(out));
    } else if constexpr (detail::is_optional<T>::value) {
        return v ? to_value(*v) : Value{};
    } else {
        static_assert(detail::Record<T>, "to_value takes strings, bools, numbers, enums, vectors, optionals and structs of them");
        Value out(Value::Map{});
        template for (constexpr auto m : detail::members<T>()) out.set(std::string(std::meta::identifier_of(m)), to_value(v.[:m:]));
        return out;
    }
}

// openmui's values as a T: what is missing keeps T's default; what is of
// another type says where.
template <class T>
[[nodiscard]] std::expected<T, std::string> from_value(const Value& v, std::string_view where = "value") {
    if constexpr (detail::Text<T>) {
        if (v.type() != Value::Type::text) return std::unexpected(std::format("`{}` is not a string", where));
        return std::string(v.as_string());
    } else if constexpr (std::same_as<T, bool>) {
        if (v.type() != Value::Type::boolean) return std::unexpected(std::format("`{}` is not a bool", where));
        return v.as_bool();
    } else if constexpr (std::integral<T>) {
        if (!v.is_number()) return std::unexpected(std::format("`{}` is not a number", where));
        return static_cast<T>(v.as_int());
    } else if constexpr (std::floating_point<T>) {
        if (!v.is_number()) return std::unexpected(std::format("`{}` is not a number", where));
        return static_cast<T>(v.as_double());
    } else if constexpr (detail::Enum<T>) {
        auto e = detail::parse<T>(v.as_string());
        if (!e) return std::unexpected(std::format("`{}` is not one of {}", where, detail::names<T>().json()));
        return *e;
    } else if constexpr (detail::is_vector<T>::value) {
        const Value::List* l = v.list();
        if (!l) return std::unexpected(std::format("`{}` is not a list", where));
        T out;
        for (std::size_t i = 0; i < l->size(); ++i) {
            auto x = from_value<typename detail::is_vector<T>::item>((*l)[i], std::format("{}[{}]", where, i));
            if (!x) return std::unexpected(x.error());
            out.push_back(std::move(*x));
        }
        return out;
    } else if constexpr (detail::is_optional<T>::value) {
        if (v.is_null()) return T{};
        auto x = from_value<typename detail::is_optional<T>::item>(v, where);
        if (!x) return std::unexpected(x.error());
        return T{std::move(*x)};
    } else {
        static_assert(detail::Record<T>, "from_value makes strings, bools, numbers, enums, vectors, optionals and structs of them");
        if (!v.map()) return std::unexpected(std::format("`{}` is not a map", where));
        T out{};
        std::string problem;
        template for (constexpr auto m : detail::members<T>()) {
            const std::string name(std::meta::identifier_of(m));
            if (problem.empty())
                if (const Value* x = v.get(name)) {
                    auto f = from_value<typename[:std::meta::type_of(m):]>(*x, std::format("{}.{}", where, name));
                    if (f) out.[:m:] = std::move(*f);
                    else problem = f.error();
                }
        }
        if (!problem.empty()) return std::unexpected(problem);
        return out;
    }
}

// T's members as a model's rows declare them: [{"name", "type"}], with
// "string", "bool", "int", "number", the names of an enum, "list", "optional"
// or "object".
template <detail::Record T>
[[nodiscard]] Value fields() {
    Value::List out;
    template for (constexpr auto m : detail::members<T>()) {
        using F = [:std::meta::type_of(m):];
        Value type;
        if constexpr (detail::Text<F>) type = Value("string");
        else if constexpr (std::same_as<F, bool>) type = Value("bool");
        else if constexpr (std::integral<F>) type = Value("int");
        else if constexpr (std::floating_point<F>) type = Value("number");
        else if constexpr (detail::Enum<F>) type = detail::names<F>();
        else if constexpr (detail::is_vector<F>::value) type = Value("list");
        else if constexpr (detail::is_optional<F>::value) type = Value("optional");
        else type = Value("object");
        out.push_back(Value{}.set("name", std::string(std::meta::identifier_of(m))).set("type", std::move(type)));
    }
    return Value(std::move(out));
}

}  // namespace omui::reflect
