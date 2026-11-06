module openmui.expr;

import std;
import openmui.spec;
import :ast;
import :parse;

namespace omui::expr {

namespace {

enum class Tok : std::uint8_t {
    end, ident, integer, real, string, true_, false_, null_, in,
    plus, minus, star, slash, percent, bang, lt, le, gt, ge, eqeq, ne, andand, oror, question, colon, dot, comma,
    lparen, rparen, lbracket, rbracket, lbrace, rbrace, error,
};

struct Token {
    Tok           kind = Tok::end;
    std::uint32_t at = 0;
    std::string   text;   // identifier, decoded string, the number's text, or an error message
    std::string   hint;   // for an error token
    std::int64_t  i = 0;
    double        d = 0;
};

std::string_view describe(Tok t) {
    switch (t) {
        case Tok::end: return "the end";
        case Tok::ident: return "a name";
        case Tok::integer: case Tok::real: return "a number";
        case Tok::string: return "a string";
        case Tok::rparen: return "`)`";
        case Tok::rbracket: return "`]`";
        case Tok::rbrace: return "`}`";
        case Tok::colon: return "`:`";
        case Tok::comma: return "`,`";
        default: return "an operator";
    }
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Lexer {
public:
    explicit Lexer(std::string_view s) : s_(s) {}

    Token next() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
        Token t;
        t.at = static_cast<std::uint32_t>(i_);
        if (i_ >= s_.size()) return t;
        const char c = s_[i_];
        auto one = [&](Tok k) {
            ++i_;
            t.kind = k;
            return t;
        };
        auto two = [&](char second, Tok both, Tok single) {
            if (i_ + 1 < s_.size() && s_[i_ + 1] == second) {
                i_ += 2;
                t.kind = both;
                return t;
            }
            return one(single);
        };
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            if ((c == 'r' || c == 'R') && i_ + 1 < s_.size() && (s_[i_ + 1] == '"' || s_[i_ + 1] == '\'')) {
                ++i_;
                return string(t, true);
            }
            std::size_t j = i_;
            while (j < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[j])) || s_[j] == '_')) ++j;
            t.text = std::string(s_.substr(i_, j - i_));
            i_ = j;
            t.kind = t.text == "true" ? Tok::true_ : t.text == "false" ? Tok::false_ : t.text == "null" ? Tok::null_ : t.text == "in" ? Tok::in : Tok::ident;
            return t;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_ + 1]))))
            return number(t);
        switch (c) {
            case '"': case '\'': return string(t, false);
            case '+': return one(Tok::plus);
            case '-': return one(Tok::minus);
            case '*': return one(Tok::star);
            case '/': return one(Tok::slash);
            case '%': return one(Tok::percent);
            case '?': return one(Tok::question);
            case ':': return one(Tok::colon);
            case '.': return one(Tok::dot);
            case ',': return one(Tok::comma);
            case '(': return one(Tok::lparen);
            case ')': return one(Tok::rparen);
            case '[': return one(Tok::lbracket);
            case ']': return one(Tok::rbracket);
            case '{': return one(Tok::lbrace);
            case '}': return one(Tok::rbrace);
            case '<': return two('=', Tok::le, Tok::lt);
            case '>': return two('=', Tok::ge, Tok::gt);
            case '!': return two('=', Tok::ne, Tok::bang);
            case '=':
                if (i_ + 1 < s_.size() && s_[i_ + 1] == '=') return two('=', Tok::eqeq, Tok::eqeq);
                return error(t, "`=` is not an operator here", "compare with `==` (expressions do not assign)");
            case '&':
                if (i_ + 1 < s_.size() && s_[i_ + 1] == '&') return two('&', Tok::andand, Tok::andand);
                return error(t, "`&` is not an operator", "write `&&` for and");
            case '|':
                if (i_ + 1 < s_.size() && s_[i_ + 1] == '|') return two('|', Tok::oror, Tok::oror);
                return error(t, "`|` is not an operator", "write `||` for or");
            default: break;
        }
        return error(t, std::format("`{}` cannot start anything here", s_.substr(i_, 1)), "");
    }

