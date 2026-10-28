module openmui.i18n;

import std;

namespace omui::i18n {

namespace {

// ── numbers ──────────────────────────────────────────────────────────────

// A CLDR decimal pattern's parts (`#,##0.###`, `#,##0%`, `-#,##0.00`).
struct Pattern {
    std::string prefix, suffix, negative_prefix, negative_suffix;
    bool        negative_given = false;
    int         minimum_integer = 1, minimum_fraction = 0, maximum_fraction = 3;
    int         primary = 0, secondary = 0;  // 0: no grouping
    bool        percent = false;
};

Pattern parse_pattern(std::string_view text) {
    Pattern p;
    const std::size_t semi = text.find(';');
    const std::string_view positive = text.substr(0, semi);
    auto split = [&](std::string_view part, std::string& prefix, std::string& suffix, bool numbers) {
        std::size_t first = part.find_first_of("#0,.");
        std::size_t last = part.find_last_of("#0,.");
        if (first == std::string_view::npos) {
            prefix = std::string(part);
            return;
        }
        prefix = std::string(part.substr(0, first));
        suffix = std::string(part.substr(last + 1));
        if (!numbers) return;
        const std::string_view body = part.substr(first, last - first + 1);
        const std::size_t dot = body.find('.');
        const std::string_view whole = body.substr(0, dot), frac = dot == std::string_view::npos ? std::string_view{} : body.substr(dot + 1);
        p.minimum_integer = static_cast<int>(std::ranges::count(whole, '0'));
        p.minimum_fraction = static_cast<int>(std::ranges::count(frac, '0'));
        p.maximum_fraction = static_cast<int>(std::ranges::count_if(frac, [](char c) { return c == '0' || c == '#'; }));
        const std::size_t c1 = whole.rfind(',');
        if (c1 != std::string_view::npos) {
            p.primary = static_cast<int>(whole.size() - c1 - 1);
            const std::size_t c2 = whole.rfind(',', c1 == 0 ? 0 : c1 - 1);
            p.secondary = c2 != std::string_view::npos && c2 < c1 ? static_cast<int>(c1 - c2 - 1) : p.primary;
        }
    };
    split(positive, p.prefix, p.suffix, true);
    if (semi != std::string_view::npos) {
        p.negative_given = true;
        split(text.substr(semi + 1), p.negative_prefix, p.negative_suffix, false);
    }
    p.percent = p.prefix.find('%') != std::string::npos || p.suffix.find('%') != std::string::npos;
    return p;
}

// Pattern characters as the locale's symbols (%, -, +), quoted text as written.
std::string affix(std::string_view text, const NumberData& n) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\'') {
            const std::size_t end = text.find('\'', i + 1);
            if (end == i + 1) out += '\'';
            else out += text.substr(i + 1, (end == std::string_view::npos ? text.size() : end) - i - 1);
            i = end == std::string_view::npos ? text.size() : end;
        } else if (c == '%') {
            out += n.percent;
        } else if (c == '-') {
            out += n.minus;
        } else if (c == '+') {
            out += n.plus;
        } else {
            out += c;
        }
    }
    return out;
}

// The ASCII digits of |value| with `max_fraction` digits, correctly rounded
// (the standard library's exact conversion), trailing zeros dropped down to
// `min_fraction`.
std::string ascii_digits(double value, int min_fraction, int max_fraction) {
    std::array<char, 400> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), std::fabs(value), std::chars_format::fixed, std::clamp(max_fraction, 0, 30));
    std::string s(buf.data(), r.ptr);
    if (const std::size_t dot = s.find('.'); dot != std::string::npos) {
        std::size_t keep = s.size();
        while (keep > dot + 1 + static_cast<std::size_t>(min_fraction) && s[keep - 1] == '0') --keep;
        if (keep == dot + 1) keep = dot;  // no fraction left
        s.resize(keep);
    }
    if (min_fraction > 0) {
        if (s.find('.') == std::string::npos) s += '.';
        const std::size_t have = s.size() - s.find('.') - 1;
        if (have < static_cast<std::size_t>(min_fraction)) s.append(static_cast<std::size_t>(min_fraction) - have, '0');
    }
    return s;
}

