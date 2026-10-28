module openmui.core;

import std;
import nlohmann.json;

namespace omui {

namespace {

// nlohmann's SAX protocol, building a Value tree.
struct Builder {
    using number_integer_t  = nlohmann::json::number_integer_t;
    using number_unsigned_t = nlohmann::json::number_unsigned_t;
    using number_float_t    = nlohmann::json::number_float_t;
    using string_t          = nlohmann::json::string_t;
    using binary_t          = nlohmann::json::binary_t;

    struct Frame {
        Value       value;
        std::string key;  // pending key of a map frame
    };
    std::vector<Frame> stack;
    Value              root;
    bool               have_root = false;
    std::size_t        error_offset = 0;
    std::string        error;

    void put(Value v) {
        if (stack.empty()) {
            root = std::move(v);
            have_root = true;
            return;
        }
        Frame& top = stack.back();
        if (Value::List* l = top.value.list()) l->push_back(std::move(v));
        else top.value.set(top.key, std::move(v));
    }

    bool null() { put(Value{}); return true; }
    bool boolean(bool b) { put(Value(b)); return true; }
    bool number_integer(number_integer_t v) { put(Value(static_cast<std::int64_t>(v))); return true; }
    bool number_unsigned(number_unsigned_t v) {
        if (v <= static_cast<number_unsigned_t>(std::numeric_limits<std::int64_t>::max())) put(Value(static_cast<std::int64_t>(v)));
        else put(Value(static_cast<double>(v)));
        return true;
    }
    bool number_float(number_float_t v, const string_t&) { put(Value(static_cast<double>(v))); return true; }
    bool string(string_t& s) { put(Value(std::move(s))); return true; }
    bool binary(binary_t&) {
        error = "binary values are not JSON";
        return false;
    }
    bool start_object(std::size_t) {
        stack.push_back({Value(Value::Map{}), {}});
        return true;
    }
    bool key(string_t& k) {
        stack.back().key = std::move(k);
        return true;
    }
    bool end_object() {
        Value v = std::move(stack.back().value);
        stack.pop_back();
        put(std::move(v));
        return true;
    }
    bool start_array(std::size_t) {
        stack.push_back({Value(Value::List{}), {}});
        return true;
    }
    bool end_array() { return end_object(); }

    template <class Exception>
    bool parse_error(std::size_t position, const std::string& last_token, const Exception& ex) {
        error_offset = position;
        std::string_view what = ex.what();
        // nlohmann's text is "[json.exception.parse_error.101] parse error at line 1, column 5: …"
        auto colon = what.rfind(": ");
        error = std::string(colon == std::string_view::npos ? what : what.substr(colon + 2));
        if (!last_token.empty() && error.find(last_token) == std::string::npos)
            error += std::format(" (at '{}')", last_token);
        return false;
    }
};

JsonError locate(std::string_view text, std::size_t offset, std::string message) {
    JsonError e;
    e.offset = offset;
    e.message = std::move(message);
    e.line = 1;
    std::size_t line_start = 0;
    std::size_t end = std::min(offset, text.size());
    for (std::size_t i = 0; i < end; ++i)
        if (text[i] == '\n') {
            ++e.line;
            line_start = i + 1;
        }
    e.column = end - line_start + (offset > 0 ? 0 : 1);
    if (e.column == 0) e.column = 1;
    return e;
}

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

}  // namespace

std::optional<JsonError> json_location(std::string_view text, std::string_view pointer) {
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
    JsonError at;
    at.offset = s.i;
    at.line = 1 + static_cast<std::size_t>(std::ranges::count(text.substr(0, s.i), '\n'));
    const std::size_t line_start = s.i == 0 ? 0 : (text.rfind('\n', s.i - 1) == std::string_view::npos ? 0 : text.rfind('\n', s.i - 1) + 1);
    at.column = s.i - line_start + 1;
    return at;
}

std::expected<Value, JsonError> parse_json(std::string_view text) {
    Builder b;
    bool ok = nlohmann::json::sax_parse(text.begin(), text.end(), &b, nlohmann::json::input_format_t::json, true, false);
    if (!ok || !b.have_root) {
        if (b.error.empty()) b.error = "not a JSON value";
        return std::unexpected(locate(text, b.error_offset, std::move(b.error)));
    }
    return std::move(b.root);
}

std::expected<std::vector<Value>, JsonError> parse_ndjson(std::string_view text) {
    std::vector<Value> out;
    std::size_t line_no = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        auto nl = text.find('\n', start);
        std::string_view line = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        ++line_no;
        bool blank = std::ranges::all_of(line, [](char c) { return c == ' ' || c == '\t' || c == '\r'; });
        if (!blank) {
            auto v = parse_json(line);
            if (!v) {
                JsonError e = v.error();
                e.line = line_no;
                e.offset += start;
                return std::unexpected(std::move(e));
            }
            out.push_back(std::move(*v));
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return out;
}

}  // namespace omui
