// Structured design operations as edits of a script's source (omui.lang.ts:edit).
//
// A small lexer is enough: strings, template literals, comments and regular
// expressions are skipped as wholes, so the brackets left are the code's and
// match; the builder call, its object of properties and its list of children
// are found by them. Edits splice the text: what is not touched stays byte for
// byte, and what is written follows its neighbours' layout (one line, or one a
// line at their indentation).
module omui.lang.ts;

import std;

namespace omui::lang::ts {

namespace {

enum class Kind : std::uint8_t { name, punct, literal };

struct Lexeme {
    Kind kind = Kind::punct;
    std::size_t start = 0, end = 0;
};

bool name_start(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80; }
bool name_part(unsigned char c) { return name_start(c) || (c >= '0' && c <= '9'); }

// Words after which a `/` begins a regular expression, not a division.
constexpr std::array regex_after{std::string_view{"return"}, std::string_view{"typeof"}, std::string_view{"case"},
                                 std::string_view{"do"},     std::string_view{"else"},   std::string_view{"in"},
                                 std::string_view{"of"},     std::string_view{"new"},    std::string_view{"delete"},
                                 std::string_view{"void"},   std::string_view{"throw"},  std::string_view{"yield"},
                                 std::string_view{"await"}};

class Lexer {
public:
    explicit Lexer(std::string_view text) : text_(text) {}

    std::expected<std::vector<Lexeme>, std::string> run() {
        while (position_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[position_]);
            const std::size_t start = position_;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
            } else if (text_.substr(position_).starts_with("//")) {
                position_ = std::min(text_.find('\n', position_), text_.size());
            } else if (text_.substr(position_).starts_with("/*")) {
                const auto close = text_.find("*/", position_ + 2);
                if (close == std::string_view::npos) return problem(start, "a comment is not closed");
                position_ = close + 2;
            } else if (c == '"' || c == '\'') {
                if (!quoted(static_cast<char>(c))) return problem(start, "a string is not closed");
                out_.push_back({Kind::literal, start, position_});
            } else if (c == '`') {
                ++position_;
                if (!template_rest()) return problem(start, "a template is not closed");
                out_.push_back({Kind::literal, start, position_});
            } else if (c == '}' && !templates_.empty() && templates_.back() == depth_ - 1) {
                // The end of a template's `${ … }`: the template goes on.
                templates_.pop_back();
                --depth_;
                ++position_;
                if (!template_rest()) return problem(start, "a template is not closed");
                out_.push_back({Kind::literal, start, position_});
            } else if (c == '/' && regex_here()) {
                if (!regex()) return problem(start, "a regular expression is not closed");
                out_.push_back({Kind::literal, start, position_});
            } else if (name_start(c)) {
                while (position_ < text_.size() && name_part(static_cast<unsigned char>(text_[position_]))) ++position_;
                out_.push_back({Kind::name, start, position_});
            } else if (c >= '0' && c <= '9') {
                while (position_ < text_.size() && (name_part(static_cast<unsigned char>(text_[position_])) || text_[position_] == '.')) ++position_;
                out_.push_back({Kind::literal, start, position_});
            } else {
                if (c == '{' || c == '(' || c == '[') ++depth_;
                if (c == '}' || c == ')' || c == ']') --depth_;
                ++position_;
                out_.push_back({Kind::punct, start, position_});
            }
        }
        return std::move(out_);
    }

private:
    std::unexpected<std::string> problem(std::size_t at, std::string_view what) const {
        const auto line = std::ranges::count(text_.substr(0, at), '\n') + 1;
        return std::unexpected(std::format("line {}: {}", line, what));
    }
    bool quoted(char quote) {
        for (++position_; position_ < text_.size(); ++position_) {
            if (text_[position_] == '\\') ++position_;
            else if (text_[position_] == quote) return ++position_, true;
            else if (text_[position_] == '\n') return false;
        }
        return false;
    }
    // From inside a template to its end, or to a `${` (its expression is lexed as code).
    bool template_rest() {
        for (; position_ < text_.size(); ++position_) {
            if (text_[position_] == '\\') ++position_;
            else if (text_[position_] == '`') return ++position_, true;
            else if (text_.substr(position_).starts_with("${")) {
                position_ += 2;
                templates_.push_back(depth_++);  // its `}` comes back to this depth (`${` counts as a bracket)
                return true;
            }
        }
        return false;
    }
    bool regex_here() const {
        if (out_.empty()) return true;
        const Lexeme& last = out_.back();
        const std::string_view word = text_.substr(last.start, last.end - last.start);
        if (last.kind == Kind::name) return std::ranges::find(regex_after, word) != regex_after.end();
        if (last.kind == Kind::literal) return false;
        return word != ")" && word != "]" && word != "}";
    }
    bool regex() {
        bool in_class = false;
        for (++position_; position_ < text_.size(); ++position_) {
            const char c = text_[position_];
            if (c == '\\') ++position_;
            else if (c == '\n') return false;
            else if (c == '[') in_class = true;
            else if (c == ']') in_class = false;
            else if (c == '/' && !in_class) {
                ++position_;
                while (position_ < text_.size() && name_part(static_cast<unsigned char>(text_[position_]))) ++position_;  // its flags
                return true;
            }
        }
        return false;
    }