// 10^e, exactly (every power to 10^22 is a double).
double power_of_ten(int e) noexcept {
    constexpr std::array<double, 23> table{1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                           1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    return table[static_cast<std::size_t>(std::clamp(e, 0, 22))];
}

bool all_zero(std::string_view digits) noexcept {
    return std::ranges::all_of(digits, [](char c) { return c == '0' || c == '.'; });
}

// ASCII digits as the locale writes them: its separators, grouped.
std::string localise(std::string_view ascii, const NumberData& n, int min_integer, int primary, int secondary, int min_grouping) {
    const std::size_t dot = ascii.find('.');
    std::string whole(ascii.substr(0, dot));
    if (static_cast<int>(whole.size()) < min_integer) whole.insert(0, static_cast<std::size_t>(min_integer) - whole.size(), '0');
    std::string out;
    const int len = static_cast<int>(whole.size());
    if (primary > 0 && len >= primary + min_grouping) {
        // Group sizes from the right: primary, then secondary.
        std::vector<int> cuts;
        int at = len - primary;
        while (at > 0) {
            cuts.push_back(at);
            at -= secondary > 0 ? secondary : primary;
        }
        int prev = 0;
        for (auto it = cuts.rbegin(); it != cuts.rend(); ++it) {
            out += whole.substr(static_cast<std::size_t>(prev), static_cast<std::size_t>(*it - prev));
            out += n.group;
            prev = *it;
        }
        out += whole.substr(static_cast<std::size_t>(prev));
    } else {
        out = whole;
    }
    if (dot != std::string_view::npos) {
        out += n.decimal;
        out += ascii.substr(dot + 1);
    }
    return out;
}

// ── calendars ────────────────────────────────────────────────────────────

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's algorithm).
std::int64_t days_from_civil(std::int64_t y, int m, int d) noexcept {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<std::int64_t>(y - era * 400);
    const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civil_from_days(std::int64_t z, int& y, int& m, int& d) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<std::int64_t>(z - era * 146097);
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2));
}

std::string pad(std::int64_t v, int width) {
    std::string s = std::to_string(v < 0 ? -v : v);
    if (static_cast<int>(s.size()) < width) s.insert(0, static_cast<std::size_t>(width - static_cast<int>(s.size())), '0');
    return v < 0 ? "-" + s : s;
}

std::string replace_all(std::string text, std::string_view what, std::string_view with) {
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + with.size())) text.replace(at, what.size(), with);
    return text;
}

// {0} and {1} of a CLDR pattern (date-time and list patterns), in one pass
// so a value that contains "{1}" is not replaced again.
std::string substitute(std::string_view pattern, std::string_view zero, std::string_view one) {
    std::string out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern.substr(i, 3) == "{0}") {
            out += zero;
            i += 2;
        } else if (pattern.substr(i, 3) == "{1}") {
            out += one;
            i += 2;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

}  // namespace

// ── CivilTime ────────────────────────────────────────────────────────────

CivilTime CivilTime::from_unix(std::int64_t seconds, int offset_minutes, int millisecond) {
    const std::int64_t local = seconds + static_cast<std::int64_t>(offset_minutes) * 60;
    std::int64_t days = local / 86400, rest = local % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    CivilTime t;
    civil_from_days(days, t.year, t.month, t.day);
    t.hour = static_cast<int>(rest / 3600);
    t.minute = static_cast<int>(rest % 3600 / 60);
    t.second = static_cast<int>(rest % 60);
    t.millisecond = millisecond;
    t.offset_minutes = offset_minutes;
    return t;
}

int CivilTime::weekday() const noexcept {
    const std::int64_t z = days_from_civil(year, month, day);
    return static_cast<int>(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);  // 1970-01-01 was a Thursday
}

// ── Formatter ────────────────────────────────────────────────────────────

Formatter::Formatter(std::shared_ptr<const LocaleData> data) : data_(std::move(data)) {}

Plural Formatter::plural(const Operands& x, bool ordinal) const noexcept { return (ordinal ? data_->ordinal : data_->cardinal).select(x); }

