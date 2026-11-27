// mlibs.json: parsing (nlohmann.json), nodes with their pointers, and the
// places of pointers in the text.
module mlibs.json;

import std;
import nlohmann.json;

namespace mlibs::json {

namespace {

// The module's own scanner (RFC 8259): it follows one pointer down and skips
// everything else (locate, Scan), and checks the grammar as it goes — where it
// stops, `failure` says why and `i` is the place.
struct Seek {
    std::string_view text;
    std::size_t      i = 0;
    std::string_view failure;  // empty while it has not stopped

    // A text's first value begins after its byte order mark, as nlohmann's lexer skips it.
    static Seek of(std::string_view text) { return {text, text.starts_with("\xEF\xBB\xBF") ? std::size_t{3} : std::size_t{0}}; }

    bool fail(std::string_view why) {
        if (failure.empty()) failure = why;
        return false;
    }
    static bool digit(char c) { return c >= '0' && c <= '9'; }

    void space() {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
    }

    static void utf8(std::string& out, std::uint32_t cp) {
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

    std::optional<std::uint32_t> hex4() {
        if (i + 4 > text.size()) return std::nullopt;
        std::uint32_t v = 0;
        if (std::from_chars(text.data() + i, text.data() + i + 4, v, 16).ptr != text.data() + i + 4) return std::nullopt;
        i += 4;
        return v;
    }

    // A string at i (at its quote), decoded.
    std::optional<std::string> string() {
        if (i >= text.size() || text[i] != '"') {
            fail("a string begins with `\"`");
            return std::nullopt;
        }
        ++i;
        std::string out;
        while (true) {
            if (i >= text.size()) {
                fail("a string ends with `\"`");
                return std::nullopt;
            }
            const char here = text[i];
            if (here == '"') break;
            if (static_cast<unsigned char>(here) < 0x20) {
                fail("a control character in a string is escaped (\\n, \\t, \\u0001)");
                return std::nullopt;
            }
            if (here != '\\') {
                out += here;
                ++i;
                continue;
            }
            if (++i >= text.size()) {
                fail("a string ends with `\"`");
                return std::nullopt;
            }
            const char escaped = text[i++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/': out += escaped; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto code = hex4();
                    if (!code) {
                        fail("`\\u` is followed by four hexadecimal digits");
                        return std::nullopt;
                    }
                    if (*code >= 0xDC00 && *code < 0xE000) {
                        fail("a low surrogate (\\uDC00 to \\uDFFF) follows a high one");
                        return std::nullopt;
                    }
                    if (*code >= 0xD800 && *code < 0xDC00) {
                        std::optional<std::uint32_t> low;
                        if (text.substr(i, 2) == "\\u") {
                            i += 2;
                            low = hex4();
                        }
                        if (!low || *low < 0xDC00 || *low >= 0xE000) {
                            fail("a high surrogate (\\uD800 to \\uDBFF) is followed by a low one (\\uDC00 to \\uDFFF)");
                            return std::nullopt;
                        }
                        *code = 0x10000 + ((*code - 0xD800) << 10) + (*low - 0xDC00);
                    }
                    utf8(out, *code);
                    break;
                }
                default:
                    --i;
                    fail("an escape is one of \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\uXXXX");
                    return std::nullopt;
            }
        }
        ++i;
        return out;
    }

    // A number at i: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
    bool number() {
        if (i < text.size() && text[i] == '-') ++i;
        if (i >= text.size() || !digit(text[i])) return fail("a number has a digit after its sign");
        if (text[i] == '0') ++i;
        else
            while (i < text.size() && digit(text[i])) ++i;
        if (i < text.size() && text[i] == '.') {
            ++i;
            if (i >= text.size() || !digit(text[i])) return fail("a number's fraction has a digit after its point");
            while (i < text.size() && digit(text[i])) ++i;
        }
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
            ++i;
            if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
            if (i >= text.size() || !digit(text[i])) return fail("a number's exponent has a digit");
            while (i < text.size() && digit(text[i])) ++i;
        }
        return true;
    }

    bool word(std::string_view expected) {
        if (text.substr(i, expected.size()) != expected) return fail("a value is an object, an array, a string, a number, true, false or null");
        i += expected.size();
        return true;
    }

