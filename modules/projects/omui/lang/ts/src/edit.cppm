// omui.lang.ts:edit — structured design operations as edits of a script's
// source (Live Design, W14): the builder call a node was built by (where it was
// built, `built_at`: `file:line:column`, development mode) found in the text, and a property set
// or removed in its object of properties, a child inserted into its list of
// children, the call itself removed — the rest of the text as it was, so the
// TypeScript stays the one source and reads as a person wrote it. What is not
// a builder call there (a function of the script's own, a computed list) is a
// problem that says so: the agent then writes the edit itself (design.edit).
export module omui.lang.ts:edit;

import std;

export namespace omui::lang::ts {

// A builder call in source text, as byte offsets: `text({ … }, [ … ])`.
struct BuilderCall {
    std::size_t start = 0, end = 0;  // from its callee's first byte to past its `)`
    std::size_t open = 0;            // its `(`
    std::string callee;              // `text`, `ui.text` …
    // Its object of properties `{ … }` and its list of children `[ … ]`, the
    // brackets included (`end` past the closing one), when it has them.
    std::optional<std::pair<std::size_t, std::size_t>> properties, children;
};

// The builder call at a 1-based line and column (where an element was built): the
// call whose callee or `(` is there.
[[nodiscard]] std::expected<BuilderCall, std::string> builder_call_at(std::string_view source, std::size_t line, std::size_t column);

// The text with property `name` of that call set to `value` (code: `"Save"`,
// `12`, `{ x: 1 }`): its value replaced where it is written, else the property
// added at the end of its object (the object written when the call has none).
[[nodiscard]] std::expected<std::string, std::string> set_property(std::string_view source, std::size_t line, std::size_t column,
                                                                   std::string_view name, std::string_view value);
// The text with property `name` of that call taken out (with its comma).
[[nodiscard]] std::expected<std::string, std::string> remove_property(std::string_view source, std::size_t line, std::size_t column,
                                                                      std::string_view name);
// The text with `code` (an element: `text({ text: "Hi" })`) inserted into that
// call's children at `index` (the end when absent or past it), indented as its
// siblings are.
[[nodiscard]] std::expected<std::string, std::string> insert_child(std::string_view source, std::size_t line, std::size_t column,
                                                                   std::string_view code, std::optional<std::size_t> index = std::nullopt);
// The text with that call taken out of the list it is in (with its comma and,
// when it had the line to itself, its line).
[[nodiscard]] std::expected<std::string, std::string> remove_call(std::string_view source, std::size_t line, std::size_t column);

// A source's calls, lexed once: where an element was built, from the
// position QuickJS gives its call (the last its arguments recorded — inside
// them, often in a nested call) to the call itself (development mode places
// where every element was built with it).
class CallIndex {
public:
    [[nodiscard]] static std::expected<CallIndex, std::string> of(std::string_view source);
    // The call of `callee` (a callee's last name: `text` for `ui.text`)
    // around a 1-based line and column, the `skip`-th such outward (0: the
    // innermost): its callee's first line and column.
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> enclosing(std::size_t line, std::size_t column, std::string_view callee,
                                                                               std::size_t skip = 0) const;

private:
    struct Span {
        std::size_t start = 0, end = 0;  // the call, from its callee to past its `)`
        std::string name;                // its callee's last name
    };
    std::string source_;
    std::vector<std::size_t> line_starts_;
    std::vector<Span> calls_;  // in source order
};

}  // namespace omui::lang::ts
