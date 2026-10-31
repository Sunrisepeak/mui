// openmui.i18n:data — what a locale pack holds (`<tag>.locale.omui.json`,
// generated from CLDR by `omui-gen cldr`): plural rules, number symbols and
// patterns, compact forms, calendar names and patterns, relative times and
// list patterns. The engine knows languages only through these; a pack is
// read whole (a problem is a diagnostic with its place), never compiled in.
export module openmui.i18n:data;

import std;
import :locale;
import :plural;

export namespace omui::i18n {

// The widths CLDR gives names and patterns in.
enum class Width : std::uint8_t { wide, abbreviated, narrow };
// The four lengths of CLDR's date and time formats.
enum class DateStyle : std::uint8_t { full, long_, medium, short_ };

struct NumberData {
    std::string decimal = ".", group = ",", minus = "-", plus = "+", percent = "%", exponential = "E", infinity = "∞", nan = "NaN";
    std::string decimal_pattern = "#,##0.###";
    std::string percent_pattern = "#,##0%";
    int         minimum_grouping = 1;  // integer digits beyond the group before grouping starts (es: 2)
    // Compact forms (1.2K, 1.2万): for each power of ten, a pattern per plural
    // category; "0" means the number is shown plain.
    struct Compact {
        int         magnitude = 0;  // 3 for 1000
        Plural      count = Plural::other;
        std::string pattern;        // "0K", "00万"
    };
    std::vector<Compact> compact_short, compact_long;
};

struct CalendarData {
    // Indexed by month - 1 and by weekday (0 Sunday); format context, and
    // stand-alone for the L and c fields.
    std::array<std::array<std::string, 12>, 3> months, months_standalone;  // by Width
    std::array<std::array<std::string, 7>, 3>  days, days_standalone;
    std::array<std::string, 7>                 days_short;  // EEEEEE: Su, 周日
    std::array<std::string, 3>                 am, pm;      // by Width
    std::array<std::string, 2>                 eras;        // abbreviated: BC, AD
    std::array<std::string, 4>                 date_formats, time_formats, datetime_formats;  // by DateStyle
    std::vector<std::pair<std::string, std::string>> skeletons;  // availableFormats: yMMMd → MMM d, y
    // Flexible day periods (B): a name from a time of day, CLDR's rules.
    struct Period {
        std::string name;      // morning1
        int         from = 0;  // minutes after midnight, inclusive
        int         before = 0;  // exclusive; `at` when from == before
        bool        at = false;
    };
    std::vector<Period>                              period_rules;
    std::vector<std::pair<std::string, std::string>> period_names;  // morning1 → 早上
    // Localised offsets: GMT{0} with +HH:mm;-HH:mm, GMT for zero.
    std::string gmt_format = "GMT{0}", gmt_zero = "GMT", hour_format = "+HH:mm;-HH:mm";
};

enum class TimeUnit : std::uint8_t { year, quarter, month, week, day, hour, minute, second };

struct RelativeData {
    struct Unit {
        std::vector<std::pair<int, std::string>>    named;   // -1 → yesterday
        std::vector<std::pair<Plural, std::string>> future;  // other → in {0} days
        std::vector<std::pair<Plural, std::string>> past;    // other → {0} days ago
    };
    std::array<std::array<Unit, 3>, 8> units;  // by TimeUnit, then Width
};

enum class ListType : std::uint8_t { conjunction, disjunction, unit };

struct ListData {
    struct Patterns {
        std::string start = "{0}, {1}", middle = "{0}, {1}", end = "{0}, {1}", two = "{0}, {1}";
    };
    std::array<std::array<Patterns, 3>, 3> types;  // by ListType, then Width
};

struct LocaleData {
    Locale       locale;
    std::string  cldr;  // the CLDR version it was made from
    PluralRules  cardinal, ordinal;
    NumberData   number;
    CalendarData calendar;
    RelativeData relative;
    ListData     lists;
};

// What a pack or a catalog says is wrong, and where.
struct Diagnostic {
    std::string file;
    int         line = 0, column = 0;
    std::string message;
    [[nodiscard]] std::string text() const;
};

// `<tag>.locale.omui.json`: one locale's data.
[[nodiscard]] std::expected<LocaleData, std::vector<Diagnostic>> read_locale_data(std::string_view json, std::string_view file = "locale.omui.json");
// `likely.locale.omui.json`: CLDR's likely subtags for the languages packed.
[[nodiscard]] std::expected<LikelySubtags, std::vector<Diagnostic>> read_likely_subtags(std::string_view json, std::string_view file = "likely.locale.omui.json");

}  // namespace omui::i18n
