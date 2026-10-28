// openmui.i18n:message — messages in Unicode MessageFormat 2 (the LDML
// message syntax): text with placeholders, functions and their options,
// declarations, and selection by plural category or exact value.
//
//   Hello, {$name}!
//   You have {$count :number} new messages.
//   .input {$count :number}
//   .match $count
//   0   {{No new messages}}
//   one {{One new message}}
//   *   {{{$count} new messages}}
//
// Functions: :number (minimumFractionDigits, maximumFractionDigits,
// minimumIntegerDigits, useGrouping, signDisplay, notation=compact,
// compactDisplay, select=plural|ordinal|exact), :integer, :percent, :string,
// :date and :time (style), :datetime (dateStyle, timeStyle), :list (type,
// width). Markup ({#b} … {/b}) and attributes are read and left out of the
// text. Formatting never fails: what cannot be resolved is shown as its
// fallback ({$name}) and reported.
export module openmui.i18n:message;

import std;
import :plural;
import :data;
import :format;

export namespace omui::i18n {

using Arg = std::variant<std::monostate, bool, std::int64_t, double, std::string, CivilTime, std::vector<std::string>>;

// A message's arguments by name.
class Args {
public:
    Args() = default;
    Args(std::initializer_list<std::pair<std::string_view, Arg>> values);
    Args& set(std::string_view name, Arg value);
    [[nodiscard]] const Arg* get(std::string_view name) const noexcept;

private:
    std::vector<std::pair<std::string, Arg>> values_;
};

class Message {
public:
    // An error says what and where: `1:12: …`.
    [[nodiscard]] static std::expected<Message, std::string> parse(std::string_view source);

    // The text, its numbers and dates written by `fmt`'s locale; what could
    // not be resolved goes to `errors` (when given) and shows as its fallback.
    [[nodiscard]] std::string format(const Formatter& fmt, const Args& args = {}, std::vector<std::string>* errors = nullptr) const;
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    // The variables it reads, for checks that a translation keeps them.
    [[nodiscard]] std::vector<std::string> variables() const;

    struct Option {
        std::string name, value;
        bool        variable = false;  // value is a $variable's name
    };
    struct Expression {
        enum class Kind : std::uint8_t { none, variable, literal } kind = Kind::none;
        std::string         operand;
        std::string         function;  // without its ':'
        std::vector<Option> options;
    };
    struct Part {
        std::string               text;
        std::optional<Expression> expression;
    };
    using Pattern = std::vector<Part>;
    struct Declaration {
        bool        input = true;  // .input; else .local
        std::string name;
        Expression  expression;
    };
    struct Variant {
        std::vector<std::optional<std::string>> keys;  // nothing: the catch-all *
        Pattern                                 pattern;
    };

private:
    std::string              source_;
    std::vector<Declaration> declarations_;
    std::vector<std::string> selectors_;
    std::vector<Variant>     variants_;  // with .match
    Pattern                  pattern_;   // without
};

}  // namespace omui::i18n
