// openmui.i18n:format — values as a locale writes them: numbers (decimal,
// percent, compact: 1.2K, 12万), dates and times (CLDR's patterns and
// skeletons), relative times (3 days ago, 3天前), and lists (a, b, and c;
// a、b和c). Each is a function of the value and the locale's data alone:
// digits come from the standard library's exact conversions, never from the
// C locale or the system, so every platform writes the same text.
export module openmui.i18n:format;

import std;
import :locale;
import :plural;
import :data;

export namespace omui::i18n {

struct NumberOptions {
    enum class Style : std::uint8_t { decimal, percent, compact_short, compact_long };
    enum class Sign : std::uint8_t { negative, always, never };  // negative: a minus for a value below zero once rounded
    Style              style = Style::decimal;
    std::optional<int> minimum_fraction, maximum_fraction;  // the pattern's by default
    std::optional<int> minimum_integer;
    bool               grouping = true;
    Sign               sign = Sign::negative;
};

// A number as written, with the operands of the digits it shows (what its
// plural category is chosen by).
struct FormattedNumber {
    std::string text;
    Operands    operands;
};

// A moment as a calendar on a wall shows it: the proleptic Gregorian
// calendar, a fixed offset from UTC (time zones by name are the caller's).
struct CivilTime {
    int year = 1970, month = 1, day = 1;
    int hour = 0, minute = 0, second = 0, millisecond = 0;
    int offset_minutes = 0;  // east of UTC: +480 for China

    // Seconds since 1970-01-01T00:00:00Z, seen `offset_minutes` east of UTC.
    [[nodiscard]] static CivilTime from_unix(std::int64_t seconds, int offset_minutes = 0, int millisecond = 0);
    [[nodiscard]] int weekday() const noexcept;  // 0 Sunday … 6 Saturday
};

class Formatter {
public:
    explicit Formatter(std::shared_ptr<const LocaleData> data);

    [[nodiscard]] const LocaleData& data() const noexcept { return *data_; }
    [[nodiscard]] const Locale& locale() const noexcept { return data_->locale; }

    [[nodiscard]] FormattedNumber number(double value, const NumberOptions& options = {}) const;
    [[nodiscard]] FormattedNumber number(std::int64_t value, const NumberOptions& options = {}) const;
    [[nodiscard]] Plural plural(const Operands& x, bool ordinal = false) const noexcept;

    [[nodiscard]] std::string date(const CivilTime& t, DateStyle style = DateStyle::medium) const;
    [[nodiscard]] std::string time(const CivilTime& t, DateStyle style = DateStyle::short_) const;
    [[nodiscard]] std::string datetime(const CivilTime& t, DateStyle date_style = DateStyle::medium, DateStyle time_style = DateStyle::short_) const;
    // A CLDR pattern (`y年M月d日 EEEE`, `h:mm a`), its letters the fields.
    [[nodiscard]] std::string pattern(const CivilTime& t, std::string_view cldr_pattern) const;
    // A skeleton (`yMMMd`, `Hm`): the locale's pattern for it; nothing when the pack has none.
    [[nodiscard]] std::optional<std::string> skeleton(const CivilTime& t, std::string_view skeleton) const;

    // `value` units from now: negative is past. With `named`, a whole value
    // the locale has a word for uses it (yesterday, 明天).
    [[nodiscard]] std::string relative(double value, TimeUnit unit, Width width = Width::wide, bool named = true) const;
    [[nodiscard]] std::string list(std::span<const std::string> items, ListType type = ListType::conjunction, Width width = Width::wide) const;

private:
    std::shared_ptr<const LocaleData> data_;
};

}  // namespace omui::i18n