    // Skips the value at i.
    bool skip(int depth = 0) {
        if (depth > 10000) return fail("nested deeper than 10,000 levels");
        space();
        if (i >= text.size()) return fail("a value is missing: the text ends");
        const char c = text[i];
        if (c == '"') return string().has_value();
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++i;
            space();
            if (i < text.size() && text[i] == close) {
                ++i;
                return true;
            }
            while (true) {
                if (c == '{') {
                    space();
                    if (i >= text.size() || text[i] != '"') return fail("a member's name is a string");
                    if (!string()) return false;
                    space();
                    if (i >= text.size() || text[i] != ':') return fail("a member's name is followed by `:`");
                    ++i;
                }
                if (!skip(depth + 1)) return false;
                space();
                if (i < text.size() && text[i] == ',') {
                    ++i;
                    continue;
                }
                if (i >= text.size() || text[i] != close) return fail(c == '{' ? "a member is followed by `,` or `}`" : "an element is followed by `,` or `]`");
                ++i;
                return true;
            }
        }
        if (c == 't') return word("true");
        if (c == 'f') return word("false");
        if (c == 'n') return word("null");
        if (c == '-' || digit(c)) return number();
        return fail("a value is an object, an array, a string, a number, true, false or null");
    }

    // Moves i to the start of the member or element `token` of the value at i.
    bool enter(std::string_view token) {
        space();
        if (i >= text.size()) return false;
        if (text[i] == '{') {
            ++i;
            while (true) {
                space();
                if (i < text.size() && text[i] == '}') return false;
                auto key = string();
                if (!key) return false;
                space();
                if (i >= text.size() || text[i] != ':') return false;
                ++i;
                space();
                if (*key == token) return true;
                if (!skip()) return false;
                space();
                if (i >= text.size() || text[i] != ',') return false;
                ++i;
            }
        }
        if (text[i] == '[') {
            std::size_t want = 0;
            const auto r = std::from_chars(token.data(), token.data() + token.size(), want);
            if (r.ec != std::errc{} || r.ptr != token.data() + token.size() || token.empty()) return false;
            ++i;
            for (std::size_t k = 0;; ++k) {
                space();
                if (i < text.size() && text[i] == ']') return false;
                if (k == want) return true;
                if (!skip()) return false;
                space();
                if (i >= text.size() || text[i] != ',') return false;
                ++i;
            }
        }
        return false;
    }
};


// nlohmann's SAX protocol, keeping only where the text stops being JSON.
struct Where {
    using number_integer_t  = nlohmann::ordered_json::number_integer_t;
    using number_unsigned_t = nlohmann::ordered_json::number_unsigned_t;
    using number_float_t    = nlohmann::ordered_json::number_float_t;
    using string_t          = nlohmann::ordered_json::string_t;
    using binary_t          = nlohmann::ordered_json::binary_t;

    std::size_t offset = 0;
    std::string message;

    bool null() { return true; }
    bool boolean(bool) { return true; }
    bool number_integer(number_integer_t) { return true; }
    bool number_unsigned(number_unsigned_t) { return true; }
    bool number_float(number_float_t, const string_t&) { return true; }
    bool string(string_t&) { return true; }
    bool binary(binary_t&) { return true; }
    bool start_object(std::size_t) { return true; }
    bool key(string_t&) { return true; }
    bool end_object() { return true; }
    bool start_array(std::size_t) { return true; }
    bool end_array() { return true; }
    template <class Exception>
    bool parse_error(std::size_t position, const std::string&, const Exception& error) {
        offset = position == 0 ? 0 : position - 1;
        // nlohmann's text is "[json.exception.parse_error.101] parse error at line 1, column 5: …": the last part.
        const std::string_view what = error.what();
        const auto colon = what.rfind(": ");
        message = std::string(colon == std::string_view::npos ? what : what.substr(colon + 2));
        return false;
    }
};

Place place_at(std::string_view text, std::size_t offset) {
    Place at;
    at.offset = offset;
    at.line = 1 + static_cast<std::size_t>(std::ranges::count(text.substr(0, offset), '\n'));
    const std::size_t line_start = offset == 0 ? 0 : (text.rfind('\n', offset - 1) == std::string_view::npos ? 0 : text.rfind('\n', offset - 1) + 1);
    at.column = offset - line_start + 1;
    return at;
}

