module openmui.i18n;

import std;

namespace omui::i18n {

namespace {

using Expression = Message::Expression;
using Pattern = Message::Pattern;

// ── parsing ──────────────────────────────────────────────────────────────

bool name_char(char c, bool first) noexcept {
    const auto u = static_cast<unsigned char>(c);
    if (u >= 0x80) return true;  // any non-ASCII character of a name (MF2 name-char)
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return true;
    return !first && ((c >= '0' && c <= '9') || c == '-' || c == '.');
}

struct Parser {
    std::string_view s;
    std::size_t      at = 0;
    std::string      error;

    bool fail(std::string_view what) {
        if (error.empty()) {
            int line = 1, column = 1;
            for (std::size_t i = 0; i < at && i < s.size(); ++i) {
                if (s[i] == '\n') {
                    ++line;
                    column = 1;
                } else if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
                    ++column;
                }
            }
            error = std::format("{}:{}: {}", line, column, what);
        }
        return false;
    }
    [[nodiscard]] bool end() const noexcept { return at >= s.size(); }
    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept { return at + ahead < s.size() ? s[at + ahead] : '\0'; }
    bool ws() {  // space, tab, CR, LF, and the ideographic space U+3000
        const std::size_t from = at;
        while (!end()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++at;
            else if (s.substr(at, 3) == "\xE3\x80\x80") at += 3;
            else break;
        }
        return at > from;
    }
    bool take(std::string_view t) {
        if (s.substr(at, t.size()) != t) return false;
        at += t.size();
        return true;
    }
    std::optional<std::string> name() {
        if (end() || !name_char(peek(), true)) return std::nullopt;
        const std::size_t from = at;
        while (!end() && name_char(peek(), false)) ++at;
        return std::string(s.substr(from, at - from));
    }
    // A function's identifier: a name, or namespace:name.
    std::optional<std::string> identifier() {
        auto n = name();
        if (!n) return std::nullopt;
        if (peek() == ':' && name_char(peek(1), true)) {
            ++at;
            auto rest = name();
            return *n + ":" + *rest;
        }
        return n;
    }
    // |quoted| (\| and \\ escaped) or unquoted: a name or a number.
    std::optional<std::string> literal() {
        if (take("|")) {
            std::string out;
            while (!end() && peek() != '|') {
                if (peek() == '\\' && (peek(1) == '|' || peek(1) == '\\' || peek(1) == '{' || peek(1) == '}')) ++at;
                out += s[at++];
            }
            if (!take("|")) {
                fail("a quoted literal without its closing |");
                return std::nullopt;
            }
            return out;
        }
        const std::size_t from = at;
        if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
            if (peek() == '-') ++at;
            while (!end() && ((peek() >= '0' && peek() <= '9') || peek() == '.' || peek() == 'e' || peek() == 'E' || peek() == '+' ||
                              (peek() == '-' && (s[at - 1] == 'e' || s[at - 1] == 'E'))))
                ++at;
            if (at > from) return std::string(s.substr(from, at - from));
        }
        return name();
    }
    // After the opening {: an expression; false with `markup` set for {#…}, {/…}.
    bool expression(Expression& e, bool& markup) {
        ws();
        markup = false;
        if (peek() == '#' || peek() == '/') {
            markup = true;
            int depth = 0;
            while (!end() && !(peek() == '}' && depth == 0)) {
                if (peek() == '|') depth = 1 - depth;
                ++at;
            }
            if (!take("}")) return fail("markup without its closing }");
            return true;
        }
        if (take("$")) {
            auto n = name();
            if (!n) return fail("a variable's name after $");
            e.kind = Expression::Kind::variable;
            e.operand = std::move(*n);
        } else if (peek() != ':' && peek() != '}' && peek() != '@') {
            auto l = literal();
            if (!l) return fail(error.empty() ? "an operand: $variable or a literal" : error);
            e.kind = Expression::Kind::literal;
            e.operand = std::move(*l);
        }
        ws();
        if (take(":")) {
            auto f = identifier();
            if (!f) return fail("a function's name after :");
            e.function = std::move(*f);
            while (true) {
                const std::size_t before = at;
                if (!ws()) break;
                auto opt = identifier();
                if (!opt) {
                    at = before;
                    break;
                }
                ws();
                if (!take("=")) return fail(std::format("`=` after the option `{}`", *opt));
                ws();
                Message::Option o;
                o.name = std::move(*opt);
                if (take("$")) {
                    auto v = name();
                    if (!v) return fail("a variable's name after $");
                    o.variable = true;
                    o.value = std::move(*v);
                } else {
                    auto v = literal();
                    if (!v) return fail(std::format("a value for the option `{}`", o.name));
                    o.value = std::move(*v);
                }
                e.options.push_back(std::move(o));
            }
        }
        // Attributes (@translate=no) are for tools, not the text.
        while (true) {
            ws();
            if (!take("@")) break;
            if (!identifier()) return fail("an attribute's name after @");
            ws();
            if (take("=")) {
                ws();
                if (take("$")) (void)name();
                else if (!literal()) return fail("an attribute's value");
            }
        }
        ws();
        if (e.kind == Expression::Kind::none && e.function.empty()) return fail("an empty expression {}");
        if (!take("}")) return fail("} to close the expression");
        return true;
    }
    // Text and placeholders, to the end (a simple message) or to }} (quoted).
    bool pattern(Pattern& out, bool quoted) {
        std::string text;
        auto flush = [&] {
            if (!text.empty()) out.push_back({std::move(text), std::nullopt});
            text.clear();
        };
        while (!end()) {
            const char c = peek();
            if (quoted && c == '}' && peek(1) == '}') {
                at += 2;
                flush();
                return true;
            }
            if (c == '\\') {
                const char n = peek(1);
                if (n == '\\' || n == '{' || n == '}' || n == '|') {
                    text += n;
                    at += 2;
                    continue;
                }
                return fail("\\ escapes only \\ { } and |");
            }
            if (c == '{') {
                ++at;
                Expression e;
                bool markup = false;
                if (!expression(e, markup)) return false;
                if (markup) continue;
                flush();
                out.push_back({{}, std::move(e)});
                continue;
            }
            if (c == '}') return fail("} outside an expression (escape it: \\})");
            text += c;
            ++at;
        }
        if (quoted) return fail("a quoted pattern without its closing }}");
        flush();
        return true;
    }
};

