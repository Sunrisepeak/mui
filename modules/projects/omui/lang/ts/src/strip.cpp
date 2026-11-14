// TypeScript to JavaScript by blanking the types (omui.lang.ts:strip).
//
// One pass of a skimming parser: it knows the JavaScript grammar well enough
// to tell where a type begins and ends — an annotation after a parameter, the
// `<T>` of a call, a return type from an arrow's body — and builds nothing.
// Types are skipped by their own small grammar (balanced brackets are enough
// inside them, since all of a type is blanked). What is ambiguous is decided
// as TypeScript decides it: an arrow's head is tried and taken back when it
// is not one, `f<T>(x)` is a call with type arguments only when what follows
// could not go on an expression. Blanking keeps every line break; where it
// would change how a statement ends, a `;` is written into the blank (a type
// at the end of a statement, a whole statement removed), and an arrow whose
// return type spans lines has its `)` moved to the type's last column (no line
// may come between `)` and `=>` in JavaScript).
module omui.lang.ts;

import std;
import mcpplibs.os;

namespace omui::lang::ts {

namespace {

enum class Kind : std::uint8_t { end, name, private_name, number, string, template_piece, regex, punct };

struct Token {
    Kind kind = Kind::end;
    std::size_t start = 0, end = 0;
    bool line_before = false;    // a line break between it and the token before
    bool template_open = false;  // a template piece ending in `${`
};

bool name_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c == '\\' || c >= 0x80;
}
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool name_part(unsigned char c) { return name_start(c) || digit(c); }

// Punctuators longest first; `>` is always one character (a type argument
// list may close with it), joined into `>>`, `>=` … where an operator goes.
constexpr std::array punctuators{
    std::string_view{"..."}, std::string_view{"==="}, std::string_view{"!=="}, std::string_view{"**="},
    std::string_view{"<<="}, std::string_view{"&&="},  std::string_view{"||="}, std::string_view{"??="},
    std::string_view{"=>"},  std::string_view{"=="},   std::string_view{"!="},  std::string_view{"<="},
    std::string_view{"<<"},  std::string_view{"&&"},   std::string_view{"||"},  std::string_view{"??"},
    std::string_view{"?."},  std::string_view{"++"},   std::string_view{"--"},  std::string_view{"+="},
    std::string_view{"-="},  std::string_view{"*="},   std::string_view{"/="},  std::string_view{"%="},
    std::string_view{"&="},  std::string_view{"|="},   std::string_view{"^="},  std::string_view{"**"},
};

constexpr std::array binary_operators{
    std::string_view{"??"}, std::string_view{"||"},  std::string_view{"&&"},  std::string_view{"|"},
    std::string_view{"^"},  std::string_view{"&"},   std::string_view{"=="},  std::string_view{"!="},
    std::string_view{"==="}, std::string_view{"!=="}, std::string_view{"<"},  std::string_view{">"},
    std::string_view{"<="}, std::string_view{">="},  std::string_view{"<<"},  std::string_view{">>"},
    std::string_view{">>>"}, std::string_view{"+"},  std::string_view{"-"},   std::string_view{"*"},
    std::string_view{"/"},  std::string_view{"%"},   std::string_view{"**"},  std::string_view{"instanceof"},
    std::string_view{"in"},
};

constexpr std::array assignment_operators{
    std::string_view{"="},   std::string_view{"+="},  std::string_view{"-="},   std::string_view{"*="},
    std::string_view{"/="},  std::string_view{"%="},  std::string_view{"**="},  std::string_view{"<<="},
    std::string_view{">>="}, std::string_view{">>>="}, std::string_view{"&="},  std::string_view{"|="},
    std::string_view{"^="},  std::string_view{"&&="}, std::string_view{"||="},  std::string_view{"??="},
};

constexpr std::array expression_punctuators{
    std::string_view{"("}, std::string_view{"["},  std::string_view{"{"},  std::string_view{"!"},
    std::string_view{"~"}, std::string_view{"+"},  std::string_view{"-"},  std::string_view{"++"},
    std::string_view{"--"}, std::string_view{"/"}, std::string_view{"/="}, std::string_view{"<"},
    std::string_view{"@"}, std::string_view{"#"},
};

// Names that read as an operator, not an operand, where an expression could begin.
constexpr std::array operator_words{
    std::string_view{"in"}, std::string_view{"of"}, std::string_view{"instanceof"}, std::string_view{"as"},
    std::string_view{"satisfies"}, std::string_view{"extends"}, std::string_view{"implements"},
};

// Modifiers of TypeScript's own, blanked where they stand.
constexpr std::array type_modifiers{
    std::string_view{"public"}, std::string_view{"private"}, std::string_view{"protected"},
    std::string_view{"readonly"}, std::string_view{"override"},
};

constexpr std::array member_modifiers{
    std::string_view{"public"},   std::string_view{"private"}, std::string_view{"protected"}, std::string_view{"readonly"},
    std::string_view{"override"}, std::string_view{"static"},  std::string_view{"abstract"},  std::string_view{"declare"},
    std::string_view{"accessor"},
};

template <std::size_t Count>
bool one_of(std::string_view word, const std::array<std::string_view, Count>& words) {
    return std::ranges::find(words, word) != words.end();
}

constexpr std::string_view enum_advice =
    "`enum` is not erasable: write `const Name = { A: 0, B: 1 } as const;` and, for the type, "
    "`type Name = (typeof Name)[keyof typeof Name];`";
constexpr std::string_view namespace_advice =
    "`namespace` is not erasable: use a module of its own (or `declare namespace` for types only)";
constexpr std::string_view parameter_property_advice =
    "a parameter property is not erasable: declare the field in the class and assign it in the constructor";
constexpr std::string_view import_equals_advice = "`import x = …` is not erasable: write `import x from \"…\"`";
constexpr std::string_view export_equals_advice = "`export =` is not erasable: write `export default …`";
constexpr std::string_view assertion_advice = "`<T>value` is not erasable: write `value as T`";
constexpr std::string_view decorator_advice = "a decorator does not run here: call the function on the value instead";

class Stripper {
public:
    explicit Stripper(std::string_view source) : source_(source) {}

