// mlibs.message — a message's words with values in them, without a locale: `{}`
// for each value, in order (`{{` and `}}` for the braces themselves), the
// pattern checked as it is compiled as std::format's is. Numbers are written as
// std::to_chars writes them (an integer in decimal, a real the shortest that
// reads back the same), the same on every target.
//
//     return std::unexpected(mlibs::message::format("{} normals for {} positions", normals, count));
//
// Why not std::format: on the Web, the first std::format in a program brings
// std::locale and every facet the standard library has (≈ 87 KB of a page's
// Wasm), for the `L` option a message never asks for. A library a page links
// (mlibs.rhi, mlibs.geom …) says its errors with this; a program that already
// pays for std::format loses nothing by it.
export module mlibs.message;

import std;

export namespace mlibs::message {

// Called (never defined) by a pattern that is not one: not a constant expression,
// so the program does not compile, with this name and its reason in the error.
void pattern_is_not_valid(const char* why);

// A pattern whose `{}` count matches its values, checked as it is compiled. A
// placeholder with a specification (`{:x}`, `{0}`) is refused: the words are
// the message's, the values are written one way.
template <class... Values>
class Pattern {
public:
    template <class Text>
        requires std::convertible_to<const Text&, std::string_view>
    consteval Pattern(const Text& text) : text_(text) {
        std::size_t holes = 0;
        for (std::size_t at = 0; at < text_.size(); ++at) {
            const char here = text_[at];
            const char next = at + 1 < text_.size() ? text_[at + 1] : '\0';
            if ((here == '{' && next == '{') || (here == '}' && next == '}')) ++at;
            else if (here == '{' && next == '}') ++holes, ++at;
            else if (here == '{' || here == '}') pattern_is_not_valid("a placeholder is `{}` alone, a brace is `{{` or `}}`");
        }
        if (holes != sizeof...(Values)) pattern_is_not_valid("as many `{}` as values");
    }
    [[nodiscard]] constexpr std::string_view text() const noexcept { return text_; }

private:
    std::string_view text_;
};

// The pattern's words with each value's text put in its `{}`, in order.
[[nodiscard]] std::string fill(std::string_view pattern, std::span<const std::string_view> values);

// One value as a message writes it.
[[nodiscard]] std::string text(std::int64_t value);
[[nodiscard]] std::string text(std::uint64_t value);
[[nodiscard]] std::string text(double value);
[[nodiscard]] inline std::string_view text(bool value) noexcept { return value ? "true" : "false"; }
[[nodiscard]] inline std::string text(char value) { return std::string(1, value); }
[[nodiscard]] inline std::string_view text(std::string_view value) noexcept { return value; }

template <class Value>
[[nodiscard]] decltype(auto) text_of(const Value& value) {
    if constexpr (std::is_same_v<Value, bool> || std::is_same_v<Value, char>) return text(value);
    else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>) return text(static_cast<std::int64_t>(value));
    else if constexpr (std::is_integral_v<Value>) return text(static_cast<std::uint64_t>(value));
    else if constexpr (std::is_floating_point_v<Value>) return text(static_cast<double>(value));
    else if constexpr (std::is_convertible_v<const Value&, std::string_view>) return std::string_view(value);
    else static_assert(sizeof(Value) == 0, "mlibs::message writes numbers, truth, characters and text");
}

template <class... Values>
[[nodiscard]] std::string format(Pattern<std::type_identity_t<Values>...> pattern, const Values&... values) {
    if constexpr (sizeof...(Values) == 0) return fill(pattern.text(), {});
    else {
        const auto written = std::tuple<decltype(text_of(values))...>(text_of(values)...);  // owners of the numbers' text
        const auto pieces = std::apply([](const auto&... each) { return std::array<std::string_view, sizeof...(Values)>{std::string_view(each)...}; }, written);
        return fill(pattern.text(), pieces);
    }
}

}  // namespace mlibs::message
