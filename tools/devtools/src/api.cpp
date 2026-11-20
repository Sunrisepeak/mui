module omui.devtools.api;

import std;
import mcpplibs.os;

namespace omui::devtools::api {
namespace {

// The unit without comments and preprocessor lines; string and character literals kept as they are written.
std::string stripped(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool line_start = true;
    for (std::size_t at = 0; at < text.size();) {
        const char here = text[at];
        const char next = at + 1 < text.size() ? text[at + 1] : '\0';
        if (line_start && (here == ' ' || here == '\t')) {
            out += here, ++at;
            continue;
        }
        if (line_start && here == '#') {  // a preprocessor line (a global module fragment's includes)
            while (at < text.size() && text[at] != '\n') ++at;
            continue;
        }
        line_start = false;
        if (here == '/' && next == '/') {
            while (at < text.size() && text[at] != '\n') ++at;
            continue;
        }
        if (here == '/' && next == '*') {
            const auto end = text.find("*/", at + 2);
            at = end == std::string_view::npos ? text.size() : end + 2;
            out += ' ';
            continue;
        }
        if (here == 'R' && next == '"') {  // a raw string: R"delimiter( … )delimiter"
            const auto open = text.find('(', at + 2);
            if (open != std::string_view::npos) {
                const std::string closing = ")" + std::string(text.substr(at + 2, open - at - 2)) + "\"";
                const auto end = text.find(closing, open);
                const std::size_t stop = end == std::string_view::npos ? text.size() : end + closing.size();
                out += "R\"…\"", at = stop;  // a raw string's text is a value, not the surface
                continue;
            }
        }
        if (here == '"' || here == '\'') {
            const std::size_t begin = at++;
            while (at < text.size() && text[at] != here) at += text[at] == '\\' ? 2 : 1;
            at = std::min(at + 1, text.size());
            out += text.substr(begin, at - begin);
            continue;
        }
        if (here == '\n') line_start = true;
        out += here, ++at;
    }
    return out;
}

std::string normal(std::string_view text) {
    std::string out;
    bool space = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = !out.empty();
            continue;
        }
        if (space && c != ';' && c != ',' && c != ')' && !(c == '>' && !out.empty() && out.back() == '>')) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

bool starts_with_word(std::string_view text, std::string_view word) {
    return text.starts_with(word) && (text.size() == word.size() || !(std::isalnum(static_cast<unsigned char>(text[word.size()])) || text[word.size()] == '_'));
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

// Whether a declaration assigns at its own level: `x = …`, not a parameter's default and not `==`, `<=`, `operator=`.
bool assigns(std::string_view text) {
    int depth = 0;
    for (std::size_t at = 0; at < text.size(); ++at) {
        const char c = text[at];
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        else if (c == '=' && depth == 0) {
            const char before = at ? text[at - 1] : ' ', after = at + 1 < text.size() ? text[at + 1] : ' ';
            if (after == '=' || before == '=' || before == '!' || before == '<' || before == '>') continue;
            if (text.substr(0, at).ends_with("operator")) continue;
            return true;
        }
    }
    return false;
}

// After `template<…>`, if the declaration begins with it.
std::string_view after_template(std::string_view text) {
    text = trimmed(text);
    if (!starts_with_word(text, "template")) return text;
    std::size_t at = text.find('<');
    if (at == std::string_view::npos) return text;
    int depth = 0;
    for (; at < text.size(); ++at) {
        if (text[at] == '<') ++depth;
        else if (text[at] == '>' && --depth == 0) return trimmed(text.substr(at + 1));
    }
    return text;
}

class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {}

    // Past the brace that closes the one at `at` (balanced, literals skipped).
    std::size_t past_block(std::size_t at) const {
        int depth = 0;
        for (; at < text_.size(); ++at) {
            const char c = text_[at];
            if (c == '"' || c == '\'') {
                for (++at; at < text_.size() && text_[at] != c; at += text_[at] == '\\' ? 2 : 1) {
                }
                continue;
            }
            if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) return at + 1;
        }
        return text_.size();
    }

