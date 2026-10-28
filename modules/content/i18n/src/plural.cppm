// openmui.i18n:plural — CLDR's plural rules (UTS #35 part 3 §5), as data:
// the rules of a locale are its pack's text (`i = 1 and v = 0`), parsed and
// evaluated here, so a language is added without code. A number is chosen
// by the digits it is shown with: "1" is one in English, "1.0" is other.
export module openmui.i18n:plural;

import std;

export namespace omui::i18n {

enum class Plural : std::uint8_t { zero, one, two, few, many, other };

[[nodiscard]] std::string_view name(Plural p) noexcept;
[[nodiscard]] std::optional<Plural> parse_plural(std::string_view name) noexcept;

// The operands of a number as it is shown (UTS #35 part 3 §5.1): n its
// absolute value, i its integer digits, v the count of visible fraction
// digits and w without trailing zeros, f and t those digits as an integer,
// c the compact exponent (1.2万: 4).
struct Operands {
    double        n = 0;
    std::uint64_t i = 0;
    int           v = 0;
    int           w = 0;
    std::uint64_t f = 0;
    std::uint64_t t = 0;
    int           c = 0;

    // From the ASCII digits a number is shown with: "-1.50", "12".
    [[nodiscard]] static std::optional<Operands> of(std::string_view decimal, int compact_exponent = 0);
    [[nodiscard]] static Operands of(std::int64_t value);
};

class PluralRules {
public:
    // Each category's rule in CLDR's syntax (samples after `@` ignored);
    // `other` needs none. An error names the rule and the place.
    [[nodiscard]] static std::expected<PluralRules, std::string> parse(std::span<const std::pair<Plural, std::string>> rules);

    // The first category whose rule holds; `other` when none does.
    [[nodiscard]] Plural select(const Operands& x) const noexcept;
    // The categories the locale has, `other` last.
    [[nodiscard]] std::vector<Plural> categories() const;

    struct Relation {
        char                                               operand = 'n';
        std::uint64_t                                      modulo = 0;  // 0: none
        bool                                               negated = false;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    };
    struct Rule {
        Plural                             category = Plural::other;
        std::vector<std::vector<Relation>> any;  // or of ands
    };

private:
    std::vector<Rule> rules_;
};

}  // namespace omui::i18n
