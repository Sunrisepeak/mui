module openmui.view;

import std;
import mcpplibs.os;
import openmui.core;
import openmui.i18n;

namespace omui {

namespace {

constexpr Log i18n_log{"i18n"};

i18n::Arg to_arg(const Value& v) {
    switch (v.type()) {
        case Value::Type::boolean: return v.as_bool();
        case Value::Type::integer: return v.as_int();
        case Value::Type::real: return v.as_double();
        case Value::Type::text: return std::string(v.as_string());
        case Value::Type::list: {
            std::vector<std::string> items;
            for (const Value& x : *v.list()) items.emplace_back(x.type() == Value::Type::text ? std::string(x.as_string()) : x.json());
            return items;
        }
        default: return std::monostate{};
    }
}

}  // namespace

struct Languages::Impl {
    mutable std::mutex                         lock;
    i18n::Library                              library;
    std::vector<i18n::Locale>                  requested;
    i18n::Locale                               fallback{"en", "", "", {}, ""};
    std::unique_ptr<i18n::Translator>          translator;
    std::atomic<std::uint64_t>                 generation{0};
    std::set<std::string, std::less<>>         missing;  // keys logged as missing

    void remake() {  // under the lock
        translator = std::make_unique<i18n::Translator>(library, requested, fallback);
        missing.clear();
        generation.fetch_add(1, std::memory_order_relaxed);
    }
};

namespace {

// This thread's signal of the language (signals belong to their thread).
Signal<std::uint64_t>& thread_signal(std::uint64_t generation) {
    thread_local std::optional<Signal<std::uint64_t>> s;
    if (!s) s = signal(generation);
    return *s;
}

}  // namespace

Languages::Languages() : impl_(std::make_unique<Impl>()) {
    std::scoped_lock g(impl_->lock);
    impl_->remake();
}
Languages::~Languages() = default;

void Languages::edit(const std::function<void(i18n::Library&)>& change) {
    {
        std::scoped_lock g(impl_->lock);
        change(impl_->library);
        impl_->remake();
    }
    thread_signal(0).set(generation());
}

std::expected<void, std::vector<i18n::Diagnostic>> Languages::add_messages(const i18n::Locale& locale, std::string_view toml, std::string_view file) {
    auto catalog = i18n::read_messages(toml, file);
    if (!catalog) return std::unexpected(std::move(catalog.error()));
    edit([&](i18n::Library& lib) { lib.catalog(locale) = std::move(*catalog); });
    return {};
}

void Languages::request(std::vector<i18n::Locale> requested) {
    {
        std::scoped_lock g(impl_->lock);
        impl_->requested = std::move(requested);
        impl_->remake();
    }
    thread_signal(0).set(generation());
    i18n_log.info("languages: {}", tag());
}

void Languages::set_fallback(i18n::Locale source) {
    {
        std::scoped_lock g(impl_->lock);
        impl_->fallback = std::move(source);
        impl_->remake();
    }
    thread_signal(0).set(generation());
}

std::vector<i18n::Locale> Languages::requested() const {
    std::scoped_lock g(impl_->lock);
    return impl_->requested;
}

std::vector<i18n::Locale> Languages::chain() const {
    std::scoped_lock g(impl_->lock);
    return impl_->translator->chain();
}

i18n::Translator::Result Languages::translate(std::string_view key, const i18n::Args& args) const {
    std::scoped_lock g(impl_->lock);
    auto r = impl_->translator->translate(key, args);
    if (!r.found) {
        // A key no catalog has is its own message (MessageFormat 2), so a
        // source-language key reads well before any translation exists.
        if (auto m = i18n::Message::parse(key)) r.text = m->format(impl_->translator->formatter(), args, &r.errors);
        if (impl_->missing.emplace(key).second) i18n_log.debug("no message `{}` in {}: shown as written", key, impl_->translator->chain().back().tag());
    }
    for (const auto& e : r.errors)
        if (r.found) i18n_log.warn("`{}`: {}", key, e);
    return r;
}

std::string Languages::number(double value, const i18n::NumberOptions& options) const {
    std::scoped_lock g(impl_->lock);
    return impl_->translator->formatter().number(value, options).text;
}

std::string Languages::date(const i18n::CivilTime& t, i18n::DateStyle style) const {
    std::scoped_lock g(impl_->lock);
    return impl_->translator->formatter().date(t, style);
}

std::string Languages::tag() const {
    std::scoped_lock g(impl_->lock);
    return impl_->requested.empty() ? impl_->fallback.tag() : impl_->requested.front().tag();
}

void Languages::track() const { (void)thread_signal(generation()).get(); }

std::uint64_t Languages::generation() const noexcept { return impl_->generation.load(std::memory_order_relaxed); }

Languages& languages() {
    static Languages l;
    return l;
}

std::string translate(std::string_view key, const Value& args) {
    Languages& l = languages();
    l.track();
    i18n::Args a;
    if (const auto* m = args.map())
        for (const auto& [k, v] : *m) a.set(k, to_arg(v));
    return l.translate(key, a).text;
}

Source tr(std::string key, std::vector<TrArg> args) {
    return Source{std::nullopt,
                  [key = std::move(key), args = std::move(args)] {
                      Languages& l = languages();
                      l.track();
                      i18n::Args a;
                      for (const TrArg& x : args) {
                          if (const auto* c = std::get_if<i18n::Arg>(&x.value)) a.set(x.name, *c);
                          else a.set(x.name, to_arg(std::get<Source>(x.value).current()));
                      }
                      return Value(l.translate(key, a).text);
                  },
                  {}};
}

std::vector<i18n::Locale> system_locales() {
    namespace os = mcpplibs::os;
    std::vector<i18n::Locale> out;
    auto add = [&](std::string_view tag) {
        if (tag.empty()) return;
        if (auto l = i18n::Locale::parse(tag); l && std::ranges::find(out, *l) == out.end()) out.push_back(std::move(*l));
    };
    // POSIX: LC_ALL wins, then LC_MESSAGES, then LANG; GNU's LANGUAGE lists more, colon-separated.
    std::string first;
    for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG"})
        if (auto value = os::env::get(v); value && !value->empty()) {
            first = *value;
            break;
        }
    if (auto list = os::env::get("LANGUAGE"); list && !list->empty() && first != "C" && first != "POSIX") {
        for (std::size_t from = 0; from <= list->size();) {
            const std::size_t colon = std::min(list->find(':', from), list->size());
            add(std::string_view(*list).substr(from, colon - from));
            from = colon + 1;
        }
    }
    add(first);
    return out;
}

}  // namespace omui
