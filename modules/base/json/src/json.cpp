// mlibs.json: parsing (nlohmann.json), nodes with their pointers, and the
// places of pointers in the text.
module mlibs.json;

import std;
import nlohmann.json;

namespace mlibs::json {

namespace {

// A scanner of JSON that is known to parse, for json_location: it follows
// one pointer down and skips everything else.
struct Seek {
    std::string_view text;
    std::size_t      i = 0;

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
        if (i >= text.size() || text[i] != '"') return std::nullopt;
        ++i;
        std::string out;
        while (i < text.size() && text[i] != '"') {
            if (text[i] != '\\') {
                out += text[i++];
                continue;
            }
            if (++i >= text.size()) return std::nullopt;
            const char e = text[i++];
            switch (e) {
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto cp = hex4();
                    if (!cp) return std::nullopt;
                    if (*cp >= 0xD800 && *cp < 0xDC00 && text.substr(i, 2) == "\\u") {
                        i += 2;
                        auto lo = hex4();
                        if (!lo) return std::nullopt;
                        *cp = 0x10000 + ((*cp - 0xD800) << 10) + (*lo - 0xDC00);
                    }
                    utf8(out, *cp);
                    break;
                }
                default: out += e;
            }
        }
        if (i >= text.size()) return std::nullopt;
        ++i;
        return out;
    }

    // Skips the value at i.
    bool skip(int depth = 0) {
        if (depth > 10000) return false;
        space();
        if (i >= text.size()) return false;
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
                    if (!string()) return false;
                    space();
                    if (i >= text.size() || text[i] != ':') return false;
                    ++i;
                }
                if (!skip(depth + 1)) return false;
                space();
                if (i >= text.size()) return false;
                if (text[i] == ',') {
                    ++i;
                    continue;
                }
                if (text[i] != close) return false;
                ++i;
                return true;
            }
        }
        while (i < text.size() && text[i] != ',' && text[i] != '}' && text[i] != ']' && text[i] != ' ' && text[i] != '\n' &&
               text[i] != '\r' && text[i] != '\t')
            ++i;
        return true;
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

}  // namespace

std::optional<Place> locate(std::string_view text, std::string_view pointer) {
    Seek s{text};
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
        if (!s.enter(token)) return std::nullopt;
        pointer = end == std::string_view::npos ? std::string_view{} : pointer.substr(end);
    }
    s.space();
    return place_at(text, s.i);
}

std::string escape(std::string_view key) {
    std::string out;
    for (char c : key) out += c == '~' ? std::string("~0") : c == '/' ? std::string("~1") : std::string(1, c);
    return out;
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
    Problem out{file_, 0, 0, at.pointer(), std::move(message), std::move(hint)};
    // An absent member: the place of what would hold it.
    std::string_view pointer = at.pointer();
    while (true) {
        if (const auto place = locate(text_, pointer)) {
            out.line = place->line;
            out.column = place->column;
            break;
        }
        if (pointer.empty()) break;
        pointer = pointer.substr(0, pointer.rfind('/'));
    }
    return out;
}

}  // namespace mlibs::json