    // The declarations of a scope from `at` to its closing brace (or the end); `members`: a class's, where access
    // matters (`open`: what is public at the start).
    void scope(std::size_t& at, int indent, bool members, bool open, std::vector<std::string>& out) {
        bool shown = open;
        std::string statement;
        int parentheses = 0;
        const auto emit = [&](std::string line) {
            const std::string text = normal(line);
            if (text.empty() || text == ";" || starts_with_word(text, "static_assert")) return;
            if (shown) out.push_back(std::string(static_cast<std::size_t>(indent) * 2, ' ') + text);
        };
        while (at < text_.size()) {
            const char c = text_[at];
            if (c == '"' || c == '\'') {
                const std::size_t begin = at;
                for (++at; at < text_.size() && text_[at] != c; at += text_[at] == '\\' ? 2 : 1) {
                }
                ++at;
                statement += text_.substr(begin, at - begin);
                continue;
            }
            if (c == '(') ++parentheses;
            if (c == ')') --parentheses;
            if (c == '}' && parentheses <= 0) {  // the scope ends
                ++at;
                return;
            }
            if (c == ':' && members && parentheses == 0) {
                const bool lone = (at + 1 >= text_.size() || text_[at + 1] != ':') && (at == 0 || text_[at - 1] != ':');
                const std::string_view label = trimmed(statement);
                if (lone && (label == "public" || label == "private" || label == "protected")) {
                    shown = label == "public";
                    statement.clear();
                    ++at;
                    continue;
                }
            }
            if (c == ';' && parentheses <= 0) {
                emit(statement + ";");
                statement.clear(), ++at, parentheses = 0;
                continue;
            }
            if (c == '{') {
                if (parentheses > 0) {  // a lambda or a braced value in an argument: its body elided
                    statement += "{…}";
                    at = past_block(at);
                    continue;
                }
                const std::string_view head = trimmed(statement);
                const std::string_view kind = after_template(head);
                const bool assigned = assigns(head);
                if (starts_with_word(head, "namespace")) {
                    emit(statement + "{");
                    ++at;
                    scope(at, indent + 1, false, true, out);
                    emit("}");
                    statement.clear();
                    continue;
                }
                if (!assigned && (starts_with_word(kind, "enum"))) {
                    const std::size_t end = past_block(at);
                    statement += text_.substr(at, end - at);
                    at = end;
                    continue;  // to its `;`
                }
                if (!assigned && (starts_with_word(kind, "class") || starts_with_word(kind, "struct") || starts_with_word(kind, "union"))) {
                    const bool was = shown;
                    if (shown) emit(statement + "{");
                    ++at;
                    if (shown) scope(at, indent + 1, true, !starts_with_word(kind, "class"), out);
                    else at = past_block(at - 1);
                    shown = was;
                    statement = "}";  // the declarators after it, to its `;`
                    continue;
                }
                if (!assigned && head.find(')') != std::string_view::npos) {  // a function's body
                    emit(statement + ";");
                    statement.clear();
                    at = past_block(at);
                    continue;
                }
                statement += "{…}";  // an initialiser
                at = past_block(at);
                continue;
            }
            statement += c;
            ++at;
        }
    }

    std::string_view text() const noexcept { return text_; }

private:
    std::string_view text_;
};

}  // namespace

std::vector<std::string> exported(std::string_view unit) {
    const std::string text = stripped(unit);
    Reader reader(text);
    std::vector<std::string> out;
    std::size_t at = 0;
    std::string statement;
    const auto declaration = [&](std::size_t& from) {
        // One exported declaration from `from`: a scope's worth read as a scope of one.
        std::vector<std::string> one;
        std::string piece;
        int parentheses = 0;
        std::size_t end = from;
        for (; end < text.size(); ++end) {
            const char c = text[end];
            if (c == '(') ++parentheses;
            if (c == ')') --parentheses;
            if (parentheses == 0 && c == ';') {
                ++end;
                break;
            }
            if (parentheses == 0 && c == '{') {
                const std::string_view head = trimmed(std::string_view(text).substr(from, end - from));
                const std::string_view kind = after_template(head);
                if (assigns(head) || starts_with_word(kind, "class") || starts_with_word(kind, "struct") || starts_with_word(kind, "union") ||
                    starts_with_word(kind, "enum")) {
                    end = reader.past_block(end) - 1;  // to its `;`
                    continue;
                }
                end = reader.past_block(end);  // a function and its body
                break;
            }
        }
        // Read that span as a scope's members (its closing brace added).
        const std::string span = std::string(std::string_view(text).substr(from, end - from)) + "}";
        Reader part(span);
        std::size_t inside = 0;
        part.scope(inside, 0, false, true, one);
        out.insert(out.end(), one.begin(), one.end());
        from = end;
    };
    while (at < text.size()) {
        const char c = text[at];
        if (c == ';' || c == '{') {
            const std::string head = normal(statement);
            if (c == ';' && (starts_with_word(head, "export") && head.find(" import ") != std::string::npos)) out.push_back(head + ";");
            if (c == '{') at = reader.past_block(at);  // not exported (a namespace of its own, a fragment)
            else ++at;
            statement.clear();
            continue;
        }
        statement += c;
        ++at;
        const std::string_view head = trimmed(statement);
        if (head == "export") {
            // What follows: `module`, `import`, `namespace N {`, `{`, or one declaration.
            std::size_t next = at;
            while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next]))) ++next;
            const std::string_view rest = std::string_view(text).substr(next);
            if (starts_with_word(rest, "module") || starts_with_word(rest, "import")) continue;  // read to its `;`
            if (starts_with_word(rest, "namespace")) {
                const auto open = text.find('{', next);
                if (open == std::string::npos) break;
                out.push_back(normal(text.substr(next, open - next)) + " {");
                std::size_t inside = open + 1;
                reader.scope(inside, 1, false, true, out);
                out.push_back("}");
                at = inside;
                statement.clear();
                continue;
            }
            if (!rest.empty() && rest.front() == '{') {
                std::size_t inside = next + 1;
                reader.scope(inside, 0, false, true, out);
                at = inside;
                statement.clear();
                continue;
            }
            declaration(next);
            at = next;
            statement.clear();
        }
    }
    return out;
}

