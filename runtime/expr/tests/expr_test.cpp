import std;
import openmui.expr;
import openmui.testing;

using namespace omui::testing;
using namespace omui::expr;

namespace {

const Value::Map env{
    {"count", Value(3)},
    {"zero", Value(0)},
    {"half", Value(0.5)},
    {"name", Value("Ada")},
    {"ok", Value(true)},
    {"tags", Value(Value::List{Value("a"), Value("b")})},
    {"user", Value(Value::Map{{"name", Value("Lin")}, {"age", Value(41)}})},
    {"big", Value(std::numeric_limits<std::int64_t>::max())},
};

Result run(std::string_view text, const Context& ctx = {}) {
    auto e = parse(text);
    if (!e) {
        fail(std::format("`{}` does not parse: {}", text, e.error().text()));
        return {};
    }
    return evaluate(*e, env, ctx);
}

// JSON, except that a double always shows it is one (3 vs 3.0 vs "3").
void repr(std::string& out, const Value& v) {
    switch (v.type()) {
        case Value::Type::real: {
            std::string d = v.json();
            if (d.find_first_of(".en") == std::string::npos) d += ".0";
            out += d;
            return;
        }
        case Value::Type::text: omui::spec::append_json_string(out, v.as_string()); return;
        case Value::Type::list: {
            out += '[';
            for (std::size_t i = 0; i < v.list()->size(); ++i) {
                if (i) out += ',';
                repr(out, (*v.list())[i]);
            }
            out += ']';
            return;
        }
        case Value::Type::map: {
            out += '{';
            bool first = true;
            for (const auto& [k, x] : *v.map()) {
                if (!first) out += ',';
                first = false;
                omui::spec::append_json_string(out, k);
                out += ':';
                repr(out, x);
            }
            out += '}';
            return;
        }
        default: out += v.json();
    }
}

std::string repr(const Value& v) {
    std::string out;
    repr(out, v);
    return out;
}

std::string json(std::string_view text, std::source_location where = std::source_location::current()) {
    Result r = run(text);
    if (!r.ok()) {
        fail(std::format("`{}`: {}", text, r.errors[0].text()), where);
        return "<error>";
    }
    return repr(r.value);
}

void is(std::string_view text, std::string_view expected, std::source_location where = std::source_location::current()) {
    const std::string got = json(text, where);
    if (got != expected) fail(std::format("`{}`: expected {}, got {}", text, expected, got), where);
}

// An evaluation error: null, and a diagnostic whose message contains `what`.
void error(std::string_view text, std::string_view what, std::source_location where = std::source_location::current()) {
    Result r = run(text);
    if (r.ok()) {
        fail(std::format("`{}` evaluated to {}, expected an error ({})", text, r.value.json(), what), where);
        return;
    }
    check(r.value.is_null(), "an error gives null", where);
    if (r.errors[0].message.find(what) == std::string::npos)
        fail(std::format("`{}`: expected an error about `{}`, got `{}`", text, what, r.errors[0].text()), where);
}

void syntax(std::string_view text, std::string_view what, std::source_location where = std::source_location::current()) {
    auto e = parse(text);
    if (e) {
        fail(std::format("`{}` parsed (as `{}`), expected a syntax error", text, e->text()), where);
        return;
    }
    if (e.error().text().find(what) == std::string::npos)
        fail(std::format("`{}`: expected `{}` in the diagnostic, got `{}`", text, what, e.error().text()), where);
}

// Text → AST → text is stable, and parses back to the same tree.
void round_trip(std::string_view text, std::string_view canonical = {}, std::source_location where = std::source_location::current()) {
    auto e = parse(text);
    if (!e) {
        fail(std::format("`{}` does not parse: {}", text, e.error().text()), where);
        return;
    }
    const std::string printed = e->text();
    if (printed != (canonical.empty() ? text : canonical)) fail(std::format("`{}` prints as `{}`", text, printed), where);
    auto again = parse(printed);
    if (!again) {
        fail(std::format("`{}` (printed) does not parse: {}", printed, again.error().text()), where);
        return;
    }
    if (!again->same(*e)) fail(std::format("`{}` reparses to a different tree", printed), where);
}

// splitmix64: the same sequence on every platform.
struct Rng {
    std::uint64_t s;
    std::uint64_t next() noexcept {
        std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    std::size_t below(std::size_t n) noexcept { return static_cast<std::size_t>(next() % n); }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"expr"};

