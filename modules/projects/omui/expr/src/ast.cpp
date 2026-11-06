module openmui.expr;

import std;
import openmui.spec;
import :ast;

namespace omui::expr {

namespace {

std::shared_ptr<const Node> leaf(Op op, Value v, std::string name) {
    auto n = std::make_shared<Node>();
    n->op = op;
    n->value = std::move(v);
    n->name = std::move(name);
    return n;
}

// CEL's precedence, lowest first; postfix (member, index, call) and primaries bind tightest.
int precedence(Op op) noexcept {
    switch (op) {
        case Op::select: return 0;
        case Op::or_: return 1;
        case Op::and_: return 2;
        case Op::lt: case Op::le: case Op::gt: case Op::ge: case Op::eq: case Op::ne: case Op::in: return 3;
        case Op::add: case Op::sub: return 4;
        case Op::mul: case Op::div: case Op::mod: return 5;
        case Op::neg: case Op::not_: return 6;
        case Op::member: case Op::index: return 7;
        default: return 8;
    }
}

std::string_view symbol(Op op) noexcept {
    switch (op) {
        case Op::mul: return "*"; case Op::div: return "/"; case Op::mod: return "%";
        case Op::add: return "+"; case Op::sub: return "-";
        case Op::lt: return "<"; case Op::le: return "<="; case Op::gt: return ">"; case Op::ge: return ">=";
        case Op::eq: return "=="; case Op::ne: return "!="; case Op::in: return "in";
        case Op::and_: return "&&"; case Op::or_: return "||";
        default: return "";
    }
}

void quote(std::string& out, std::string_view s) {
    out += '"';
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (c < 0x20 || c == 0x7f) out += std::format("\\x{:02X}", c);
                else out += ch;
        }
    }
    out += '"';
}

void literal(std::string& out, const Value& v) {
    switch (v.type()) {
        case Value::Type::null: out += "null"; break;
        case Value::Type::boolean: out += v.as_bool() ? "true" : "false"; break;
        case Value::Type::integer:
            // The lexer reads the digits before the sign, and 2^63 is not an int.
            if (v.as_int() == std::numeric_limits<std::int64_t>::min()) out += "(-9223372036854775807 - 1)";
            else out += std::to_string(v.as_int());
            break;
        case Value::Type::real: {
            const double d = v.as_double();
            if (!std::isfinite(d)) {  // no literal spells these; double() reads them
                out += d != d ? "double(\"nan\")" : d > 0 ? "double(\"inf\")" : "double(\"-inf\")";
                break;
            }
            char buf[64];
            auto [p, ec] = std::to_chars(buf, buf + sizeof buf, d);
            std::string s(buf, p);
            // Still a double when read back: CEL's 1 is an int, 1.0 a double.
            if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
            out += s;
            break;
        }
        case Value::Type::text: quote(out, v.as_string()); break;
        case Value::Type::list: {
            out += '[';
            bool first = true;
            for (const Value& x : *v.list()) {
                if (!first) out += ", ";
                first = false;
                literal(out, x);
            }
            out += ']';
            break;
        }
        case Value::Type::map: {
            out += '{';
            bool first = true;
            for (const auto& [k, x] : *v.map()) {
                if (!first) out += ", ";
                first = false;
                quote(out, k);
                out += ": ";
                literal(out, x);
            }
            out += '}';
            break;
        }
    }
}

void print(std::string& out, const Node& n, int context);

// A negative number prints with its sign, so it binds like unary minus.
int precedence(const Node& n) noexcept {
    if (n.op == Op::literal) {
        if (n.value.type() == Value::Type::integer && n.value.as_int() < 0) return 6;
        if (n.value.type() == Value::Type::real && std::signbit(n.value.as_double())) return 6;
    }
    return precedence(n.op);
}

void print_child(std::string& out, const Node& n, int min) {
    const bool paren = precedence(n) < min;
    if (paren) out += '(';
    print(out, n, paren ? 0 : min);
    if (paren) out += ')';
}

void print(std::string& out, const Node& n, int) {
    const int p = precedence(n.op);
    switch (n.op) {
        case Op::literal: literal(out, n.value); return;
        case Op::ref: out += n.name; return;
        case Op::member:
            print_child(out, *n.args[0], 7);
            out += '.';
            out += n.name;
            return;
        case Op::index:
            print_child(out, *n.args[0], 7);
            out += '[';
            print(out, *n.args[1], 0);
            out += ']';
            return;
        case Op::neg:
        case Op::not_: {
            out += n.op == Op::neg ? '-' : '!';
            // -(5) is not the literal -5 the text "-5" reads as.
            const Node& a = *n.args[0];
            const bool number = a.op == Op::literal && a.value.is_number() && precedence(a) != 6;
            if (n.op == Op::neg && number) {
                out += '(';
                print(out, a, 0);
                out += ')';
            } else {
                print_child(out, a, 6);
            }
            return;
        }
        case Op::select:
            print_child(out, *n.args[0], 1);
            out += " ? ";
            print_child(out, *n.args[1], 1);
            out += " : ";
            print_child(out, *n.args[2], 0);
            return;
        case Op::call:
        case Op::has: {
            out += n.op == Op::has ? std::string_view("has") : std::string_view(n.name);
            out += '(';
            if (n.op == Op::has) {
                print_child(out, *n.args[0], 7);
                out += '.';
                out += n.name;
            } else {
                for (std::size_t i = 0; i < n.args.size(); ++i) {
                    if (i) out += ", ";
                    print(out, *n.args[i], 0);
                }
            }
            out += ')';
            return;
        }
        case Op::list:
            out += '[';
            for (std::size_t i = 0; i < n.args.size(); ++i) {
                if (i) out += ", ";
                print(out, *n.args[i], 0);
            }
            out += ']';
            return;
        case Op::map:
            out += '{';
            for (std::size_t i = 0; i + 1 < n.args.size(); i += 2) {
                if (i) out += ", ";
                print(out, *n.args[i], 0);
                out += ": ";
                print(out, *n.args[i + 1], 0);
            }
            out += '}';
            return;
        default:
            // Binary, left-associative; relations do not chain without parentheses.
            print_child(out, *n.args[0], p == 3 ? p + 1 : p);
            out += ' ';
            out += symbol(n.op);
            out += ' ';
            print_child(out, *n.args[1], p + 1);
            return;
    }
}