std::vector<std::string> interface_globs(std::string_view manifest) {
    std::vector<std::string> globs;
    std::size_t at = 0;
    while ((at = manifest.find("sources", at)) != std::string_view::npos) {
        const auto open = manifest.find('[', at), line_end = manifest.find('\n', at);
        if (open == std::string_view::npos || open > line_end) {
            ++at;
            continue;
        }
        const auto close = manifest.find(']', open);
        for (std::size_t quote = manifest.find('"', open); quote != std::string_view::npos && quote < close; quote = manifest.find('"', quote + 1)) {
            const auto end = manifest.find('"', quote + 1);
            const std::string glob(manifest.substr(quote + 1, end - quote - 1));
            if (glob.ends_with(".cppm") && !std::ranges::contains(globs, glob)) globs.push_back(glob);
            quote = end;
        }
        at = close;
    }
    return globs;
}

namespace {

// The files under `member` a glob of the manifest's kind names (`*` within one directory).
std::vector<std::filesystem::path> matched(const std::filesystem::path& member, std::string_view glob) {
    const auto slash = glob.rfind('/');
    const std::filesystem::path directory = member / std::string(slash == std::string_view::npos ? "" : glob.substr(0, slash));
    const std::string_view pattern = slash == std::string_view::npos ? glob : glob.substr(slash + 1);
    std::vector<std::filesystem::path> out;
    if (mcpplibs::os::fs::generic_utf8(directory).contains('*')) {  // a directory wildcard: src/*/x.cppm
        const std::string text = mcpplibs::os::fs::generic_utf8(directory);
        const auto star = text.find('*');
        const std::filesystem::path base = mcpplibs::os::fs::from_utf8(text.substr(0, star));
        const std::string tail = text.substr(star + 1);
        std::error_code failed;
        for (const auto& each : std::filesystem::directory_iterator(base, failed))
            if (each.is_directory())
                for (auto& file : matched(mcpplibs::os::fs::from_utf8(mcpplibs::os::fs::generic_utf8(each.path()) + tail), pattern)) out.push_back(std::move(file));
        return out;
    }
    const auto star = pattern.find('*');
    std::error_code failed;
    for (const auto& each : std::filesystem::directory_iterator(directory, failed)) {
        const std::string name = mcpplibs::os::fs::utf8(each.path().filename());
        const bool fits = star == std::string_view::npos ? name == pattern
                                                         : name.starts_with(pattern.substr(0, star)) && name.ends_with(pattern.substr(star + 1)) && name.size() >= pattern.size() - 1;
        if (fits && each.is_regular_file()) out.push_back(each.path());
    }
    return out;
}

}  // namespace

std::string surface(const std::filesystem::path& member, std::span<const std::string> sources) {
    std::map<std::string, std::vector<std::string>> units;
    for (const std::string& glob : sources)
        for (const auto& file : matched(member, glob)) {
            std::ifstream in(file, std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(in), {}};
            const std::string plain = stripped(text);
            const auto declared = plain.find("export module ");
            if (declared == std::string::npos) continue;
            const auto end = plain.find(';', declared);
            const std::string name = normal(plain.substr(declared + 14, end - declared - 14));
            auto lines = exported(text);
            if (!lines.empty()) units[name] = std::move(lines);
        }
    std::string out;
    for (const auto& [name, lines] : units) {
        out += std::format("== {}\n", name);
        for (const auto& line : lines) out += line + "\n";
    }
    return out;
}

}  // namespace omui::devtools::api
