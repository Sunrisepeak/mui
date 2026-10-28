// openmui.i18n:locale — who the text is for: BCP 47 language tags, CLDR's
// likely subtags, and the negotiation of what a person asked for against
// what an application has.
export module openmui.i18n:locale;

import std;

export namespace omui::i18n {

struct Locale {
    std::string              language = "und";  // lower case: en, zh (und: undetermined)
    std::string              script;            // title case: Hans, Latn
    std::string              region;            // upper case: CN, or three digits: 419
    std::vector<std::string> variants;          // lower case
    std::string              extensions;        // what follows the first singleton (-u-ca-…), as given, lower case

    // A tag as people and systems write it: `zh-Hant-TW`, `en_US`,
    // `zh_CN.UTF-8` (a POSIX locale: its encoding and modifier dropped),
    // `C` and `POSIX` (English). Case does not matter.
    [[nodiscard]] static std::expected<Locale, std::string> parse(std::string_view tag);
    // The canonical form: `zh-Hant-TW`.
    [[nodiscard]] std::string tag() const;
    [[nodiscard]] bool empty() const noexcept { return language == "und" && script.empty() && region.empty(); }

    // Written out: GCC 16.1 crashes on defaulted comparisons of module types.
    friend bool operator==(const Locale& a, const Locale& b) noexcept {
        return a.language == b.language && a.script == b.script && a.region == b.region && a.variants == b.variants &&
               a.extensions == b.extensions;
    }
};

// CLDR's likely subtags (supplemental likelySubtags): what a tag leaves out
// is filled in — `zh` is `zh-Hans-CN`, `zh-TW` is `zh-Hant-TW`.
class LikelySubtags {
public:
    // `from` (a tag with `und` for no language) means `to`, a full tag.
    void add(std::string_view from, std::string_view to);
    [[nodiscard]] std::size_t size() const noexcept { return table_.size(); }

    // UTS #35 §4.3: the language, script and region, each filled in.
    [[nodiscard]] Locale maximize(const Locale& locale) const;
    // The shortest tag that maximizes to the same: `zh-Hans-CN` is `zh`.
    [[nodiscard]] Locale minimize(const Locale& locale) const;

private:
    std::map<std::string, Locale, std::less<>> table_;
};

// The locales of `available` to use, best first, for what was asked for
// (`requested`, in order of preference), then `fallback`. A requested
// locale's own script comes first — its region's, then any — and then the
// same language in another script (Traditional Chinese for Simplified, and
// back), before the next requested language and the fallback.
[[nodiscard]] std::vector<Locale> negotiate(std::span<const Locale> requested, std::span<const Locale> available,
                                            const LikelySubtags& likely, const Locale& fallback);

}  // namespace omui::i18n