    s.test("literals", [] {
        is("null", "null");
        is("true", "true");
        is("42", "42");
        is("0x2A", "42");
        is("42u", "42");
        is("-7", "-7");
        is("1.5", "1.5");
        is("1e3", "1000.0");
        is("'single'", "\"single\"");
        is("\"double\"", "\"double\"");
        is(R"('a\nb\t\x41é\U0001F600\101')", "\"a\\nb\\tAé😀A\"");
        is(R"(r'a\nb')", "\"a\\\\nb\"");
        is("'''line\nbreak'''", "\"line\\nbreak\"");
        is("[1, 'two', [3.0]]", "[1,\"two\",[3.0]]");
        is("{'a': 1, 'b': [true]}", "{\"a\":1,\"b\":[true]}");
        is("[]", "[]");
        is("{}", "{}");
    });

    s.test("arithmetic: ints, doubles and their mix", [] {
        is("1 + 2 * 3", "7");
        is("(1 + 2) * 3", "9");
        is("7 / 2", "3");
        is("-7 / 2", "-3");
        is("7 % 3", "1");
        is("-7 % 3", "-1");
        is("7.0 / 2", "3.5");
        is("count * half", "1.5");
        is("count + 0.25", "3.25");
        is("1.0 / 0.0 > 1e308", "true");
        is("-count", "-3");
        is("--count", "3");
        is("big - 1 + 1", "9223372036854775807");
        is("(-9223372036854775807 - 1) % -1", "0");
        error("big + 1", "overflows");
        error("-big - 2", "overflows");
        error("big * 2", "overflows");
        error("(-9223372036854775807 - 1) / -1", "overflows");
        error("-(-9223372036854775807 - 1)", "negate");
        error("count / zero", "division by zero");
        error("count % zero", "modulus by zero");
        error("1.5 % 1", "% takes ints");
        error("'a' - 1", "no `-`");
        error("-'a'", "- of a string");
    });

    s.test("strings and lists concatenate", [] {
        is("'Hello, ' + name", "\"Hello, Ada\"");
        is("tags + ['c']", "[\"a\",\"b\",\"c\"]");
        Result r = run("'n = ' + count");
        check(!r.ok());
        check(r.errors[0].hint.find("format") != std::string::npos, "the hint names format()");
    });

    s.test("comparison and equality across numbers", [] {
        is("1 == 1.0", "true");
        is("1 != 1.0", "false");
        is("2 < 2.5", "true");
        is("2.5 <= 2", "false");
        is("'abc' < 'abd'", "true");
        is("false < true", "true");
        is("[1, 2] == [1.0, 2]", "true");
        is("{'a': 1} == {'a': 1.0}", "true");
        is("{'a': 1} == {'b': 1}", "false");
        is("null == null", "true");
        is("null == 0", "false");
        is("'1' == 1", "false");
        is("double('nan') == double('nan')", "false");
        is("double('nan') < 1", "false");
        is("double('nan') >= 1", "false");
        error("'a' < 1", "no order");
        error("[1] < [2]", "no order");
    });