private:
    Token error(Token t, std::string message, std::string hint) {
        t.kind = Tok::error;
        t.text = std::move(message);
        t.hint = std::move(hint);
        i_ = s_.size();
        return t;
    }

    Token number(Token t) {
        std::size_t j = i_;
        bool real = false;
        if (s_.substr(i_, 2) == "0x" || s_.substr(i_, 2) == "0X") {
            j += 2;
            while (j < s_.size() && std::isxdigit(static_cast<unsigned char>(s_[j]))) ++j;
            std::uint64_t v = 0;
            auto r = std::from_chars(s_.data() + i_ + 2, s_.data() + j, v, 16);
            if (r.ec != std::errc{} || j == i_ + 2 || v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                return error(t, "this hexadecimal number does not fit", "ints are 64-bit and signed");
            t.kind = Tok::integer;
            t.i = static_cast<std::int64_t>(v);
        } else {
            while (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) ++j;
            if (j < s_.size() && s_[j] == '.' && j + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j + 1]))) {
                real = true;
                ++j;
                while (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) ++j;
            }
            if (j < s_.size() && (s_[j] == 'e' || s_[j] == 'E')) {
                std::size_t k = j + 1;
                if (k < s_.size() && (s_[k] == '+' || s_[k] == '-')) ++k;
                if (k < s_.size() && std::isdigit(static_cast<unsigned char>(s_[k]))) {
                    real = true;
                    j = k;
                    while (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) ++j;
                }
            }
            const std::string_view text = s_.substr(i_, j - i_);
            if (real) {
                t.kind = Tok::real;
                if (std::from_chars(text.data(), text.data() + text.size(), t.d).ec != std::errc{})
                    return error(t, std::format("`{}` is not a number", text), "");
            } else {
                t.kind = Tok::integer;
                if (std::from_chars(text.data(), text.data() + text.size(), t.i).ec != std::errc{})
                    return error(t, std::format("{} does not fit in an int", text), "ints are 64-bit; write a double (1e20) for larger values");
            }
        }
        if (j < s_.size() && (s_[j] == 'u' || s_[j] == 'U') && t.kind == Tok::integer) ++j;  // CEL's uint suffix: an int here
        i_ = j;
        return t;
    }

    Token string(Token t, bool raw) {
        const char q = s_[i_];
        const bool triple = s_.substr(i_, 3) == std::string(3, q);
        i_ += triple ? 3 : 1;
        std::string out;
        while (true) {
            if (i_ >= s_.size()) return error(t, "this string never ends", std::format("close it with {}", triple ? std::string(3, q) : std::string(1, q)));
            const char c = s_[i_];
            if (triple ? s_.substr(i_, 3) == std::string(3, q) : c == q) {
                i_ += triple ? 3 : 1;
                break;
            }
            if (!triple && (c == '\n' || c == '\r')) return error(t, "a string ends at the end of its line", "use \\n, or a triple-quoted string");
            if (c != '\\' || raw) {
                out += c;
                ++i_;
                continue;
            }
            if (i_ + 1 >= s_.size()) return error(t, "this string never ends", "");
            const char e = s_[i_ + 1];
            i_ += 2;
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'v': out += '\v'; break;
                case 'a': out += '\a'; break;
                case '\\': out += '\\'; break;
                case '\'': out += '\''; break;
                case '"': out += '"'; break;
                case '`': out += '`'; break;
                case '?': out += '?'; break;
                case 'x': case 'X': case 'u': case 'U': {
                    const std::size_t n = e == 'x' || e == 'X' ? 2 : e == 'u' ? 4 : 8;
                    std::uint32_t cp = 0;
                    if (i_ + n > s_.size() || std::from_chars(s_.data() + i_, s_.data() + i_ + n, cp, 16).ptr != s_.data() + i_ + n)
                        return error(t, std::format("\\{} takes {} hexadecimal digits", e, n), "");
                    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return error(t, "not a Unicode scalar value", "");
                    append_utf8(out, cp);
                    i_ += n;
                    break;
                }
                default:
                    if (e >= '0' && e <= '3' && i_ + 1 < s_.size() && s_[i_] >= '0' && s_[i_] <= '7' && s_[i_ + 1] >= '0' && s_[i_ + 1] <= '7') {
                        append_utf8(out, static_cast<char32_t>((e - '0') * 64 + (s_[i_] - '0') * 8 + (s_[i_ + 1] - '0')));
                        i_ += 2;
                        break;
                    }
                    return error(t, std::format("`\\{}` is not an escape", e), "escapes: \\n \\t \\r \\\\ \\\" \\' \\xHH \\uHHHH \\UHHHHHHHH");
            }
        }
        t.kind = Tok::string;
        t.text = std::move(out);
        return t;
    }

    std::string_view s_;
    std::size_t      i_ = 0;
};

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text), lex_(text) { advance(); }

    std::expected<Expr, Diagnostic> run() {
        Expr e = expression();
        if (!failed_ && cur_.kind != Tok::end) fail(cur_.at, std::format("expected an operator or the end, found {}", describe(cur_.kind)),
                                                    cur_.kind == Tok::ident && (cur_.text == "and" || cur_.text == "or" || cur_.text == "not")
                                                        ? "CEL writes && for and, || for or, ! for not"
                                                        : "");
        if (failed_) return std::unexpected(diag_);
        return e;
    }