    std::expected<std::string, std::vector<TypeScriptProblem>> run() {
        if (source_.starts_with("#!")) position_ = std::min(source_.find('\n'), source_.size());
        next();
        while (token_.kind != Kind::end) statement();
        if (!problems_.empty()) return std::unexpected(located());
        std::string out(source_);
        for (const auto& [from, to] : blanks_)
            for (std::size_t at = from; at < to && at < out.size(); ++at) {
                if (out[at] == '\n' || out[at] == '\r') continue;
                if (line_separator_at(at)) {  // U+2028, U+2029: line breaks too
                    at += 2;
                    continue;
                }
                out[at] = ' ';
            }
        for (const auto& [at, character] : writes_) out[at] = character;
        return out;
    }

private:
    struct State {
        std::size_t position, previous_end, blanks, writes, problems;
        Token token;
        bool stopped;
    };

    // ---- scanning -------------------------------------------------------

    [[nodiscard]] unsigned char at(std::size_t offset) const {
        return offset < source_.size() ? static_cast<unsigned char>(source_[offset]) : 0;
    }
    [[nodiscard]] bool line_separator_at(std::size_t offset) const {
        return at(offset) == 0xE2 && at(offset + 1) == 0x80 && (at(offset + 2) == 0xA8 || at(offset + 2) == 0xA9);
    }

    // Past spaces and comments; whether a line break was among them.
    bool skip_trivia() {
        bool line = false;
        while (position_ < source_.size()) {
            const unsigned char character = at(position_);
            if (character == '\n' || character == '\r') {
                line = true;
                ++position_;
            } else if (character == ' ' || character == '\t' || character == '\v' || character == '\f') {
                ++position_;
            } else if (line_separator_at(position_)) {
                line = true;
                position_ += 3;
            } else if (character == 0xC2 && at(position_ + 1) == 0xA0) {
                position_ += 2;  // no-break space
            } else if (character == 0xEF && at(position_ + 1) == 0xBB && at(position_ + 2) == 0xBF) {
                position_ += 3;  // byte order mark
            } else if (character == '/' && at(position_ + 1) == '/') {
                while (position_ < source_.size() && at(position_) != '\n' && at(position_) != '\r' &&
                       !line_separator_at(position_))
                    ++position_;
            } else if (character == '/' && at(position_ + 1) == '*') {
                const auto close = source_.find("*/", position_ + 2);
                if (close == std::string_view::npos) {
                    problem(position_, "a comment that does not end");
                    position_ = source_.size();
                    break;
                }
                const auto body = source_.substr(position_, close - position_);
                if (body.find_first_of("\r\n") != std::string_view::npos || body.find("\xE2\x80\xA8") != std::string_view::npos ||
                    body.find("\xE2\x80\xA9") != std::string_view::npos)
                    line = true;
                position_ = close + 2;
            } else {
                break;
            }
        }
        return line;
    }

    void scan_name() {
        while (position_ < source_.size()) {
            const unsigned char character = at(position_);
            if (character == '\\') {  // \uXXXX or \u{X…}
                position_ += 2;
                if (at(position_) == '{') {
                    while (position_ < source_.size() && at(position_) != '}') ++position_;
                    ++position_;
                } else {
                    position_ += 4;
                }
            } else if (name_part(character)) {
                ++position_;
            } else {
                break;
            }
        }
        position_ = std::min(position_, source_.size());
    }

    void scan_number() {
        if (at(position_) == '0' && std::string_view("xXoObB").contains(static_cast<char>(at(position_ + 1)))) {
            position_ += 2;
            while (name_part(at(position_))) ++position_;
            return;
        }
        while (digit(at(position_)) || at(position_) == '_') ++position_;
        if (at(position_) == '.') {
            ++position_;
            while (digit(at(position_)) || at(position_) == '_') ++position_;
        }
        if (at(position_) == 'e' || at(position_) == 'E') {
            ++position_;
            if (at(position_) == '+' || at(position_) == '-') ++position_;
            while (digit(at(position_)) || at(position_) == '_') ++position_;
        }
        if (at(position_) == 'n') ++position_;
    }

    void scan_string(unsigned char quote) {
        ++position_;
        while (position_ < source_.size()) {
            const unsigned char character = at(position_);
            if (character == quote) {
                ++position_;
                return;
            }
            if (character == '\\') {
                position_ += at(position_ + 1) == '\r' && at(position_ + 2) == '\n' ? 3 : 2;
                continue;
            }
            if (character == '\n' || character == '\r') break;
            ++position_;
        }
        problem(position_, "a string that does not end");
        position_ = std::min(position_, source_.size());
    }

    // A template from just past its '`' or a substitution's '}': to the closing '`' or a `${`.
    void scan_template(Token& token) {
        while (position_ < source_.size()) {
            const unsigned char character = at(position_);
            if (character == '`') {
                ++position_;
                token.template_open = false;
                return;
            }
            if (character == '\\') {
                position_ += 2;
                continue;
            }
            if (character == '$' && at(position_ + 1) == '{') {
                position_ += 2;
                token.template_open = true;
                return;
            }
            ++position_;
        }
        problem(token.start, "a template that does not end");
        position_ = source_.size();
    }

    Token scan() {
        Token token;
        token.start = position_;
        if (position_ >= source_.size()) {
            token.end = position_;
            return token;
        }
        const unsigned char character = at(position_);
        if (name_start(character)) {
            token.kind = Kind::name;
            scan_name();
        } else if (character == '#' && name_start(at(position_ + 1))) {
            token.kind = Kind::private_name;
            ++position_;
            scan_name();
        } else if (digit(character) || (character == '.' && digit(at(position_ + 1)))) {
            token.kind = Kind::number;
            scan_number();
        } else if (character == '"' || character == '\'') {
            token.kind = Kind::string;
            scan_string(character);
        } else if (character == '`') {
            token.kind = Kind::template_piece;
            ++position_;
            scan_template(token);
        } else {
            token.kind = Kind::punct;
            std::size_t length = 1;
            for (const auto punctuator : punctuators)
                if (source_.substr(position_).starts_with(punctuator)) {
                    length = punctuator.size();
                    break;
                }
            if (length == 2 && source_.substr(position_, 2) == "?." && digit(at(position_ + 2))) length = 1;  // a ? .5 : 1
            position_ += length;
        }
        token.end = position_;
        return token;
    }