    std::string_view text_;
    std::size_t position_ = 0;
    int depth_ = 0;
    std::vector<int> templates_;  // the bracket depth each open `${` returns to
    std::vector<Lexeme> out_;
};

// The source lexed, each bracket's partner found.
struct Code {
    std::string_view text;
    std::vector<Lexeme> lexemes;
    std::vector<std::size_t> partner;  // a bracket's matching one; others: npos

    [[nodiscard]] std::string_view word(std::size_t index) const {
        return index < lexemes.size() ? text.substr(lexemes[index].start, lexemes[index].end - lexemes[index].start) : std::string_view();
    }
    [[nodiscard]] bool is(std::size_t index, std::string_view what) const { return word(index) == what; }
};

std::expected<Code, std::string> code_of(std::string_view text) {
    auto lexemes = Lexer(text).run();
    if (!lexemes) return std::unexpected(lexemes.error());
    Code code{text, std::move(*lexemes), {}};
    code.partner.assign(code.lexemes.size(), std::string_view::npos);
    std::vector<std::size_t> open;
    for (std::size_t index = 0; index < code.lexemes.size(); ++index) {
        if (code.lexemes[index].kind != Kind::punct) continue;
        const std::string_view bracket = code.word(index);
        if (bracket == "(" || bracket == "[" || bracket == "{") {
            open.push_back(index);
        } else if (bracket == ")" || bracket == "]" || bracket == "}") {
            if (open.empty()) return std::unexpected(std::format("a `{}` closes nothing", bracket));
            const std::string_view opening = code.word(open.back());
            if ((bracket == ")") != (opening == "(") || (bracket == "]") != (opening == "[")) return std::unexpected(std::format("a `{}` closes a `{}`", bracket, opening));
            code.partner[open.back()] = index;
            code.partner[index] = open.back();
            open.pop_back();
        }
    }
    if (!open.empty()) return std::unexpected(std::format("a `{}` is not closed", code.word(open.back())));
    return code;
}

std::expected<std::size_t, std::string> offset_of(std::string_view text, std::size_t line, std::size_t column) {
    std::size_t offset = 0;
    for (std::size_t current = 1; current < line; ++current) {
        offset = text.find('\n', offset);
        if (offset == std::string_view::npos) return std::unexpected(std::format("the text has no line {}", line));
        ++offset;
    }
    const std::size_t end = std::min(text.find('\n', offset), text.size());
    if (column == 0 || offset + column - 1 > end) return std::unexpected(std::format("line {} has no column {}", line, column));
    return offset + column - 1;
}

// The arguments (or members, or elements) between a bracket and its partner:
// each one's first and last lexeme, split at the commas of that level.
std::vector<std::pair<std::size_t, std::size_t>> items(const Code& code, std::size_t open) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    const std::size_t close = code.partner[open];
    std::size_t first = open + 1;
    for (std::size_t index = open + 1; index < close; ++index) {
        if (code.partner[index] != std::string_view::npos && code.partner[index] > index) {
            index = code.partner[index];
            continue;
        }
        if (code.is(index, ",")) {
            if (index > first) out.emplace_back(first, index - 1);
            first = index + 1;
        }
    }
    if (close > first) out.emplace_back(first, close - 1);
    return out;
}

struct Call {
    BuilderCall found;
    std::size_t open = 0, callee = 0;  // lexemes: its `(`, its callee's first
    std::optional<std::size_t> properties, children;  // lexemes: the `{` and the `[`
};

