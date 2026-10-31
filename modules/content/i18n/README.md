# openmui.i18n

Internationalisation as an engine without data (D-74). Nothing here knows a language: the languages are data packs, generated from CLDR (`modules/content/i18n/cldr`, `omui-gen cldr`), and an application's catalogs. It depends on the standard library, nlohmann/json and toml++, knows nothing of openmui, and can be released as a library of its own.

```cpp
import openmui.i18n;
using namespace omui::i18n;

Library lib;
lib.add(std::make_shared<LocaleData>(*read_locale_data(zh_pack)));   // or openmui.i18n.cldr's add_to(lib)
lib.set_likely(*read_likely_subtags(likely_pack));
lib.catalog(*Locale::parse("zh-Hans")) = *read_messages(toml, "app.zh-Hans.messages.omui.toml");

const std::array asked{*Locale::parse("zh-TW")};
Translator t(lib, asked, *Locale::parse("en"));
t.tr("inbox.count", {{"count", std::int64_t{1234}}});        // 1,234 条新消息
t.formatter().number(123456.0, {.style = NumberOptions::Style::compact_short}).text;  // 12萬
t.formatter().date({2026, 10, 28}, DateStyle::full);         // 2026年10月28日 星期三
```

| Partition | What |
|---|---|
| `:locale` | BCP 47 tags (and POSIX ones: `zh_CN.UTF-8`), CLDR's likely subtags (maximize, minimize), negotiation: a person's languages against what an application has — their script first, then the same language in the other script (Traditional and Simplified Chinese fall back to each other), then the source language |
| `:plural` | CLDR's plural rule language, parsed from the packs and evaluated on the digits a number is shown with (`1` is *one* in English, `1.0` is *other*); cardinal and ordinal |
| `:data` | What a pack holds, and its reader (`*.locale.omui.json`, every problem with its JSON path) |
| `:format` | Numbers (grouping, fractions, signs, percent, compact: 1.2K, 12万), dates and times (every length, CLDR patterns and skeletons, flexible day periods: 下午3:05, localised offsets), relative times (yesterday, 3天前), lists (a, b, and c; a、b和c) |
| `:message` | Unicode MessageFormat 2: placeholders, functions and options (`:number`, `:integer`, `:percent`, `:string`, `:date`, `:time`, `:datetime`, `:list`), declarations, selection by exact value and plural category over several selectors; never fails — what cannot be resolved shows its fallback and is reported |
| `:catalog` | Catalogs (`*.messages.omui.toml`, nested tables are dotted keys), a library of packs and catalogs, a translator over the negotiated chain (each message in its own locale's ways), checks of a translation against its source, pseudo-localisation |

**The same text everywhere.** Digits come from the standard library's exact conversions (`std::to_chars`, correctly rounded, ties to even), never from the C locale or the system; powers of ten are exact; nothing reads the system's locale data. A pack's text is the only input about a language, and it carries the CLDR version it was made from.

The tests (`tests/i18n_test.cpp`) check the engine against CLDR's own answers for English, Simplified and Traditional Chinese, and MessageFormat 2 against its specification.

## Inside

- [cldr](cldr) (`openmui.i18n.cldr`): CLDR 49 locale packs — en, zh (Simplified), zh-Hant — and likely subtags.