    void next() {
        previous_end_ = token_.end;
        if (stopped_) {
            token_ = Token{Kind::end, source_.size(), source_.size(), true, false};
            return;
        }
        const bool line = skip_trivia();
        token_ = scan();
        token_.line_before = line;
    }

    // The current `/` or `/=` read again as a regular expression.
    void rescan_regex() {
        position_ = token_.start + 1;
        bool in_class = false;
        while (true) {
            const unsigned char character = at(position_);
            if (position_ >= source_.size() || character == '\n' || character == '\r') {
                fail("a regular expression that does not end");
                return;
            }
            ++position_;
            if (character == '\\') {
                ++position_;
            } else if (character == '[') {
                in_class = true;
            } else if (character == ']') {
                in_class = false;
            } else if (character == '/' && !in_class) {
                break;
            }
        }
        while (name_part(at(position_))) ++position_;
        token_.kind = Kind::regex;
        token_.end = position_;
    }

    // The current `}` read again as the rest of a template.
    void rescan_template() {
        position_ = token_.start + 1;
        token_.kind = Kind::template_piece;
        scan_template(token_);
        token_.end = position_;
    }

    // The current `>` joined with the `>` and `=` right after it, as an operator.
    void join_greater() {
        if (!is(">")) return;
        std::size_t end = token_.start + 1;
        if (at(end) == '>') ++end;
        if (at(end) == '>' && end == token_.start + 2) ++end;
        if (at(end) == '=') ++end;
        token_.end = position_ = end;
    }

    // ---- tokens -----------------------------------------------------------

    [[nodiscard]] std::string_view text_of(const Token& token) const {
        return source_.substr(token.start, token.end - token.start);
    }
    [[nodiscard]] std::string_view text() const { return text_of(token_); }
    [[nodiscard]] bool is(std::string_view word) const {
        return (token_.kind == Kind::punct || token_.kind == Kind::name) && text() == word;
    }
    [[nodiscard]] bool token_is(const Token& token, std::string_view word) const {
        return (token.kind == Kind::punct || token.kind == Kind::name) && text_of(token) == word;
    }
    [[nodiscard]] bool at_end() const { return token_.kind == Kind::end; }
    bool eat(std::string_view word) {
        if (!is(word)) return false;
        next();
        return true;
    }
    void expect(std::string_view word) {
        if (!eat(word)) fail(std::format("`{}` expected", word));
    }
    void expect_name() {
        if (token_.kind == Kind::name)
            next();
        else
            fail("a name expected");
    }

    [[nodiscard]] Token peek() {
        const State before = save();
        next();
        const Token ahead = token_;
        restore(before);
        return ahead;
    }

    [[nodiscard]] bool starts_expression(const Token& token) const {
        switch (token.kind) {
        case Kind::end: return false;
        case Kind::name: return !one_of(text_of(token), operator_words);
        case Kind::punct: return one_of(text_of(token), expression_punctuators);
        default: return true;
        }
    }

    [[nodiscard]] State save() const {
        return {position_, previous_end_, blanks_.size(), writes_.size(), problems_.size(), token_, stopped_};
    }
    void restore(const State& state) {
        position_ = state.position;
        previous_end_ = state.previous_end;
        blanks_.resize(state.blanks);
        writes_.resize(state.writes);
        problems_.resize(state.problems);
        token_ = state.token;
        stopped_ = state.stopped;
    }

    // ---- results ----------------------------------------------------------

    void problem(std::size_t offset, std::string_view text) { problems_.emplace_back(offset, std::string(text)); }
    // Not this grammar: stop reading (an attempt is taken back by its caller).
    void fail(std::string_view text) {
        if (stopped_) return;
        problem(token_.start, text);
        stopped_ = true;
        token_ = Token{Kind::end, source_.size(), source_.size(), true, false};
    }
    void blank(std::size_t from, std::size_t to) {
        if (to > from) blanks_.emplace_back(from, to);
    }
    // A whole statement or class member gone. Where the one before it did
    // not end with `;` and what follows would continue it (`(`, `[`, a
    // template …), a `;` where it began keeps them apart.
    void blank_statement(std::size_t from, std::size_t to) {
        blank(from, to);
        if (to <= from || boundary_ == 0 || !continues(token_)) return;
        const char before = source_[boundary_ - 1];
        if (before != ';' && before != '{') writes_.emplace_back(from, ';');
    }

    // Whether a statement or member starting with `token` would go on the one before it, were nothing between them.
    [[nodiscard]] bool continues(const Token& token) const {
        if (token.kind == Kind::template_piece || token.kind == Kind::regex) return true;
        return token_is(token, "(") || token_is(token, "[") || token_is(token, "+") || token_is(token, "-") || token_is(token, "/") ||
               token_is(token, "/=") || token_is(token, "*");
    }
    // A statement ended without `;`: when a blank ends it, a `;` goes into it.
    void end_with_blank() {
        if (!blanks_.empty() && blanks_.back().second == previous_end_ && previous_end_ > 0)
            writes_.emplace_back(previous_end_ - 1, ';');
    }

    [[nodiscard]] std::vector<TypeScriptProblem> located() const {
        std::vector<TypeScriptProblem> out;
        for (const auto& [offset, text] : problems_) {
            std::size_t line = 1, line_start = 0;
            for (std::size_t index = 0; index < offset && index < source_.size(); ++index)
                if (source_[index] == '\n') {
                    ++line;
                    line_start = index + 1;
                }
            out.push_back({line, offset - line_start + 1, text});
        }
        return out;
    }

    // ---- types --------------------------------------------------------------

    // A type, from the current token to just past it (the caller blanks it).
    void type() {
        if (function_type_ahead()) {
            function_type();
            return;
        }
        union_type();
        if (!in_extends_ && !token_.line_before && is("extends")) {
            next();
            in_extends_ = true;
            type();
            in_extends_ = false;
            expect("?");
            type();
            expect(":");
            type();
        }
    }

    // A type in brackets of its own: conditional types are allowed again.
    void nested_type() {
        const bool outer = in_extends_;
        in_extends_ = false;
        type();
        in_extends_ = outer;
    }

