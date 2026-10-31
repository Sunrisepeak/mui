module openmui.i18n;

import std;

namespace omui::i18n {

namespace {

// a → à, …: letters a reader still reads, in other bytes.
constexpr std::array<std::string_view, 26> accented_lower{"à", "ƀ", "ç", "ð", "é", "ƒ", "ĝ", "ĥ", "î", "ĵ", "ķ", "ļ", "ɱ",
                                                         "ñ", "ö", "þ", "ǫ", "ŕ", "š", "ţ", "û", "ṽ", "ŵ", "ẋ", "ý", "ž"};
constexpr std::array<std::string_view, 26> accented_upper{"À", "Ɓ", "Ç", "Ð", "É", "Ƒ", "Ĝ", "Ĥ", "Î", "Ĵ", "Ķ", "Ļ", "Ṁ",
                                                         "Ñ", "Ö", "Þ", "Ǫ", "Ŕ", "Š", "Ţ", "Û", "Ṽ", "Ŵ", "Ẋ", "Ý", "Ž"};

// An expression ({…}) copied as it is, from `at` (its `{`); answers where it ends.
std::size_t copy_expression(std::string_view s, std::size_t at, std::string& out) {
    bool quoted = false;
    for (std::size_t i = at; i < s.size(); ++i) {
        out += s[i];
        if (s[i] == '\\' && i + 1 < s.size()) {
            out += s[++i];
            continue;
        }
        if (s[i] == '|') quoted = !quoted;
        else if (s[i] == '}' && !quoted) return i + 1;
    }
    return s.size();
}

// A pattern's text accented and lengthened; its placeholders as they are.
// From `at` to the end, or (quoted) to its `}}`; answers where it ends.
std::size_t pseudo_pattern(std::string_view s, std::size_t at, bool quoted, std::string& out) {
    out += "⟦";
    std::size_t letters = 0, i = at;
    for (; i < s.size();) {
        const char c = s[i];
        if (quoted && c == '}' && i + 1 < s.size() && s[i + 1] == '}') break;
        if (c == '\\' && i + 1 < s.size()) {
            out += s.substr(i, 2);
            i += 2;
        } else if (c == '{') {
            i = copy_expression(s, i, out);
        } else if (c >= 'a' && c <= 'z') {
            out += accented_lower[static_cast<std::size_t>(c - 'a')];
            ++letters;
            ++i;
        } else if (c >= 'A' && c <= 'Z') {
            out += accented_upper[static_cast<std::size_t>(c - 'A')];
            ++letters;
            ++i;
        } else {
            out += c;
            ++i;
        }
    }
    // About 40% longer, so a label that only just fits its English shows now.
    out += ' ';
    out.append(std::max<std::size_t>(1, (letters * 2 + 4) / 5), '~');
    out += "⟧";
    return i;
}

}  // namespace

// ── Catalog ──────────────────────────────────────────────────────────────

std::expected<void, std::string> Catalog::add(std::string_view key, std::string_view source) {
    if (key.empty()) return std::unexpected(std::string("an empty key"));
    auto m = Message::parse(source);
    if (!m) return std::unexpected(std::format("`{}`: {}", key, m.error()));
    messages_.insert_or_assign(std::string(key), std::move(*m));
    return {};
}

const Message* Catalog::find(std::string_view key) const noexcept {
    const auto it = messages_.find(key);
    return it == messages_.end() ? nullptr : &it->second;
}

std::vector<std::string> Catalog::keys() const {
    std::vector<std::string> out;
    out.reserve(messages_.size());
    for (const auto& [k, m] : messages_) out.push_back(k);
    return out;
}

std::vector<std::string> compare(const Catalog& reference, const Catalog& translation) {
    std::vector<std::string> out;
    for (const std::string& k : reference.keys()) {
        const Message* t = translation.find(k);
        if (!t) {
            out.push_back(std::format("`{}` is not translated", k));
            continue;
        }
        auto want = reference.find(k)->variables(), got = t->variables();
        std::ranges::sort(want);
        std::ranges::sort(got);
        for (const auto& v : want)
            if (!std::ranges::binary_search(got, v)) out.push_back(std::format("`{}` drops ${}", k, v));
        for (const auto& v : got)
            if (!std::ranges::binary_search(want, v)) out.push_back(std::format("`{}` reads ${}, which the source does not give", k, v));
    }
    for (const std::string& k : translation.keys())
        if (!reference.find(k)) out.push_back(std::format("`{}` is not in the source", k));
    return out;
}

// ── Library ──────────────────────────────────────────────────────────────

void Library::add(std::shared_ptr<const LocaleData> data) {
    std::erase_if(data_, [&](const auto& d) { return d->locale == data->locale; });
    data_.push_back(std::move(data));
}

Catalog& Library::catalog(const Locale& locale) {
    for (auto& [l, c] : catalogs_)
        if (l == locale) return c;
    return catalogs_.emplace_back(locale, Catalog{}).second;
}

const Catalog* Library::find_catalog(const Locale& locale) const noexcept {
    for (const auto& [l, c] : catalogs_)
        if (l == locale) return &c;
    return nullptr;
}

std::shared_ptr<const LocaleData> Library::data(const Locale& locale) const {
    for (const auto& d : data_)
        if (d->locale == locale) return d;
    const Locale want = likely_.maximize(locale);
    for (const bool script : {true, false})
        for (const auto& d : data_) {
            const Locale have = likely_.maximize(d->locale);
            if (have.language == want.language && (!script || have.script == want.script)) return d;
        }
    return nullptr;
}

std::vector<Locale> Library::locales() const {
    std::vector<Locale> out;
    for (const auto& [l, c] : catalogs_) out.push_back(l);
    return out;
}

// ── Translator ───────────────────────────────────────────────────────────

namespace {

// A locale's pack, or the fallback's, or none at all (plain digits, no names).
std::shared_ptr<const LocaleData> pack_for(const Library& library, const Locale& locale, const Locale& fallback) {
    static const auto plain = std::make_shared<const LocaleData>();
    if (auto d = library.data(locale)) return d;
    if (auto d = library.data(fallback)) return d;
    return plain;
}

}  // namespace

Translator::Translator(const Library& library, std::span<const Locale> requested, const Locale& fallback)
    : library_(library),
      chain_(negotiate(requested, library.locales(), library.likely(), fallback)),
      primary_(pack_for(library, requested.empty() || !library.data(requested.front()) ? chain_.front() : requested.front(), fallback)) {
    for (const Locale& l : chain_) formatters_.emplace_back(pack_for(library, l, fallback));
}

Translator::Result Translator::translate(std::string_view key, const Args& args) const {
    Result r;
    for (std::size_t i = 0; i < chain_.size(); ++i) {
        const Catalog* c = library_.find_catalog(chain_[i]);
        const Message* m = c ? c->find(key) : nullptr;
        if (!m) continue;
        r.text = m->format(formatters_[i], args, &r.errors);
        r.from = chain_[i];
        r.found = true;
        return r;
    }
    r.text = std::string(key);
    r.errors.push_back(std::format("no message `{}` in {}", key, chain_.empty() ? std::string("any locale") : chain_.back().tag()));
    return r;
}

// ── pseudo-localisation ──────────────────────────────────────────────────

std::string pseudo_localize(std::string_view s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    const std::string_view rest = s.substr(i);
    const bool complex = rest.starts_with("{{") || rest.starts_with(".input") || rest.starts_with(".local") || rest.starts_with(".match");
    if (!complex) {
        pseudo_pattern(s, 0, false, out);
        return out;
    }
    // Declarations, selectors and keys as written; each quoted pattern's text changed.
    bool quoted = false;
    for (i = 0; i < s.size();) {
        if (!quoted && s.substr(i, 2) == "{{") {
            out += "{{";
            i = pseudo_pattern(s, i + 2, true, out);
            if (i < s.size()) {
                out += "}}";
                i += 2;
            }
            continue;
        }
        if (s[i] == '{') {
            i = copy_expression(s, i, out);
            continue;
        }
        if (s[i] == '|') quoted = !quoted;
        out += s[i++];
    }
    return out;
}

Catalog pseudo_catalog(const Catalog& source) {
    Catalog out;
    for (const std::string& k : source.keys()) (void)out.add(k, pseudo_localize(source.find(k)->source()));
    return out;
}

}  // namespace omui::i18n