// The scanner at the start of the value a pointer names in a text that parses, or nothing.
std::optional<Seek> walk(std::string_view text, std::string_view pointer) {
    Seek seek = Seek::of(text);
    if (!pointer.empty() && pointer.front() != '/') return std::nullopt;
    while (!pointer.empty()) {
        pointer.remove_prefix(1);
        const std::size_t end = pointer.find('/');
        std::string token;
        for (std::size_t k = 0; k < std::min(end, pointer.size()); ++k) {  // ~1 is '/', ~0 is '~'
            if (pointer[k] == '~' && k + 1 < pointer.size() && (pointer[k + 1] == '0' || pointer[k + 1] == '1')) {
                token += pointer[k + 1] == '0' ? '~' : '/';
                ++k;
            } else {
                token += pointer[k];
            }
        }
        if (!seek.enter(token)) return std::nullopt;
        pointer = end == std::string_view::npos ? std::string_view{} : pointer.substr(end);
    }
    seek.space();
    return seek;
}

// A problem with what is at a pointer of a text that parses, placed: an absent member at the place of what would
// hold it.
Problem placed(std::string_view text, std::string file, std::string_view pointer, std::string message, std::string hint) {
    Problem out{std::move(file), 0, 0, std::string(pointer), std::move(message), std::move(hint)};
    while (true) {
        if (const auto place = locate(text, pointer)) {
            out.line = place->line;
            out.column = place->column;
            break;
        }
        if (pointer.empty()) break;
        pointer = pointer.substr(0, pointer.rfind('/'));
    }
    return out;
}

}  // namespace

std::optional<Place> locate(std::string_view text, std::string_view pointer) {
    const auto seek = walk(text, pointer);
    if (!seek) return std::nullopt;
    return place_at(text, seek->i);
}

std::string escape(std::string_view key) {
    std::string out;
    for (char c : key) out += c == '~' ? std::string("~0") : c == '/' ? std::string("~1") : std::string(1, c);
    return out;
}

std::string quote(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (const char here : text) {
        switch (here) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(here) < 0x20) {
                    constexpr std::string_view digits = "0123456789abcdef";
                    out += "\\u00";
                    out += digits[static_cast<unsigned char>(here) >> 4];
                    out += digits[static_cast<unsigned char>(here) & 0xF];
                } else {
                    out += here;
                }
        }
    }
    out += '"';
    return out;
}

std::expected<Scan, Problem> Scan::read(std::string_view text, std::string file) {
    Seek seek = Seek::of(text);
    if (seek.skip()) {
        seek.space();
        if (seek.i < text.size()) seek.fail("one value, then nothing but white space");
    }
    if (!seek.failure.empty()) {
        const Place at = place_at(text, std::min(seek.i, text.size()));
        return std::unexpected(Problem{std::move(file), at.line, at.column, "", std::string(seek.failure), ""});
    }
    Scan out;
    out.file_ = std::move(file);
    out.text_ = std::string(text);
    return out;
}

std::optional<std::string> Scan::text(std::string_view pointer) const {
    auto seek = walk(text_, pointer);
    if (!seek || seek->i >= text_.size() || text_[seek->i] != '"') return std::nullopt;
    return seek->string();
}

std::optional<std::vector<std::string>> Scan::texts(std::string_view pointer) const {
    auto seek = walk(text_, pointer);
    if (!seek || seek->i >= text_.size() || text_[seek->i] != '[') return std::nullopt;
    std::vector<std::string> out;
    ++seek->i;
    while (true) {  // the text parses: an element, then `,` or `]`
        seek->space();
        if (seek->i >= text_.size() || text_[seek->i] == ']') break;
        if (text_[seek->i] == '"') {
            if (auto element = seek->string()) out.push_back(std::move(*element));
            else break;
        } else if (!seek->skip()) {
            break;
        }
        seek->space();
        if (seek->i >= text_.size() || text_[seek->i] != ',') break;
        ++seek->i;
    }
    return out;
}

Problem Scan::problem(std::string_view pointer, std::string message, std::string hint) const {
    return placed(text_, file_, pointer, std::move(message), std::move(hint));
}

std::string Problem::text() const {
    return std::format("{}:{}:{}: {}{}{}", file, line, column, at.empty() ? "" : std::format("{}: ", at), message, hint.empty() ? "" : std::format(" ({})", hint));
}

Node Node::operator[](std::string_view key) const {
    const std::string pointer = std::format("{}/{}", pointer_, escape(key));
    if (!value_ || !value_->is_object()) return Node(nullptr, pointer);
    const auto found = value_->find(key);
    return Node(found == value_->end() ? nullptr : &*found, pointer);
}

