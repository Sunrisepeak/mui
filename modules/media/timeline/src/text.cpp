// Times as text, and a problem's text.
module openmui.timeline;

import std;

namespace omui::timeline {

std::string Problem::text() const {
    std::string s = std::format("{}:{}:{}: {}", file, line, column, message);
    if (!hint.empty()) s += std::format(" ({})", hint);
    return s;
}

namespace {

// "123", "1.25" as a whole part and a fraction of `digits` places; nothing when it is not a plain decimal.
struct Decimal {
    std::int64_t whole = 0, fraction = 0, scale = 1;
};
std::optional<Decimal> decimal(std::string_view s) {
    Decimal d;
    const std::size_t dot = s.find('.');
    const std::string_view w = s.substr(0, dot), f = dot == std::string_view::npos ? std::string_view() : s.substr(dot + 1);
    if (w.empty() && f.empty()) return std::nullopt;
    for (const char c : w) {
        if (c < '0' || c > '9' || d.whole > 1'000'000'000) return std::nullopt;
        d.whole = d.whole * 10 + (c - '0');
    }
    if (dot != std::string_view::npos && f.empty()) return std::nullopt;
    for (const char c : f) {
        if (c < '0' || c > '9' || d.scale >= 1'000'000'000) return std::nullopt;
        d.fraction = d.fraction * 10 + (c - '0');
        d.scale *= 10;
    }
    return d;
}

// `d` units of `unit` flicks each, exactly; nothing when it falls between flicks.
std::optional<Flicks> exactly(const Decimal& d, Flicks unit) {
    const Flicks whole = d.whole * unit;
    const Flicks part = d.fraction * unit;  // fraction < 1e9, unit ≤ 705,600,000: within 64 bits
    if (part % d.scale != 0) return std::nullopt;
    return whole + part / d.scale;
}

}  // namespace

std::optional<Flicks> parse_time(std::string_view text, Rate rate) {
    bool negative = false;
    if (!text.empty() && text.front() == '-') negative = true, text.remove_prefix(1);
    std::optional<Flicks> t;
    auto ends = [&](std::string_view suffix) { return text.size() > suffix.size() && text.ends_with(suffix); };
    if (ends("fl")) {
        if (auto d = decimal(text.substr(0, text.size() - 2)); d && d->scale == 1) t = d->whole;
    } else if (ends("ms")) {
        if (auto d = decimal(text.substr(0, text.size() - 2))) t = exactly(*d, flicks_per_second / 1000);
    } else if (ends("f")) {
        if (auto d = decimal(text.substr(0, text.size() - 1)); d && d->scale == 1 && exact(rate)) t = d->whole * frame_length(rate);
    } else if (ends("s")) {
        if (auto d = decimal(text.substr(0, text.size() - 1))) t = exactly(*d, flicks_per_second);
    }
    if (!t) return std::nullopt;
    return negative ? -*t : *t;
}

std::string time_text(Flicks t, Rate rate) {
    const std::string sign = t < 0 ? "-" : "";
    const Flicks a = t < 0 ? -t : t;
    if (exact(rate) && a % frame_length(rate) == 0) return std::format("{}{}f", sign, a / frame_length(rate));
    // Seconds, when a few decimal places say it exactly.
    const Flicks whole = a / flicks_per_second, rest = a % flicks_per_second;
    std::int64_t scale = 1, fraction = 0;
    for (int places = 1; places <= 9; ++places) {
        scale *= 10;
        if ((rest * scale) % flicks_per_second == 0) {
            fraction = rest * scale / flicks_per_second;
            return std::format("{}{}.{:0{}}s", sign, whole, fraction, places);
        }
    }
    return std::format("{}{}fl", sign, a);
}

}  // namespace omui::timeline
