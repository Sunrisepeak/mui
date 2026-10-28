// openmui.i18n against CLDR's own answers: the packs generated from CLDR 49
// (i18n/cldr/data) give the texts CLDR's charts and ICU give for English,
// Simplified and Traditional Chinese — numbers, compact forms, dates in every
// length, day periods, relative times, lists, plural and ordinal categories —
// and MessageFormat 2 messages select and format as the specification says.
import std;
import openmui.i18n;
import openmui.testing;

using namespace omui::testing;
using namespace omui::i18n;

namespace {

std::filesystem::path packs() {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path() / "i18n" /
           "cldr" / "data";
}

std::string read(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::shared_ptr<const LocaleData> pack(std::string_view id) {
    const auto file = packs() / std::format("{}.locale.omui.json", id);
    auto d = read_locale_data(read(file), file.filename().string());
    if (!d) {
        for (const auto& p : d.error()) std::println("    {}", p.text());
        check(false, std::format("the {} pack reads", id));
        return std::make_shared<const LocaleData>();
    }
    return std::make_shared<const LocaleData>(std::move(*d));
}

Locale tag(std::string_view t) { return Locale::parse(t).value(); }

const CivilTime afternoon{2026, 10, 28, 15, 5, 9, 0, 0};  // a Wednesday

}  // namespace

int main(int argc, char** argv) {
    Suite s{"i18n"};

    s.test("locales: BCP 47 and POSIX tags, canonical; likely subtags; negotiation keeps the script, then the other script", [] {
        check_eq(tag("zh-hant-tw").tag(), std::string("zh-Hant-TW"));
        check_eq(tag("en_US").tag(), std::string("en-US"));
        check_eq(tag("zh_CN.UTF-8").tag(), std::string("zh-CN"));
        check_eq(tag("C").tag(), std::string("en"));
        check_eq(tag("es-419").tag(), std::string("es-419"));
        check_eq(tag("de-CH-1996-u-co-phonebk").tag(), std::string("de-CH-1996-u-co-phonebk"));
        check(!Locale::parse("e").has_value() && !Locale::parse("en-$$").has_value() && !Locale::parse("").has_value());
        auto likely = read_likely_subtags(read(packs() / "likely.locale.omui.json"));
        check(likely.has_value(), "the likely subtags read");
        if (!likely) return;
        check_eq(likely->maximize(tag("zh")).tag(), std::string("zh-Hans-CN"));
        check_eq(likely->maximize(tag("zh-TW")).tag(), std::string("zh-Hant-TW"));
        check_eq(likely->maximize(tag("zh-HK")).tag(), std::string("zh-Hant-HK"));
        check_eq(likely->maximize(tag("und-Hant")).tag(), std::string("zh-Hant-TW"));
        check_eq(likely->minimize(tag("zh-Hans-CN")).tag(), std::string("zh"));
        check_eq(likely->minimize(tag("zh-Hant-TW")).tag(), std::string("zh-TW"));
        const std::array available{tag("en"), tag("zh-Hans"), tag("zh-Hant")};
        auto chain = [&](std::initializer_list<std::string_view> asked) {
            std::vector<Locale> r;
            for (auto a : asked) r.push_back(tag(a));
            std::string out;
            for (const Locale& l : negotiate(r, available, *likely, tag("en"))) out += std::format("{}{}", out.empty() ? "" : " ", l.tag());
            return out;
        };
        check_eq(chain({"zh-TW"}), std::string("zh-Hant zh-Hans en"));
        check_eq(chain({"zh-CN", "en-GB"}), std::string("zh-Hans zh-Hant en"));
        check_eq(chain({"zh-HK"}), std::string("zh-Hant zh-Hans en"));
        check_eq(chain({"en-GB"}), std::string("en"));
        check_eq(chain({"fr", "zh"}), std::string("zh-Hans zh-Hant en"));
    });

    s.test("plural rules: CLDR's syntax, chosen by the digits shown (1 is one, 1.0 is other); ordinals", [] {
        auto en = pack("en");
        auto cat = [&](std::string_view digits, bool ordinal = false) {
            return std::string(name((ordinal ? en->ordinal : en->cardinal).select(*Operands::of(digits))));
        };
        check_eq(cat("1"), std::string("one"));
        check_eq(cat("1.0"), std::string("other"));
        check_eq(cat("0"), std::string("other"));
        check_eq(cat("2"), std::string("other"));
        for (auto [n, want] : {std::pair{"1", "one"}, std::pair{"2", "two"}, std::pair{"3", "few"}, std::pair{"4", "other"}, std::pair{"11", "other"},
                               std::pair{"12", "other"}, std::pair{"13", "other"}, std::pair{"21", "one"}, std::pair{"102", "two"}, std::pair{"113", "other"}})
            check(cat(n, true) == want, std::format("{} is {}, not {}", n, cat(n, true), want));
        check_eq(std::string(name(pack("zh")->cardinal.select(Operands::of(std::int64_t{1})))), std::string("other"));
        // A rule language with ranges, modulus and or (Russian's few), and its errors.
        const std::array<std::pair<Plural, std::string>, 2> ru{{{Plural::one, "v = 0 and i % 10 = 1 and i % 100 != 11"},
                                                                 {Plural::few, "v = 0 and i % 10 = 2..4 and i % 100 != 12..14"}}};
        auto rules = PluralRules::parse(ru);
        check(rules.has_value());
        if (rules) {
            check(rules->select(*Operands::of("22")) == Plural::few && rules->select(*Operands::of("12")) == Plural::other &&
                  rules->select(*Operands::of("101")) == Plural::one);
            check(rules->categories() == std::vector<Plural>{Plural::one, Plural::few, Plural::other});
        }
        const std::array<std::pair<Plural, std::string>, 1> bad{{{Plural::one, "q = 1"}}};
        check(!PluralRules::parse(bad).has_value());
        auto o = Operands::of("1.50");
        check(o && o->i == 1 && o->v == 2 && o->w == 1 && o->f == 50 && o->t == 5);
    });

    s.test("numbers: grouping, fractions, signs, percent, and compact forms (1.2K, 12万, 1.5亿, 12萬)", [] {
        const Formatter en(pack("en")), zh(pack("zh")), hant(pack("zh-Hant"));
        auto t = [](const Formatter& f, double v, NumberOptions o = {}) { return f.number(v, o).text; };
        check_eq(t(en, 1234.5), std::string("1,234.5"));
        check_eq(t(en, 1234567.891), std::string("1,234,567.891"));
        check_eq(t(en, 0.0004), std::string("0"));
        check_eq(t(en, -0.0004), std::string("0"));  // a minus only for a value below zero once rounded
        check_eq(t(en, -12.5), std::string("-12.5"));
        check_eq(t(en, 2.675, {.maximum_fraction = 2}), std::string("2.67"));  // 2.675 is 2.67499… in binary, everywhere
        check_eq(t(en, 0.5, {.maximum_fraction = 0}), std::string("0"));      // halves to even
        check_eq(t(en, 1.5, {.maximum_fraction = 0}), std::string("2"));
        check_eq(t(en, 3, {.minimum_fraction = 2}), std::string("3.00"));
        check_eq(t(en, 1234, {.grouping = false}), std::string("1234"));
        check_eq(t(en, 5, {.sign = NumberOptions::Sign::always}), std::string("+5"));
        check_eq(t(en, 0.256, {.style = NumberOptions::Style::percent}), std::string("26%"));
        check_eq(en.number(std::int64_t{-9007199254740993}).text, std::string("-9,007,199,254,740,993"));
        check_eq(en.number(std::numeric_limits<double>::infinity()).text, std::string("∞"));
        check_eq(en.number(std::nan("")).text, std::string("NaN"));
        const NumberOptions shortc{.style = NumberOptions::Style::compact_short}, longc{.style = NumberOptions::Style::compact_long};
        check_eq(t(en, 999, shortc), std::string("999"));
        check_eq(t(en, 1234, shortc), std::string("1.2K"));
        check_eq(t(en, 12345, shortc), std::string("12K"));
        check_eq(t(en, 1234567, shortc), std::string("1.2M"));
        check_eq(t(en, 999950, shortc), std::string("1M"));
        check_eq(t(en, -1500, shortc), std::string("-1.5K"));
        check_eq(t(en, 1234, longc), std::string("1.2 thousand"));
        check_eq(t(en, 1000, longc), std::string("1 thousand"));
        check_eq(t(zh, 1234.5), std::string("1,234.5"));
        check_eq(t(zh, 1234, shortc), std::string("1234"));
        check_eq(t(zh, 123456, shortc), std::string("12万"));
        check_eq(t(zh, 150000000, shortc), std::string("1.5亿"));
        check_eq(t(hant, 123456, shortc), std::string("12萬"));
        const auto f = en.number(1000.0, shortc);
        check(f.operands.c == 3 && f.operands.i == 1, "1K: its operands carry the compact exponent");
    });

    s.test("dates and times: every length, weekdays, day periods (下午), offsets, skeletons", [] {
        const Formatter en(pack("en")), zh(pack("zh")), hant(pack("zh-Hant"));
        check_eq(afternoon.weekday(), 3);
        check_eq(en.date(afternoon, DateStyle::full), std::string("Wednesday, October 28, 2026"));
        check_eq(en.date(afternoon, DateStyle::long_), std::string("October 28, 2026"));
        check_eq(en.date(afternoon, DateStyle::medium), std::string("Oct 28, 2026"));
        check_eq(en.date(afternoon, DateStyle::short_), std::string("10/28/26"));
        check_eq(en.time(afternoon, DateStyle::short_), std::string("3:05 PM"));
        check_eq(en.time(afternoon, DateStyle::medium), std::string("3:05:09 PM"));
        check_eq(en.datetime(afternoon), std::string("Oct 28, 2026, 3:05 PM"));
        check_eq(zh.date(afternoon, DateStyle::full), std::string("2026年10月28日星期三"));
        check_eq(zh.date(afternoon, DateStyle::medium), std::string("2026年10月28日"));
        check_eq(zh.date(afternoon, DateStyle::short_), std::string("2026/10/28"));
        check_eq(zh.time(afternoon, DateStyle::short_), std::string("15:05"));
        check_eq(zh.datetime(afternoon), std::string("2026年10月28日 15:05"));
        check_eq(hant.date(afternoon, DateStyle::full), std::string("2026年10月28日 星期三"));
        check_eq(hant.time(afternoon, DateStyle::short_), std::string("下午3:05"));
        CivilTime night = afternoon;
        night.hour = 2;
        check_eq(hant.time(night, DateStyle::short_), std::string("凌晨2:05"));
        CivilTime shanghai = afternoon;
        shanghai.offset_minutes = 480;
        check_eq(zh.time(shanghai, DateStyle::full), std::string("GMT+08:00 15:05:09"));
        check_eq(en.time(shanghai, DateStyle::long_), std::string("3:05:09 PM GMT+8"));
        check_eq(en.pattern(afternoon, "yyyy-MM-dd'T'HH:mm:ss 'o''clock'"), std::string("2026-10-28T15:05:09 o'clock"));
        check_eq(en.skeleton(afternoon, "yMMMd").value_or("?"), std::string("Oct 28, 2026"));
        check_eq(zh.skeleton(afternoon, "MMMd").value_or("?"), std::string("10月28日"));
        check(!en.skeleton(afternoon, "nonsense").has_value());
        // Seconds since the epoch at an offset; the days before 1970 too.
        const CivilTime t = CivilTime::from_unix(1793199909, 480);
        check(t.year == 2026 && t.month == 10 && t.day == 28 && t.hour == 23 && t.minute == 5);
        const CivilTime before = CivilTime::from_unix(-1, 0);
        check(before.year == 1969 && before.month == 12 && before.day == 31 && before.hour == 23 && before.second == 59);
    });

    s.test("relative times and lists: named days (yesterday, 后天), plural forms, every list type", [] {
        const Formatter en(pack("en")), zh(pack("zh")), hant(pack("zh-Hant"));
        check_eq(en.relative(-1, TimeUnit::day), std::string("yesterday"));
        check_eq(en.relative(-1, TimeUnit::day, Width::wide, false), std::string("1 day ago"));
        check_eq(en.relative(-3, TimeUnit::day), std::string("3 days ago"));
        check_eq(en.relative(2, TimeUnit::day), std::string("in 2 days"));
        check_eq(en.relative(1.5, TimeUnit::hour), std::string("in 1.5 hours"));
        check_eq(zh.relative(2, TimeUnit::day), std::string("后天"));
        check_eq(zh.relative(-3, TimeUnit::day), std::string("3天前"));
        check_eq(zh.relative(5, TimeUnit::day), std::string("5天后"));
        check_eq(hant.relative(5, TimeUnit::day), std::string("5 天後"));
        const std::vector<std::string> three{"Ada", "Grace", "Linus"}, two{"Ada", "Grace"}, one{"Ada"};
        check_eq(en.list(three), std::string("Ada, Grace, and Linus"));
        check_eq(en.list(two), std::string("Ada and Grace"));
        check_eq(en.list(one), std::string("Ada"));
        check_eq(en.list(three, ListType::disjunction), std::string("Ada, Grace, or Linus"));
        check_eq(zh.list(three), std::string("Ada、Grace和Linus"));
        check_eq(zh.list(std::vector<std::string>{"甲", "乙", "丙", "丁"}), std::string("甲、乙、丙和丁"));
        check_eq(en.list(std::vector<std::string>{}), std::string());
    });

    s.test("MessageFormat 2: placeholders, functions and options, declarations, selection by value and by category", [] {
        const Formatter en(pack("en")), zh(pack("zh"));
        auto fmt = [](const Formatter& f, std::string_view src, const Args& args = {}) {
            auto m = Message::parse(src);
            check(m.has_value(), m ? std::string() : m.error());
            return m ? m->format(f, args) : std::string("<unparsed>");
        };
        check_eq(fmt(en, "Hello, {$name}!", {{"name", std::string("Ada")}}), std::string("Hello, Ada!"));
        check_eq(fmt(en, "{$n :number minimumFractionDigits=2} items", {{"n", 3.0}}), std::string("3.00 items"));
        check_eq(fmt(en, "{$share :percent}", {{"share", 0.5}}), std::string("50%"));
        check_eq(fmt(en, "{$n :number notation=compact} views", {{"n", std::int64_t{12345}}}), std::string("12K views"));
        check_eq(fmt(zh, "{$n :number notation=compact}次播放", {{"n", std::int64_t{123456}}}), std::string("12万次播放"));
        check_eq(fmt(en, "Due {$when :date style=long}", {{"when", afternoon}}), std::string("Due October 28, 2026"));
        check_eq(fmt(zh, "截止{$when :datetime dateStyle=medium timeStyle=short}", {{"when", afternoon}}), std::string("截止2026年10月28日 15:05"));
        check_eq(fmt(en, "With {$who :list}", {{"who", std::vector<std::string>{"Ada", "Grace", "Linus"}}}), std::string("With Ada, Grace, and Linus"));
        check_eq(fmt(en, "Escapes: \\{ \\} \\\\ and {|literal {}|}"), std::string("Escapes: { } \\ and literal {}"));
        check_eq(fmt(en, "Markup {#b}stays{/b} text"), std::string("Markup stays text"));
        const std::string_view inbox = ".input {$count :number}\n.match $count\n0 {{No new messages}}\none {{One new message}}\n* {{{$count} new messages}}";
        check_eq(fmt(en, inbox, {{"count", std::int64_t{0}}}), std::string("No new messages"));
        check_eq(fmt(en, inbox, {{"count", std::int64_t{1}}}), std::string("One new message"));
        check_eq(fmt(en, inbox, {{"count", std::int64_t{1234}}}), std::string("1,234 new messages"));
        const std::string_view place = ".input {$n :number select=ordinal}\n.match $n\none {{{$n}st}}\ntwo {{{$n}nd}}\nfew {{{$n}rd}}\n* {{{$n}th}}";
        check_eq(fmt(en, place, {{"n", std::int64_t{22}}}), std::string("22nd"));
        check_eq(fmt(en, place, {{"n", std::int64_t{13}}}), std::string("13th"));
        const std::string_view two = ".input {$photos :number}\n.input {$who :string}\n.match $photos $who\n"
                                     "1 me {{You added a photo}}\n* me {{You added {$photos} photos}}\n1 * {{{$who} added a photo}}\n* * {{{$who} added {$photos} photos}}";
        check_eq(fmt(en, two, {{"photos", std::int64_t{1}}, {"who", std::string("me")}}), std::string("You added a photo"));
        check_eq(fmt(en, two, {{"photos", std::int64_t{3}}, {"who", std::string("Ada")}}), std::string("Ada added 3 photos"));
        check_eq(fmt(en, ".local $total = {$n :number maximumFractionDigits=0}\n{{About {$total}}}", {{"n", 9.6}}), std::string("About 10"));
        // What cannot be resolved shows its fallback, and is reported.
        std::vector<std::string> errors;
        auto m = Message::parse("Hi {$missing}, {$n :nosuch}");
        check(m.has_value());
        if (m) {
            check_eq(m->format(en, {{"n", 1.0}}, &errors), std::string("Hi {$missing}, {$n}"));
            check_eq(errors.size(), std::size_t{2});
            check((m->variables() == std::vector<std::string>{"missing", "n"}));
        }
        // Syntax errors say where.
        for (auto [src, where] : {std::pair{"Hi {$}", "1:6"}, std::pair{".match $n\n1 {{one}}", "2:10"}, std::pair{"Hi }", "1:4"},
                                  std::pair{"Hi {$n :number x}", "1:17"}})
            check(!Message::parse(src).has_value() && Message::parse(src).error().starts_with(where), std::format("{}: {}", src, Message::parse(src).error()));
    });

    s.test("catalogs and a translator: TOML tables of keys, the negotiated chain, each message in its own locale's ways", [] {
        Library lib;
        lib.add(pack("en"));
        lib.add(pack("zh"));
        lib.add(pack("zh-Hant"));
        lib.set_likely(*read_likely_subtags(read(packs() / "likely.locale.omui.json")));
        auto en = read_messages(R"(title = "Inbox"
[inbox]
count = ".input {$count :number}\n.match $count\none {{{$count} new message}}\n* {{{$count} new messages}}"
only_english = "Only in English"
)",
                                "app.en.messages.omui.toml");
        auto zh = read_messages(R"(title = "收件箱"
[inbox]
count = "{$count :number} 条新消息"
)",
                                "app.zh-Hans.messages.omui.toml");
        check(en.has_value() && zh.has_value());
        if (!en || !zh) return;
        lib.catalog(tag("en")) = *en;
        lib.catalog(tag("zh-Hans")) = *zh;
        const std::array asked{tag("zh-TW")};
        Translator t(lib, asked, tag("en"));
        check_eq(t.tr("title"), std::string("收件箱"));
        check_eq(t.tr("inbox.count", {{"count", std::int64_t{1234}}}), std::string("1,234 条新消息"));
        const auto r = t.translate("inbox.only_english");
        check(r.found && r.text == "Only in English" && r.from == tag("en"));
        const auto missing = t.translate("nowhere");
        check(!missing.found && missing.text == "nowhere" && !missing.errors.empty());
        check_eq(t.formatter().date(afternoon, DateStyle::full), std::string("2026年10月28日 星期三"));  // zh-TW's own ways
        const std::array english{tag("en-GB")};
        check_eq(Translator(lib, english, tag("en")).tr("inbox.count", {{"count", std::int64_t{1}}}), std::string("1 new message"));
        // Problems, located; a translation checked against its source.
        auto bad = read_messages("[a]\nb = \"{$x\"\nc = 3\n", "bad.messages.omui.toml");
        check(!bad.has_value());
        if (!bad) {
            check_eq(bad.error().size(), std::size_t{2});
            check(bad.error()[0].text().starts_with("bad.messages.omui.toml:2:"), bad.error()[0].text());
        }
        const auto diff = compare(*en, *zh);
        check(std::ranges::find(diff, std::string("`inbox.only_english` is not translated")) != diff.end(), diff.empty() ? "" : diff.front());
        auto pack_problems = read_locale_data(R"({"format":"openmui.locale@0.1","locale":"x-?!","plural":{"cardinal":{"seven":"n = 7"}}})");
        check(!pack_problems.has_value() && pack_problems.error().size() >= 2);
    });

    s.test("pseudo-localisation: text accented and 40% longer, placeholders and syntax untouched", [] {
        check_eq(pseudo_localize("Save {$n} files"), std::string("⟦Šàṽé {$n} ƒîļéš ~~~~⟧"));
        const std::string complex = pseudo_localize(".input {$n :number}\n.match $n\none {{One file}}\n* {{{$n} files}}");
        check(complex.starts_with(".input {$n :number}\n.match $n\none {{⟦Öñé ƒîļé ~~~⟧}}"), complex);
        const Formatter en(pack("en"));
        auto m = Message::parse(complex);
        check(m.has_value(), m ? "" : m.error());
        if (m) check_eq(m->format(en, {{"n", std::int64_t{3}}}), std::string("⟦3 ƒîļéš ~~⟧"));
    });

    return s.run(argc, argv);
}