void collect(const Node& n, std::vector<std::string>& out) {
    if (n.op == Op::ref) out.push_back(n.name);
    for (const auto& a : n.args) collect(*a, out);
}

bool same_node(const Node& a, const Node& b) noexcept {
    if (a.op != b.op || a.name != b.name || !(a.value == b.value) || a.args.size() != b.args.size()) return false;
    for (std::size_t i = 0; i < a.args.size(); ++i)
        if (!same_node(*a.args[i], *b.args[i])) return false;
    return true;
}

}  // namespace

std::string_view name(Op op) noexcept {
    constexpr std::array<std::string_view, 25> names{"literal", "ref", "member", "index", "neg", "not", "mul", "div", "mod",
                                                     "add", "sub", "lt", "le", "gt", "ge", "eq", "ne", "in", "and", "or",
                                                     "select", "call", "list", "map", "has"};
    return names[static_cast<std::size_t>(op)];
}

Expr::Expr(Value v) : node_(leaf(Op::literal, std::move(v), {})) {}

std::string Expr::text() const {
    std::string out;
    print(out, *node_, 0);
    return out;
}

std::vector<std::string> Expr::references() const {
    std::vector<std::string> out;
    collect(*node_, out);
    std::ranges::sort(out);
    out.erase(std::ranges::unique(out).begin(), out.end());
    return out;
}

bool Expr::same(const Expr& other) const noexcept { return same_node(*node_, *other.node_); }

Expr Expr::operator[](Expr key) const { return make(Op::index, {*this, std::move(key)}); }
Expr Expr::field(std::string name) const { return make(Op::member, {*this}, std::move(name)); }

Expr make(Op op, std::vector<Expr> args, std::string name) {
    auto n = std::make_shared<Node>();
    n->op = op;
    n->name = std::move(name);
    for (Expr& a : args) {
        n->height = std::max(n->height, a.node().height + 1);
        n->args.push_back(a.ptr());
    }
    return Expr(std::shared_ptr<const Node>(std::move(n)));
}

Expr ref(std::string name) { return Expr(leaf(Op::ref, {}, std::move(name))); }
Expr lit(Value v) { return Expr(std::move(v)); }
Expr neg(Expr a) { return make(Op::neg, {std::move(a)}); }
Expr not_(Expr a) { return make(Op::not_, {std::move(a)}); }
Expr mul(Expr a, Expr b) { return make(Op::mul, {std::move(a), std::move(b)}); }
Expr div(Expr a, Expr b) { return make(Op::div, {std::move(a), std::move(b)}); }
Expr mod(Expr a, Expr b) { return make(Op::mod, {std::move(a), std::move(b)}); }
Expr add(Expr a, Expr b) { return make(Op::add, {std::move(a), std::move(b)}); }
Expr sub(Expr a, Expr b) { return make(Op::sub, {std::move(a), std::move(b)}); }
Expr lt(Expr a, Expr b) { return make(Op::lt, {std::move(a), std::move(b)}); }
Expr le(Expr a, Expr b) { return make(Op::le, {std::move(a), std::move(b)}); }
Expr gt(Expr a, Expr b) { return make(Op::gt, {std::move(a), std::move(b)}); }
Expr ge(Expr a, Expr b) { return make(Op::ge, {std::move(a), std::move(b)}); }
Expr eq(Expr a, Expr b) { return make(Op::eq, {std::move(a), std::move(b)}); }
Expr ne(Expr a, Expr b) { return make(Op::ne, {std::move(a), std::move(b)}); }
Expr in(Expr a, Expr b) { return make(Op::in, {std::move(a), std::move(b)}); }
Expr and_(Expr a, Expr b) { return make(Op::and_, {std::move(a), std::move(b)}); }
Expr or_(Expr a, Expr b) { return make(Op::or_, {std::move(a), std::move(b)}); }
Expr select(Expr cond, Expr then, Expr otherwise) { return make(Op::select, {std::move(cond), std::move(then), std::move(otherwise)}); }
Expr has(Expr object, std::string field) { return make(Op::has, {std::move(object)}, std::move(field)); }
Expr list(std::vector<Expr> items) { return make(Op::list, std::move(items)); }
Expr map(std::vector<std::pair<Expr, Expr>> entries) {
    std::vector<Expr> flat;
    for (auto& [k, v] : entries) {
        flat.push_back(std::move(k));
        flat.push_back(std::move(v));
    }
    return make(Op::map, std::move(flat));
}
Expr call(std::string function, std::vector<Expr> args) { return make(Op::call, std::move(args), std::move(function)); }

}  // namespace omui::expr