std::expected<Call, std::string> call_at(const Code& code, std::size_t offset, std::size_t line, std::size_t column) {
    const auto at = std::ranges::find_if(code.lexemes, [&](const Lexeme& lexeme) { return lexeme.end > offset; });
    auto none = [&] {
        return std::unexpected(std::format("no builder call at line {}, column {} (design.locate says where a node was built)", line, column));
    };
    if (at == code.lexemes.end()) return none();
    std::size_t index = static_cast<std::size_t>(at - code.lexemes.begin());
    // At its callee (or a name of it, `ui.text`) or at its `(`.
    while (code.lexemes[index].kind == Kind::name && code.is(index + 1, ".") && index + 2 < code.lexemes.size() &&
           code.lexemes[index + 2].kind == Kind::name)
        index += 2;
    std::size_t open = index;
    if (code.lexemes[index].kind == Kind::name) open = index + 1;
    if (!code.is(open, "(") || open == 0 || code.lexemes[open - 1].kind != Kind::name) return none();
    std::size_t callee = open - 1;
    while (callee >= 2 && code.is(callee - 1, ".") && code.lexemes[callee - 2].kind == Kind::name) callee -= 2;
    Call call;
    call.open = open;
    call.callee = callee;
    const std::size_t close = code.partner[open];
    call.found.start = code.lexemes[callee].start;
    call.found.end = code.lexemes[close].end;
    call.found.open = code.lexemes[open].start;
    call.found.callee = std::string(code.text.substr(call.found.start, code.lexemes[open].start - call.found.start));
    const auto arguments = items(code, open);
    for (std::size_t argument = 0; argument < arguments.size(); ++argument) {
        const auto [first, last] = arguments[argument];
        const bool whole = code.partner[first] == last;
        if (argument == 0 && whole && code.is(first, "{")) call.properties = first;
        else if (whole && code.is(first, "[") && !call.children) call.children = first;
    }
    if (call.properties) call.found.properties = std::pair{code.lexemes[*call.properties].start, code.lexemes[code.partner[*call.properties]].end};
    if (call.children) call.found.children = std::pair{code.lexemes[*call.children].start, code.lexemes[code.partner[*call.children]].end};
    return call;
}

struct Found {
    Code code;
    Call call;
};

std::expected<Found, std::string> find(std::string_view source, std::size_t line, std::size_t column) {
    auto code = code_of(source);
    if (!code) return std::unexpected(code.error());
    auto offset = offset_of(source, line, column);
    if (!offset) return std::unexpected(offset.error());
    auto call = call_at(*code, *offset, line, column);
    if (!call) return std::unexpected(call.error());
    return Found{std::move(*code), std::move(*call)};
}

// The whitespace a line begins with, for the line holding `offset`.
std::string indentation_at(std::string_view text, std::size_t offset) {
    const std::size_t start = text.rfind('\n', offset == 0 ? 0 : offset - 1);
    const std::size_t begin = start == std::string_view::npos ? 0 : start + 1;
    std::size_t end = begin;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) ++end;
    return std::string(text.substr(begin, end - begin));
}

std::string spliced(std::string_view text, std::size_t from, std::size_t to, std::string_view with) {
    return std::format("{}{}{}", text.substr(0, from), with, text.substr(to));
}

bool is_name(std::string_view word) {
    return !word.empty() && name_start(static_cast<unsigned char>(word.front())) &&
           std::ranges::all_of(word, [](char c) { return name_part(static_cast<unsigned char>(c)); });
}
std::string key_of(std::string_view name) { return is_name(name) ? std::string(name) : std::format("\"{}\"", name); }

// A member of an object `{ … }` named `name`: its first and last lexeme.
std::optional<std::pair<std::size_t, std::size_t>> member(const Code& code, std::size_t open, std::string_view name) {
    for (const auto& [first, last] : items(code, open)) {
        std::string_view key = code.word(first);
        if (code.lexemes[first].kind == Kind::literal && key.size() >= 2 && (key.front() == '"' || key.front() == '\'')) key = key.substr(1, key.size() - 2);
        if (key == name && (first == last || code.is(first + 1, ":"))) return std::pair{first, last};
    }
    return std::nullopt;
}