private:
    void advance() {
        cur_ = lex_.next();
        if (cur_.kind == Tok::error) fail(cur_.at, cur_.text, cur_.hint);
    }

    void fail(std::uint32_t at, std::string message, std::string hint = {}) {
        if (failed_) return;
        failed_ = true;
        diag_.at = at;
        diag_.message = std::move(message);
        diag_.hint = std::move(hint);
        locate(diag_, text_);
    }

    bool accept(Tok k) {
        if (cur_.kind != k) return false;
        advance();
        return true;
    }

    void expect(Tok k, std::string_view what) {
        if (failed_) return;
        if (cur_.kind == k) {
            advance();
            return;
        }
        fail(cur_.at, std::format("expected {}, found {}", what, describe(cur_.kind)),
             cur_.kind == Tok::end ? "the expression ends early" : "");
    }

    struct Depth {
        Parser& p;
        bool    ok;
        explicit Depth(Parser& parser) : p(parser), ok(++p.depth_ <= max_depth) {
            if (!ok) p.fail(p.cur_.at, std::format("nested deeper than {} levels", max_depth), "split it into simpler expressions");
        }
        ~Depth() { --p.depth_; }
    };

    // Every node of the parser passes here: a tree taller than max_depth is an
    // error, so evaluating, printing and comparing a parsed expression recurse
    // at most that deep (a 5000-term sum is too, not only 5000 parentheses).
    Expr located(Expr e, std::uint32_t at) {
        if (e.node().height > max_depth && !failed_)
            fail(at, std::format("nested deeper than {} levels", max_depth), "split it into simpler expressions");
        auto n = std::make_shared<Node>(e.node());
        n->at = at;
        return Expr(std::shared_ptr<const Node>(std::move(n)));
    }

    Expr expression() {
        Depth d(*this);
        if (!d.ok) return {};
        const std::uint32_t at = cur_.at;
        Expr c = binary(1);
        if (failed_ || !accept(Tok::question)) return c;
        // CEL's middle branch is a ConditionalOr; a conditional there is
        // accepted too (it is unambiguous, as in C) and prints in parentheses.
        Expr a = expression();
        expect(Tok::colon, "`:` (the other branch)");
        Expr b = expression();
        return located(select(std::move(c), std::move(a), std::move(b)), at);
    }

    static int precedence(Tok t) {
        switch (t) {
            case Tok::oror: return 1;
            case Tok::andand: return 2;
            case Tok::lt: case Tok::le: case Tok::gt: case Tok::ge: case Tok::eqeq: case Tok::ne: case Tok::in: return 3;
            case Tok::plus: case Tok::minus: return 4;
            case Tok::star: case Tok::slash: case Tok::percent: return 5;
            default: return 0;
        }
    }

    static Op op_of(Tok t) {
        switch (t) {
            case Tok::oror: return Op::or_;
            case Tok::andand: return Op::and_;
            case Tok::lt: return Op::lt;
            case Tok::le: return Op::le;
            case Tok::gt: return Op::gt;
            case Tok::ge: return Op::ge;
            case Tok::eqeq: return Op::eq;
            case Tok::ne: return Op::ne;
            case Tok::in: return Op::in;
            case Tok::plus: return Op::add;
            case Tok::minus: return Op::sub;
            case Tok::star: return Op::mul;
            case Tok::slash: return Op::div;
            default: return Op::mod;
        }
    }

    Expr binary(int min) {
        Expr left = unary();
        while (!failed_) {
            const int p = precedence(cur_.kind);
            if (p == 0 || p < min) break;
            const Tok op = cur_.kind;
            const std::uint32_t at = cur_.at;
            advance();
            Expr right = binary(p + 1);
            left = located(make(op_of(op), {std::move(left), std::move(right)}), at);
        }
        return left;
    }

    // The recursion that is not a node yet (parentheses, prefix operators,
    // nested arguments) is bounded here, in expression() and unary().
    Expr unary() {
        const std::uint32_t at = cur_.at;
        if (cur_.kind != Tok::bang && cur_.kind != Tok::minus) return postfix();
        Depth d(*this);
        if (!d.ok) return {};
        if (accept(Tok::bang)) return located(not_(unary()), at);
        if (accept(Tok::minus)) {
            // A negative literal stays a literal.
            if (cur_.kind == Tok::integer || cur_.kind == Tok::real) {
                Expr inner = postfix();
                const Node& n = inner.node();
                if (n.op == Op::literal && n.value.type() == Value::Type::integer) return located(Expr(Value(-n.value.as_int())), at);
                if (n.op == Op::literal && n.value.type() == Value::Type::real) return located(Expr(Value(-n.value.as_double())), at);
                return located(neg(std::move(inner)), at);
            }
            return located(neg(unary()), at);
        }
        return postfix();
    }

    std::vector<Expr> arguments(Tok close, std::string_view what) {
        std::vector<Expr> out;
        if (accept(close)) return out;
        while (!failed_) {
            out.push_back(expression());
            if (accept(close)) break;
            expect(Tok::comma, std::format("`,` or {}", what));
        }
        return out;
    }

    Expr postfix() {
        Expr e = primary();
        while (!failed_) {
            const std::uint32_t at = cur_.at;
            if (accept(Tok::dot)) {
                if (cur_.kind != Tok::ident) {
                    fail(cur_.at, std::format("expected a field or a function after `.`, found {}", describe(cur_.kind)));
                    break;
                }
                std::string name = cur_.text;
                advance();
                if (accept(Tok::lparen)) {
                    // A function called on its first argument: x.size() is size(x).
                    std::vector<Expr> args{std::move(e)};
                    for (Expr& a : arguments(Tok::rparen, "`)`")) args.push_back(std::move(a));
                    e = located(call(std::move(name), std::move(args)), at);
                } else {
                    e = located(e.field(std::move(name)), at);
                }
            } else if (accept(Tok::lbracket)) {
                Expr key = expression();
                expect(Tok::rbracket, "`]`");
                e = located(e[std::move(key)], at);
            } else {
                break;
            }
        }
        return e;
    }

    Expr primary() {
        const Token t = cur_;
        switch (t.kind) {
            case Tok::integer: advance(); return located(Expr(Value(t.i)), t.at);
            case Tok::real: advance(); return located(Expr(Value(t.d)), t.at);
            case Tok::string: advance(); return located(Expr(Value(t.text)), t.at);
            case Tok::true_: advance(); return located(Expr(true), t.at);
            case Tok::false_: advance(); return located(Expr(false), t.at);
            case Tok::null_: advance(); return located(Expr(Value{}), t.at);
            case Tok::lparen: {
                advance();
                Expr e = expression();
                expect(Tok::rparen, "`)`");
                return e;
            }
            case Tok::lbracket: {
                advance();
                return located(list(arguments(Tok::rbracket, "`]`")), t.at);
            }
            case Tok::lbrace: {
                advance();
                std::vector<std::pair<Expr, Expr>> entries;
                if (!accept(Tok::rbrace)) {
                    while (!failed_) {
                        Expr k = expression();
                        expect(Tok::colon, "`:` after a key");
                        Expr v = expression();
                        entries.emplace_back(std::move(k), std::move(v));
                        if (accept(Tok::rbrace)) break;
                        expect(Tok::comma, "`,` or `}`");
                    }
                }
                return located(map(std::move(entries)), t.at);
            }
            case Tok::ident: {
                advance();
                if (!accept(Tok::lparen)) return located(ref(t.text), t.at);
                if (t.text == "has") {
                    // CEL's macro: has(a.b) asks whether the field is there.
                    Expr arg = expression();
                    expect(Tok::rparen, "`)`");
                    if (!failed_ && arg.node().op != Op::member) {
                        fail(t.at, "has() takes a field", "write has(object.field)");
                        return {};
                    }
                    if (failed_) return {};
                    return located(expr::has(Expr(arg.node().args[0]), arg.node().name), t.at);
                }
                std::vector<Expr> args = arguments(Tok::rparen, "`)`");
                // select(c, a, b) is the combinators' name of c ? a : b.
                if (t.text == "select" && args.size() == 3 && !failed_)
                    return located(select(std::move(args[0]), std::move(args[1]), std::move(args[2])), t.at);
                return located(call(t.text, std::move(args)), t.at);
            }
            case Tok::end:
                fail(t.at, "the expression ends early", "complete it");
                return {};
            case Tok::error: return {};
            default:
                fail(t.at, std::format("expected a value, found {}", describe(t.kind)));
                return {};
        }
    }

    std::string_view text_;
    Lexer            lex_;
    Token            cur_;
    bool             failed_ = false;
    Diagnostic       diag_;
    int              depth_ = 0;
};

}  // namespace

void locate(Diagnostic& d, std::string_view source) noexcept {
    // Line and column, in code points, of the byte offset.
    std::uint32_t line = 1, col = 1;
    for (std::size_t i = 0; i < d.at && i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++line;
            col = 1;
        } else if ((static_cast<unsigned char>(source[i]) & 0xC0) != 0x80) {
            ++col;
        }
    }
    d.line = line;
    d.column = col;
}

std::string Diagnostic::text() const {
    const std::string where = line ? std::format("{}:{}: ", line, column) : std::string();
    return hint.empty() ? where + message : std::format("{}{} ({})", where, message, hint);
}

std::expected<Expr, Diagnostic> parse(std::string_view text) {
    Parser p(text);
    return p.run();
}

}  // namespace omui::expr