FormattedNumber Formatter::number(double value, const NumberOptions& o) const {
    const NumberData& n = data_->number;
    FormattedNumber out;
    if (std::isnan(value)) {
        out.text = n.nan;
        return out;
    }
    const Pattern p = parse_pattern(o.style == NumberOptions::Style::percent ? n.percent_pattern : n.decimal_pattern);
    const bool negative_in = std::signbit(value);
    if (std::isinf(value)) {
        out.text = std::format("{}{}", negative_in && o.sign != NumberOptions::Sign::never ? n.minus : "", n.infinity);
        return out;
    }
    const bool compact = o.style == NumberOptions::Style::compact_short || o.style == NumberOptions::Style::compact_long;
    double v = o.style == NumberOptions::Style::percent ? value * 100 : value;
    int min_frac = o.minimum_fraction.value_or(p.minimum_fraction);
    int max_frac = std::max(min_frac, o.maximum_fraction.value_or(p.maximum_fraction));
    int min_grouping = n.minimum_grouping;
    std::string ascii;
    const NumberData::Compact* form = nullptr;
    int exponent = 0;
    if (compact) {
        const auto& table = o.style == NumberOptions::Style::compact_long && !n.compact_long.empty() ? n.compact_long : n.compact_short;
        min_grouping = std::max(min_grouping, 2);  // 1234, not 1,234, beside 12K (as CLDR's compact forms are shown)
        // The form of the largest power of ten the value reaches ("0": none).
        auto pick = [&](double magnitude) -> std::pair<int, const NumberData::Compact*> {
            const NumberData::Compact* best = nullptr;
            for (const auto& c : table)
                if (c.magnitude <= 22 && power_of_ten(c.magnitude) <= magnitude && (!best || c.magnitude > best->magnitude)) best = &c;
            if (!best || best->pattern.find('0') == std::string::npos || best->pattern == "0") return {0, nullptr};
            return {best->magnitude - static_cast<int>(std::ranges::count(best->pattern, '0')) + 1, best};
        };
        double reach = std::fabs(v);
        for (int pass = 0; pass < 2; ++pass) {
            std::tie(exponent, form) = pick(reach);
            const double scaled = std::fabs(v) / power_of_ten(exponent);
            // Two significant digits at least: 1.2K, 12K, 123K; 1.5; 999.
            max_frac = o.maximum_fraction.value_or(scaled < 10 ? 1 : 0);
            min_frac = std::min(o.minimum_fraction.value_or(0), max_frac);
            ascii = ascii_digits(scaled, min_frac, max_frac);
            double shown = 0;
            std::from_chars(ascii.data(), ascii.data() + ascii.size(), shown);
            shown *= power_of_ten(exponent);
            if (pass == 0 && pick(shown).first != exponent) {  // 999,950 is 1M, not 1000K
                reach = shown;
                continue;
            }
            break;
        }
    } else {
        ascii = ascii_digits(v, min_frac, max_frac);
    }
    out.operands = Operands::of(ascii, exponent).value_or(Operands{});
    const bool negative = negative_in && !all_zero(ascii);
    std::string digits = localise(ascii, n, o.minimum_integer.value_or(p.minimum_integer), o.grouping ? p.primary : 0, p.secondary, min_grouping);
    if (form) {
        // The plural form the shown number takes (other when the locale has no other), its run of zeros the number.
        const auto& table = o.style == NumberOptions::Style::compact_long && !n.compact_long.empty() ? n.compact_long : n.compact_short;
        const Plural cat = data_->cardinal.select(out.operands);
        const NumberData::Compact* chosen = form;
        for (const auto& c : table)
            if (c.magnitude == form->magnitude && c.count == cat) chosen = &c;
        const std::string_view pat = chosen->pattern;
        const std::size_t first = pat.find('0'), last = pat.find_last_of('0');
        digits = affix(pat.substr(0, first), n) + digits + affix(pat.substr(last + 1), n);
    }
    std::string sign;
    if (negative && o.sign != NumberOptions::Sign::never) sign = n.minus;
    else if (!negative && o.sign == NumberOptions::Sign::always) sign = n.plus;
    if (negative && p.negative_given && o.sign != NumberOptions::Sign::never)
        out.text = affix(p.negative_prefix, n) + digits + affix(p.negative_suffix, n);
    else
        out.text = sign + affix(p.prefix, n) + digits + affix(p.suffix, n);
    return out;
}