Node Node::operator[](std::size_t index) const {
    const std::string pointer = std::format("{}/{}", pointer_, index);
    if (!value_ || !value_->is_array() || index >= value_->size()) return Node(nullptr, pointer);
    return Node(&(*value_)[index], pointer);
}

Node Node::at(std::string_view pointer) const {
    Node found = *this;
    while (found && !pointer.empty()) {
        if (pointer.front() != '/') return {};
        pointer.remove_prefix(1);
        const std::size_t end = pointer.find('/');
        std::string key(pointer.substr(0, end));
        pointer = end == std::string_view::npos ? std::string_view{} : pointer.substr(end);
        for (std::size_t place = 0; (place = key.find('~', place)) != std::string::npos; ++place)
            if (place + 1 < key.size() && (key[place + 1] == '0' || key[place + 1] == '1')) key.replace(place, 2, key[place + 1] == '1' ? "/" : "~");
        if (found.is_array()) {
            std::size_t index = 0;
            const auto read = std::from_chars(key.data(), key.data() + key.size(), index);
            if (read.ec != std::errc{} || read.ptr != key.data() + key.size()) return {};
            found = found[index];
        } else {
            found = found[std::string_view(key)];
        }
    }
    return found;
}

bool Node::is_object() const noexcept { return value_ && value_->is_object(); }
bool Node::is_array() const noexcept { return value_ && value_->is_array(); }
bool Node::is_string() const noexcept { return value_ && value_->is_string(); }
bool Node::is_number() const noexcept { return value_ && value_->is_number(); }
bool Node::is_integer() const noexcept { return value_ && value_->is_number_integer(); }
bool Node::is_boolean() const noexcept { return value_ && value_->is_boolean(); }
bool Node::is_null() const noexcept { return value_ && value_->is_null(); }

std::optional<std::string> Node::text() const {
    if (!is_string()) return std::nullopt;
    return value_->get<std::string>();
}

std::optional<double> Node::number() const {
    if (!is_number()) return std::nullopt;
    return value_->get<double>();
}

std::optional<std::int64_t> Node::integer() const {
    if (!is_integer()) return std::nullopt;
    return value_->get<std::int64_t>();
}

std::optional<bool> Node::boolean() const {
    if (!is_boolean()) return std::nullopt;
    return value_->get<bool>();
}

std::size_t Node::size() const noexcept { return value_ && (value_->is_object() || value_->is_array()) ? value_->size() : 0; }

std::vector<std::pair<std::string, Node>> Node::members() const {
    std::vector<std::pair<std::string, Node>> out;
    if (!is_object()) return out;
    for (auto member = value_->begin(); member != value_->end(); ++member)
        out.emplace_back(member.key(), Node(&member.value(), std::format("{}/{}", pointer_, escape(member.key()))));
    return out;
}

std::vector<std::pair<std::string, Node>> Node::members_by_key() const {
    auto out = members();
    std::ranges::stable_sort(out, {}, &std::pair<std::string, Node>::first);
    return out;
}

std::vector<Node> Node::items() const {
    std::vector<Node> out;
    if (!is_array()) return out;
    for (std::size_t index = 0; index < value_->size(); ++index) out.push_back(Node(&(*value_)[index], std::format("{}/{}", pointer_, index)));
    return out;
}

std::string Node::dump() const { return value_ ? value_->dump() : std::string(); }

Document::~Document() = default;
Document::Document(Document&& other) noexcept = default;
Document& Document::operator=(Document&& other) noexcept = default;

std::expected<Document, Problem> Document::parse(std::string_view text, std::string file) {
    Document out;
    out.file_ = std::move(file);
    out.text_ = std::string(text);
    out.value_ = nlohmann::ordered_json::parse(out.text_, nullptr, false);
    if (out.value_.is_discarded()) {
        Where where;
        (void)nlohmann::ordered_json::sax_parse(out.text_.begin(), out.text_.end(), &where, nlohmann::ordered_json::input_format_t::json, true, false);
        const Place at = place_at(out.text_, std::min(where.offset, out.text_.size()));
        return std::unexpected(Problem{out.file_, at.line, at.column, "", where.message.empty() ? std::string("not JSON") : where.message, ""});
    }
    return out;
}

Problem Document::problem(const Node& at, std::string message, std::string hint) const {
    return placed(text_, file_, at.pointer(), std::move(message), std::move(hint));
}

}  // namespace mlibs::json