    // `(…) =>`, `new (…) =>`, `<T>(…) =>`: TypeScript's test, where `(A | B)` is a type in parentheses.
    bool function_type_ahead() {
        if (is("<") || is("new")) return true;
        if (is("abstract")) return token_is(peek(), "new");
        if (!is("(")) return false;
        const State before = save();
        next();
        bool function = false;
        if (is(")") || is("...")) {
            function = true;
        } else {
            while (token_.kind == Kind::name && one_of(text(), type_modifiers) && peek().kind == Kind::name) next();
            bool parameter = false;
            if (token_.kind == Kind::name) {
                next();
                parameter = true;
            } else if (is("{") || is("[")) {
                skip_balanced();
                parameter = !stopped_;
            }
            if (parameter) {
                if (is(":") || is(",") || is("?") || is("=")) {
                    function = true;
                } else if (is(")")) {
                    next();
                    function = is("=>");
                }
            }
        }
        restore(before);
        return function;
    }

    void function_type() {
        eat("abstract");
        eat("new");
        if (is("<")) type_parameters();
        if (is("("))
            skip_balanced();
        else
            fail("`(` expected");
        expect("=>");
        type();
    }

    void union_type() {
        eat("|");
        intersection_type();
        while (eat("|")) intersection_type();
    }

    void intersection_type() {
        eat("&");
        operand_type();
        while (eat("&")) operand_type();
    }

    void operand_type() {
        if (function_type_ahead()) {
            function_type();
            return;
        }
        if ((is("keyof") || is("unique") || is("readonly")) && peek().kind != Kind::punct) {
            next();
            operand_type();
            return;
        }
        if ((is("keyof") || is("readonly")) && (token_is(peek(), "(") || token_is(peek(), "[") || token_is(peek(), "{"))) {
            next();
            operand_type();
            return;
        }
        if (is("infer")) {
            next();
            expect_name();
            if (is("extends")) {  // a constraint, unless it is a conditional's
                const State before = save();
                const bool outer = in_extends_;
                next();
                in_extends_ = true;
                type();
                in_extends_ = outer;
                if (!outer && is("?")) restore(before);
            }
            return;
        }
        primary_type();
        while (!token_.line_before && is("[")) {
            next();
            if (!is("]")) type();
            expect("]");
        }
    }

    void primary_type() {
        if (token_.kind == Kind::string || token_.kind == Kind::number) {
            next();
            return;
        }
        if (token_.kind == Kind::template_piece) {
            while (token_.template_open) {
                next();
                nested_type();
                if (!is("}")) {
                    fail("`}` expected");
                    return;
                }
                rescan_template();
            }
            next();
            return;
        }
        if (is("-")) {
            next();
            if (token_.kind == Kind::number)
                next();
            else
                fail("a number expected");
            return;
        }
        if (is("(")) {
            next();
            nested_type();
            expect(")");
            return;
        }
        if (is("{") || is("[")) {
            skip_balanced();
            return;
        }
        if (is("typeof")) {
            next();
            if (is("import"))
                import_type();
            else
                entity_name();
            if (!token_.line_before && is("<")) type_arguments();
            return;
        }
        if (is("import")) {
            import_type();
            return;
        }
        if (is("asserts")) {
            const Token ahead = peek();
            if (ahead.kind == Kind::name && !ahead.line_before) {
                next();
                next();
                if (eat("is")) type();
                return;
            }
        }
        if (token_.kind == Kind::name) {
            entity_name();
            if (!token_.line_before && is("<")) type_arguments();
            if (!token_.line_before && is("is")) {
                next();
                type();
            }
            return;
        }
        fail("a type expected");
    }

    void entity_name() {
        expect_name();
        while (is(".")) {
            next();
            expect_name();
        }
    }

    void import_type() {
        next();  // import
        if (is("("))
            skip_balanced();
        else
            fail("`(` expected");
        while (eat(".")) expect_name();
        if (!token_.line_before && is("<")) type_arguments();
    }

    void type_arguments() {
        expect("<");
        while (!is(">") && !at_end()) {
            nested_type();
            if (!eat(",")) break;
        }
        expect(">");
    }

    void type_parameters() {
        expect("<");
        while (!is(">") && !at_end()) {
            while ((is("const") || is("in") || is("out")) && peek().kind == Kind::name) next();
            expect_name();
            if (eat("extends")) type();
            if (eat("=")) type();
            if (!eat(",")) break;
        }
        expect(">");
    }

    // From `(`, `[` or `{` to just past its match (templates and their substitutions too).
    void skip_balanced() {
        std::vector<char> closers;  // ')', ']', '}', or 't' for a template's substitution
        do {
            if (at_end()) {
                fail("brackets that do not close");
                return;
            }
            if (token_.kind == Kind::punct) {
                const auto word = text();
                if (word == "(")
                    closers.push_back(')');
                else if (word == "[")
                    closers.push_back(']');
                else if (word == "{")
                    closers.push_back('}');
                else if (word == ")" || word == "]" || word == "}") {
                    if (closers.empty()) {
                        fail("brackets that do not match");
                        return;
                    }
                    if (closers.back() == 't' && word == "}") {
                        closers.pop_back();
                        rescan_template();
                        if (token_.template_open) closers.push_back('t');
                    } else if (closers.back() == word[0]) {
                        closers.pop_back();
                    } else {
                        fail("brackets that do not match");
                        return;
                    }
                }
            } else if (token_.kind == Kind::template_piece && token_.template_open) {
                closers.push_back('t');
            }
            next();
        } while (!closers.empty());
    }

    // `: T` where the current token is the colon: blanked.
    void annotation() {
        const std::size_t start = token_.start;
        next();
        type();
        blank(start, previous_end_);
    }

    // ---- expressions --------------------------------------------------------

    void expression(bool no_in = false) {
        assignment(no_in);
        while (eat(",")) assignment(no_in);
    }

    void assignment(bool no_in = false, bool consequent = false) {
        if (try_arrow(no_in, consequent, true)) return;
        if (is("yield")) {
            next();
            if (!token_.line_before && (is("*") || starts_expression(token_))) {
                eat("*");
                assignment(no_in);
            }
            return;
        }
        binary(no_in);
        if (is("?")) {
            next();
            assignment(false, true);
            expect(":");
            assignment(no_in);
            return;
        }
        join_greater();
        if ((token_.kind == Kind::punct) && one_of(text(), assignment_operators)) {
            next();
            assignment(no_in);
        }
    }