    s.test("logic needs bools and absorbs errors like CEL", [] {
        is("ok && count > 2", "true");
        is("!ok || false", "false");
        is("false && missing", "false");
        is("missing && false", "false");
        is("true || missing", "true");
        is("missing || true", "true");
        is("count > 0 ? 'some' : 'none'", "\"some\"");
        is("zero > 0 ? 'some' : zero == 0 ? 'zero' : 'negative'", "\"zero\"");
        is("ok ? 1 : missing", "1");
        error("true && missing", "`missing` is not defined");
        error("missing || false", "`missing` is not defined");
        error("count && true", "takes bools");
        error("!count", "takes a bool");
        error("count ? 1 : 2", "must be a bool");
        Result r = run("count ? 1 : 2");
        check(!r.errors.empty() && r.errors[0].hint.find("compare") != std::string::npos);
    });

    s.test("in, members, indexes and has()", [] {
        is("'a' in tags", "true");
        is("'z' in tags", "false");
        is("1.0 in [1, 2]", "true");
        is("'name' in user", "true");
        is("user.name", "\"Lin\"");
        is("user['age'] + 1", "42");
        is("tags[1]", "\"b\"");
        is("[[1, 2], [3]][0][1]", "2");
        is("has(user.name)", "true");
        is("has(user.email)", "false");
        is("{'a': {'b': 1}}.a.b", "1");
        error("user.email", "no field `email`");
        error("tags[2]", "index 2 of a list of 2");
        error("tags[-1]", "index -1");
        error("tags['0']", "indexed by an int");
        error("user[0]", "indexed by a string");
        error("count.x", "only maps have fields");
        error("count[0]", "only lists and maps");
        error("'a' in 'abc'", "`in` a string");
        error("has(count.x)", "only maps have fields");
        error("{'a': 1, 'a': 2}", "the key `a` twice");
        error("{1: 2}", "keys are strings");
        Result r = run("user.email");
        check(!r.errors.empty() && r.errors[0].hint.find("has(") != std::string::npos);
    });

    s.test("text functions", [] {
        is("format('{} of {}', 1, count)", "\"1 of 3\"");
        is("format('{{}} {}', 0.1 + 0.2)", "\"{} 0.30000000000000004\"");
        is("format('{} {} {}', null, [1, 'a'], {'k': true})", "\"null [1,\\\"a\\\"] {\\\"k\\\":true}\"");
        is("size('héllo')", "5");
        is("size('🇯🇵👨‍👩‍👧')", "2");
        is("len(tags)", "2");
        is("size(user)", "2");
        is("name.size()", "3");
        is("contains(name, 'd')", "true");
        is("tags.contains('b')", "true");
        is("startsWith(name, 'Ad')", "true");
        is("name.endsWith('a')", "true");
        is("upper('straße ǆ')", "\"STRASSE Ǆ\"");
        is("lower('ΑΒΓ İ')", "\"αβγ i̇\"");
        is("trim(' \\t\\u3000 mid dle \\n')", "\"mid dle\"");
        is("trim('   ')", "\"\"");
        is("string(1.0)", "\"1\"");
        is("string(0.1)", "\"0.1\"");
        is("string(true)", "\"true\"");
        error("format('{} {}', 1)", "more {}");
        error("format('{}', 1, 2)", "1 argument more");
        error("format('{', 1)", "a lone `{`");
        error("format()", "takes a pattern");
        error("size(1)", "size() of a int");
        error("upper(1)", "takes a string");
    });

    s.test("number functions", [] {
        is("min(3, 1, 2)", "1");
        is("max(3, 1.5)", "3.0");
        is("clamp(15, 0, 10)", "10");
        is("clamp(half, 0, 1)", "0.5");
        is("abs(-4)", "4");
        is("abs(-2.5)", "2.5");
        is("round(2.5)", "3");
        is("round(-2.5)", "-3");
        is("round(2.4999)", "2");
        is("floor(-1.5)", "-2");
        is("ceil(1.1)", "2");
        is("round(7)", "7");
        is("int(3.99)", "3");
        is("int(-3.99)", "-3");
        is("int('42')", "42");
        is("int('4x')", "0");
        is("int(true)", "1");
        is("int(1e30)", "0");
        is("double('2.5')", "2.5");
        is("double(2)", "2.0");
        is("double('x')", "0.0");
        error("clamp(1, 2, 0)", "lo above hi");
        error("round(1e30)", "does not fit");
        error("min()", "at least one");
        error("max(1, 'a')", "takes numbers");
        error("abs(-9223372036854775807 - 1)", "overflows");
    });

