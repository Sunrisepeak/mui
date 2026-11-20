module omui.core;

import std;
import nlohmann.json;
import mlibs.json;

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

}  // namespace

std::optional<JsonError> json_location(std::string_view text, std::string_view pointer) {
    const auto place = mlibs::json::locate(text, pointer);
    if (!place) return std::nullopt;
    JsonError at;
    at.line = place->line;
    at.column = place->column;
    at.offset = place->offset;
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

namespace {

bool flat(const Value& value) {
    if (const Value::List* list = value.list())
        return std::ranges::none_of(*list, [](const Value& item) { return item.list() || item.map(); });
    if (const Value::Map* map = value.map())
        return std::ranges::none_of(*map, [](const auto& member) { return member.second.list() || member.second.map(); });
    return true;
}

void indent(std::string& out, const Value& value, std::size_t depth) {
    if (flat(value)) {
        // One line, with a space after each comma and colon.
        if (const Value::List* list = value.list()) {
            out += '[';
            for (std::size_t index = 0; index < list->size(); ++index) out += (index ? ", " : "") + (*list)[index].json();
            out += ']';
        } else if (const Value::Map* map = value.map()) {
            out += map->empty() ? "{" : "{ ";
            for (std::size_t index = 0; index < map->size(); ++index) {
                if (index) out += ", ";
                spec::append_json_string(out, (*map)[index].first);
                out += ": " + (*map)[index].second.json();
            }
            out += map->empty() ? "}" : " }";
        } else {
            out += value.json();
        }
        return;
    }
    const std::string inner((depth + 1) * 2, ' ');
    if (const Value::List* list = value.list()) {
        out += "[\n";
        for (std::size_t index = 0; index < list->size(); ++index) {
            out += inner;
            indent(out, (*list)[index], depth + 1);
            out += index + 1 < list->size() ? ",\n" : "\n";
        }
        out += std::string(depth * 2, ' ') + ']';
        return;
    }
    const Value::Map& map = *value.map();
    out += "{\n";
    for (std::size_t index = 0; index < map.size(); ++index) {
        out += inner;
        spec::append_json_string(out, map[index].first);
        out += ": ";
        indent(out, map[index].second, depth + 1);
        out += index + 1 < map.size() ? ",\n" : "\n";
    }
    out += std::string(depth * 2, ' ') + '}';
}

}  // namespace

std::string indented_json(const Value& value) {
    std::string out;
    indent(out, value, 0);
    return out + '\n';
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
