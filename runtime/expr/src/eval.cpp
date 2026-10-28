module openmui.expr;

import std;
import openmui.spec;
import openmui.unicode;
import openmui.anim;
import openmui.i18n;
import :ast;
import :parse;
import :eval;

namespace omui::expr {

namespace {

// A message's argument as openmui.i18n takes it.
i18n::Arg message_arg(const Value& v) {
    switch (v.type()) {
        case Value::Type::boolean: return v.as_bool();
        case Value::Type::integer: return v.as_int();
        case Value::Type::real: return v.as_double();
        case Value::Type::text: return std::string(v.as_string());
        case Value::Type::list: {
            std::vector<std::string> items;
            for (const Value& x : *v.list()) items.emplace_back(x.type() == Value::Type::text ? std::string(x.as_string()) : x.json());
            return items;
        }
        default: return std::monostate{};
    }
}

// No catalog: the key is the message, written with no locale's data.
std::string untranslated(std::string_view key, const Value& args) {
    auto m = i18n::Message::parse(key);
    if (!m) return std::string(key);
    i18n::Args a;
    if (const auto* map = args.map())
        for (const auto& [k, v] : *map) a.set(k, message_arg(v));
    static const i18n::Formatter plain(std::make_shared<const i18n::LocaleData>());
    return m->format(plain, a);
}

}  // namespace


namespace {

using R = std::optional<Value>;  // nullopt: an error, recorded
using T = Value::Type;

constexpr std::int64_t imax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t imin = std::numeric_limits<std::int64_t>::min();

std::string_view type_name(const Value& v) {
    switch (v.type()) {
        case T::null: return "null";
        case T::boolean: return "bool";
        case T::integer: return "int";
        case T::real: return "double";
        case T::text: return "string";
        case T::list: return "list";
        case T::map: return "map";
    }
    return "?";
}

std::string_view symbol(Op op) noexcept {
    switch (op) {
        case Op::mul: return "*"; case Op::div: return "/"; case Op::mod: return "%";
        case Op::add: return "+"; case Op::sub: return "-";
        default: return name(op);
    }
}

bool is_int(const Value& v) { return v.type() == T::integer; }
bool is_num(const Value& v) { return v.is_number(); }

// Equality across int and double, deep for lists and maps (CEL).
bool equal(const Value& a, const Value& b) {
    if (is_num(a) && is_num(b)) {
        if (is_int(a) && is_int(b)) return a.as_int() == b.as_int();
        return a.as_double() == b.as_double();
    }
    if (a.type() != b.type()) return false;
    if (const auto* la = a.list()) {
        const auto* lb = b.list();
        if (la->size() != lb->size()) return false;
        for (std::size_t i = 0; i < la->size(); ++i)
            if (!equal((*la)[i], (*lb)[i])) return false;
        return true;
    }
    if (const auto* ma = a.map()) {
        const auto* mb = b.map();
        if (ma->size() != mb->size()) return false;
        for (const auto& [k, v] : *ma) {
            const Value* w = b.get(k);
            if (!w || !equal(v, *w)) return false;
        }
        return true;
    }
    return a == b;
}

// The functions of registry/expr.toml (spec's generated table), sorted.
constexpr auto function_names = [] {
    std::array<std::string_view, spec::functions.size()> names{};
    for (std::size_t i = 0; i < names.size(); ++i) names[i] = spec::functions[i].name;
    std::ranges::sort(names);
    return names;
}();

std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    std::iota(row.begin(), row.end(), std::size_t{0});
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (std::tolower(a[i - 1]) == std::tolower(b[j - 1]) ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

std::string nearest(std::string_view name, std::span<const std::string_view> names) {
    std::string_view best;
    std::size_t d = 3;
    for (std::string_view n : names)
        if (const std::size_t x = distance(name, n); x < d) {
            d = x;
            best = n;
        }
    return best.empty() ? std::string() : std::format("did you mean `{}`?", best);
}

bool white(char32_t c) {
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

struct Eval {
    const Resolve&           names;
    const Context&           ctx;
    std::vector<Diagnostic>& errors;
    int                      depth = 0;

    R fail(const Node& n, std::string message, std::string hint = {}) {
        Diagnostic d;
        d.at = n.at;
        d.message = std::move(message);
        d.hint = std::move(hint);
        errors.push_back(std::move(d));
        return std::nullopt;
    }

    R eval(const Node& n) {
        if (++depth > max_depth) {
            --depth;
            return fail(n, std::format("nested deeper than {} levels", max_depth));
        }
        R r = dispatch(n);
        --depth;
        return r;
    }

    R dispatch(const Node& n) {
        switch (n.op) {
            case Op::literal: return n.value;
            case Op::ref: {
                if (names)
                    if (auto v = names(n.name)) return std::move(*v);
                return fail(n, std::format("`{}` is not defined here", n.name));
            }
            case Op::member: {
                R o = eval(*n.args[0]);
                if (!o) return o;
                if (!o->map()) return fail(n, std::format("`.{}` of a {}: only maps have fields", n.name, type_name(*o)));
                if (const Value* f = o->get(n.name)) return *f;
                return fail(n, std::format("no field `{}`", n.name), std::format("test it first: has(….{})", n.name));
            }
            case Op::has: {
                R o = eval(*n.args[0]);
                if (!o) return o;
                if (!o->map()) return fail(n, std::format("has() of a {}: only maps have fields", type_name(*o)));
                return Value(o->get(n.name) != nullptr);
            }
            case Op::index: return index(n);
            case Op::neg: {
                R a = eval(*n.args[0]);
                if (!a) return a;
                if (is_int(*a)) {
                    if (a->as_int() == imin) return fail(n, "the int is too large to negate");
                    return Value(-a->as_int());
                }
                if (a->type() == T::real) return Value(-a->as_double());
                return fail(n, std::format("- of a {}", type_name(*a)));
            }
            case Op::not_: {
                R a = eval(*n.args[0]);
                if (!a) return a;
                if (a->type() != T::boolean) return fail(n, std::format("! of a {}: it takes a bool", type_name(*a)));
                return Value(!a->as_bool());
            }
            case Op::and_:
            case Op::or_: return logic(n);
            case Op::select: {
                R c = eval(*n.args[0]);
                if (!c) return c;
                if (c->type() != T::boolean) return fail(n, std::format("the condition is a {}: it must be a bool", type_name(*c)), "compare it: x != null, size(s) > 0");
                return eval(*n.args[c->as_bool() ? 1 : 2]);
            }
            case Op::list: {
                Value::List out;
                for (const auto& a : n.args) {
                    R v = eval(*a);
                    if (!v) return v;
                    out.push_back(std::move(*v));
                }
                return Value(std::move(out));
            }
            case Op::map: {
                Value::Map out;
                for (std::size_t i = 0; i + 1 < n.args.size(); i += 2) {
                    R k = eval(*n.args[i]);
                    if (!k) return k;
                    if (k->type() != T::text) return fail(*n.args[i], std::format("a map key is a {}: keys are strings", type_name(*k)));
                    R v = eval(*n.args[i + 1]);
                    if (!v) return v;
                    const std::string key(k->as_string());
                    if (std::ranges::any_of(out, [&](const auto& e) { return e.first == key; })) return fail(*n.args[i], std::format("the key `{}` twice", key));
                    out.emplace_back(key, std::move(*v));
                }
                return Value(std::move(out));
            }
            case Op::call: return call(n);
            default: return binary(n);
        }
    }

    R index(const Node& n) {
        R o = eval(*n.args[0]);
        if (!o) return o;
        R k = eval(*n.args[1]);
        if (!k) return k;
        if (const auto* l = o->list()) {
            if (!is_int(*k)) return fail(n, std::format("a list is indexed by an int, not a {}", type_name(*k)));
            const std::int64_t i = k->as_int();
            if (i < 0 || static_cast<std::size_t>(i) >= l->size()) return fail(n, std::format("index {} of a list of {}", i, l->size()), "lookup(list, i, default) gives a default instead");
            return (*l)[static_cast<std::size_t>(i)];
        }
        if (o->map()) {
            if (k->type() != T::text) return fail(n, std::format("a map is indexed by a string, not a {}", type_name(*k)));
            if (const Value* v = o->get(k->as_string())) return *v;
            return fail(n, std::format("no key `{}`", k->as_string()), "lookup(map, key, default) gives a default instead");
        }
        return fail(n, std::format("[] of a {}: only lists and maps are indexed", type_name(*o)));
    }

    // && and ||, with CEL's absorption: false && error is false, true || error is true.
    R logic(const Node& n) {
        const bool is_and = n.op == Op::and_;
        const std::size_t mark = errors.size();
        R a = eval(*n.args[0]);
        if (a && a->type() != T::boolean) a = fail(*n.args[0], std::format("{} takes bools, not a {}", is_and ? "&&" : "||", type_name(*a)));
        if (a && a->as_bool() != is_and) return a;  // decided by the left side
        R b = eval(*n.args[1]);
        if (b && b->type() != T::boolean) b = fail(*n.args[1], std::format("{} takes bools, not a {}", is_and ? "&&" : "||", type_name(*b)));
        if (b && b->as_bool() != is_and) {
            errors.resize(mark);  // the right side decides: an error on the left is absorbed
            return b;
        }
        if (!a || !b) return std::nullopt;
        return Value(is_and);
    }

    std::optional<int> order(const Node& n, const Value& a, const Value& b) {
        if (is_num(a) && is_num(b)) {
            if (is_int(a) && is_int(b)) return a.as_int() < b.as_int() ? -1 : a.as_int() > b.as_int() ? 1 : 0;
            const double x = a.as_double(), y = b.as_double();
            if (x != x || y != y) return std::nullopt;  // NaN: unordered (every comparison false)
            return x < y ? -1 : x > y ? 1 : 0;
        }
        if (a.type() == T::text && b.type() == T::text) {
            const int c = a.as_string().compare(b.as_string());
            return c < 0 ? -1 : c > 0 ? 1 : 0;
        }
        if (a.type() == T::boolean && b.type() == T::boolean) return static_cast<int>(a.as_bool()) - static_cast<int>(b.as_bool());
        (void)fail(n, std::format("a {} and a {} have no order", type_name(a), type_name(b)));
        return std::nullopt;
    }

    R arith(const Node& n, const Value& a, const Value& b) {
        const Op op = n.op;
        if (op == Op::add && a.type() == T::text && b.type() == T::text) return Value(std::string(a.as_string()) + std::string(b.as_string()));
        if (op == Op::add && a.list() && b.list()) {
            Value::List out = *a.list();
            out.insert(out.end(), b.list()->begin(), b.list()->end());
            return Value(std::move(out));
        }
        if (!is_num(a) || !is_num(b)) {
            const std::string hint = (a.type() == T::text || b.type() == T::text) && op == Op::add ? "join text with format('{}{}', a, b) or string(x)" : "";
            return fail(n, std::format("no `{}` for a {} and a {}", symbol(op), type_name(a), type_name(b)), hint);
        }
        if (is_int(a) && is_int(b)) {
            const std::int64_t x = a.as_int(), y = b.as_int();
            switch (op) {
                case Op::add:
                    if ((y > 0 && x > imax - y) || (y < 0 && x < imin - y)) return fail(n, "the int overflows");
                    return Value(x + y);
                case Op::sub:
                    if ((y < 0 && x > imax + y) || (y > 0 && x < imin + y)) return fail(n, "the int overflows");
                    return Value(x - y);
                case Op::mul: {
                    const bool over = x > 0 ? (y > 0 ? x > imax / y : y < imin / x) : (y > 0 ? x < imin / y : x != 0 && y < imax / x);
                    if (over) return fail(n, "the int overflows");
                    return Value(x * y);
                }
                case Op::div:
                    if (y == 0) return fail(n, "division by zero", "divide doubles (1.0 / 0.0 is infinity) or test the divisor");
                    if (x == imin && y == -1) return fail(n, "the int overflows");
                    return Value(x / y);
                case Op::mod:
                    if (y == 0) return fail(n, "modulus by zero");
                    if (y == -1) return Value(std::int64_t{0});
                    return Value(x % y);
                default: break;
            }
        }
        const double x = a.as_double(), y = b.as_double();
        switch (op) {
            case Op::add: return Value(x + y);
            case Op::sub: return Value(x - y);
            case Op::mul: return Value(x * y);
            case Op::div: return Value(x / y);
            case Op::mod: return fail(n, "% takes ints", "use floor: x - floor(x / y) * y");
            default: break;
        }
        return fail(n, "not an arithmetic operator");
    }

    R binary(const Node& n) {
        R a = eval(*n.args[0]);
        if (!a) return a;
        R b = eval(*n.args[1]);
        if (!b) return b;
        switch (n.op) {
            case Op::eq: return Value(equal(*a, *b));
            case Op::ne: return Value(!equal(*a, *b));
            case Op::lt: case Op::le: case Op::gt: case Op::ge: {
                const std::size_t mark = errors.size();
                const auto c = order(n, *a, *b);
                if (!c) {
                    if (errors.size() > mark) return std::nullopt;
                    return Value(false);  // NaN
                }
                switch (n.op) {
                    case Op::lt: return Value(*c < 0);
                    case Op::le: return Value(*c <= 0);
                    case Op::gt: return Value(*c > 0);
                    default: return Value(*c >= 0);
                }
            }
            case Op::in: {
                if (const auto* l = b->list()) return Value(std::ranges::any_of(*l, [&](const Value& x) { return equal(*a, x); }));
                if (b->map()) {
                    if (a->type() != T::text) return fail(n, "a map's keys are strings");
                    return Value(b->get(a->as_string()) != nullptr);
                }
                return fail(n, std::format("`in` a {}: it takes a list or a map", type_name(*b)), "for text: contains(s, part)");
            }
            default: return arith(n, *a, *b);
        }
    }

    // ── functions ──

    R expect_args(const Node& n, std::size_t lo, std::size_t hi) {
        if (n.args.size() < lo || n.args.size() > hi)
            return fail(n, lo == hi ? std::format("{}() takes {} argument{}", n.name, lo, lo == 1 ? "" : "s")
                                    : std::format("{}() takes {} to {} arguments", n.name, lo, hi));
        return Value(true);
    }

    std::optional<std::vector<Value>> args_of(const Node& n) {
        std::vector<Value> out;
        out.reserve(n.args.size());
        for (const auto& a : n.args) {
            R v = eval(*a);
            if (!v) return std::nullopt;
            out.push_back(std::move(*v));
        }
        return out;
    }

    // `strict`: every argument has its {} (format); a translation may leave
    // some out (tr: "{} items" in one language, "one item" in another).
    R format(const Node& n, std::string_view pattern, std::span<const Value> args, bool strict = true) {
        std::string out;
        std::size_t next = 0;
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            const char c = pattern[i];
            if ((c == '{' || c == '}') && i + 1 < pattern.size() && pattern[i + 1] == c) {
                out += c;
                ++i;
                continue;
            }
            if (c == '{' && i + 1 < pattern.size() && pattern[i + 1] == '}') {
                if (next >= args.size()) return fail(n, std::format("the pattern has more {{}} than the {} argument{} given", args.size(), args.size() == 1 ? "" : "s"));
                out += text_of(args[next++]);
                ++i;
                continue;
            }
            if (c == '{' || c == '}') return fail(n, std::format("a lone `{}` in the pattern", c), "write {{ or }} for a brace");
            out += c;
        }
        if (strict && next != args.size()) return fail(n, std::format("{} argument{} more than the pattern's {{}}", args.size() - next, args.size() - next == 1 ? "" : "s"));
        return Value(std::move(out));
    }

    R number_or_fail(const Node& n, const Value& v, std::string_view what) {
        if (!is_num(v)) return fail(n, std::format("{}() takes numbers, not a {}", what, type_name(v)));
        return v;
    }

    R to_int_from_double(const Node& n, double d) {
        if (d != d || d >= 9223372036854775808.0 || d < -9223372036854775808.0) return fail(n, std::format("{} does not fit in an int", d));
        return Value(static_cast<std::int64_t>(d));
    }

    R call(const Node& n) {
        const std::string_view f = n.name;
        auto args = args_of(n);
        if (!args) return std::nullopt;
        std::vector<Value>& a = *args;
        auto need = [&](std::size_t lo, std::size_t hi) { return expect_args(n, lo, hi).has_value(); };
        auto text = [&](std::size_t i) -> std::optional<std::string_view> {
            if (a[i].type() != T::text) {
                (void)fail(n, std::format("{}() takes a string as argument {}, not a {}", f, i + 1, type_name(a[i])));
                return std::nullopt;
            }
            return a[i].as_string();
        };
        if (f == "format") {
            if (a.empty()) return fail(n, "format() takes a pattern");
            auto p = text(0);
            if (!p) return std::nullopt;
            return format(n, *p, std::span(a).subspan(1));
        }
        if (f == "tr") {
            if (a.empty() || a.size() > 2) return fail(n, "tr() takes a message key and a map of its arguments", "tr('inbox.count', {'count': count})");
            auto key = text(0);
            if (!key) return std::nullopt;
            const Value args = a.size() == 2 ? a[1] : Value(Value::Map{});
            if (!args.map()) return fail(n, std::format("tr()'s arguments are a map by name, not a {}", type_name(args)), "tr('inbox.count', {'count': count})");
            if (ctx.translate) return Value(ctx.translate(*key, args));
            return Value(untranslated(*key, args));
        }
        if (f == "size" || f == "len") {
            if (!need(1, 1)) return std::nullopt;
            if (a[0].type() == T::text) return Value(static_cast<std::int64_t>(unicode::grapheme_count(a[0].as_string())));
            if (const auto* l = a[0].list()) return Value(static_cast<std::int64_t>(l->size()));
            if (const auto* m = a[0].map()) return Value(static_cast<std::int64_t>(m->size()));
            return fail(n, std::format("{}() of a {}", f, type_name(a[0])));
        }
        if (f == "contains") {
            if (!need(2, 2)) return std::nullopt;
            if (a[0].type() == T::text) {
                auto t = text(1);
                if (!t) return std::nullopt;
                return Value(a[0].as_string().find(*t) != std::string_view::npos);
            }
            if (const auto* l = a[0].list()) return Value(std::ranges::any_of(*l, [&](const Value& x) { return equal(x, a[1]); }));
            return fail(n, std::format("contains() in a {}", type_name(a[0])));
        }
        if (f == "startsWith" || f == "endsWith") {
            if (!need(2, 2)) return std::nullopt;
            auto s = text(0), t = s ? text(1) : std::nullopt;
            if (!s || !t) return std::nullopt;
            return Value(f == "startsWith" ? s->starts_with(*t) : s->ends_with(*t));
        }
        if (f == "lower" || f == "upper" || f == "trim") {
            if (!need(1, 1)) return std::nullopt;
            auto s = text(0);
            if (!s) return std::nullopt;
            if (f == "lower") return Value(unicode::lower(*s));
            if (f == "upper") return Value(unicode::upper(*s));
            std::size_t b = 0, e = s->size();
            while (b < e) {
                const auto d = unicode::decode(*s, b);
                if (!white(d.cp)) break;
                b += d.length;
            }
            while (e > b) {
                const std::size_t p = unicode::previous(*s, e);
                if (!white(unicode::decode(*s, p).cp)) break;
                e = p;
            }
            return Value(s->substr(b, e - b));
        }
        if (f == "min" || f == "max") {
            if (a.empty()) return fail(n, std::format("{}() takes at least one number", f));
            bool any_real = false;
            for (const Value& v : a) {
                if (!number_or_fail(n, v, f)) return std::nullopt;
                any_real = any_real || v.type() == T::real;
            }
            std::size_t best = 0;
            for (std::size_t i = 1; i < a.size(); ++i) {
                const bool better = any_real ? (f == "min" ? a[i].as_double() < a[best].as_double() : a[i].as_double() > a[best].as_double())
                                             : (f == "min" ? a[i].as_int() < a[best].as_int() : a[i].as_int() > a[best].as_int());
                if (better) best = i;
            }
            return any_real ? Value(a[best].as_double()) : a[best];
        }
        if (f == "clamp") {
            if (!need(3, 3)) return std::nullopt;
            for (const Value& v : a)
                if (!number_or_fail(n, v, f)) return std::nullopt;
            if (a[1].as_double() > a[2].as_double()) return fail(n, "clamp(x, lo, hi) with lo above hi");
            if (is_int(a[0]) && is_int(a[1]) && is_int(a[2])) return Value(std::clamp(a[0].as_int(), a[1].as_int(), a[2].as_int()));
            return Value(std::clamp(a[0].as_double(), a[1].as_double(), a[2].as_double()));
        }
        if (f == "abs") {
            if (!need(1, 1) || !number_or_fail(n, a[0], f)) return std::nullopt;
            if (is_int(a[0])) {
                if (a[0].as_int() == imin) return fail(n, "the int overflows");
                return Value(a[0].as_int() < 0 ? -a[0].as_int() : a[0].as_int());
            }
            return Value(std::abs(a[0].as_double()));
        }
        if (f == "round" || f == "floor" || f == "ceil") {
            if (!need(1, 1) || !number_or_fail(n, a[0], f)) return std::nullopt;
            if (is_int(a[0])) return a[0];
            const double d = a[0].as_double();
            return to_int_from_double(n, f == "round" ? std::round(d) : f == "floor" ? std::floor(d) : std::ceil(d));
        }
        if (f == "int") {
            if (!need(1, 1)) return std::nullopt;
            switch (a[0].type()) {
                case T::integer: return a[0];
                case T::real: {
                    const double d = a[0].as_double();
                    if (d != d || d >= 9223372036854775808.0 || d < -9223372036854775808.0) return Value(std::int64_t{0});
                    return Value(static_cast<std::int64_t>(d));
                }
                case T::boolean: return Value(std::int64_t{a[0].as_bool() ? 1 : 0});
                case T::text: {
                    std::int64_t v = 0;
                    const std::string_view s = a[0].as_string();
                    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
                    return Value(r.ec == std::errc{} && r.ptr == s.data() + s.size() ? v : std::int64_t{0});
                }
                default: return Value(std::int64_t{0});
            }
        }
        if (f == "double") {
            if (!need(1, 1)) return std::nullopt;
            switch (a[0].type()) {
                case T::integer: case T::real: return Value(a[0].as_double());
                case T::boolean: return Value(a[0].as_bool() ? 1.0 : 0.0);
                case T::text: {
                    double v = 0;
                    const std::string_view s = a[0].as_string();
                    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
                    return Value(r.ec == std::errc{} && r.ptr == s.data() + s.size() ? v : 0.0);
                }
                default: return Value(0.0);
            }
        }
        if (f == "string") {
            if (!need(1, 1)) return std::nullopt;
            return Value(text_of(a[0]));
        }
        if (f == "lookup") {
            if (!need(2, 3)) return std::nullopt;
            const Value fallback = a.size() == 3 ? a[2] : Value{};
            if (const auto* l = a[0].list()) {
                if (!is_int(a[1])) return fail(n, std::format("lookup() in a list takes an int index, not a {}", type_name(a[1])));
                const std::int64_t i = a[1].as_int();
                return i >= 0 && static_cast<std::size_t>(i) < l->size() ? (*l)[static_cast<std::size_t>(i)] : fallback;
            }
            if (a[0].map()) {
                if (a[1].type() != T::text) return fail(n, std::format("lookup() in a map takes a string key, not a {}", type_name(a[1])));
                const Value* v = a[0].get(a[1].as_string());
                return v ? *v : fallback;
            }
            if (a[0].is_null()) return fallback;
            return fail(n, std::format("lookup() in a {}", type_name(a[0])));
        }
        if (f == "interpolate") {
            if (!need(3, 4) || !number_or_fail(n, a[0], f)) return std::nullopt;
            const auto* in = a[1].list();
            const auto* out = a[2].list();
            if (!in || !out || in->size() != out->size() || in->size() < 2)
                return fail(n, "interpolate(x, input, output) takes two lists of the same length, at least 2");
            for (std::size_t i = 0; i < in->size(); ++i) {
                if (!is_num((*in)[i]) || !is_num((*out)[i])) return fail(n, "interpolate() takes lists of numbers");
                if (i > 0 && !((*in)[i].as_double() > (*in)[i - 1].as_double())) return fail(n, "interpolate()'s input must increase");
            }
            anim::Ease ease = anim::ease::linear;
            if (a.size() == 4) {
                auto e = text(3);
                if (!e) return std::nullopt;
                auto parsed = anim::parse_ease(*e);
                if (!parsed) return fail(n, std::format("no ease is named `{}`", *e), "out_cubic, in_out_sine, cubic-bezier(…) …");
                ease = *parsed;
            }
            const double x = a[0].as_double();
            const std::size_t last = in->size() - 1;
            if (x <= (*in)[0].as_double()) return Value((*out)[0].as_double());
            if (x >= (*in)[last].as_double()) return Value((*out)[last].as_double());
            std::size_t i = 1;
            while (x > (*in)[i].as_double()) ++i;
            const double x0 = (*in)[i - 1].as_double(), x1 = (*in)[i].as_double();
            const double y0 = (*out)[i - 1].as_double(), y1 = (*out)[i].as_double();
            return Value(anim::dm::mix(y0, y1, ease((x - x0) / (x1 - x0))));
        }
        if (f == "ease") {
            if (!need(2, 2)) return std::nullopt;
            auto e = text(0);
            if (!e || !number_or_fail(n, a[1], f)) return std::nullopt;
            auto parsed = anim::parse_ease(*e);
            if (!parsed) return fail(n, std::format("no ease is named `{}`", *e), "out_cubic, in_out_sine, cubic-bezier(…) …");
            return Value((*parsed)(a[1].as_double()));
        }
        if (f == "spring") {
            if (!need(3, 6)) return std::nullopt;
            for (const Value& v : a)
                if (!number_or_fail(n, v, f)) return std::nullopt;
            anim::SpringParams p = anim::springs::standard;
            if (a.size() > 3) p.stiffness = a[3].as_double();
            if (a.size() > 4) p.damping = a[4].as_double();
            if (a.size() > 5) p.mass = a[5].as_double();
            if (!(p.stiffness > 0) || !(p.mass > 0) || p.damping < 0) return fail(n, "a spring needs stiffness > 0, mass > 0, damping ≥ 0");
            const anim::Oscillator o(a[1].as_double(), 0, a[2].as_double(), p);
            return Value(o.position(std::max(0.0, a[0].as_double())));
        }
        return fail(n, std::format("no function `{}`", f), nearest(f, function_names));
    }
};

}  // namespace

std::string text_of(const Value& v) {
    switch (v.type()) {
        case T::null: return "null";
        case T::boolean: return v.as_bool() ? "true" : "false";
        case T::integer: return std::to_string(v.as_int());
        case T::real: {
            char buf[64];
            const auto r = std::to_chars(buf, buf + sizeof buf, v.as_double());
            return std::string(buf, r.ptr);
        }
        case T::text: return std::string(v.as_string());
        default: return v.json();
    }
}

std::span<const std::string_view> functions() noexcept { return function_names; }

Result evaluate(const Expr& e, const Resolve& names, const Context& context) {
    Result r;
    Eval ev{names, context, r.errors};
    if (e.node().height > max_depth) {  // built in code: parse() already refuses these
        (void)ev.fail(e.node(), std::format("nested deeper than {} levels", max_depth), "split it into simpler expressions");
        return r;
    }
    if (auto v = ev.eval(e.node())) r.value = std::move(*v);
    return r;
}

Result evaluate(const Expr& e, const Value::Map& names, const Context& context) {
    const Resolve resolve = [&names](std::string_view n) -> std::optional<Value> {
        for (const auto& [k, v] : names)
            if (k == n) return v;
        return std::nullopt;
    };
    return evaluate(e, resolve, context);
}

}  // namespace omui::expr