    s.test("lookup and tr", [] {
        is("lookup(tags, 1)", "\"b\"");
        is("lookup(tags, 5, 'none')", "\"none\"");
        is("lookup(user, 'email', '-')", "\"-\"");
        is("lookup(user, 'email')", "null");
        is("lookup(null, 'x', 0)", "0");
        error("lookup(tags, 'a')", "int index");

        is("tr('items', count)", "\"items\"");  // no catalog: the key is the pattern; a translation may leave arguments out
        Context de;
        de.translate = [](std::string_view key) -> std::string {
            if (key == "items") return "{} Einträge";
            return std::string(key);
        };
        Result r = run("tr('items', count)", de);
        check(r.ok());
        check_eq(r.value.json(), std::string("\"3 Einträge\""));
    });

    s.test("tr() without a catalog formats the key", [] {
        // The key is the pattern: tr('{} left', n) reads well untranslated.
        is("tr('{} left', count)", "\"3 left\"");
    });

    s.test("animation functions", [] {
        is("interpolate(5, [0, 10], [0, 100])", "50.0");
        is("interpolate(-5, [0, 10], [0, 100])", "0.0");
        is("interpolate(50, [0, 10], [0, 100])", "100.0");
        is("interpolate(15, [0, 10, 20], [0, 100, 0])", "50.0");
        is("ease('linear', 0.25)", "0.25");
        is("ease('out_cubic', 1)", "1.0");
        is("interpolate(0.5, [0, 1], [0, 8], 'in_quad')", "2.0");
        is("spring(0, 0, 1)", "0.0");
        is("spring(60, 0, 1) == 1.0", "true");
        is("spring(0.1, 0, 1) > 0.0", "true");
        // The same function of time as the engine's springs.
        is("spring(0.2, 0, 1, 170, 26) == spring(0.2, 0, 1, 170, 26, 1)", "true");
        error("interpolate(1, [0], [0])", "at least 2");
        error("interpolate(1, [1, 0], [0, 1])", "must increase");
        error("interpolate(1, [0, 1], [0, 1], 'wobble')", "no ease is named");
        error("ease('nope', 0.5)", "no ease is named");
        error("spring(1, 0, 1, 0)", "stiffness > 0");
    });

    s.test("unknown names and functions suggest the nearest", [] {
        error("cout + 1", "`cout` is not defined");
        Result r = run("sise(tags)");
        check(!r.ok());
        check(r.errors.size() == 1 && r.errors[0].hint == "did you mean `size`?", r.errors.empty() ? "" : r.errors[0].text());
        r = run("startswith(name, 'A')");
        check(r.errors.size() == 1 && r.errors[0].hint == "did you mean `startsWith`?");
        r = run("frobnicate(1)");
        check(r.errors.size() == 1 && r.errors[0].hint.empty());
    });

    s.test("an evaluation diagnostic points at the node", [] {
        const std::string_view text = "count > 0 &&\n  user.email == 'x'";
        auto e = parse(text);
        check(e.has_value());
        Result r = evaluate(*e, env);
        check(r.errors.size() == 1);
        if (r.errors.empty()) return;
        Diagnostic d = r.errors[0];
        locate(d, text);
        check_eq(d.line, 2u);
        check_eq(d.column, 7u);
        check_eq(d.text(), std::string("2:7: no field `email` (test it first: has(….email))"));
    });

