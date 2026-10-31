module openmui.gen.cldr;

import std;
import nlohmann.json;
import openmui.gen.model;  // generic_utf8

namespace omui::gen {

namespace {

using json = nlohmann::json;
using ordered = nlohmann::ordered_json;

std::expected<json, std::string> load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{} is not there", generic_utf8(file)));
    try {
        return json::parse(in);
    } catch (const json::exception& e) {
        return std::unexpected(std::format("{}: {}", generic_utf8(file), e.what()));
    }
}

// A value at a path of keys; an error says which key was missing.
std::expected<const json*, std::string> at(const json& j, std::initializer_list<std::string_view> path, std::string_view where) {
    const json* cur = &j;
    for (std::string_view k : path) {
        if (!cur->is_object() || !cur->contains(k)) return std::unexpected(std::format("{}: no `{}`", where, k));
        cur = &cur->at(k);
    }
    return cur;
}

// A rule without its samples: `i = 1 and v = 0 @integer 1` is `i = 1 and v = 0`.
std::string rule_text(std::string_view r) {
    r = r.substr(0, r.find('@'));
    while (!r.empty() && r.front() == ' ') r.remove_prefix(1);
    while (!r.empty() && r.back() == ' ') r.remove_suffix(1);
    return std::string(r);
}

ordered plural_rules(const json& table, const std::string& id) {
    const std::string lang = id.substr(0, id.find('-'));
    const json* rules = table.contains(id) ? &table.at(id) : table.contains(lang) ? &table.at(lang) : nullptr;
    ordered out = ordered::object();
    if (!rules) return out;
    constexpr std::string_view prefix = "pluralRule-count-";
    for (auto it = rules->begin(); it != rules->end(); ++it) {
        const std::string& k = it.key();
        if (!k.starts_with(prefix)) continue;
        const std::string category = k.substr(prefix.size());
        const std::string text = rule_text(it.value().get<std::string>());
        if (category == "other" && text.empty()) continue;
        out[category] = text;
    }
    return out;
}

// CLDR's "1000-count-one": "0K" as {magnitude 3, count one, pattern 0K}.
ordered compact(const json& formats) {
    ordered out = ordered::array();
    std::vector<std::tuple<int, std::string, std::string>> rows;
    for (auto it = formats.begin(); it != formats.end(); ++it) {
        const std::string& k = it.key();
        if (k.find("-alt-") != std::string::npos) continue;
        const std::size_t dash = k.find("-count-");
        if (dash == std::string::npos) continue;
        const std::string power = k.substr(0, dash);
        if (power.empty() || power[0] != '1' || power.find_first_not_of('0', 1) != std::string::npos) continue;
        rows.emplace_back(static_cast<int>(power.size() - 1), k.substr(dash + 7), it.value().get<std::string>());
    }
    std::ranges::sort(rows);
    for (const auto& [m, count, pattern] : rows) out.push_back(ordered{{"magnitude", m}, {"count", count}, {"pattern", pattern}});
    return out;
}

ordered names(const json& by_index, std::initializer_list<std::string_view> keys) {
    ordered out = ordered::array();
    for (std::string_view k : keys) out.push_back(by_index.contains(k) ? by_index.at(k).get<std::string>() : std::string());
    return out;
}

constexpr std::initializer_list<std::string_view> month_keys{"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};
constexpr std::initializer_list<std::string_view> day_keys{"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

ordered widths(const json& context, std::initializer_list<std::string_view> keys) {
    ordered out = ordered::object();
    for (std::string_view w : {"wide", "abbreviated", "narrow"}) out[std::string(w)] = names(context.at(w), keys);
    return out;
}

// A pattern that CLDR gives as text or as {"_value": …} (newer releases).
std::string pattern_of(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_object() && v.contains("_value")) return v.at("_value").get<std::string>();
    return {};
}

std::expected<std::string, std::string> pack(const std::filesystem::path& cldr, const std::string& id, std::string_view version, const json& plurals,
                                             const json& ordinals, const json& period_rules) {
    const std::string where = std::format("CLDR {}", id);
    auto numbers_file = load(cldr / "cldr-numbers-full" / "main" / id / "numbers.json");
    auto greg_file = load(cldr / "cldr-dates-full" / "main" / id / "ca-gregorian.json");
    auto zones_file = load(cldr / "cldr-dates-full" / "main" / id / "timeZoneNames.json");
    auto fields_file = load(cldr / "cldr-dates-full" / "main" / id / "dateFields.json");
    auto lists_file = load(cldr / "cldr-misc-full" / "main" / id / "listPatterns.json");
    for (const auto* f : {&numbers_file, &greg_file, &zones_file, &fields_file, &lists_file})
        if (!*f) return std::unexpected(f->error());
    auto numbers = at(*numbers_file, {"main", id, "numbers"}, where);
    auto greg = at(*greg_file, {"main", id, "dates", "calendars", "gregorian"}, where);
    auto zones = at(*zones_file, {"main", id, "dates", "timeZoneNames"}, where);
    auto fields = at(*fields_file, {"main", id, "dates", "fields"}, where);
    auto lists = at(*lists_file, {"main", id, "listPatterns"}, where);
    for (const auto* r : {&numbers, &greg, &zones, &fields, &lists})
        if (!*r) return std::unexpected(r->error());

    ordered out = ordered::object();
    out["format"] = "openmui.locale@0.1";
    out["locale"] = id;
    out["cldr"] = std::string(version);
    out["plural"] = ordered{{"cardinal", plural_rules(plurals, id)}, {"ordinal", plural_rules(ordinals, id)}};

    // Numbers: the Latin digits' symbols and patterns (openmui writes ASCII digits).
    const json& n = **numbers;
    const std::string system = "latn";
    auto symbols = at(n, {std::format("symbols-numberSystem-{}", system)}, where);
    auto decimal = at(n, {std::format("decimalFormats-numberSystem-{}", system)}, where);
    auto percent = at(n, {std::format("percentFormats-numberSystem-{}", system), "standard"}, where);
    if (!symbols || !decimal || !percent) return std::unexpected(!symbols ? symbols.error() : !decimal ? decimal.error() : percent.error());
    const json& s = **symbols;
    ordered num = ordered::object();
    num["symbols"] = ordered{{"decimal", s.at("decimal")},      {"group", s.at("group")},         {"minus", s.at("minusSign")},
                             {"plus", s.at("plusSign")},        {"percent", s.at("percentSign")}, {"exponential", s.at("exponential")},
                             {"infinity", s.at("infinity")},    {"nan", s.at("nan")}};
    num["decimal"] = (**decimal).at("standard");
    num["percent"] = **percent;
    int grouping = 1;
    if (n.contains("minimumGroupingDigits")) {
        const std::string g = n.at("minimumGroupingDigits").get<std::string>();
        std::from_chars(g.data(), g.data() + g.size(), grouping);
    }
    num["minimumGrouping"] = grouping;
    if ((**decimal).contains("short")) num["compactShort"] = compact((**decimal).at("short").at("decimalFormat"));
    if ((**decimal).contains("long")) num["compactLong"] = compact((**decimal).at("long").at("decimalFormat"));
    out["number"] = std::move(num);

    // The Gregorian calendar.
    const json& g = **greg;
    ordered cal = ordered::object();
    cal["months"] = widths(g.at("months").at("format"), month_keys);
    cal["monthsStandalone"] = widths(g.at("months").at("stand-alone"), month_keys);
    cal["days"] = widths(g.at("days").at("format"), day_keys);
    cal["daysStandalone"] = widths(g.at("days").at("stand-alone"), day_keys);
    cal["daysShort"] = names(g.at("days").at("format").at("short"), day_keys);
    const json& periods = g.at("dayPeriods").at("format");
    cal["am"] = ordered{{"wide", periods.at("wide").at("am")}, {"abbreviated", periods.at("abbreviated").at("am")}, {"narrow", periods.at("narrow").at("am")}};
    cal["pm"] = ordered{{"wide", periods.at("wide").at("pm")}, {"abbreviated", periods.at("abbreviated").at("pm")}, {"narrow", periods.at("narrow").at("pm")}};
    cal["eras"] = names(g.at("eras").at("eraAbbr"), {"0", "1"});
    for (auto [key, from] : {std::pair{"dateFormats", "dateFormats"}, std::pair{"timeFormats", "timeFormats"}, std::pair{"dateTimeFormats", "dateTimeFormats"}}) {
        ordered styles = ordered::object();
        for (std::string_view st : {"full", "long", "medium", "short"}) styles[std::string(st)] = pattern_of(g.at(from).at(st));
        cal[key] = std::move(styles);
    }
    ordered skeletons = ordered::object();
    const json& available = g.at("dateTimeFormats").at("availableFormats");
    for (auto it = available.begin(); it != available.end(); ++it)
        if (it.key().find("-alt-") == std::string::npos && it.key().find("-count-") == std::string::npos && it.value().is_string())
            skeletons[it.key()] = it.value();
    cal["skeletons"] = std::move(skeletons);
    // Day periods: the language's rules, the calendar's abbreviated names.
    const std::string lang = id.substr(0, id.find('-'));
    if (period_rules.contains(lang)) {
        ordered rules = ordered::array();
        ordered pnames = ordered::object();
        const json& set = period_rules.at(lang);
        for (auto it = set.begin(); it != set.end(); ++it) {
            const json& r = it.value();
            ordered rule{{"name", it.key()}};
            if (r.contains("_at")) rule["at"] = r.at("_at");
            else if (r.contains("_from") && r.contains("_before")) rule["from"] = r.at("_from"), rule["before"] = r.at("_before");
            else continue;
            rules.push_back(std::move(rule));
            if (periods.at("abbreviated").contains(it.key())) pnames[it.key()] = periods.at("abbreviated").at(it.key());
        }
        cal["dayPeriods"] = ordered{{"rules", std::move(rules)}, {"names", std::move(pnames)}};
    }
    const json& z = **zones;
    cal["gmtFormat"] = z.value("gmtFormat", "GMT{0}");
    cal["gmtZeroFormat"] = z.value("gmtZeroFormat", "GMT");
    cal["hourFormat"] = z.value("hourFormat", "+HH:mm;-HH:mm");
    out["calendar"] = std::move(cal);

    // Relative times: per unit and width.
    ordered rel = ordered::object();
    const json& f = **fields;
    for (std::string_view unit : {"year", "quarter", "month", "week", "day", "hour", "minute", "second"}) {
        ordered by_width = ordered::object();
        for (auto [width, suffix] : {std::pair{"wide", ""}, std::pair{"abbreviated", "-short"}, std::pair{"narrow", "-narrow"}}) {
            const std::string key = std::format("{}{}", unit, suffix);
            if (!f.contains(key)) continue;
            const json& e = f.at(key);
            ordered entry = ordered::object();
            ordered named = ordered::object();
            for (auto it = e.begin(); it != e.end(); ++it)
                if (it.key().starts_with("relative-type-")) named[it.key().substr(14)] = it.value();
            if (!named.empty()) entry["named"] = std::move(named);
            for (auto [dir, cldr_key] : {std::pair{"future", "relativeTime-type-future"}, std::pair{"past", "relativeTime-type-past"}}) {
                if (!e.contains(cldr_key)) continue;
                ordered by_count = ordered::object();
                const json& pat = e.at(cldr_key);
                for (auto it = pat.begin(); it != pat.end(); ++it)
                    if (it.key().starts_with("relativeTimePattern-count-")) by_count[it.key().substr(26)] = it.value();
                entry[dir] = std::move(by_count);
            }
            by_width[width] = std::move(entry);
        }
        rel[std::string(unit)] = std::move(by_width);
    }
    out["relative"] = std::move(rel);

    // Lists: and, or, units; each width.
    ordered lst = ordered::object();
    const json& l = **lists;
    for (auto [type, base] : {std::pair{"conjunction", "standard"}, std::pair{"disjunction", "or"}, std::pair{"unit", "unit"}}) {
        ordered by_width = ordered::object();
        for (auto [width, suffix] : {std::pair{"wide", ""}, std::pair{"abbreviated", "-short"}, std::pair{"narrow", "-narrow"}}) {
            const std::string key = std::format("listPattern-type-{}{}", base, suffix);
            if (!l.contains(key)) continue;
            const json& p = l.at(key);
            by_width[width] = ordered{{"start", p.at("start")}, {"middle", p.at("middle")}, {"end", p.at("end")}, {"two", p.at("2")}};
        }
        lst[type] = std::move(by_width);
    }
    out["lists"] = std::move(lst);
    return out.dump(1, ' ', false) + "\n";
}

}  // namespace

std::expected<std::vector<CldrPack>, std::string> emit_cldr_packs(const std::filesystem::path& cldr, std::span<const std::string> locales,
                                                                  std::string_view version) {
    const auto core = cldr / "cldr-core" / "supplemental";
    auto plurals = load(core / "plurals.json");
    auto ordinals = load(core / "ordinals.json");
    auto periods = load(core / "dayPeriods.json");
    auto likely = load(core / "likelySubtags.json");
    for (const auto* f : {&plurals, &ordinals, &periods, &likely})
        if (!*f) return std::unexpected(f->error());
    auto cardinal = at(*plurals, {"supplemental", "plurals-type-cardinal"}, "plurals.json");
    auto ordinal = at(*ordinals, {"supplemental", "plurals-type-ordinal"}, "ordinals.json");
    auto rules = at(*periods, {"supplemental", "dayPeriodRuleSet"}, "dayPeriods.json");
    auto subtags = at(*likely, {"supplemental", "likelySubtags"}, "likelySubtags.json");
    for (const auto* r : {&cardinal, &ordinal, &rules, &subtags})
        if (!*r) return std::unexpected(r->error());

    std::vector<CldrPack> out;
    std::set<std::string> languages;
    for (const std::string& id : locales) {
        auto text = pack(cldr, id, version, **cardinal, **ordinal, **rules);
        if (!text) return std::unexpected(text.error());
        out.push_back({std::format("{}.locale.omui.json", id), std::move(*text)});
        languages.insert(id.substr(0, id.find('-')));
    }
    // Likely subtags of the languages packed: their own tags, and those
    // without a language (und-TW) that resolve to one of them.
    ordered table = ordered::object();
    for (auto it = (**subtags).begin(); it != (**subtags).end(); ++it) {
        const std::string& from = it.key();
        const std::string to = it.value().get<std::string>();
        const std::string from_lang = from.substr(0, from.find('-')), to_lang = to.substr(0, to.find('-'));
        if (languages.contains(from_lang) || (from_lang == "und" && languages.contains(to_lang))) table[from] = to;
    }
    ordered doc{{"format", "openmui.likely@0.1"}, {"cldr", std::string(version)}, {"likely", std::move(table)}};
    out.push_back({"likely.locale.omui.json", doc.dump(1, ' ', false) + "\n"});
    return out;
}

}  // namespace omui::gen