    void binary(bool no_in) {
        unary();
        while (!at_end()) {
            join_greater();
            if (!token_.line_before && (is("as") || is("satisfies"))) {
                const std::size_t start = token_.start;
                next();
                if (is("const"))
                    next();
                else
                    type();
                blank(start, previous_end_);
                continue;
            }
            const bool operator_token = token_.kind == Kind::punct || is("instanceof") || is("in");
            if (!operator_token || !one_of(text(), binary_operators) || (no_in && is("in"))) break;
            next();
            unary();
        }
    }

    void unary() {
        if (is("!") || is("~") || is("+") || is("-") || is("++") || is("--") || is("typeof") || is("void") ||
            is("delete")) {
            next();
            unary();
            return;
        }
        if (is("await") && starts_expression(peek())) {
            next();
            unary();
            return;
        }
        if (is("<")) {  // <T>value
            problem(token_.start, assertion_advice);
            type_arguments();
            unary();
            return;
        }
        primary();
        member_rest(false);
        if (!token_.line_before && (is("++") || is("--"))) next();
    }

    void member_rest(bool no_call) {
        while (!at_end()) {
            if (is(".")) {
                next();
                if (token_.kind == Kind::name || token_.kind == Kind::private_name)
                    next();
                else
                    fail("a name expected");
            } else if (is("?.")) {
                next();
                if (is("<")) {
                    const std::size_t start = token_.start;
                    type_arguments();
                    blank(start, previous_end_);
                }
                if (is("("))
                    arguments();
                else if (is("[")) {
                    next();
                    expression();
                    expect("]");
                } else if (token_.kind == Kind::name || token_.kind == Kind::private_name)
                    next();
                else
                    fail("a name expected");
            } else if (is("[")) {
                next();
                expression();
                expect("]");
            } else if (!no_call && is("(")) {
                arguments();
            } else if (token_.kind == Kind::template_piece) {
                template_literal();
            } else if (!token_.line_before && is("!")) {  // value!
                blank(token_.start, token_.end);
                next();
            } else if (is("<") && type_arguments_in_expression()) {
            } else {
                break;
            }
        }
    }

    // `f<T>(…)`, `f<T>` …: type arguments when what follows could not go on an expression.
    bool type_arguments_in_expression() {
        const State before = save();
        const std::size_t start = token_.start;
        type_arguments();
        bool taken = false;
        if (!stopped_) {
            if (is("(") || token_.kind == Kind::template_piece)
                taken = true;
            else if (is("<") || is(">") || is("+") || is("-"))
                taken = false;
            else
                taken = token_.line_before || at_end() ||
                        (token_.kind == Kind::punct && one_of(text(), binary_operators)) || !starts_expression(token_);
        }
        if (!taken) {
            restore(before);
            return false;
        }
        blank(start, previous_end_);
        return true;
    }

    void arguments() {
        expect("(");
        while (!is(")") && !at_end()) {
            eat("...");
            assignment();
            if (!eat(",")) break;
        }
        expect(")");
    }

    void template_literal() {
        while (token_.template_open) {
            next();
            expression();
            if (!is("}")) {
                fail("`}` expected");
                return;
            }
            rescan_template();
        }
        next();
    }

    void primary() {
        const std::size_t start = token_.start;
        switch (token_.kind) {
        case Kind::number:
        case Kind::string:
        case Kind::regex:
        case Kind::private_name: next(); return;
        case Kind::template_piece: template_literal(); return;
        case Kind::end: fail("an expression expected"); return;
        case Kind::punct:
            if (is("/") || is("/=")) {
                rescan_regex();
                next();
            } else if (is("(")) {
                next();
                expression();
                expect(")");
            } else if (is("[")) {
                array_literal();
            } else if (is("{")) {
                object_literal();
            } else if (is("@")) {
                decorators();
                primary();
            } else {
                fail("an expression expected");
            }
            return;
        case Kind::name: break;
        }
        if (is("function") || (is("async") && token_is(peek(), "function") && !peek().line_before)) {
            eat("async");
            next();
            eat("*");
            if (token_.kind == Kind::name && !is("<")) next();
            callable_rest(start, false);
        } else if (is("class")) {
            class_(start);
        } else if (is("new")) {
            next();
            if (eat(".")) {
                expect_name();
                return;
            }
            primary();
            member_rest(true);
            if (is("(")) arguments();
        } else if (is("import")) {
            next();
            if (eat("."))
                expect_name();
            else
                arguments();
        } else {
            next();
        }
    }

    void array_literal() {
        expect("[");
        while (!is("]") && !at_end()) {
            if (eat(",")) continue;
            eat("...");
            assignment();
            if (!is("]")) expect(",");
        }
        expect("]");
    }

    void object_literal() {
        expect("{");
        while (!is("}") && !at_end()) {
            if (eat("...")) {
                assignment();
            } else {
                const std::size_t start = token_.start;
                if ((is("get") || is("set") || is("async")) && name_follows()) next();
                eat("*");
                property_name();
                if (is("(") || is("<")) {
                    callable_rest(start, false);
                } else if (eat(":")) {
                    assignment();
                } else if (eat("=")) {  // a default in a pattern
                    assignment();
                }
            }
            if (!is("}")) expect(",");
        }
        expect("}");
    }

    // After `get`, `set`, `async`, `static` …: whether a member's name follows (else it is the name).
    bool name_follows() {
        const Token ahead = peek();
        if (ahead.line_before) return false;
        return ahead.kind == Kind::name || ahead.kind == Kind::string || ahead.kind == Kind::number ||
               ahead.kind == Kind::private_name || token_is(ahead, "[") || token_is(ahead, "*");
    }

    void property_name() {
        if (is("[")) {
            next();
            assignment();
            expect("]");
        } else if (token_.kind == Kind::name || token_.kind == Kind::string || token_.kind == Kind::number ||
                   token_.kind == Kind::private_name) {
            next();
        } else {
            fail("a property name expected");
        }
    }

