module openmui.i18n;

import std;

namespace omui::i18n {

namespace {

constexpr std::array<std::string_view, 6> plural_names{"zero", "one", "two", "few", "many", "other"};

// A cursor over a rule's text.
struct Scan {
    std::string_view text;
    std::size_t      at = 0;

    void space() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) ++at;
    }
    bool done() {
        space();
        return at >= text.size();
    }
    bool take(std::string_view word) {
        space();
        if (text.substr(at, word.size()) != word) return false;
        at += word.size();
        return true;
    }
    std::optional<std::uint64_t> number() {
        space();
        std::uint64_t v = 0;
        const auto r = std::from_chars(text.data() + at, text.data() + text.size(), v);
        if (r.ec != std::errc{}) return std::nullopt;
        at = static_cast<std::size_t>(r.ptr - text.data());
        return v;
    }
};

double operand(const Operands& x, char o) noexcept {
    switch (o) {
        case 'n': return x.n;
        case 'i': return static_cast<double>(x.i);
        case 'v': return x.v;
        case 'w': return x.w;
        case 'f': return static_cast<double>(x.f);
        case 't': return static_cast<double>(x.t);
        case 'c':
        case 'e': return x.c;
        default: return 0;
    }
}

bool holds(const PluralRules::Relation& r, const Operands& x) noexcept {
    double v = operand(x, r.operand);
    if (r.modulo) v = std::fmod(v, static_cast<double>(r.modulo));
    bool in = false;
    // A value with a fraction is in no range of integers.
    if (v == std::floor(v))
        for (const auto& [lo, hi] : r.ranges)
            if (v >= static_cast<double>(lo) && v <= static_cast<double>(hi)) in = true;
    return r.negated ? !in : in;
}

}  // namespace

std::string_view name(Plural p) noexcept { return plural_names[static_cast<std::size_t>(p)]; }

std::optional<Plural> parse_plural(std::string_view n) noexcept {
    for (std::size_t i = 0; i < plural_names.size(); ++i)
        if (plural_names[i] == n) return static_cast<Plural>(i);
    return std::nullopt;
}

std::optional<Operands> Operands::of(std::string_view s, int compact_exponent) {
    if (!s.empty() && (s.front() == '-' || s.front() == '+')) s.remove_prefix(1);
    const std::size_t dot = s.find('.');
    const std::string_view whole = s.substr(0, dot), frac = dot == std::string_view::npos ? std::string_view{} : s.substr(dot + 1);
    if (whole.empty() || !std::ranges::all_of(whole, [](char c) { return c >= '0' && c <= '9'; }) ||
        !std::ranges::all_of(frac, [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    Operands o;
    // i: the integer digits; beyond 64 bits only the last 18 matter to any rule (mod 10^k).
    const std::string_view tail = whole.size() > 18 ? whole.substr(whole.size() - 18) : whole;
    std::from_chars(tail.data(), tail.data() + tail.size(), o.i);
    o.v = static_cast<int>(frac.size());
    std::string_view trimmed = frac;
    while (!trimmed.empty() && trimmed.back() == '0') trimmed.remove_suffix(1);
    o.w = static_cast<int>(trimmed.size());
    const std::string_view f18 = frac.substr(0, std::min<std::size_t>(frac.size(), 18)), t18 = trimmed.substr(0, std::min<std::size_t>(trimmed.size(), 18));
    if (!f18.empty()) std::from_chars(f18.data(), f18.data() + f18.size(), o.f);
    if (!t18.empty()) std::from_chars(t18.data(), t18.data() + t18.size(), o.t);
    double n = 0;
    std::from_chars(s.data(), s.data() + s.size(), n);
    o.n = n;
    o.c = compact_exponent;
    return o;
}

Operands Operands::of(std::int64_t value) {
    Operands o;
    const std::uint64_t a = value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1 : static_cast<std::uint64_t>(value);
    o.n = static_cast<double>(a);
    o.i = a;
    return o;
}

std::expected<PluralRules, std::string> PluralRules::parse(std::span<const std::pair<Plural, std::string>> rules) {
    PluralRules out;
    for (const auto& [category, source] : rules) {
        // Samples (`@integer …`) are documentation.
        const std::string_view text = std::string_view(source).substr(0, source.find('@'));
        Rule rule;
        rule.category = category;
        Scan s{text};
        if (s.done()) {
            if (category != Plural::other) return std::unexpected(std::format("`{}` has no rule", name(category)));
            continue;  // other: whatever no rule took
        }
        auto fail = [&](std::string_view what) {
            return std::unexpected(std::format("the `{}` rule `{}`: {} at {}", name(category), text, what, s.at + 1));
        };
        std::vector<Relation> ands;
        while (true) {
            Relation r;
            s.space();
            if (s.at >= text.size() || std::string_view("nivwftce").find(text[s.at]) == std::string_view::npos) return fail("an operand (n i v w f t c e)");
            r.operand = text[s.at++];
            if (s.take("%") || s.take("mod")) {
                auto m = s.number();
                if (!m || *m == 0) return fail("a modulus");
                r.modulo = *m;
            }
            if (s.take("!=")) r.negated = true;
            else if (!s.take("=")) return fail("`=` or `!=`");
            do {
                auto lo = s.number();
                if (!lo) return fail("a value");
                std::uint64_t hi = *lo;
                if (s.take("..")) {
                    auto h = s.number();
                    if (!h || *h < *lo) return fail("the end of a range");
                    hi = *h;
                }
                r.ranges.emplace_back(*lo, hi);
            } while (s.take(","));
            ands.push_back(std::move(r));
            if (s.take("and")) continue;
            rule.any.push_back(std::move(ands));
            ands.clear();
            if (s.take("or")) continue;
            if (!s.done()) return fail("`and`, `or` or the end");
            break;
        }
        out.rules_.push_back(std::move(rule));
    }
    return out;
}

Plural PluralRules::select(const Operands& x) const noexcept {
    for (const Rule& r : rules_)
        for (const auto& ands : r.any)
            if (std::ranges::all_of(ands, [&](const Relation& rel) { return holds(rel, x); })) return r.category;
    return Plural::other;
}

std::vector<Plural> PluralRules::categories() const {
    std::vector<Plural> out;
    for (const Rule& r : rules_) out.push_back(r.category);
    out.push_back(Plural::other);
    return out;
}

}  // namespace omui::i18n