bool complex_start(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    const std::string_view r = s.substr(i);
    return r.starts_with("{{") || r.starts_with(".input") || r.starts_with(".local") || r.starts_with(".match");
}

// ── formatting ───────────────────────────────────────────────────────────

struct Resolved {
    bool                 ok = true;
    bool                 number = false;
    std::string          text;
    std::string          string_value;  // a string's own value (selection by :string)
    Operands             operands;
    double               value = 0;     // a number's value as shown, signed
    std::string          select = "plural";
    Arg                  raw;
    std::string          function;
};

std::optional<double> numeric(const Arg& a) {
    if (const auto* i = std::get_if<std::int64_t>(&a)) return static_cast<double>(*i);
    if (const auto* d = std::get_if<double>(&a)) return *d;
    if (const auto* s = std::get_if<std::string>(&a)) {
        double v = 0;
        const auto r = std::from_chars(s->data(), s->data() + s->size(), v);
        if (r.ec == std::errc{} && r.ptr == s->data() + s->size()) return v;
    }
    return std::nullopt;
}

std::string as_text(const Arg& a) {
    if (const auto* s = std::get_if<std::string>(&a)) return *s;
    if (const auto* b = std::get_if<bool>(&a)) return *b ? "true" : "false";
    if (const auto* i = std::get_if<std::int64_t>(&a)) return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&a)) {
        std::array<char, 64> buf{};
        const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), *d);
        return std::string(buf.data(), r.ptr);
    }
    return {};
}

std::optional<int> int_option(std::string_view v) {
    int x = 0;
    const auto r = std::from_chars(v.data(), v.data() + v.size(), x);
    if (r.ec != std::errc{} || r.ptr != v.data() + v.size() || x < 0 || x > 20) return std::nullopt;
    return x;
}

std::optional<DateStyle> style_of(std::string_view v) {
    if (v == "full") return DateStyle::full;
    if (v == "long") return DateStyle::long_;
    if (v == "medium") return DateStyle::medium;
    if (v == "short") return DateStyle::short_;
    return std::nullopt;
}

struct Context {
    const Formatter&                                  fmt;
    const Args&                                       args;
    std::vector<std::string>*                         errors;
    std::vector<std::pair<std::string, Resolved>>     env;  // declarations, in order

    void report(std::string what) {
        if (errors) errors->push_back(std::move(what));
    }
    const Resolved* bound(std::string_view n) const {
        for (auto it = env.rbegin(); it != env.rend(); ++it)
            if (it->first == n) return &it->second;
        return nullptr;
    }
    std::string option(const Message::Option& o) {
        if (!o.variable) return o.value;
        if (const Resolved* r = bound(o.value)) return r->number ? as_text(r->raw) : r->string_value;
        if (const Arg* a = args.get(o.value)) return as_text(*a);
        report(std::format("the option `{}` names ${}, which has no value", o.name, o.value));
        return {};
    }