    // An arrow function, if one starts here: `x =>`, `(…) =>`, `<T>(…) =>`, `async …`.
    bool try_arrow(bool no_in, bool consequent, bool return_type_allowed) {
        const std::size_t start = token_.start;
        if (token_.kind == Kind::name && !is("async")) {
            const Token ahead = peek();
            if (!token_is(ahead, "=>") || ahead.line_before) return false;
            next();
            next();
            arrow_body(no_in);
            return true;
        }
        if (is("async")) {
            const Token ahead = peek();
            if (ahead.line_before || !(token_is(ahead, "(") || token_is(ahead, "<") ||
                                       (ahead.kind == Kind::name && !token_is(ahead, "function"))))
                return false;
        } else if (!is("(") && !is("<")) {
            return false;
        }
        if (return_type_allowed && not_arrows_.contains(start)) return false;
        const State before = save();
        eat("async");
        bool typed = false;
        std::size_t close = 0, type_end = 0;
        if (token_.kind == Kind::name && !is("async")) {
            next();
        } else {
            if (is("<")) {
                const std::size_t from = token_.start;
                type_parameters();
                blank(from, previous_end_);
            }
            parameters();
            close = previous_end_ - 1;
            if (is(":")) {
                if (!return_type_allowed) {
                    restore(before);
                    return false;
                }
                typed = true;
                annotation();
                type_end = previous_end_;
            }
        }
        if (stopped_ || !is("=>") || token_.line_before) {
            restore(before);
            if (return_type_allowed) not_arrows_.insert(start);
            return false;
        }
        next();
        arrow_body(no_in);
        if (typed && consequent && !is(":") && !stopped_) {  // `a ? (b) : c => d` is not an arrow with a return type
            restore(before);
            return try_arrow(no_in, consequent, false);
        }
        if (typed && source_.substr(close, type_end - close).find_first_of("\r\n") != std::string_view::npos) {
            writes_.emplace_back(close, ' ');
            writes_.emplace_back(type_end - 1, ')');
        }
        return true;
    }

    void arrow_body(bool no_in) {
        if (is("{"))
            function_body();
        else
            assignment(no_in);
    }

    void function_body() {
        expect("{");
        while (!is("}") && !at_end()) statement();
        expect("}");
    }

    void parameters() {
        expect("(");
        while (!is(")") && !at_end()) {
            parameter();
            if (!is(")")) expect(",");
        }
        expect(")");
    }

    void parameter() {
        const std::size_t start = token_.start;
        if (is("@")) decorators();
        if (is("this") && token_is(peek(), ":")) {  // this: T — gone, with its comma
            next();
            next();
            type();
            blank(start, is(",") ? token_.end : previous_end_);
            return;
        }
        while (token_.kind == Kind::name && one_of(text(), type_modifiers)) {
            const Token ahead = peek();
            if (!(ahead.kind == Kind::name || token_is(ahead, "{") || token_is(ahead, "[") || token_is(ahead, "...")))
                break;
            problem(start, parameter_property_advice);
            next();
        }
        eat("...");
        binding();
        if (is("?")) {
            blank(token_.start, token_.end);
            next();
        }
        if (is(":")) annotation();
        if (eat("=")) assignment();
    }

    void binding() {
        if (is("{"))
            object_literal();
        else if (is("["))
            array_literal();
        else
            expect_name();
    }

    // From type parameters or `(`: through the body. Without a body (and
    // `signature`), it was an overload or an abstract member: blanked from `start`.
    void callable_rest(std::size_t start, bool signature) {
        if (is("<")) {
            const std::size_t from = token_.start;
            type_parameters();
            blank(from, previous_end_);
        }
        parameters();
        if (is(":")) annotation();
        if (is("{")) {
            function_body();
            return;
        }
        if (signature && (is(";") || is("}") || at_end() || token_.line_before)) {
            eat(";");
            blank_statement(start, previous_end_);
            return;
        }
        fail("`{` expected");
    }

    void decorators() {
        while (is("@")) {
            problem(token_.start, decorator_advice);
            next();
            primary();
            member_rest(false);
        }
    }

    // ---- classes --------------------------------------------------------------

    void class_(std::size_t start) {
        (void)start;
        expect("class");
        if (token_.kind == Kind::name && !is("extends") && !is("implements")) next();
        if (is("<")) {
            const std::size_t from = token_.start;
            type_parameters();
            blank(from, previous_end_);
        }
        if (eat("extends")) {
            primary();
            member_rest(false);
            if (is("<")) {
                const std::size_t from = token_.start;
                type_arguments();
                blank(from, previous_end_);
            }
        }
        if (is("implements")) {
            const std::size_t from = token_.start;
            next();
            type();
            while (eat(",")) type();
            blank(from, previous_end_);
        }
        expect("{");
        while (!is("}") && !at_end()) {
            if (eat(";")) continue;
            class_member();
        }
        expect("}");
    }

    void class_member() {
        const std::size_t start = token_.start;
        boundary_ = previous_end_;
        if (is("@")) decorators();
        if (is("static") && token_is(peek(), "{")) {
            next();
            function_body();
            return;
        }
        bool erased = false;  // declare and abstract members
        while (token_.kind == Kind::name && one_of(text(), member_modifiers) && name_follows()) {
            if (is("declare") || is("abstract")) erased = true;
            if (one_of(text(), type_modifiers)) blank(token_.start, token_.end);
            next();
        }
        if (is("[")) {  // [key: K]: V — an index signature
            const State before = save();
            next();
            const bool signature = token_.kind == Kind::name && token_is(peek(), ":");
            restore(before);
            if (signature) {
                skip_balanced();
                if (is(":")) {
                    next();
                    type();
                }
                if (!eat(";")) eat(",");
                blank_statement(start, previous_end_);
                return;
            }
        }
        if ((is("get") || is("set") || is("async")) && name_follows()) next();
        eat("*");
        property_name();
        if (is("?") || is("!")) {
            blank(token_.start, token_.end);
            next();
        }
        if (is("(") || is("<")) {
            callable_rest(start, true);
            if (erased) blank_statement(start, previous_end_);
            return;
        }
        if (is(":")) annotation();
        if (eat("=")) assignment();
        if (erased) {
            eat(";");
            blank_statement(start, previous_end_);
            return;
        }
        semicolon();
    }

