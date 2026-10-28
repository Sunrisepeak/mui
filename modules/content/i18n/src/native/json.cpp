// Reading *.locale.omui.json: nlohmann/json reports a malformed file by
// throwing, which this unit — the library's adapter — turns into a
// diagnostic; a pack's shape is checked whole, each problem by its path.
module openmui.i18n;

import std;
import nlohmann.json;

namespace omui::i18n {

namespace {

using json = nlohmann::json;

struct Reader {
    std::string_view        file;
    std::vector<Diagnostic> problems;

    void problem(std::string_view path, std::string what) { problems.push_back({std::string(file), 0, 0, std::format("{}: {}", path, std::move(what))}); }

    std::string text(const json& j, std::string_view key, std::string_view path, std::string_view fallback = {}) {
        if (!j.is_object() || !j.contains(key)) {
            if (fallback.empty()) problem(path, std::format("needs `{}`", key));
            return std::string(fallback);
        }
        const json& v = j.at(key);
        if (!v.is_string()) {
            problem(std::format("{}/{}", path, key), "is not a string");
            return std::string(fallback);
        }
        return v.get<std::string>();
    }

    template <std::size_t N>
    void texts(const json& j, std::string_view path, std::array<std::string, N>& out) {
        if (!j.is_array() || j.size() != N) {
            problem(path, std::format("is not a list of {} strings", N));
            return;
        }
        for (std::size_t i = 0; i < N; ++i) {
            if (!j[i].is_string()) problem(std::format("{}/{}", path, i), "is not a string");
            else out[i] = j[i].get<std::string>();
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
            if (!j.contains(names[w])) {
                problem(path, std::format("needs `{}`", names[w]));
                continue;
            }
            texts(j.at(names[w]), std::format("{}/{}", path, names[w]), out[w]);
        }
    }

