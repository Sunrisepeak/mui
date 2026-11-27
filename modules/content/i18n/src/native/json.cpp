// Reading *.locale.xdef.json through mlibs.json — the one reader of JSON documents a program carries, so a program
// holds no second parser's tree for these: a malformed file is one diagnostic at its line and column; a pack's shape
// is checked whole, each problem by its path.
module mlibs.i18n.api;

import std;
import mlibs.json;

namespace mlibs::i18n {

namespace {

using json = mlibs::json::Node;

struct Reader {
    std::string_view        file;
    std::vector<Diagnostic> problems;

    void problem(std::string_view path, std::string what) { problems.push_back({std::string(file), 0, 0, std::format("{}: {}", path, std::move(what))}); }

    std::string text(const json& j, std::string_view key, std::string_view path, std::string_view fallback = {}) {
        const json v = j.is_object() ? j[key] : json{};
        if (!v) {
            if (fallback.empty()) problem(path, std::format("needs `{}`", key));
            return std::string(fallback);
        }
        auto s = v.text();
        if (!s) {
            problem(std::format("{}/{}", path, key), "is not a string");
            return std::string(fallback);
        }
        return std::move(*s);
    }

    template <std::size_t N>
    void texts(const json& j, std::string_view path, std::array<std::string, N>& out) {
        if (!j.is_array() || j.size() != N) {
            problem(path, std::format("is not a list of {} strings", N));
            return;
        }
        for (std::size_t i = 0; i < N; ++i) {
            if (auto s = j[i].text()) out[i] = std::move(*s);
            else problem(std::format("{}/{}", path, i), "is not a string");
        }
    }

    template <std::size_t N>
    void widths(const json& j, std::string_view path, std::array<std::array<std::string, N>, 3>& out) {
        if (!j.is_object()) {
            problem(path, "is not an object of widths (wide, abbreviated, narrow)");
            return;
        }
        constexpr std::array<std::string_view, 3> names{"wide", "abbreviated", "narrow"};
        for (std::size_t w = 0; w < 3; ++w) {
            const json width = j[names[w]];
            if (!width) {
                problem(path, std::format("needs `{}`", names[w]));
                continue;
            }
            texts(width, std::format("{}/{}", path, names[w]), out[w]);
        }
    }

    std::vector<std::pair<Plural, std::string>> by_plural(const json& j, std::string_view path) {
        std::vector<std::pair<Plural, std::string>> out;
        if (!j.is_object()) {
            problem(path, "is not an object of plural categories");
            return out;
        }
        for (const auto& [k, v] : j.members()) {
            auto p = parse_plural(k);
            auto s = v.text();
            if (!p) problem(path, std::format("`{}` is not a plural category (zero one two few many other)", k));
            else if (!s) problem(std::format("{}/{}", path, k), "is not a string");
            else out.emplace_back(*p, std::move(*s));
        }
        return out;
    }

    PluralRules rules(const json& j, std::string_view path) {
        auto r = PluralRules::parse(by_plural(j, path));
        if (!r) {
            problem(path, r.error());
            return {};
        }
        return std::move(*r);
    }

    std::vector<NumberData::Compact> compact(const json& j, std::string_view path) {
        std::vector<NumberData::Compact> out;
        if (!j.is_array()) {
            problem(path, "is not a list");
            return out;
        }
        for (std::size_t i = 0; i < j.size(); ++i) {
            const json e = j[i];
            const std::string at = std::format("{}/{}", path, i);
            NumberData::Compact c;
            const auto magnitude = e.is_object() && e["magnitude"].is_integer() ? e["magnitude"].integer() : std::nullopt;
            if (!magnitude) {
                problem(at, "needs an integer `magnitude`");
                continue;
            }
            c.magnitude = static_cast<int>(*magnitude);
            auto p = parse_plural(text(e, "count", at, "other"));
            c.count = p.value_or(Plural::other);
            c.pattern = text(e, "pattern", at);
            out.push_back(std::move(c));
        }
        return out;
    }

    int minutes(std::string_view hhmm, std::string_view path) {
        int h = 0, m = 0;
        if (hhmm.size() != 5 || hhmm[2] != ':' || std::from_chars(hhmm.data(), hhmm.data() + 2, h).ec != std::errc{} ||
            std::from_chars(hhmm.data() + 3, hhmm.data() + 5, m).ec != std::errc{} || h > 24 || m > 59) {
            problem(path, std::format("`{}` is not a time of day (HH:mm)", hhmm));
            return 0;
        }
        return h * 60 + m;
    }