    s.test("syntax errors say where and how to fix it", [] {
        syntax("count = 3", "1:7: `=` is not an operator here (compare with `==`");
        syntax("a & b", "write `&&`");
        syntax("a | b", "write `||`");
        syntax("a and b", "CEL writes && for and");
        syntax("1 +", "1:4: the expression ends early");
        syntax("(1 + 2", "`)`");
        syntax("[1, 2", "`]`");
        syntax("{'a' 1}", "`:`");
        syntax("'unterminated", "ends");
        syntax("'a\nb'", "a string ends at the end of its line");
        syntax("99999999999999999999", "does not fit in an int");
        syntax("has(x)", "has() takes a field");
        syntax("'\\q'", "\\q");
        syntax("1 2", "expected an operator or the end");
        syntax("#", "cannot start");
        syntax("", "ends early");
        auto e = parse("x +\n  * 2");
        check(!e && e.error().line == 2 && e.error().column == 3);
    });

    s.test("nesting is bounded, not a stack overflow", [] {
        syntax(std::string(10000, '('), "nested deeper than");
        syntax(std::string(10000, '-') + "x", "nested deeper than");
        syntax(std::string(10000, '[') + std::string(10000, ']'), "nested deeper than");
        std::string ok(150, '(');
        ok += "1";
        ok += std::string(150, ')');
        is(ok, "1");
        // A chain is a tall tree too: everything that walks it is bounded.
        std::string sum = "0";
        for (int i = 0; i < 150; ++i) sum += " + 1";
        is(sum, "150");
        for (int i = 0; i < 5000; ++i) sum += " + 1";
        syntax(sum, "nested deeper than");
        std::string fields = "user";
        for (int i = 0; i < 300; ++i) fields += ".name";
        syntax(fields, "nested deeper than");
        // Trees built in code are bounded too.
        Expr deep = ref("count");
        for (int i = 0; i < 1000; ++i) deep = -deep;
        Result r = evaluate(deep, env);
        check(!r.ok() && r.errors[0].message.find("nested deeper") != std::string::npos);
    });

    s.test("printing round-trips", [] {
        round_trip("count > 0 ? format(\"{} left\", count) : \"done\"");
        round_trip("(a + b) * c");
        round_trip("a + b * c");
        round_trip("a - (b - c)");
        round_trip("a - b - c");
        round_trip("-a.b");
        round_trip("(-a).b");
        round_trip("!(a && b) || c");
        round_trip("a ? b : c ? d : e");
        round_trip("(a ? b : c) ? d : e");
        round_trip("a[0].b[\"c\"]");
        round_trip("has(a.b.c)");
        round_trip("x in [1, 2.5, -3, \"s\", null, true]");
        round_trip("{\"k\": [1], \"j\": {}}");
        round_trip("(a < b) == (c < d)");
        round_trip("x - -5");
        round_trip("(-5).abs()", "abs(-5)");
        round_trip("name.size()", "size(name)");
        round_trip("1.0 + 2.5e-8 + 1e+300", "1.0 + 2.5e-08 + 1e+300");
        round_trip("'it\\'s'", "\"it's\"");
        round_trip("\"q\\\"\\\\\\n\\x01\"");
        round_trip("'é😀'", "\"é😀\"");
    });