    std::vector<std::pair<Plural, std::string>> by_plural(const json& j, std::string_view path) {
        std::vector<std::pair<Plural, std::string>> out;
        if (!j.is_object()) {
            problem(path, "is not an object of plural categories");
            return out;
        }
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string k = it.key();
            const json& v = it.value();
            auto p = parse_plural(k);
            if (!p) problem(path, std::format("`{}` is not a plural category (zero one two few many other)", k));
            else if (!v.is_string()) problem(std::format("{}/{}", path, k), "is not a string");
            else out.emplace_back(*p, v.get<std::string>());
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
            const json& e = j[i];
            const std::string at = std::format("{}/{}", path, i);
            NumberData::Compact c;
            if (!e.is_object() || !e.contains("magnitude") || !e.at("magnitude").is_number_integer()) {
                problem(at, "needs an integer `magnitude`");
                continue;
            }
            c.magnitude = e.at("magnitude").get<int>();
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
        if (j.contains("plural")) {
            const json& p = j.at("plural");
            if (p.contains("cardinal")) d.cardinal = rules(p.at("cardinal"), "/plural/cardinal");
            if (p.contains("ordinal")) d.ordinal = rules(p.at("ordinal"), "/plural/ordinal");
        }
        if (j.contains("number")) {
            const json& n = j.at("number");
            NumberData& nd = d.number;
            if (n.contains("symbols")) {
                const json& s = n.at("symbols");
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
            if (n.contains("minimumGrouping") && n.at("minimumGrouping").is_number_integer()) nd.minimum_grouping = n.at("minimumGrouping").get<int>();
            if (n.contains("compactShort")) nd.compact_short = compact(n.at("compactShort"), "/number/compactShort");
            if (n.contains("compactLong")) nd.compact_long = compact(n.at("compactLong"), "/number/compactLong");
        }
        if (j.contains("calendar")) {
            const json& c = j.at("calendar");
            CalendarData& cd = d.calendar;
            if (c.contains("months")) widths(c.at("months"), "/calendar/months", cd.months);
            if (c.contains("monthsStandalone")) widths(c.at("monthsStandalone"), "/calendar/monthsStandalone", cd.months_standalone);
            else cd.months_standalone = cd.months;
            if (c.contains("days")) widths(c.at("days"), "/calendar/days", cd.days);
            if (c.contains("daysStandalone")) widths(c.at("daysStandalone"), "/calendar/daysStandalone", cd.days_standalone);
            else cd.days_standalone = cd.days;
            if (c.contains("daysShort")) texts(c.at("daysShort"), "/calendar/daysShort", cd.days_short);
            else cd.days_short = cd.days[1];
            if (c.contains("am")) {
                const json& am = c.at("am");
                cd.am = {text(am, "wide", "/calendar/am"), text(am, "abbreviated", "/calendar/am"), text(am, "narrow", "/calendar/am")};
            }
            if (c.contains("pm")) {
                const json& pm = c.at("pm");
                cd.pm = {text(pm, "wide", "/calendar/pm"), text(pm, "abbreviated", "/calendar/pm"), text(pm, "narrow", "/calendar/pm")};
            }
            if (c.contains("eras")) texts(c.at("eras"), "/calendar/eras", cd.eras);
            constexpr std::array<std::string_view, 4> styles{"full", "long", "medium", "short"};
            for (auto [key, out] : {std::pair{"dateFormats", &cd.date_formats}, std::pair{"timeFormats", &cd.time_formats},
                                    std::pair{"dateTimeFormats", &cd.datetime_formats}}) {
                if (!c.contains(key)) {
                    problem("/calendar", std::format("needs `{}`", key));
                    continue;
                }
                for (std::size_t i = 0; i < 4; ++i) (*out)[i] = text(c.at(key), styles[i], std::format("/calendar/{}", key));
            }
            if (c.contains("skeletons") && c.at("skeletons").is_object())
                for (auto it = c.at("skeletons").begin(); it != c.at("skeletons").end(); ++it) {
                    const std::string k = it.key();
                    const json& v = it.value();
                    if (v.is_string()) cd.skeletons.emplace_back(k, v.get<std::string>());
                }
            if (c.contains("dayPeriods")) {
                const json& dp = c.at("dayPeriods");
                if (dp.contains("rules") && dp.at("rules").is_array())
                    for (std::size_t i = 0; i < dp.at("rules").size(); ++i) {
                        const json& r = dp.at("rules")[i];
                        const std::string at = std::format("/calendar/dayPeriods/rules/{}", i);
                        CalendarData::Period p;
                        p.name = text(r, "name", at);
                        if (r.contains("at")) {
                            p.at = true;
                            p.from = p.before = minutes(text(r, "at", at), at);
                        } else {
                            p.from = minutes(text(r, "from", at), at);
                            p.before = minutes(text(r, "before", at), at);
                        }
                        cd.period_rules.push_back(std::move(p));
                    }
                if (dp.contains("names") && dp.at("names").is_object())
                    for (auto it = dp.at("names").begin(); it != dp.at("names").end(); ++it) {
                        const std::string k = it.key();
                        const json& v = it.value();
                        if (v.is_string()) cd.period_names.emplace_back(k, v.get<std::string>());
                    }
            }
            cd.gmt_format = text(c, "gmtFormat", "/calendar", cd.gmt_format);
            cd.gmt_zero = text(c, "gmtZeroFormat", "/calendar", cd.gmt_zero);
            cd.hour_format = text(c, "hourFormat", "/calendar", cd.hour_format);
        }
        if (j.contains("relative")) {
            constexpr std::array<std::string_view, 8> units{"year", "quarter", "month", "week", "day", "hour", "minute", "second"};
            constexpr std::array<std::string_view, 3> names{"wide", "abbreviated", "narrow"};
            const json& r = j.at("relative");
            for (std::size_t u = 0; u < units.size(); ++u) {
                if (!r.contains(units[u])) continue;
                for (std::size_t w = 0; w < 3; ++w) {
                    if (!r.at(units[u]).contains(names[w])) continue;
                    const json& e = r.at(units[u]).at(names[w]);
                    const std::string at = std::format("/relative/{}/{}", units[u], names[w]);
                    RelativeData::Unit& out = d.relative.units[u][w];
                    if (e.contains("named") && e.at("named").is_object())
                        for (auto it = e.at("named").begin(); it != e.at("named").end(); ++it) {
                            const std::string k = it.key();
                            const json& v = it.value();
                            int offset = 0;
                            if (std::from_chars(k.data(), k.data() + k.size(), offset).ec != std::errc{} || !v.is_string()) problem(at, std::format("`{}` is not an offset", k));
                            else out.named.emplace_back(offset, v.get<std::string>());
                        }
                    if (e.contains("future")) out.future = by_plural(e.at("future"), at + "/future");
                    if (e.contains("past")) out.past = by_plural(e.at("past"), at + "/past");
                }
            }
        }
        if (j.contains("lists")) {
            constexpr std::array<std::string_view, 3> types{"conjunction", "disjunction", "unit"};
            constexpr std::array<std::string_view, 3> names{"wide", "abbreviated", "narrow"};
            const json& l = j.at("lists");
            for (std::size_t t = 0; t < 3; ++t) {
                if (!l.contains(types[t])) continue;
                for (std::size_t w = 0; w < 3; ++w) {
                    if (!l.at(types[t]).contains(names[w])) continue;
                    const json& e = l.at(types[t]).at(names[w]);
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

}  // namespace

std::string Diagnostic::text() const {
    if (line > 0) return std::format("{}:{}:{}: {}", file, line, column, message);
    return std::format("{}: {}", file, message);
}

std::expected<LocaleData, std::vector<Diagnostic>> read_locale_data(std::string_view text, std::string_view file) {
    Reader r{file, {}};
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        r.problems.push_back({std::string(file), 0, 0, std::format("not JSON (byte {}): {}", e.byte, e.what())});
        return std::unexpected(std::move(r.problems));
    }
    if (!j.is_object() || !j.contains("format") || j.at("format") != "openmui.locale@0.1") {
        r.problem("/format", "must be \"openmui.locale@0.1\"");
        return std::unexpected(std::move(r.problems));
    }
    LocaleData d;
    try {
        r.read(j, d);
    } catch (const json::exception& e) {
        r.problems.push_back({std::string(file), 0, 0, std::format("a value of the wrong type: {}", e.what())});
    }
    if (!r.problems.empty()) return std::unexpected(std::move(r.problems));
    return d;
}

std::expected<LikelySubtags, std::vector<Diagnostic>> read_likely_subtags(std::string_view text, std::string_view file) {
    std::vector<Diagnostic> problems;
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        problems.push_back({std::string(file), 0, 0, std::format("not JSON (byte {}): {}", e.byte, e.what())});
        return std::unexpected(std::move(problems));
    }
    if (!j.is_object() || !j.contains("format") || j.at("format") != "openmui.likely@0.1" || !j.contains("likely") || !j.at("likely").is_object()) {
        problems.push_back({std::string(file), 0, 0, "must be {\"format\": \"openmui.likely@0.1\", \"likely\": {…}}"});
        return std::unexpected(std::move(problems));
    }
    LikelySubtags out;
    for (auto it = j.at("likely").begin(); it != j.at("likely").end(); ++it) {
        const std::string k = it.key();
        const json& v = it.value();
        if (!v.is_string() || !Locale::parse(k) || !Locale::parse(v.get<std::string>())) {
            problems.push_back({std::string(file), 0, 0, std::format("/likely/{}: not a tag mapped to a tag", k)});
            continue;
        }
        out.add(k, v.get<std::string>());
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return out;
}

}  // namespace omui::i18n
