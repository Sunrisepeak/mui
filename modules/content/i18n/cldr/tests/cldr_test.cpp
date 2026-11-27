// The embedded packs read whole and agree with the files they were made from.
import std;
import mlibs.i18n;
import mlibs.i18n.cldr;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace mlibs::i18n;

UTest cases{"i18n.cldr", [] {

    "every pack and the likely subtags are in the program and read whole"_test = [] {
        for (std::string_view id : cldr::locales) {
            auto d = cldr::data(id);
            expect(d != nullptr) << std::string(id);
            if (d) expect(eq(d->cldr, std::string(cldr::version)));
        }
        expect(cldr::likely().size() > 20) << "the likely subtags of en and zh";
        expect(!cldr::data("fr")) << "not packed";
    };

    "a library with the packs: a Traditional Chinese reader's numbers and dates"_test = [] {
        Library lib;
        cldr::add_to(lib);
        const std::array asked{*Locale::parse("zh-HK")};
        Translator t(lib, asked, *Locale::parse("en"));
        expect(eq(t.formatter().number(123456.0, {.style = NumberOptions::Style::compact_short}).text, std::string("12萬")));
        expect(eq(t.formatter().date(CivilTime{2026, 10, 28}, DateStyle::full), std::string("2026年10月28日 星期三")));
    };
}};