FormattedNumber Formatter::number(std::int64_t value, const NumberOptions& o) const {
    // Exact for every 64-bit value: the digits are the integer's own.
    if (o.style != NumberOptions::Style::decimal) return number(static_cast<double>(value), o);
    const NumberData& n = data_->number;
    const Pattern p = parse_pattern(n.decimal_pattern);
    const std::uint64_t a = value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1 : static_cast<std::uint64_t>(value);
    std::string ascii = std::to_string(a);
    const int min_frac = o.minimum_fraction.value_or(0);
    if (min_frac > 0) ascii += "." + std::string(static_cast<std::size_t>(min_frac), '0');
    FormattedNumber out;
    out.operands = Operands::of(ascii).value_or(Operands{});
    std::string sign;
    if (value < 0 && o.sign != NumberOptions::Sign::never) sign = n.minus;
    else if (value >= 0 && o.sign == NumberOptions::Sign::always) sign = n.plus;
    out.text = sign + affix(p.prefix, n) + localise(ascii, n, o.minimum_integer.value_or(p.minimum_integer), o.grouping ? p.primary : 0, p.secondary, n.minimum_grouping) +
               affix(p.suffix, n);
    return out;
}

// ── dates ────────────────────────────────────────────────────────────────

std::string Formatter::pattern(const CivilTime& t, std::string_view pat) const {
    const CalendarData& c = data_->calendar;
    auto width_of = [](int count) { return count == 4 ? Width::wide : count == 5 ? Width::narrow : Width::abbreviated; };
    auto gmt = [&](bool long_form) {
        if (t.offset_minutes == 0) return c.gmt_zero;
        const std::string_view hf = c.hour_format;
        const std::size_t semi = hf.find(';');
        const std::string_view part = t.offset_minutes > 0 ? hf.substr(0, semi) : hf.substr(semi == std::string_view::npos ? 0 : semi + 1);
        const int h = std::abs(t.offset_minutes) / 60, m = std::abs(t.offset_minutes) % 60;
        std::string off;
        if (long_form) {
            off = replace_all(replace_all(std::string(part), "HH", pad(h, 2)), "mm", pad(m, 2));
        } else {
            // The short form: GMT+8, GMT+5:30.
            off = std::string(1, part.empty() ? '+' : part[0]) + std::to_string(h) + (m ? ":" + pad(m, 2) : "");
        }
        return replace_all(c.gmt_format, "{0}", off);
    };
    std::string out;
    for (std::size_t i = 0; i < pat.size();) {
        const char ch = pat[i];
        if (ch == '\'') {
            if (i + 1 < pat.size() && pat[i + 1] == '\'') {
                out += '\'';
                i += 2;
                continue;
            }
            // Quoted text, as written; '' inside it is a quote.
            std::size_t k = i + 1;
            for (; k < pat.size(); ++k) {
                if (pat[k] != '\'') {
                    out += pat[k];
                } else if (k + 1 < pat.size() && pat[k + 1] == '\'') {
                    out += '\'';
                    ++k;
                } else {
                    break;
                }
            }
            i = k + 1;
            continue;
        }
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
            out += ch;
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < pat.size() && pat[j] == ch) ++j;
        const int count = static_cast<int>(j - i);
        i = j;
        const int h12 = t.hour % 12 == 0 ? 12 : t.hour % 12;
        switch (ch) {
            case 'G': out += c.eras[t.year > 0 ? 1 : 0]; break;
            case 'y':
            case 'Y':
            case 'u': out += count == 2 ? pad(((t.year % 100) + 100) % 100, 2) : pad(t.year, count); break;
            case 'M':
            case 'L': {
                const auto& names = ch == 'M' ? c.months : c.months_standalone;
                if (count <= 2) out += pad(t.month, count);
                else out += names[static_cast<std::size_t>(width_of(count))][static_cast<std::size_t>(std::clamp(t.month, 1, 12) - 1)];
                break;
            }
            case 'd': out += pad(t.day, count); break;
            case 'E':
            case 'c':
            case 'e': {
                if ((ch == 'c' || ch == 'e') && count <= 2) {
                    out += pad(t.weekday() + 1, count);  // the weekday's number, Sunday first
                    break;
                }
                const auto& names = ch == 'E' ? c.days : c.days_standalone;
                if (count == 6) out += c.days_short[static_cast<std::size_t>(t.weekday())];
                else out += names[static_cast<std::size_t>(width_of(count <= 3 ? 3 : count))][static_cast<std::size_t>(t.weekday())];
                break;
            }
            case 'a': out += (t.hour < 12 ? c.am : c.pm)[static_cast<std::size_t>(width_of(count))]; break;
            case 'b':
            case 'B': {
                // The day period the rules give this time of day; am/pm when none.
                const int minutes = t.hour * 60 + t.minute;
                std::string name;
                for (const auto& r : c.period_rules) {
                    const bool in = r.at ? minutes == r.from && t.second == 0
                                         : (r.from <= r.before ? minutes >= r.from && minutes < r.before : minutes >= r.from || minutes < r.before);
                    if (!in || (ch == 'b' && r.name != "noon" && r.name != "midnight")) continue;
                    for (const auto& [k, v] : c.period_names)
                        if (k == r.name) name = v;
                    if (!name.empty()) break;
                }
                out += name.empty() ? (t.hour < 12 ? c.am : c.pm)[static_cast<std::size_t>(width_of(count))] : name;
                break;
            }
            case 'h': out += pad(h12, count); break;
            case 'H': out += pad(t.hour, count); break;
            case 'K': out += pad(t.hour % 12, count); break;
            case 'k': out += pad(t.hour == 0 ? 24 : t.hour, count); break;
            case 'm': out += pad(t.minute, count); break;
            case 's': out += pad(t.second, count); break;
            case 'S': out += pad(t.millisecond, 3).substr(0, static_cast<std::size_t>(std::min(count, 3))) + std::string(static_cast<std::size_t>(std::max(0, count - 3)), '0'); break;
            case 'z':
            case 'v':
            case 'V':
            case 'O': out += gmt(count >= 4); break;
            case 'Z':
            case 'x':
            case 'X': {
                if (t.offset_minutes == 0 && ch == 'X') {
                    out += 'Z';
                    break;
                }
                const int h = std::abs(t.offset_minutes) / 60, m = std::abs(t.offset_minutes) % 60;
                out += std::format("{}{}{}{}", t.offset_minutes < 0 ? '-' : '+', pad(h, 2), count >= 3 && ch != 'Z' ? ":" : "", pad(m, 2));
                break;
            }
            default: out.append(static_cast<std::size_t>(count), ch);  // a field not supported: as written
        }
    }
    return out;
}