    static std::string fallback(const Expression& e) {
        if (e.kind == Expression::Kind::variable) return std::format("{{${}}}", e.operand);
        if (e.kind == Expression::Kind::literal) return std::format("{{|{}|}}", e.operand);
        return std::format("{{:{}}}", e.function);
    }

    Resolved resolve(const Expression& e) {
        Resolved r;
        // The operand: a declaration's, an argument, or a literal.
        const Resolved* earlier = e.kind == Expression::Kind::variable ? bound(e.operand) : nullptr;
        if (earlier && e.function.empty()) return *earlier;
        if (earlier) r.raw = earlier->raw;
        else if (e.kind == Expression::Kind::variable) {
            const Arg* a = args.get(e.operand);
            if (!a || std::holds_alternative<std::monostate>(*a)) {
                report(std::format("${} has no value", e.operand));
                r.ok = false;
                r.text = fallback(e);
                return r;
            }
            r.raw = *a;
        } else if (e.kind == Expression::Kind::literal) {
            r.raw = e.operand;
        }
        // The function: the one named, or what fits the value.
        std::string fn = e.function;
        if (fn.empty()) {
            if (std::holds_alternative<std::int64_t>(r.raw) || std::holds_alternative<double>(r.raw)) fn = "number";
            else if (std::holds_alternative<CivilTime>(r.raw)) fn = "datetime";
            else if (std::holds_alternative<std::vector<std::string>>(r.raw)) fn = "list";
            else fn = "string";
        }
        r.function = fn;
        std::map<std::string, std::string, std::less<>> opts;
        for (const auto& o : e.options) opts[o.name] = option(o);
        auto opt = [&](std::string_view k) -> std::string_view {
            auto it = opts.find(k);
            return it == opts.end() ? std::string_view{} : std::string_view(it->second);
        };
        auto failed = [&](std::string what) {
            report(std::format("{}: {}", fallback(e), what));
            r.ok = false;
            r.text = fallback(e);
            return r;
        };
        if (fn == "number" || fn == "integer" || fn == "percent") {
            const auto v = numeric(r.raw);
            if (!v) return failed(std::format(":{} needs a number", fn));
            NumberOptions no;
            if (fn == "percent" || opt("style") == "percent") no.style = NumberOptions::Style::percent;
            if (opt("notation") == "compact") no.style = opt("compactDisplay") == "long" ? NumberOptions::Style::compact_long : NumberOptions::Style::compact_short;
            for (auto [key, field] : {std::pair{"minimumFractionDigits", &no.minimum_fraction}, std::pair{"maximumFractionDigits", &no.maximum_fraction},
                                      std::pair{"minimumIntegerDigits", &no.minimum_integer}})
                if (auto s = opt(key); !s.empty()) {
                    if (auto x = int_option(s)) *field = *x;
                    else return failed(std::format("{} is a whole number from 0 to 20, not `{}`", key, s));
                }
            if (fn == "integer") no.maximum_fraction = 0, no.minimum_fraction = 0;
            if (auto g = opt("useGrouping"); g == "never" || g == "false") no.grouping = false;
            if (auto sd = opt("signDisplay"); sd == "always" || sd == "exceptZero") no.sign = NumberOptions::Sign::always;
            else if (sd == "never") no.sign = NumberOptions::Sign::never;
            const double x = fn == "integer" ? std::trunc(*v) : *v;
            const bool whole = std::holds_alternative<std::int64_t>(r.raw) && no.style == NumberOptions::Style::decimal;
            const FormattedNumber f = whole ? fmt.number(std::get<std::int64_t>(r.raw), no) : fmt.number(x, no);
            r.number = true;
            r.text = f.text;
            r.operands = f.operands;
            r.value = std::signbit(x) ? -f.operands.n : f.operands.n;
            if (auto sel = opt("select"); !sel.empty()) {
                if (sel != "plural" && sel != "ordinal" && sel != "exact") return failed(std::format("select is plural, ordinal or exact, not `{}`", sel));
                r.select = std::string(sel);
            }
            return r;
        }
        if (fn == "string") {
            r.string_value = as_text(r.raw);
            r.text = r.string_value;
            return r;
        }
        if (fn == "date" || fn == "time" || fn == "datetime") {
            CivilTime t;
            if (const auto* c = std::get_if<CivilTime>(&r.raw)) t = *c;
            else if (const auto* i = std::get_if<std::int64_t>(&r.raw)) t = CivilTime::from_unix(*i);
            else return failed(std::format(":{} needs a time", fn));
            auto style = [&](std::string_view key, DateStyle d) -> std::optional<DateStyle> {
                const auto s = opt(key);
                if (s.empty()) return d;
                return style_of(s);
            };
            std::optional<DateStyle> ds, ts;
            if (fn == "date") ds = style("style", DateStyle::medium);
            else if (fn == "time") ts = style("style", DateStyle::short_);
            else ds = style("dateStyle", DateStyle::medium), ts = style("timeStyle", DateStyle::short_);
            if ((fn != "time" && !ds) || (fn != "date" && !ts)) return failed("a style is full, long, medium or short");
            r.text = fn == "date" ? fmt.date(t, *ds) : fn == "time" ? fmt.time(t, *ts) : fmt.datetime(t, *ds, *ts);
            r.string_value = r.text;
            return r;
        }
        if (fn == "list") {
            const auto* items = std::get_if<std::vector<std::string>>(&r.raw);
            if (!items) return failed(":list needs a list of strings");
            const auto type = opt("type");
            const ListType lt = type == "disjunction" || type == "or" ? ListType::disjunction : type == "unit" ? ListType::unit : ListType::conjunction;
            const auto w = opt("width");
            const Width wd = w == "short" ? Width::abbreviated : w == "narrow" ? Width::narrow : Width::wide;
            r.text = fmt.list(*items, lt, wd);
            r.string_value = r.text;
            return r;
        }
        return failed(std::format("no function :{}", fn));
    }