// `addition` written into a bracket's items after the last (or as the only
// one), as they are laid out: on one line, or one a line at their indentation.
std::string appended(const Code& code, std::size_t open, std::string_view addition) {
    const std::string_view text = code.text;
    const std::size_t close = code.partner[open];
    const auto list = items(code, open);
    const std::size_t inside_from = code.lexemes[open].end, inside_to = code.lexemes[close].start;
    const bool spaced = code.is(open, "{");
    if (list.empty()) return spliced(text, inside_from, inside_to, spaced ? std::format(" {} ", addition) : std::string(addition));
    const std::size_t last_end = code.lexemes[list.back().second].end;
    const bool trailing_comma = code.is(list.back().second + 1, ",");
    const bool lines = text.substr(inside_from, inside_to - inside_from).contains('\n');
    if (!lines) return spliced(text, last_end, last_end, std::format(", {}", addition));
    const std::string indent = indentation_at(text, code.lexemes[list.back().first].start);
    if (trailing_comma) {
        const std::size_t comma_end = code.lexemes[list.back().second + 1].end;
        return spliced(text, comma_end, comma_end, std::format("\n{}{},", indent, addition));
    }
    return spliced(text, last_end, last_end, std::format(",\n{}{}", indent, addition));
}

}  // namespace

std::expected<BuilderCall, std::string> builder_call_at(std::string_view source, std::size_t line, std::size_t column) {
    auto found = find(source, line, column);
    if (!found) return std::unexpected(found.error());
    return found->call.found;
}

std::expected<std::string, std::string> set_property(std::string_view source, std::size_t line, std::size_t column, std::string_view name,
                                                     std::string_view value) {
    auto found = find(source, line, column);
    if (!found) return std::unexpected(found.error());
    const Code& code = found->code;
    const Call& call = found->call;
    if (!call.properties) {
        // Its object of properties written: first, before its children.
        const std::string object = std::format("{{ {}: {} }}", key_of(name), value);
        const std::size_t after_open = code.lexemes[call.open].end;
        return spliced(source, after_open, after_open, code.is(call.open + 1, ")") ? object : object + ", ");
    }
    if (const auto at = member(code, *call.properties, name)) {
        const auto [first, last] = *at;
        if (first == last) return spliced(source, code.lexemes[first].start, code.lexemes[last].end, std::format("{}: {}", key_of(name), value));  // `{ title }`
        return spliced(source, code.lexemes[first + 2].start, code.lexemes[last].end, value);
    }
    return appended(code, *call.properties, std::format("{}: {}", key_of(name), value));
}

std::expected<std::string, std::string> remove_property(std::string_view source, std::size_t line, std::size_t column, std::string_view name) {
    auto found = find(source, line, column);
    if (!found) return std::unexpected(found.error());
    const Code& code = found->code;
    const Call& call = found->call;
    const auto at = call.properties ? member(code, *call.properties, name) : std::nullopt;
    if (!at) return std::unexpected(std::format("{} has no property `{}` written", call.found.callee, name));
    const auto list = items(code, *call.properties);
    const auto [first, last] = *at;
    std::size_t from = code.lexemes[first].start, to = code.lexemes[last].end;
    if (code.is(last + 1, ",")) {
        // Up to the next member (or the closing bracket's line).
        to = code.lexemes[last + 1].end;
        const std::size_t next = code.lexemes[last + 2].start;
        if (list.back().first != first || !code.is(last + 2, "}")) to = next;
        const std::size_t line_start = source.rfind('\n', from) == std::string_view::npos ? 0 : source.rfind('\n', from) + 1;
        const bool own_line = indentation_at(source, from).size() == from - line_start;
        if (own_line && source.substr(code.lexemes[last + 1].end, next - code.lexemes[last + 1].end).contains('\n')) {
            from = source.rfind('\n', from) + 1;
            to = source.rfind('\n', next) + 1;
        }
    } else if (list.size() > 1) {
        from = code.lexemes[first - 1].start;  // the comma before it
    } else {
        from = code.lexemes[*call.properties].end;
        to = code.lexemes[code.partner[*call.properties]].start;
    }
    return spliced(source, from, to, "");
}