    // ---- statements -------------------------------------------------------

    void semicolon() {
        if (eat(";")) return;
        if (is("}") || at_end() || token_.line_before) {
            end_with_blank();
            return;
        }
        fail("`;` expected");
    }

    void parenthesized() {
        expect("(");
        expression();
        expect(")");
    }

    void declarations(bool no_in) {
        next();  // var, let, const
        do {
            binding();
            if (is("!")) {
                blank(token_.start, token_.end);
                next();
            }
            if (is(":")) annotation();
            if (eat("=")) assignment(no_in);
        } while (eat(","));
    }

    [[nodiscard]] bool declaration_ahead() {
        if (is("var") || is("const")) return true;
        if (!is("let")) return false;
        const Token ahead = peek();
        return ahead.kind == Kind::name || token_is(ahead, "[") || token_is(ahead, "{");
    }

    // A TypeScript word that starts a declaration when a name follows it on its line.
    [[nodiscard]] bool word_with_name(std::string_view word) {
        if (!is(word)) return false;
        const Token ahead = peek();
        return !ahead.line_before && (ahead.kind == Kind::string ||
                                      (ahead.kind == Kind::name && !one_of(text_of(ahead), operator_words)));
    }

    void statement() {
        const std::size_t start = token_.start;
        boundary_ = previous_end_;
        if (is("{")) {
            function_body();
            return;
        }
        if (eat(";")) return;
        if (is("@")) {
            decorators();
            return;
        }
        if (token_.kind != Kind::name) {
            expression();
            semicolon();
            return;
        }
        if (is("const") && token_is(peek(), "enum")) {
            next();
            enum_(start, false);
            return;
        }
        if (declaration_ahead()) {
            declarations(false);
            semicolon();
            return;
        }
        if (is("function") || (is("async") && token_is(peek(), "function") && !peek().line_before)) {
            function_declaration(start);
            return;
        }
        if (is("class")) {
            class_(start);
            return;
        }
        if (is("abstract") && token_is(peek(), "class") && !peek().line_before) {
            blank(token_.start, token_.end);
            next();
            class_(start);
            return;
        }
        if (is("if")) {
            next();
            parenthesized();
            statement();
            if (eat("else")) statement();
            return;
        }
        if (is("for")) {
            for_statement();
            return;
        }
        if (is("while") || is("with")) {
            next();
            parenthesized();
            statement();
            return;
        }
        if (is("do")) {
            next();
            statement();
            expect("while");
            parenthesized();
            eat(";");
            return;
        }
        if (is("return") || is("throw")) {
            next();
            if (!token_.line_before && !is(";") && !is("}") && !at_end()) expression();
            semicolon();
            return;
        }
        if (is("break") || is("continue")) {
            next();
            if (!token_.line_before && token_.kind == Kind::name) next();
            semicolon();
            return;
        }
        if (is("try")) {
            next();
            function_body();
            if (eat("catch")) {
                if (eat("(")) {
                    binding();
                    if (is(":")) annotation();
                    expect(")");
                }
                function_body();
            }
            if (eat("finally")) function_body();
            return;
        }
        if (is("switch")) {
            next();
            parenthesized();
            expect("{");
            while (!is("}") && !at_end()) {
                if (eat("case")) {
                    expression();
                    expect(":");
                } else if (eat("default")) {
                    expect(":");
                } else {
                    statement();
                }
            }
            expect("}");
            return;
        }
        if (is("debugger")) {
            next();
            semicolon();
            return;
        }
        if (is("import") && !token_is(peek(), "(") && !token_is(peek(), ".")) {
            import_declaration(start);
            return;
        }
        if (is("export")) {
            export_declaration(start);
            return;
        }
        if (word_with_name("type")) {
            type_alias(start);
            return;
        }
        if (word_with_name("interface")) {
            interface_(start);
            return;
        }
        if (word_with_name("declare")) {
            declare(start);
            return;
        }
        if (is("enum")) {
            enum_(start, false);
            return;
        }
        if (word_with_name("namespace") || word_with_name("module")) {
            namespace_(start, false);
            return;
        }
        if (token_is(peek(), ":")) {  // a label
            next();
            next();
            statement();
            return;
        }
        expression();
        semicolon();
    }

    void for_statement() {
        next();
        eat("await");
        expect("(");
        if (is(";")) {
        } else if (declaration_ahead()) {
            declarations(true);
        } else {
            expression(true);
        }
        if (eat("of")) {
            assignment();
        } else if (eat("in")) {
            expression();
        } else {
            expect(";");
            if (!is(";")) expression();
            expect(";");
            if (!is(")")) expression();
        }
        expect(")");
        statement();
    }

    void function_declaration(std::size_t start) {
        eat("async");
        expect("function");
        eat("*");
        if (token_.kind == Kind::name && !is("<")) next();
        callable_rest(start, true);
    }

    void type_alias(std::size_t start) {
        next();  // type
        expect_name();
        if (is("<")) type_parameters();
        expect("=");
        type();
        eat(";");
        blank_statement(start, previous_end_);
    }

    void interface_(std::size_t start) {
        next();  // interface
        expect_name();
        if (is("<")) type_parameters();
        if (eat("extends")) {
            type();
            while (eat(",")) type();
        }
        if (is("{"))
            skip_balanced();
        else
            fail("`{` expected");
        blank_statement(start, previous_end_);
    }

    void declare(std::size_t start) {
        next();  // declare
        if (is("global")) {
            next();
            skip_balanced();
        } else if (is("module") || is("namespace")) {
            namespace_(token_.start, true);
        } else if (is("enum") || (is("const") && token_is(peek(), "enum"))) {
            eat("const");
            enum_(token_.start, true);
        } else {
            statement();
        }
        eat(";");
        blank_statement(start, previous_end_);
    }

    void enum_(std::size_t start, bool declared) {
        if (!declared) problem(start, enum_advice);
        next();  // enum
        expect_name();
        if (is("{"))
            skip_balanced();
        else
            fail("`{` expected");
    }

