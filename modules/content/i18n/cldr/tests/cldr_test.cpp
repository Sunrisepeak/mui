// The embedded packs read whole and agree with the files they were made from.
import std;
import openmui.i18n;
import openmui.i18n.cldr;
import openmui.testing;

using namespace omui::testing;
using namespace omui::i18n;

int main(int argc, char** argv) {
    Suite s{"i18n.cldr"};

    s.test("every pack and the likely subtags are in the program and read whole", [] {
        for (std::string_view id : cldr::locales) {
            auto d = cldr::data(id);
            check(d != nullptr, std::string(id));
            if (d) check_eq(d->cldr, std::string(cldr::version));
        }
        check(cldr::likely().size() > 20, "the likely subtags of en and zh");
        check(!cldr::data("fr"), "not packed");
    });

    s.test("a library with the packs: a Traditional Chinese reader's numbers and dates", [] {
        Library lib;
        cldr::add_to(lib);
        const std::array asked{*Locale::parse("zh-HK")};
        Translator t(lib, asked, *Locale::parse("en"));
        check_eq(t.formatter().number(123456.0, {.style = NumberOptions::Style::compact_short}).text, std::string("12萬"));
        check_eq(t.formatter().date(CivilTime{2026, 10, 28}, DateStyle::full), std::string("2026年10月28日 星期三"));
    });

    return s.run(argc, argv);
}