    std::string format(const Pattern& p) {
        std::string out;
        for (const auto& part : p) {
            if (part.expression) out += resolve(*part.expression).text;
            else out += part.text;
        }
        return out;
    }
};

}  // namespace

Args::Args(std::initializer_list<std::pair<std::string_view, Arg>> values) {
    for (const auto& [k, v] : values) set(k, v);
}

Args& Args::set(std::string_view name, Arg value) {
    for (auto& [k, v] : values_)
        if (k == name) {
            v = std::move(value);
            return *this;
        }
    values_.emplace_back(std::string(name), std::move(value));
    return *this;
}

const Arg* Args::get(std::string_view name) const noexcept {
    for (const auto& [k, v] : values_)
        if (k == name) return &v;
    return nullptr;
}

std::expected<Message, std::string> Message::parse(std::string_view source) {
    Message m;
    m.source_ = std::string(source);
    Parser p{source};
    if (!complex_start(source)) {
        if (!p.pattern(m.pattern_, false)) return std::unexpected(p.error);
        return m;
    }
    while (true) {
        p.ws();
        if (p.take(".input")) {
            p.ws();
            if (!p.take("{")) return std::unexpected((p.fail("{ after .input"), p.error));
            Declaration d;
            bool markup = false;
            if (!p.expression(d.expression, markup)) return std::unexpected(p.error);
            if (markup || d.expression.kind != Expression::Kind::variable) return std::unexpected((p.fail(".input declares a {$variable …}"), p.error));
            d.name = d.expression.operand;
            m.declarations_.push_back(std::move(d));
            continue;
        }
        if (p.take(".local")) {
            if (!p.ws()) return std::unexpected((p.fail("a space after .local"), p.error));
            if (!p.take("$")) return std::unexpected((p.fail("$name after .local"), p.error));
            auto n = p.name();
            if (!n) return std::unexpected((p.fail("a variable's name"), p.error));
            p.ws();
            if (!p.take("=")) return std::unexpected((p.fail("= after the variable"), p.error));
            p.ws();
            if (!p.take("{")) return std::unexpected((p.fail("{ for the value"), p.error));
            Declaration d;
            d.input = false;
            d.name = std::move(*n);
            bool markup = false;
            if (!p.expression(d.expression, markup) || markup) return std::unexpected((p.fail("an expression"), p.error));
            m.declarations_.push_back(std::move(d));
            continue;
        }
        break;
    }
    if (p.take("{{")) {
        if (!p.pattern(m.pattern_, true)) return std::unexpected(p.error);
        p.ws();
        if (!p.end()) return std::unexpected((p.fail("nothing after the quoted pattern"), p.error));
        return m;
    }
    if (!p.take(".match")) return std::unexpected((p.fail("{{a quoted pattern}} or .match"), p.error));
    // The selectors: $variables (a {$x :fn} is declared as one).
    while (true) {
        const std::size_t before = p.at;
        p.ws();
        if (p.take("$")) {
            auto n = p.name();
            if (!n) return std::unexpected((p.fail("a selector's name"), p.error));
            m.selectors_.push_back(std::move(*n));
        } else if (p.peek() == '{' && p.peek(1) != '{') {
            ++p.at;
            Declaration d;
            d.input = false;
            bool markup = false;
            if (!p.expression(d.expression, markup) || markup) return std::unexpected(p.error);
            d.name = std::format("\x1f{}", m.selectors_.size());  // a name no message can write
            m.selectors_.push_back(d.name);
            m.declarations_.push_back(std::move(d));
        } else {
            p.at = before;
            break;
        }
    }
    if (m.selectors_.empty()) return std::unexpected((p.fail(".match needs a selector: $variable"), p.error));
    bool catch_all = false;
    while (true) {
        p.ws();
        if (p.end()) break;
        Variant v;
        while (!p.take("{{")) {
            if (p.end()) return std::unexpected((p.fail("a variant's {{pattern}}"), p.error));
            if (p.take("*")) {
                v.keys.emplace_back(std::nullopt);
            } else {
                auto k = p.literal();
                if (!k) return std::unexpected((p.fail("a key: a literal or *"), p.error));
                v.keys.emplace_back(std::move(*k));
            }
            p.ws();
        }
        if (v.keys.size() != m.selectors_.size())
            return std::unexpected((p.fail(std::format("a variant has {} keys for {} selectors", v.keys.size(), m.selectors_.size())), p.error));
        if (!p.pattern(v.pattern, true)) return std::unexpected(p.error);
        if (std::ranges::all_of(v.keys, [](const auto& k) { return !k; })) catch_all = true;
        m.variants_.push_back(std::move(v));
    }
    if (!catch_all) return std::unexpected((p.fail("a .match needs a variant whose keys are all *"), p.error));
    return m;
}

