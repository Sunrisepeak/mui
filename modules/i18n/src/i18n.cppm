// openmui.i18n — internationalisation as an engine without data (D-74).
//
//   import openmui.i18n;
//   using namespace omui::i18n;
//
//   Library lib;                                     // the locale packs and the catalogs
//   lib.add(std::make_shared<LocaleData>(*read_locale_data(json)));
//   lib.set_likely(*read_likely_subtags(likely_json));
//   lib.catalog(*Locale::parse("zh-Hans")) = *read_messages(toml);
//
//   Translator t(lib, requested, *Locale::parse("en"));
//   t.tr("inbox.count", {{"count", std::int64_t{3}}});  // 3 条新消息
//   t.formatter().number(1234.5).text;               // 1,234.5
//
// Partitions: locale (BCP 47, likely subtags, negotiation), plural (CLDR
// rules), data (what a pack holds, and its reader), format (numbers, dates,
// relative times, lists), message (MessageFormat 2), catalog (catalogs,
// library, translator, pseudo-localisation). Nothing here knows a language:
// that is the packs' (i18n/cldr) and the application's catalogs'.
export module openmui.i18n;

export import :locale;
export import :plural;
export import :data;
export import :format;
export import :message;
export import :catalog;