    void namespace_(std::size_t start, bool declared) {
        next();  // namespace, module
        if (token_.kind == Kind::string)
            next();
        else
            entity_name();
        if (!declared) problem(start, namespace_advice);
        if (is("{"))
            skip_balanced();
        else if (!declared)
            fail("`{` expected");
    }

    void module_specifier() {
        if (token_.kind == Kind::string)
            next();
        else
            fail("a module name (a string) expected");
        if (!token_.line_before && (is("with") || is("assert"))) {
            next();
            object_literal();
        }
    }

    // `{ a, type B, c as d }`: a `type` specifier is blanked with its comma.
    void specifiers() {
        expect("{");
        while (!is("}") && !at_end()) {
            const std::size_t start = token_.start;
            bool type_only = false;
            if (is("type")) {
                const Token ahead = peek();
                type_only = (ahead.kind == Kind::name || ahead.kind == Kind::string) && !token_is(ahead, "as");
            }
            if (type_only) next();
            if (token_.kind == Kind::name || token_.kind == Kind::string)
                next();
            else
                fail("a name expected");
            if (eat("as")) {
                if (token_.kind == Kind::name || token_.kind == Kind::string)
                    next();
                else
                    fail("a name expected");
            }
            if (is(",")) {
                if (type_only) blank(start, token_.end);
                next();
            } else {
                if (type_only) blank(start, previous_end_);
                break;
            }
        }
        expect("}");
    }

    void import_declaration(std::size_t start) {
        next();  // import
        bool type_only = false;
        if (is("type")) {
            const Token ahead = peek();
            type_only = token_is(ahead, "{") || token_is(ahead, "*") ||
                        (ahead.kind == Kind::name && !token_is(ahead, "from") && !token_is(ahead, "="));
            if (type_only) next();
        }
        if (token_.kind == Kind::string) {
            module_specifier();
            semicolon();
            return;
        }
        if (token_.kind == Kind::name && token_is(peek(), "=")) {
            problem(start, import_equals_advice);
            next();
            next();
            expression();
            semicolon();
            return;
        }
        if (token_.kind == Kind::name && !is("from")) {
            next();
            if (!eat(",")) {
                expect("from");
                import_end(start, type_only);
                return;
            }
        } else if (token_.kind == Kind::name && is("from") && token_is(peek(), "from")) {
            next();  // import from from "…"
        }
        if (eat("*")) {
            expect("as");
            expect_name();
        } else if (is("{")) {
            specifiers();
        }
        expect("from");
        import_end(start, type_only);
    }

    void import_end(std::size_t start, bool type_only) {
        module_specifier();
        if (type_only) {
            eat(";");
            blank_statement(start, previous_end_);
        } else {
            semicolon();
        }
    }

    void export_declaration(std::size_t start) {
        next();  // export
        if (is("=")) {
            problem(start, export_equals_advice);
            next();
            expression();
            semicolon();
            return;
        }
        if (is("as")) {  // export as namespace X;
            next();
            next();
            expect_name();
            eat(";");
            blank_statement(start, previous_end_);
            return;
        }
        if (is("import")) {
            problem(start, import_equals_advice);
            next();
            expect_name();
            expect("=");
            expression();
            semicolon();
            return;
        }
        if (is("type") && (token_is(peek(), "{") || token_is(peek(), "*"))) {
            next();
            if (eat("*")) {
                if (eat("as")) expect_name();
            } else {
                specifiers();
            }
            if (eat("from")) module_specifier();
            eat(";");
            blank_statement(start, previous_end_);
            return;
        }
        if (word_with_name("type") || word_with_name("interface") || word_with_name("declare")) {
            statement();
            blank_statement(start, previous_end_);
            return;
        }
        if (is("default")) {
            next();
            if (word_with_name("interface")) {
                statement();
                blank_statement(start, previous_end_);
            } else if (is("function") || (is("async") && token_is(peek(), "function") && !peek().line_before)) {
                function_declaration(start);
            } else if (is("class") || (is("abstract") && token_is(peek(), "class"))) {
                statement();
            } else {
                assignment();
                semicolon();
            }
            return;
        }
        if (eat("*")) {
            if (eat("as")) {
                if (token_.kind == Kind::name || token_.kind == Kind::string)
                    next();
                else
                    fail("a name expected");
            }
            expect("from");
            module_specifier();
            semicolon();
            return;
        }
        if (is("{")) {
            specifiers();
            if (eat("from")) module_specifier();
            semicolon();
            return;
        }
        if (is("function") || (is("async") && token_is(peek(), "function"))) {
            function_declaration(start);
            return;
        }
        statement();
    }

    std::string_view source_;
    std::size_t position_ = 0;      // where the scanner reads next
    std::size_t previous_end_ = 0;  // where the token before the current one ended
    Token token_;
    bool stopped_ = false;
    bool in_extends_ = false;  // in the `extends` clause of a conditional type
    std::size_t boundary_ = 0;  // where the token before the current statement or member ended
    std::vector<std::pair<std::size_t, std::size_t>> blanks_;
    std::vector<std::pair<std::size_t, char>> writes_;
    std::vector<std::pair<std::size_t, std::string>> problems_;
    std::unordered_set<std::size_t> not_arrows_;  // where an arrow was tried and was not one
};

}  // namespace

std::expected<std::string, std::vector<TypeScriptProblem>> strip_types(std::string_view source) {
    return Stripper(source).run();
}

std::string to_text(const std::vector<TypeScriptProblem>& problems) {
    std::string out;
    for (const auto& problem : problems) out += std::format("{}:{}: {}\n", problem.line, problem.column, problem.text);
    if (!out.empty()) out.pop_back();
    return out;
}

std::expected<std::string, std::string> compile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot be read", mcpplibs::os::fs::utf8(file)));
    const std::string source{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return compile_source(source, mcpplibs::os::fs::utf8(file));
}

std::expected<std::string, std::string> compile_source(std::string_view source, std::string_view name) {
    auto js = strip_types(source);
    if (js) return std::move(*js);
    std::string out;
    for (const auto& problem : js.error())
        out += std::format("{}{}:{}:{}: {}", out.empty() ? "" : "\n", name, problem.line, problem.column, problem.text);
    return std::unexpected(std::move(out));
}

}  // namespace omui::lang::ts