std::string Formatter::date(const CivilTime& t, DateStyle s) const { return pattern(t, data_->calendar.date_formats[static_cast<std::size_t>(s)]); }
std::string Formatter::time(const CivilTime& t, DateStyle s) const { return pattern(t, data_->calendar.time_formats[static_cast<std::size_t>(s)]); }

std::string Formatter::datetime(const CivilTime& t, DateStyle ds, DateStyle ts) const {
    return substitute(data_->calendar.datetime_formats[static_cast<std::size_t>(ds)], time(t, ts), date(t, ds));
}

std::optional<std::string> Formatter::skeleton(const CivilTime& t, std::string_view skeleton) const {
    for (const auto& [k, v] : data_->calendar.skeletons)
        if (k == skeleton) return pattern(t, v);
    return std::nullopt;
}

// ── relative times and lists ─────────────────────────────────────────────

std::string Formatter::relative(double value, TimeUnit unit, Width width, bool named) const {
    const RelativeData::Unit& u = data_->relative.units[static_cast<std::size_t>(unit)][static_cast<std::size_t>(width)];
    if (named && value == std::floor(value) && std::fabs(value) < 1e6)
        for (const auto& [offset, text] : u.named)
            if (offset == static_cast<int>(value)) return text;
    const bool past = std::signbit(value);
    const FormattedNumber n = number(std::fabs(value), NumberOptions{.maximum_fraction = 3});
    const Plural cat = data_->cardinal.select(n.operands);
    const auto& patterns = past ? u.past : u.future;
    std::string_view chosen;
    for (const auto& [p, text] : patterns)
        if (p == cat) chosen = text;
    if (chosen.empty())
        for (const auto& [p, text] : patterns)
            if (p == Plural::other) chosen = text;
    return substitute(chosen, n.text, "");
}

std::string Formatter::list(std::span<const std::string> items, ListType type, Width width) const {
    const ListData::Patterns& p = data_->lists.types[static_cast<std::size_t>(type)][static_cast<std::size_t>(width)];
    if (items.empty()) return {};
    if (items.size() == 1) return items[0];
    if (items.size() == 2) return substitute(p.two, items[0], items[1]);
    // From the right: end joins the last two, middle each before, start the first.
    std::string tail = substitute(p.end, items[items.size() - 2], items.back());
    for (std::size_t i = items.size() - 2; i-- > 1;) tail = substitute(p.middle, items[i], tail);
    return substitute(p.start, items[0], tail);
}

}  // namespace omui::i18n
