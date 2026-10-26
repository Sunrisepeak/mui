// openmui.core:json — JSON text to Values and back.
//
// Parsing is nlohmann.json's (D-56), driven through its SAX interface so the
// result is built as a Value directly and an error carries its line and
// column. Writing is the spec's canonical form (Value::json).
export module openmui.core:json;

import std;
import :base;

export namespace omui {

struct JsonError {
    std::size_t line   = 0;  // 1-based
    std::size_t column = 0;  // 1-based, in bytes
    std::size_t offset = 0;
    std::string message;

    [[nodiscard]] std::string text() const { return std::format("{}:{}: {}", line, column, message); }
};

// Parses one JSON value; trailing text other than white space is an error.
[[nodiscard]] std::expected<Value, JsonError> parse_json(std::string_view text);

// The values of newline-delimited JSON (envelopes, the agent protocol, event
// recordings); blank lines are skipped.
[[nodiscard]] std::expected<std::vector<Value>, JsonError> parse_ndjson(std::string_view text);

}  // namespace omui