    s.test("combinators build what the text says", [] {
        const auto same = [](const Expr& built, std::string_view text, std::source_location where = std::source_location::current()) {
            auto parsed = parse(text);
            check(parsed.has_value(), text, where);
            if (parsed && !built.same(*parsed)) fail(std::format("built `{}`, text `{}`", built.text(), text), where);
        };
        same(select(ref("count") > 0, fmt("{} left", ref("count")), "done"), "count > 0 ? format('{} left', count) : 'done'");
        same(ref("a") + ref("b") * 2, "a + b * 2");
        same((ref("a") + ref("b")) * 2.5, "(a + b) * 2.5");
        same(!(ref("ok") && ref("a") <= ref("b")), "!(ok && a <= b)");
        same(ref("user").field("name"), "user.name");
        same(ref("tags")[0], "tags[0]");
        same(has(ref("user"), "email"), "has(user.email)");
        same(in("a", ref("tags")), "'a' in tags");
        same(size(ref("tags")), "size(tags)");
        same(lookup(ref("m"), "k", 0), "lookup(m, 'k', 0)");
        same(tr("hello", ref("name")), "tr('hello', name)");
        same(interpolate(ref("t"), list({0, 1}), list({0, 10}), "out_cubic"), "interpolate(t, [0, 1], [0, 10], 'out_cubic')");
        same(map({{"a", 1}}), "{'a': 1}");
        same(call("clamp", {ref("x"), 0, 1}), "clamp(x, 0, 1)");
        same(spring(ref("t"), 0, 1), "spring(t, 0, 1)");
        same(ne(ref("a") % 2, 0) || ref("b") >= -1, "a % 2 != 0 || b >= -1");

        // Literals that no single token spells still evaluate to themselves.
        for (double d : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
            auto back = parse(lit(Value(d)).text());
            check(back && evaluate(*back, env).value.as_double() == d);
        }
        auto nan = parse(lit(Value(std::numeric_limits<double>::quiet_NaN())).text());
        check(nan && std::isnan(evaluate(*nan, env).value.as_double()));
        auto smallest = parse(lit(Value(std::numeric_limits<std::int64_t>::min())).text());
        check(smallest && evaluate(*smallest, env).value.as_int() == std::numeric_limits<std::int64_t>::min());
        auto negative = parse((lit(Value(-2)) * 3).text());
        check(negative && evaluate(*negative, env).value.as_int() == -6);
        check_eq((lit(Value(-2)).field("x")).text(), std::string("(-2).x"));
    });

    s.test("references are the names read, sorted, once", [] {
        auto e = parse("a.b + c[d] + size(a) + has(e.f) + (c ? g : 1)");
        check(e.has_value());
        check(e->references() == std::vector<std::string>{"a", "c", "d", "e", "g"});
        check(parse("1 + 2")->references().empty());
    });

    s.test("the function list is sorted and complete", [] {
        auto f = functions();
        check(std::ranges::is_sorted(f));
        check_eq(f.size(), omui::spec::functions.size());
        for (std::string_view name : {"format", "size", "lookup", "interpolate", "spring", "tr", "clamp", "ease"})
            check(std::ranges::find(f, name) != f.end(), name);
        // Every listed function is known to the evaluator (none says "no function").
        for (std::string_view name : f) {
            if (name == "has") continue;  // a macro: it takes a field, not a value
            Result r = run(std::format("{}()", name));
            for (const auto& d : r.errors) check(d.message.find("no function") == std::string::npos, name);
        }
    });

    s.test("random input never crashes and always round-trips", [] {
        Rng rng{0x6f6d7569};
        static constexpr std::array<std::string_view, 48> atoms{
            "count", "half", "name", "tags", "user", "ok", "missing", "0", "1", "-1", "2.5", "1e3", "'s'", "\"t\"", "null",
            "true", "false", "+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "&&", "||", "!", "?", ":", "in",
            "(", ")", "[", "]", "{", "}", ",", ".", "size", "format", "has", "lookup", ".name", "'{}'"};
        int parsed = 0;
        for (int round = 0; round < 20000; ++round) {
            std::string text;
            const std::size_t n = 1 + rng.below(14);
            for (std::size_t i = 0; i < n; ++i) {
                if (rng.below(16) == 0) {
                    text += static_cast<char>(rng.below(256));  // any byte
                } else {
                    text += atoms[rng.below(atoms.size())];
                    if (rng.below(2)) text += ' ';
                }
            }
            auto e = parse(text);
            if (!e) {
                check(!e.error().message.empty(), text);
                check(e.error().at <= text.size(), text);
                continue;
            }
            ++parsed;
            const std::string printed = e->text();
            auto again = parse(printed);
            if (!again) {
                fail(std::format("`{}` printed as `{}`, which does not parse: {}", text, printed, again.error().text()));
                continue;
            }
            if (again->text() != printed) fail(std::format("`{}` → `{}` → `{}`", text, printed, again->text()));
            Result a = evaluate(*e, env), b = evaluate(*again, env);
            check(a.ok() == b.ok(), printed);
            if (a.ok() && b.ok() && a.value.json() != b.value.json() && a.value.json().find("nan") == std::string::npos)
                fail(std::format("`{}` gives {}, its printed form {}", text, a.value.json(), b.value.json()));
            if (failures() > 20) return;
        }
        check(parsed > 500, std::format("only {} of the random inputs parsed", parsed));
    });

