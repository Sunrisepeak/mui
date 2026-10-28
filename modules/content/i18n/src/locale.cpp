module openmui.i18n;

import std;

namespace omui::i18n {

namespace {

bool alpha(std::string_view s) noexcept {
    return std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); });
}
bool digits(std::string_view s) noexcept {
    return std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}
bool alnum(std::string_view s) noexcept { return std::ranges::all_of(s, [](char c) { return alpha(std::string_view(&c, 1)) || digits(std::string_view(&c, 1)); }); }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}
std::string title(std::string_view s) {
    std::string out = lower(s);
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

std::string key_of(std::string_view language, std::string_view script, std::string_view region) {
    std::string k(language.empty() ? "und" : language);
    if (!script.empty()) k += std::format("-{}", script);
    if (!region.empty()) k += std::format("-{}", region);
    return k;
}

}  // namespace

std::expected<Locale, std::string> Locale::parse(std::string_view tag) {
    // A POSIX locale's encoding and modifier are not part of the language.
    std::string_view t = tag.substr(0, std::min(tag.find('.'), tag.find('@')));
    if (lower(t) == "c" || lower(t) == "posix") return Locale{"en", "", "", {}, ""};
    if (t.empty()) return std::unexpected(std::string("an empty language tag"));
    std::vector<std::string_view> parts;
    for (std::size_t from = 0;;) {
        const std::size_t at = t.find_first_of("-_", from);
        parts.push_back(t.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
        if (at == std::string_view::npos) break;
        from = at + 1;
    }
    Locale l;
    std::size_t i = 0;
    const std::string_view lang = parts[i];
    if (lower(lang) == "root") {
        l.language = "und";
    } else if (alpha(lang) && (lang.size() == 2 || lang.size() == 3 || (lang.size() >= 5 && lang.size() <= 8))) {
        l.language = lower(lang);
    } else {
        return std::unexpected(std::format("`{}` is not a language subtag (two or three letters: en, zh)", lang));
    }
    ++i;
    if (i < parts.size() && parts[i].size() == 4 && alpha(parts[i])) l.script = title(parts[i++]);
    if (i < parts.size() && ((parts[i].size() == 2 && alpha(parts[i])) || (parts[i].size() == 3 && digits(parts[i])))) l.region = upper(parts[i++]);
    while (i < parts.size()) {
        const std::string_view v = parts[i];
        if (v.size() == 1) {  // a singleton: the extensions, kept as they are
            std::string rest;
            for (; i < parts.size(); ++i) rest += std::format("{}{}", rest.empty() ? "" : "-", lower(parts[i]));
            l.extensions = std::move(rest);
            break;
        }
        const bool variant = alnum(v) && ((v.size() >= 5 && v.size() <= 8) || (v.size() == 4 && v[0] >= '0' && v[0] <= '9'));
        if (!variant) return std::unexpected(std::format("`{}` in `{}` is not a script, a region or a variant", v, tag));
        l.variants.push_back(lower(v));
        ++i;
    }
    return l;
}

std::string Locale::tag() const {
    std::string out = key_of(language, script, region);
    for (const std::string& v : variants) out += std::format("-{}", v);
    if (!extensions.empty()) out += std::format("-{}", extensions);
    return out;
}

void LikelySubtags::add(std::string_view from, std::string_view to) {
    auto f = Locale::parse(from);
    auto t = Locale::parse(to);
    if (!f || !t) return;
    table_.insert_or_assign(key_of(f->language, f->script, f->region), std::move(*t));
}

Locale LikelySubtags::maximize(const Locale& l) const {
    // UTS #35 §4.3, Add Likely Subtags: the first of these the table has.
    const std::string& lang = l.language;
    std::vector<std::string> keys;
    if (!l.script.empty() && !l.region.empty()) keys.push_back(key_of(lang, l.script, l.region));
    if (!l.region.empty()) keys.push_back(key_of(lang, "", l.region));
    if (!l.script.empty()) keys.push_back(key_of(lang, l.script, ""));
    keys.push_back(key_of(lang, "", ""));
    if (lang != "und") {
        if (!l.script.empty()) keys.push_back(key_of("und", l.script, ""));
    }
    for (const std::string& k : keys)
        if (auto it = table_.find(k); it != table_.end()) {
            Locale out = l;
            if (out.language == "und") out.language = it->second.language;
            if (out.script.empty()) out.script = it->second.script;
            if (out.region.empty()) out.region = it->second.region;
            return out;
        }
    return l;
}

Locale LikelySubtags::minimize(const Locale& l) const {
    const Locale max = maximize(l);
    auto same = [&](const Locale& t) {
        const Locale m = maximize(t);
        return m.language == max.language && m.script == max.script && m.region == max.region;
    };
    for (const Locale& trial : {Locale{max.language, "", "", {}, ""}, Locale{max.language, "", max.region, {}, ""}, Locale{max.language, max.script, "", {}, ""}})
        if (same(trial)) {
            Locale out = trial;
            out.variants = l.variants;
            out.extensions = l.extensions;
            return out;
        }
    return max;
}

std::vector<Locale> negotiate(std::span<const Locale> requested, std::span<const Locale> available, const LikelySubtags& likely,
                              const Locale& fallback) {
    std::vector<Locale> out;
    auto push = [&](const Locale& l) {
        if (std::ranges::find(out, l) == out.end()) out.push_back(l);
    };
    std::vector<Locale> maximized;
    maximized.reserve(available.size());
    for (const Locale& a : available) maximized.push_back(likely.maximize(a));
    for (const Locale& r : requested) {
        const Locale want = likely.maximize(r);
        // Its region's, then its script's anywhere, then its language in another script.
        for (std::size_t i = 0; i < available.size(); ++i)
            if (maximized[i].language == want.language && maximized[i].script == want.script && maximized[i].region == want.region) push(available[i]);
        for (std::size_t i = 0; i < available.size(); ++i)
            if (maximized[i].language == want.language && maximized[i].script == want.script) push(available[i]);
        for (std::size_t i = 0; i < available.size(); ++i)
            if (maximized[i].language == want.language) push(available[i]);
    }
    push(fallback);
    return out;
}

}  // namespace omui::i18n