std::expected<std::string, std::string> insert_child(std::string_view source, std::size_t line, std::size_t column, std::string_view element,
                                                     std::optional<std::size_t> index) {
    auto found = find(source, line, column);
    if (!found) return std::unexpected(found.error());
    const Code& code = found->code;
    const Call& call = found->call;
    if (!call.children) {
        // A list of children written: after its properties.
        const std::size_t close = code.partner[call.open];
        if (code.is(call.open + 1, ")")) return spliced(source, code.lexemes[close].start, code.lexemes[close].start, std::format("[{}]", element));
        const std::size_t last = code.lexemes[close - 1].end;
        return spliced(source, last, last, std::format(", [{}]", element));
    }
    const auto list = items(code, *call.children);
    if (!index || *index >= list.size()) return appended(code, *call.children, element);
    // Before the one at `index`, laid out as it is.
    const std::size_t before = code.lexemes[list[*index].first].start;
    const std::size_t open_end = code.lexemes[*call.children].end, close_start = code.lexemes[code.partner[*call.children]].start;
    const bool lines = source.substr(open_end, close_start - open_end).contains('\n');
    return spliced(source, before, before, lines ? std::format("{},\n{}", element, indentation_at(source, before)) : std::format("{}, ", element));
}

std::expected<std::string, std::string> remove_call(std::string_view source, std::size_t line, std::size_t column) {
    auto found = find(source, line, column);
    if (!found) return std::unexpected(found.error());
    const Code& code = found->code;
    const Call& call = found->call;
    const std::size_t first = call.callee, last = code.partner[call.open];
    const bool after_list = first > 0 && (code.is(first - 1, "[") || code.is(first - 1, ","));
    const bool before_list = code.is(last + 1, "]") || code.is(last + 1, ",");
    if (!after_list || !before_list)
        return std::unexpected(std::format("the {} call at line {} is not in a list of children: change the code around it with design.edit", call.found.callee, line));
    std::size_t from = call.found.start, to = call.found.end;
    if (code.is(last + 1, ",")) {
        to = code.lexemes[last + 1].end;
        const std::size_t next = last + 2 < code.lexemes.size() ? code.lexemes[last + 2].start : source.size();
        const bool own_line = source.substr(to, next - to).contains('\n') && indentation_at(source, from).size() == from - (source.rfind('\n', from) + 1);
        if (own_line) {
            from = source.rfind('\n', from) + 1;
            to = source.rfind('\n', next) + 1;
        } else {
            to = next;
        }
    } else if (code.is(first - 1, ",")) {
        from = code.lexemes[first - 1].start;  // the last one: the comma before it goes
    } else {
        from = code.lexemes[first - 1].end;  // the only one
        to = code.lexemes[last + 1].start;
    }
    return spliced(source, from, to, "");
}

std::expected<CallIndex, std::string> CallIndex::of(std::string_view source) {
    auto code = code_of(source);
    if (!code) return std::unexpected(code.error());
    CallIndex index;
    index.source_ = std::string(source);
    index.line_starts_.push_back(0);
    for (std::size_t at = 0; at < source.size(); ++at)
        if (source[at] == '\n') index.line_starts_.push_back(at + 1);
    for (std::size_t open = 1; open < code->lexemes.size(); ++open) {
        if (!code->is(open, "(") || code->lexemes[open - 1].kind != Kind::name) continue;
        std::size_t callee = open - 1;
        while (callee >= 2 && code->is(callee - 1, ".") && code->lexemes[callee - 2].kind == Kind::name) callee -= 2;
        index.calls_.push_back({code->lexemes[callee].start, code->lexemes[code->partner[open]].end, std::string(code->word(open - 1))});
    }
    return index;
}

std::optional<std::pair<std::size_t, std::size_t>> CallIndex::enclosing(std::size_t line, std::size_t column, std::string_view callee,
                                                                        std::size_t skip) const {
    if (line == 0 || line > line_starts_.size()) return std::nullopt;
    const std::size_t offset = line_starts_[line - 1] + column - 1;
    // Outward: the calls around it, the latest start first.
    for (auto call = calls_.rbegin(); call != calls_.rend(); ++call) {
        if (call->start > offset || call->end <= offset || call->name != callee) continue;
        if (skip-- > 0) continue;
        const auto row = std::ranges::upper_bound(line_starts_, call->start) - line_starts_.begin();
        return std::pair{static_cast<std::size_t>(row), call->start - line_starts_[static_cast<std::size_t>(row) - 1] + 1};
    }
    return std::nullopt;
}

}  // namespace omui::lang::ts
