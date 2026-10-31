// openmui.i18n:catalog — an application's messages and who reads them: a
// catalog per locale (`<app>.<tag>.messages.omui.toml`, keys in tables,
// MessageFormat 2 values), a library of locale packs and catalogs, and a
// translator: a person's languages negotiated against what the library has,
// each message from the first locale in that chain that has it, its numbers
// and dates in that locale's ways. Pseudo-localisation makes a catalog that
// shows what a translation would break.
export module openmui.i18n:catalog;

import std;
import :locale;
import :plural;
import :data;
import :format;
import :message;

export namespace omui::i18n {

class Catalog {
public:
    // A key's message; an error says the key and what is wrong.
    std::expected<void, std::string> add(std::string_view key, std::string_view source);
    [[nodiscard]] const Message* find(std::string_view key) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return messages_.size(); }
    [[nodiscard]] std::vector<std::string> keys() const;

private:
    std::map<std::string, Message, std::less<>> messages_;
};

// `*.messages.omui.toml`: tables of keys whose values are messages
// (`[inbox] count = "…"` is `inbox.count`); every problem, located.
[[nodiscard]] std::expected<Catalog, std::vector<Diagnostic>> read_messages(std::string_view toml, std::string_view file = "messages.omui.toml");

// What a catalog says that its reference does not (keys missing or extra,
// variables a translation drops or adds), for `omui-gen i18n --check`.
[[nodiscard]] std::vector<std::string> compare(const Catalog& reference, const Catalog& translation);

class Library {
public:
    void add(std::shared_ptr<const LocaleData> data);
    void set_likely(LikelySubtags likely) { likely_ = std::move(likely); }
    // The catalog of a locale, made on first use.
    Catalog& catalog(const Locale& locale);
    [[nodiscard]] const Catalog* find_catalog(const Locale& locale) const noexcept;
    // The pack for a locale: its own, else the nearest by language and script.
    [[nodiscard]] std::shared_ptr<const LocaleData> data(const Locale& locale) const;

    [[nodiscard]] const LikelySubtags& likely() const noexcept { return likely_; }
    // The locales it has messages for (what a person's choice is negotiated against).
    [[nodiscard]] std::vector<Locale> locales() const;

private:
    std::vector<std::shared_ptr<const LocaleData>> data_;
    std::vector<std::pair<Locale, Catalog>>        catalogs_;
    LikelySubtags                                  likely_;
};

class Translator {
public:
    // `requested`: the person's languages, best first; `fallback`: the
    // language every key has (the source language).
    Translator(const Library& library, std::span<const Locale> requested, const Locale& fallback);

    struct Result {
        std::string              text;
        Locale                   from;            // the locale whose message it is
        bool                     found = false;   // false: the key itself is shown
        std::vector<std::string> errors;
    };
    [[nodiscard]] Result translate(std::string_view key, const Args& args = {}) const;
    [[nodiscard]] std::string tr(std::string_view key, const Args& args = {}) const { return translate(key, args).text; }

    [[nodiscard]] const std::vector<Locale>& chain() const noexcept { return chain_; }
    // The person's first language (when the library has its pack, else the
    // chain's first): what numbers and dates outside messages are written in.
    [[nodiscard]] const Locale& locale() const noexcept { return primary_.locale(); }
    [[nodiscard]] const Formatter& formatter() const noexcept { return primary_; }

private:
    const Library&         library_;
    std::vector<Locale>    chain_;
    std::vector<Formatter> formatters_;  // by chain index: each message in its own locale's ways
    Formatter              primary_;
};

// Pseudo-localisation: the message's text accented and lengthened by about
// 40% (what German or Russian often need), bracketed so a cut-off shows;
// placeholders, keys and syntax untouched. `⟦Ŝàvé ~~⟧`.
[[nodiscard]] std::string pseudo_localize(std::string_view message_source);
[[nodiscard]] Catalog pseudo_catalog(const Catalog& source);

}  // namespace omui::i18n
