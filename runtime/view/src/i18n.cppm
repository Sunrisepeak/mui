// openmui.view:i18n — the application's languages (D-74): the locale packs
// and the application's catalogs (an openmui.i18n Library), the person's
// languages, and `tr`, a text in them that is redone when the language or an
// argument changes — no remount, the same ids (S-9).
//
//     omui::languages().add_messages(zh, toml, "app.zh-Hans.messages.omui.toml");
//     omui::languages().request({*i18n::Locale::parse("zh-CN")});
//     text({.id = "count", .text = omui::tr("inbox.count", {{"count", count}})});
//
// The languages are the process's (one person, one language). Signals belong
// to a thread (core §signal), so each thread tracks the language with a
// signal of its own: a change made on the UI thread redoes that thread's
// texts at once; a view built later on any thread reads the language then.
export module openmui.view:i18n;

import std;
import openmui.core;
import openmui.i18n;
import :element;

export namespace omui {

// An argument of a message: a value, or a source read when the text is made.
struct TrArg {
    std::string                         name;
    std::variant<i18n::Arg, Source>     value;

    TrArg(std::string_view n, Source s) : name(n), value(std::move(s)) {}
    template <class T>
    TrArg(std::string_view n, Signal<T> s) : name(n), value(Source{std::nullopt, [s] { return ValueOf<T>::to(s.get()); }, {}}) {}
    template <class T>
    TrArg(std::string_view n, Computed<T> c) : name(n), value(Source{std::nullopt, [c] { return ValueOf<T>::to(c.get()); }, {}}) {}
    TrArg(std::string_view n, i18n::CivilTime t) : name(n), value(i18n::Arg(t)) {}
    TrArg(std::string_view n, std::vector<std::string> list) : name(n), value(i18n::Arg(std::move(list))) {}
    TrArg(std::string_view n, std::string s) : name(n), value(i18n::Arg(std::move(s))) {}
    TrArg(std::string_view n, const char* s) : name(n), value(i18n::Arg(std::string(s))) {}
    TrArg(std::string_view n, bool b) : name(n), value(i18n::Arg(b)) {}
    TrArg(std::string_view n, double d) : name(n), value(i18n::Arg(d)) {}
    template <std::integral I>
        requires(!std::same_as<I, bool>)
    TrArg(std::string_view n, I i) : name(n), value(i18n::Arg(static_cast<std::int64_t>(i))) {}
};

class Languages {
public:
    Languages();
    ~Languages();
    Languages(const Languages&) = delete;
    Languages& operator=(const Languages&) = delete;

    // Runs `change` on the library (packs, catalogs) under the languages'
    // lock, then remakes the translator; texts follow as on a request.
    void edit(const std::function<void(i18n::Library&)>& change);
    // A catalog (`*.messages.omui.toml`) for a locale; every problem located.
    std::expected<void, std::vector<i18n::Diagnostic>> add_messages(const i18n::Locale& locale, std::string_view toml, std::string_view file);

    // The person's languages, best first; `fallback` is the source language,
    // the one every key has (English unless the application says).
    void request(std::vector<i18n::Locale> requested);
    void set_fallback(i18n::Locale source);
    [[nodiscard]] std::vector<i18n::Locale> requested() const;
    // The chain negotiated from them: what messages come from.
    [[nodiscard]] std::vector<i18n::Locale> chain() const;

    // A message now, in the current language (not tracked).
    [[nodiscard]] i18n::Translator::Result translate(std::string_view key, const i18n::Args& args = {}) const;
    // Numbers and dates outside messages, in the person's first language.
    [[nodiscard]] std::string number(double value, const i18n::NumberOptions& options = {}) const;
    [[nodiscard]] std::string date(const i18n::CivilTime& t, i18n::DateStyle style = i18n::DateStyle::medium) const;
    // The language's tag (BCP 47) for the semantic tree and the host.
    [[nodiscard]] std::string tag() const;

    // Read inside a computation: it is redone when the language changes on this thread.
    void track() const;
    // How many times the language or the library changed.
    [[nodiscard]] std::uint64_t generation() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] Languages& languages();

// A message now, its arguments a map by name (a view document's, an
// expression's); tracked like tr: redone when the language changes.
[[nodiscard]] std::string translate(std::string_view key, const Value& args);

// A text in the person's language: the message for `key` with its
// arguments; redone when the language or an argument changes. A key no
// catalog has shows as itself (and is logged once).
[[nodiscard]] Source tr(std::string key, std::vector<TrArg> args = {});

// The person's languages as the system states them (LC_ALL, LC_MESSAGES,
// LANG and LANGUAGE, in POSIX's order); hosts with a better source (SDL's
// preferred locales) use theirs.
[[nodiscard]] std::vector<i18n::Locale> system_locales();

}  // namespace omui