    void read(const json& j, LocaleData& d) {
        const std::string tag = text(j, "locale", "");
        auto l = Locale::parse(tag);
        if (!l) problem("/locale", l.error());
        else d.locale = std::move(*l);
        d.cldr = text(j, "cldr", "", "?");
        if (const json p = j["plural"]) {
            if (const json cardinal = p["cardinal"]) d.cardinal = rules(cardinal, "/plural/cardinal");
            if (const json ordinal = p["ordinal"]) d.ordinal = rules(ordinal, "/plural/ordinal");
        }
        if (const json n = j["number"]) {
            NumberData& nd = d.number;
            if (const json s = n["symbols"]) {
                nd.decimal = text(s, "decimal", "/number/symbols", nd.decimal);
                nd.group = text(s, "group", "/number/symbols", nd.group);
                nd.minus = text(s, "minus", "/number/symbols", nd.minus);
                nd.plus = text(s, "plus", "/number/symbols", nd.plus);
                nd.percent = text(s, "percent", "/number/symbols", nd.percent);
                nd.exponential = text(s, "exponential", "/number/symbols", nd.exponential);
                nd.infinity = text(s, "infinity", "/number/symbols", nd.infinity);
                nd.nan = text(s, "nan", "/number/symbols", nd.nan);
            }
            nd.decimal_pattern = text(n, "decimal", "/number", nd.decimal_pattern);
            nd.percent_pattern = text(n, "percent", "/number", nd.percent_pattern);
            if (n["minimumGrouping"].is_integer()) nd.minimum_grouping = static_cast<int>(*n["minimumGrouping"].integer());
            if (const json c = n["compactShort"]) nd.compact_short = compact(c, "/number/compactShort");
            if (const json c = n["compactLong"]) nd.compact_long = compact(c, "/number/compactLong");
        }
        if (const json c = j["calendar"]) {
            CalendarData& cd = d.calendar;
            if (const json v = c["months"]) widths(v, "/calendar/months", cd.months);
            if (const json v = c["monthsStandalone"]) widths(v, "/calendar/monthsStandalone", cd.months_standalone);
            else cd.months_standalone = cd.months;
            if (const json v = c["days"]) widths(v, "/calendar/days", cd.days);
            if (const json v = c["daysStandalone"]) widths(v, "/calendar/daysStandalone", cd.days_standalone);
            else cd.days_standalone = cd.days;
            if (const json v = c["daysShort"]) texts(v, "/calendar/daysShort", cd.days_short);
            else cd.days_short = cd.days[1];
            if (const json am = c["am"]) cd.am = {text(am, "wide", "/calendar/am"), text(am, "abbreviated", "/calendar/am"), text(am, "narrow", "/calendar/am")};
            if (const json pm = c["pm"]) cd.pm = {text(pm, "wide", "/calendar/pm"), text(pm, "abbreviated", "/calendar/pm"), text(pm, "narrow", "/calendar/pm")};
            if (const json v = c["eras"]) texts(v, "/calendar/eras", cd.eras);
            constexpr std::array<std::string_view, 4> styles{"full", "long", "medium", "short"};
            for (auto [key, out] : {std::pair{"dateFormats", &cd.date_formats}, std::pair{"timeFormats", &cd.time_formats},
                                    std::pair{"dateTimeFormats", &cd.datetime_formats}}) {
                const json formats = c[key];
                if (!formats) {
                    problem("/calendar", std::format("needs `{}`", key));
                    continue;
                }
                for (std::size_t i = 0; i < 4; ++i) (*out)[i] = text(formats, styles[i], std::format("/calendar/{}", key));
            }
            if (const json skeletons = c["skeletons"]; skeletons.is_object())
                for (const auto& [k, v] : skeletons.members())
                    if (auto s = v.text()) cd.skeletons.emplace_back(k, std::move(*s));
            if (const json dp = c["dayPeriods"]) {
                if (const json rules = dp["rules"]; rules.is_array())
                    for (std::size_t i = 0; i < rules.size(); ++i) {
                        const json r = rules[i];
                        const std::string at = std::format("/calendar/dayPeriods/rules/{}", i);
                        CalendarData::Period p;
                        p.name = text(r, "name", at);
                        if (r["at"]) {
                            p.at = true;
                            p.from = p.before = minutes(text(r, "at", at), at);
                        } else {
                            p.from = minutes(text(r, "from", at), at);
                            p.before = minutes(text(r, "before", at), at);
                        }
                        cd.period_rules.push_back(std::move(p));
                    }
                if (const json names = dp["names"]; names.is_object())
                    for (const auto& [k, v] : names.members())
                        if (auto s = v.text()) cd.period_names.emplace_back(k, std::move(*s));
            }
            cd.gmt_format = text(c, "gmtFormat", "/calendar", cd.gmt_format);
            cd.gmt_zero = text(c, "gmtZeroFormat", "/calendar", cd.gmt_zero);
            cd.hour_format = text(c, "hourFormat", "/calendar", cd.hour_format);
        }
        if (const json r = j["relative"]) {
            constexpr std::array<std::string_view, 8> units{"year", "quarter", "month", "week", "day", "hour", "minute", "second"};
            constexpr std::array<std::string_view, 3> names{"wide", "abbreviated", "narrow"};
            for (std::size_t u = 0; u < units.size(); ++u) {
                const json unit = r[units[u]];
                if (!unit) continue;
                for (std::size_t w = 0; w < 3; ++w) {
                    const json e = unit[names[w]];
                    if (!e) continue;
                    const std::string at = std::format("/relative/{}/{}", units[u], names[w]);
                    RelativeData::Unit& out = d.relative.units[u][w];
                    if (const json named = e["named"]; named.is_object())
                        for (const auto& [k, v] : named.members()) {
                            int offset = 0;
                            auto s = v.text();
                            if (std::from_chars(k.data(), k.data() + k.size(), offset).ec != std::errc{} || !s) problem(at, std::format("`{}` is not an offset", k));
                            else out.named.emplace_back(offset, std::move(*s));
                        }
                    if (const json future = e["future"]) out.future = by_plural(future, at + "/future");
                    if (const json past = e["past"]) out.past = by_plural(past, at + "/past");
                }
            }
        }
        if (const json l = j["lists"]) {
            constexpr std::array<std::string_view, 3> types{"conjunction", "disjunction", "unit"};
            constexpr std::array<std::string_view, 3> names{"wide", "abbreviated", "narrow"};
            for (std::size_t t = 0; t < 3; ++t) {
                const json type = l[types[t]];
                if (!type) continue;
                for (std::size_t w = 0; w < 3; ++w) {
                    const json e = type[names[w]];
                    if (!e) continue;
                    const std::string at = std::format("/lists/{}/{}", types[t], names[w]);
                    ListData::Patterns& p = d.lists.types[t][w];
                    p.start = text(e, "start", at);
                    p.middle = text(e, "middle", at);
                    p.end = text(e, "end", at);
                    p.two = text(e, "two", at);
                }
            }
        }
    }
};

// A text that is not JSON: one diagnostic at its place.
Diagnostic not_json(const mlibs::json::Problem& problem, std::string_view file) {
    return {std::string(file), static_cast<int>(problem.line), static_cast<int>(problem.column), std::format("not JSON: {}", problem.message)};
}

}  // namespace

std::string Diagnostic::text() const {
    if (line > 0) return std::format("{}:{}:{}: {}", file, line, column, message);
    return std::format("{}: {}", file, message);
}

std::expected<LocaleData, std::vector<Diagnostic>> read_locale_data(std::string_view text, std::string_view file) {
    Reader r{file, {}};
    auto document = mlibs::json::Document::parse(text, std::string(file));
    if (!document) return std::unexpected(std::vector<Diagnostic>{not_json(document.error(), file)});
    const json j = document->root();
    if (!j.is_object() || j["format"].text() != "openmui.locale@0.1") {
        r.problem("/format", "must be \"openmui.locale@0.1\"");
        return std::unexpected(std::move(r.problems));
    }
    LocaleData d;
    r.read(j, d);
    if (!r.problems.empty()) return std::unexpected(std::move(r.problems));
    return d;
}

std::expected<LikelySubtags, std::vector<Diagnostic>> read_likely_subtags(std::string_view text, std::string_view file) {
    std::vector<Diagnostic> problems;
    auto document = mlibs::json::Document::parse(text, std::string(file));
    if (!document) return std::unexpected(std::vector<Diagnostic>{not_json(document.error(), file)});
    const json j = document->root();
    if (!j.is_object() || j["format"].text() != "openmui.likely@0.1" || !j["likely"].is_object()) {
        problems.push_back({std::string(file), 0, 0, "must be {\"format\": \"openmui.likely@0.1\", \"likely\": {…}}"});
        return std::unexpected(std::move(problems));
    }
    LikelySubtags out;
    for (const auto& [k, v] : j["likely"].members()) {
        const auto to = v.text();
        if (!to || !Locale::parse(k) || !Locale::parse(*to)) {
            problems.push_back({std::string(file), 0, 0, std::format("/likely/{}: not a tag mapped to a tag", k)});
            continue;
        }
        out.add(k, *to);
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return out;
}

}  // namespace mlibs::i18n
