// mlibs.json — JSON documents read with their places (W1 step 3, D-139).
//
//     auto doc = mlibs::json::Document::parse(text, "piece.music.omui.json");
//     const auto tempo = doc->root()["music"]["tempo"];
//     if (!tempo.number()) problems.push_back(doc->problem(tempo, "the tempo is a number", "tempo: 96"));
//     // piece.music.omui.json:3:14: /music/tempo: the tempo is a number (tempo: 96)
//
// Data files are TypeScript data modules evaluated to JSON snapshots; their
// readers (sounds, music, timelines, video descriptions, effects, behaviours)
// read the snapshot through this. A value is a Node: its JSON and its JSON
// pointer (RFC 6901), so a problem names where it is — the pointer, and the
// line and column in the text, found when a problem is made. Members keep
// the order the text has them. Parsing is nlohmann.json's (D-56).
export module mlibs.json;

import std;
import nlohmann.json;

export namespace mlibs::json {

// Where the value at a JSON pointer begins in a text that parses: 1-based
// line and column (in bytes), and its offset. Nothing when the pointer
// names no value.
struct Place {
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t offset = 0;
};
[[nodiscard]] std::optional<Place> locate(std::string_view text, std::string_view pointer);

// A key as a pointer's token: `~` is `~0`, `/` is `~1`.
[[nodiscard]] std::string escape(std::string_view key);

// What is wrong with a document, where.
struct Problem {
    std::string file;
    std::size_t line = 0;    // 0: no place in the text
    std::size_t column = 0;
    std::string at;          // the JSON pointer: "/music/sections/0/tempo"
    std::string message;
    std::string hint;        // the fix, when there is one

    // file:line:column: at: message (hint)
    [[nodiscard]] std::string text() const;
};

class Document;

// A value of a document, or a member or element it does not have (false).
class Node {
public:
    Node() = default;

    explicit operator bool() const noexcept { return value_ != nullptr; }
    // A member (of an object) or an element (of an array); absent when not there.
    [[nodiscard]] Node operator[](std::string_view key) const;
    [[nodiscard]] Node operator[](std::size_t index) const;

    [[nodiscard]] bool is_object() const noexcept;
    [[nodiscard]] bool is_array() const noexcept;
    [[nodiscard]] bool is_string() const noexcept;
    [[nodiscard]] bool is_number() const noexcept;
    [[nodiscard]] bool is_integer() const noexcept;  // a number written without a fraction or exponent
    [[nodiscard]] bool is_boolean() const noexcept;
    [[nodiscard]] bool is_null() const noexcept;

    // The value when it is of that type, else nothing (an integer is a number too).
    [[nodiscard]] std::optional<std::string> text() const;
    [[nodiscard]] std::optional<double> number() const;
    [[nodiscard]] std::optional<std::int64_t> integer() const;
    [[nodiscard]] std::optional<bool> boolean() const;

    // An object's members or an array's elements: how many, and each with its place.
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::vector<std::pair<std::string, Node>> members() const;
    // The members by key: for what must not depend on the order a file
    // writes them in (a mix, a cascade of rules).
    [[nodiscard]] std::vector<std::pair<std::string, Node>> members_by_key() const;
    [[nodiscard]] std::vector<Node> items() const;

    [[nodiscard]] const std::string& pointer() const noexcept { return pointer_; }
    // The value as compact JSON (empty when absent).
    [[nodiscard]] std::string dump() const;

private:
    friend class Document;
    Node(const nlohmann::ordered_json* value, std::string pointer) : value_(value), pointer_(std::move(pointer)) {}

    const nlohmann::ordered_json* value_ = nullptr;
    std::string                   pointer_;
};

class Document {
public:
    // Out of line: the JSON's own members are this module's (an importer
    // never instantiates them). A move keeps every node of it valid.
    ~Document();
    Document(Document&& other) noexcept;
    Document& operator=(Document&& other) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // The text parsed; `file` names it in problems. Malformed text is one
    // problem at its line and column.
    [[nodiscard]] static std::expected<Document, Problem> parse(std::string_view text, std::string file);

    [[nodiscard]] Node root() const { return Node(&value_, ""); }
    [[nodiscard]] const std::string& file() const noexcept { return file_; }
    [[nodiscard]] std::string_view text() const noexcept { return text_; }

    // A problem with what is at `at`, placed in the text.
    [[nodiscard]] Problem problem(const Node& at, std::string message, std::string hint = {}) const;

private:
    Document() = default;

    std::string            file_;
    std::string            text_;
    nlohmann::ordered_json value_;
};

}  // namespace mlibs::json