    s.test("random well-formed expressions: print, reparse, same value", [] {
        Rng rng{0x65787072};
        // A random expression of the grammar, `depth` levels at most.
        std::function<std::string(int)> gen = [&](int depth) -> std::string {
            static constexpr std::array<std::string_view, 16> leaves{
                "count", "zero", "half", "name", "ok", "tags", "user", "big", "0", "7", "-3", "2.5", "1e-3", "'x'", "null", "true"};
            static constexpr std::array<std::string_view, 13> binary{"+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", "&&", "||", "in"};
            static constexpr std::array<std::string_view, 9> unary_fns{"size", "string", "int", "double", "abs", "round", "upper", "trim", "len"};
            if (depth == 0) return std::string(leaves[rng.below(leaves.size())]);
            // One draw at a time, in order: the order of a call's arguments is
            // unspecified, and the sequence must be every compiler's.
            const std::size_t kind = rng.below(10);
            auto sub = [&] { return gen(depth - 1); };
            switch (kind) {
                case 0: case 1: case 2: {
                    std::string a = sub();
                    const std::string_view op = binary[rng.below(binary.size())];
                    return std::format("{} {} {}", a, op, sub());
                }
                case 3: return std::format("({})", sub());
                case 4: {
                    const char* prefix = rng.below(2) ? "!" : "-";
                    return prefix + sub();
                }
                case 5: {
                    std::string c = sub(), a = sub();
                    return std::format("{} ? {} : {}", c, a, sub());
                }
                case 6: {
                    const std::string_view f = unary_fns[rng.below(unary_fns.size())];
                    return std::format("{}({})", f, sub());
                }
                case 7: {
                    std::string a = sub();
                    return std::format("[{}, {}]", a, sub());
                }
                case 8: {
                    if (rng.below(2)) return sub() + ".name";
                    std::string a = sub();
                    return std::format("{}[{}]", a, sub());
                }
                default: {
                    std::string a = sub();
                    return std::format("format('{{}}/{{}}', {}, {})", a, sub());
                }
            }
        };
        Hash h;
        int values = 0;
        for (int round = 0; round < 5000; ++round) {
            const std::string text = gen(1 + static_cast<int>(rng.below(5)));
            auto e = parse(text);
            if (!e) {
                fail(std::format("`{}` does not parse: {}", text, e.error().text()));
                continue;
            }
            const std::string printed = e->text();
            auto again = parse(printed);
            if (!again || !again->same(*e)) {
                fail(std::format("`{}` printed as `{}`, which reads back differently", text, printed));
                continue;
            }
            Result a = evaluate(*e, env), b = evaluate(*again, env);
            check(a.ok() == b.ok() && repr(a.value) == repr(b.value), printed);
            values += a.ok();
            h.str(printed);
            h.str(a.ok() ? repr(a.value) : a.errors[0].message);
            if (failures() > 20) return;
        }
        check(values > 500, std::format("only {} of the expressions evaluated", values));
        // The same expressions, printed texts, values and errors on every platform.
        check_eq(h.hex(), std::string("dce364409b8b6d21"));
    });

    return s.run(argc, argv);
}