std::vector<std::string> Message::variables() const {
    std::vector<std::string> out;
    auto add = [&](const Expression& e) {
        if (e.kind == Expression::Kind::variable && std::ranges::find(out, e.operand) == out.end() &&
            std::ranges::none_of(declarations_, [&](const Declaration& d) { return !d.input && d.name == e.operand; }))
            out.push_back(e.operand);
        for (const auto& o : e.options)
            if (o.variable && std::ranges::find(out, o.value) == out.end()) out.push_back(o.value);
    };
    for (const auto& d : declarations_) add(d.expression);
    for (const auto& part : pattern_)
        if (part.expression) add(*part.expression);
    for (const auto& v : variants_)
        for (const auto& part : v.pattern)
            if (part.expression) add(*part.expression);
    return out;
}

std::string Message::format(const Formatter& fmt, const Args& args, std::vector<std::string>* errors) const {
    Context c{fmt, args, errors, {}};
    for (const Declaration& d : declarations_) c.env.emplace_back(d.name, c.resolve(d.expression));
    if (variants_.empty()) return c.format(pattern_);
    // Each selector's value, then the variant whose keys match best,
    // selector by selector: an exact value, a plural category, then *.
    std::vector<Resolved> sel;
    for (const std::string& s : selectors_) {
        const Resolved* r = c.bound(s);
        if (r) sel.push_back(*r);
        else {
            Expression e;
            e.kind = Expression::Kind::variable;
            e.operand = s;
            sel.push_back(c.resolve(e));
        }
    }
    const Variant* best = nullptr;
    std::vector<int> best_rank;
    for (const Variant& v : variants_) {
        std::vector<int> rank;
        bool ok = true;
        for (std::size_t i = 0; i < v.keys.size() && ok; ++i) {
            const auto& key = v.keys[i];
            const Resolved& r = sel[i];
            if (!key) {
                rank.push_back(2);
            } else if (!r.ok) {
                ok = false;
            } else if (r.number) {
                double k = 0;
                const auto res = std::from_chars(key->data(), key->data() + key->size(), k);
                const bool is_number = res.ec == std::errc{} && res.ptr == key->data() + key->size();
                if (is_number && k == r.value) rank.push_back(0);
                else if (!is_number && r.select != "exact" && parse_plural(*key) == fmt.plural(r.operands, r.select == "ordinal")) rank.push_back(1);
                else ok = false;
            } else if (*key == r.string_value) {
                rank.push_back(0);
            } else {
                ok = false;
            }
        }
        if (ok && (!best || rank < best_rank)) {
            best = &v;
            best_rank = std::move(rank);
        }
    }
    return best ? c.format(best->pattern) : std::string{};
}

}  // namespace omui::i18n
