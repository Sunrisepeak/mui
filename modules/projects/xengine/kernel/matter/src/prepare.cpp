// A definition made a plan: names resolved, values checked against their columns, conditions and
// amounts parsed into sums — once, with every problem said by where it is in the definition.
module xengine.matter;

import std;
import mlibs.error;
import mlibs.names;
import mlibs.trace;

namespace xengine::matter {
namespace {

[[nodiscard]] bool is_identifier(std::string_view name) noexcept {
    if (name.empty() || (name.front() >= '0' && name.front() <= '9')) return false;
    return std::ranges::all_of(name, [](char letter) {
        return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '_';
    });
}

// What a name in a law may be, while a definition is prepared.
struct Names {
    const std::vector<Column>&      columns;
    const std::vector<Channel>&     channels;
    const std::vector<Material>&    materials;
    [[nodiscard]] std::optional<std::uint32_t> column(std::string_view name) const {
        for (std::uint32_t index = 0; index < columns.size(); ++index)
            if (columns[index].name == name) return index;
        return std::nullopt;
    }
    [[nodiscard]] std::optional<std::uint32_t> channel(std::string_view name) const {
        for (std::uint32_t index = 0; index < channels.size(); ++index)
            if (channels[index].name == name) return index;
        return std::nullopt;
    }
    [[nodiscard]] std::optional<MaterialId> material(std::string_view name) const {
        for (MaterialId index = 0; index < materials.size(); ++index)
            if (materials[index].name == name) return index;
        return std::nullopt;
    }
    // "did you mean": among everything a law may read.
    [[nodiscard]] std::string nearest(std::string_view name) const {
        std::vector<std::string_view> all;
        for (const Column& each : columns) all.push_back(each.name);
        for (const Channel& each : channels) all.push_back(each.name);
        for (const std::string_view each : phase_names) all.push_back(each);
        return mlibs::names::nearest(name, all);
    }
};

// A condition's or an amount's text, read left to right. Terms are read with the bits after the point their
// names have; a comparison or an amount is then brought to one number of them.
class Parser {
public:
    Parser(std::string_view text, const Names& names, bool recipe, std::uint32_t max_terms) : text_(text), names_(names), recipe_(recipe), max_terms_(max_terms) {}

    // comparisons joined by `&&`
    [[nodiscard]] std::expected<std::vector<Test>, Failure> condition() {
        std::vector<Test> tests;
        for (;;) {
            Test test;
            auto left = sum();
            if (!left) return std::unexpected(left.error());
            skip();
            static constexpr std::array<std::pair<std::string_view, Compare>, 6> operators{{{">=", Compare::greater_equal}, {"<=", Compare::less_equal}, {"==", Compare::equal},
                                                                                           {"!=", Compare::not_equal},     {">", Compare::greater},     {"<", Compare::less}}};
            bool found = false;
            for (const auto& [sign, compare] : operators)
                if (text_.substr(at_).starts_with(sign)) {
                    test.compare = compare;
                    at_ += sign.size();
                    found = true;
                    break;
                }
            if (!found) return fail(Errc::expression, "a comparison is expected here (>=, <=, >, <, ==, !=)");
            auto right = sum();
            if (!right) return std::unexpected(right.error());
            // Both sides in the finest unit either has.
            std::uint32_t fraction = 0;
            for (const auto* side : {&*left, &*right})
                for (const Loose& loose : *side)
                    if (loose.term.source != Source::constant) fraction = std::max(fraction, loose.fraction);
            auto one = aligned(*left, fraction), other = aligned(*right, fraction);
            if (!one) return std::unexpected(one.error());
            if (!other) return std::unexpected(other.error());
            test.left = std::move(*one);
            test.right = std::move(*other);
            tests.push_back(std::move(test));
            skip();
            if (at_ == text_.size()) return tests;
            if (!text_.substr(at_).starts_with("&&")) return fail(Errc::expression, "conditions are joined by `&&`");
            at_ += 2;
        }
    }
    // a whole amount: a sum and nothing after it, in its target's unit
    [[nodiscard]] std::expected<Sum, Failure> amount(std::uint32_t fraction) {
        auto out = sum();
        if (!out) return std::unexpected(out.error());
        skip();
        if (at_ != text_.size()) return fail(Errc::expression, "an amount is a sum of terms: nothing follows it");
        return aligned(*out, fraction);
    }
    [[nodiscard]] bool reads_near() const noexcept { return reads_near_; }

private:
    // A term as read, and the bits after the point of what it names.
    struct Loose {
        Term          term;
        std::uint32_t fraction = 0;
    };
    [[nodiscard]] std::unexpected<Failure> fail(Errc code, std::string message) const {
        return std::unexpected(Failure{code, std::format("{} — at column {} of `{}`", message, at_ + 1, text_), {}, {}});
    }
    // Every term to `fraction` bits after the point: a number × 2^fraction, a name shifted by the difference.
    [[nodiscard]] std::expected<Sum, Failure> aligned(const std::vector<Loose>& terms, std::uint32_t fraction) const {
        Sum out;
        for (const Loose& loose : terms) {
            Term term = loose.term;
            if (term.source == Source::constant) {
                const std::uint64_t scaled = static_cast<std::uint64_t>(term.index) << fraction;
                if (scaled > std::numeric_limits<std::uint32_t>::max()) return fail(Errc::expression, std::format("{} is beyond what {} bits after the point leave room for", term.index, fraction));
                term.index = static_cast<std::uint32_t>(scaled);
            } else if (fraction >= loose.fraction) {
                const std::uint64_t factor = static_cast<std::uint64_t>(term.multiply) << (fraction - loose.fraction);
                if (factor > std::numeric_limits<std::uint32_t>::max()) return fail(Errc::expression, "a factor beyond 4294967295 once brought to its target's unit");
                term.multiply = static_cast<std::uint32_t>(factor);
            } else {
                term.shift += loose.fraction - fraction;
                if (term.shift > 31) return fail(Errc::expression, "a divisor beyond 2^31 once brought to its target's unit");
            }
            out.terms.push_back(term);
        }
        return out;
    }
    void skip() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t')) ++at_;
    }
    [[nodiscard]] std::optional<std::uint32_t> whole() {
        skip();
        std::uint64_t value = 0;
        const std::size_t start = at_;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
            value = value * 10 + static_cast<std::uint64_t>(text_[at_] - '0');
            if (value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
            ++at_;
        }
        if (at_ == start) return std::nullopt;
        return static_cast<std::uint32_t>(value);
    }
    // terms added and taken
    [[nodiscard]] std::expected<std::vector<Loose>, Failure> sum() {
        std::vector<Loose> out;
        bool negative = false;
        for (;;) {
            auto next = term();
            if (!next) return std::unexpected(next.error());
            next->term.negative = negative;
            out.push_back(*next);
            if (out.size() > max_terms_) return fail(Errc::limit, std::format("a sum has at most {} terms", max_terms_));
            skip();
            if (at_ < text_.size() && (text_[at_] == '+' || (text_[at_] == '-'))) {
                negative = text_[at_] == '-';
                ++at_;
                continue;
            }
            return out;
        }
    }
    [[nodiscard]] static bool digit(char letter) noexcept { return letter >= '0' && letter <= '9'; }
    [[nodiscard]] static bool letter_of_name(char letter) noexcept { return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || letter == '_' || digit(letter); }
    [[nodiscard]] char peek() {
        skip();
        return at_ < text_.size() ? text_[at_] : '\0';
    }
    // a name or a whole number, × whole numbers, / powers of two: `wetness * 40`, `3 * heat / 8`, `12`
    [[nodiscard]] std::expected<Loose, Failure> term() {
        Loose out;
        std::uint64_t factor = 1;
        if (digit(peek())) {
            const auto value = whole();
            if (!value) return fail(Errc::expression, "a whole number up to 4294967295");
            // `40 * wetness`: a number before a name is its factor
            std::size_t after = at_;
            while (after < text_.size() && text_[after] == ' ') ++after;
            std::size_t named = after + 1;
            while (named < text_.size() && text_[named] == ' ') ++named;
            if (after < text_.size() && text_[after] == '*' && named < text_.size() && !digit(text_[named])) {
                at_ = named;
                auto read = name();
                if (!read) return read;
                out = *read;
                factor = *value;
            } else {
                out.term.index = *value;
            }
        } else {
            auto read = name();
            if (!read) return read;
            out = *read;
        }
        for (;;) {
            const char next = peek();
            if (next == '*') {
                ++at_;
                const auto by = whole();
                if (!by) return fail(Errc::expression, "a term is multiplied by a whole number");
                factor *= *by;
                if (factor > std::numeric_limits<std::uint32_t>::max()) return fail(Errc::expression, "a factor beyond 4294967295");
            } else if (next == '/') {
                ++at_;
                const auto by = whole();
                if (!by || !std::has_single_bit(*by)) return fail(Errc::expression, "a term is divided by a power of two (2, 4, 8 …): the same on every machine");
                out.term.shift += static_cast<std::uint32_t>(std::countr_zero(*by));
                if (out.term.shift > 31) return fail(Errc::expression, "a divisor beyond 2^31");
            } else {
                break;
            }
        }
        out.term.multiply = static_cast<std::uint32_t>(factor);
        if (out.term.source == Source::constant) {
            // A number alone is its value: folded now.
            const std::uint64_t product = static_cast<std::uint64_t>(out.term.index) * out.term.multiply;
            out.term.index = static_cast<std::uint32_t>(std::min<std::uint64_t>(product, std::numeric_limits<std::uint32_t>::max()) >> out.term.shift);
            out.term.multiply = 1;
            out.term.shift = 0;
        }
        return out;
    }
    [[nodiscard]] std::string_view word() {
        const std::size_t start = at_;
        while (at_ < text_.size() && letter_of_name(text_[at_])) ++at_;
        return text_.substr(start, at_ - start);
    }
    [[nodiscard]] std::expected<Loose, Failure> name() {
        skip();
        std::string_view read = word();
        if (read.empty() || digit(read.front())) return fail(Errc::expression, "a name or a whole number is expected here");
        bool near = false;
        if (read == "near" && at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            read = word();
            near = true;
            if (read.empty()) return fail(Errc::expression, "`near.` is followed by what is summed over the neighbours");
        }
        Loose loose;
        Term& out = loose.term;
        if (const auto channel = names_.channel(read)) {
            if (*channel == 0 && !recipe_)
                return fail(Errc::names_material, "a law reads properties, not which material a thing is (a special case says `recipe: true`)");
            out.source = near ? Source::near_channel : Source::channel;
            out.index = *channel;
            loose.fraction = names_.channels[*channel].fraction;
        } else if (const auto column = names_.column(read)) {
            if (near && names_.columns[*column].kind != ColumnKind::number)
                return fail(Errc::expression, std::format("`near.{}`: only a number is summed over the neighbours", read));
            out.source = near ? Source::near_property : Source::property;
            out.index = *column;
            loose.fraction = names_.columns[*column].kind == ColumnKind::number ? names_.columns[*column].fraction : 0;
        } else if (near) {
            return fail(Errc::name, std::format("`near.{}`: no channel or property of that name ({})", read, names_.nearest(read)));
        } else if (const auto phase = std::ranges::find(phase_names, read); phase != phase_names.end()) {
            out.index = static_cast<std::uint32_t>(phase - phase_names.begin());
        } else if (read == "none") {
            out.index = none;
        } else if (const auto material = names_.material(read)) {
            if (!recipe_)
                return fail(Errc::names_material,
                            std::format("`{}` is a material: a law reads properties, so that whatever has them takes part (a special case says `recipe: true`)", read));
            out.index = *material;
        } else {
            return fail(Errc::name, std::format("`{}` is no channel, property or phase ({})", read, names_.nearest(read)));
        }
        reads_near_ |= near;
        return loose;
    }

    std::string_view text_;
    const Names&     names_;
    bool             recipe_;
    std::uint32_t    max_terms_;
    std::size_t      at_ = 0;
    bool             reads_near_ = false;
};

}  // namespace

Definition Definition::standard() {
    Definition out;
    const auto number = [&](std::string name, std::string unit, std::string doc, double least, double most, double initial, std::uint32_t fraction = 0) {
        out.columns.push_back({std::move(name), ColumnKind::number, std::move(unit), std::move(doc), fraction, least, most, initial});
    };
    const auto becomes = [&](std::string name, std::string doc) {
        out.columns.push_back({std::move(name), ColumnKind::material, "material", std::move(doc), 0, 0, 0, static_cast<double>(none)});
    };
    // mechanics: what dynamics reads
    number("density", "kg/m³", "how heavy a volume of it is: what sinks below what", 0, 100000, 1000, 4);
    number("friction", "0–1", "of the speed along a contact lost to it", 0, 1, 0.3, 16);
    number("restitution", "0–1", "of the speed into a contact kept off it", 0, 1, 0.4, 16);
    number("compliance", "m/N", "how much it gives when stretched (0: rigid)", 0, 1000, 0, 20);
    number("shape_stiffness", "0–1", "a soft solid's spring back to its shape, a substep", 0, 1, 0.05, 16);
    // heat
    number("thermal", "64ths", "how well it conducts heat: of a difference in temperature, to each neighbour, a step", 0, 8, 1);
    number("ignition", "K", "the temperature it catches fire at", 0, 100000, 0, 8);
    number("melting", "K", "below it a liquid sets, above it a solid melts", 0, 100000, 0, 8);
    number("boiling", "K", "above it a liquid boils, below it a gas condenses", 0, 100000, 0, 8);
    number("warmth", "K a step", "the heat it gives off by itself", 0, 100000, 0, 8);
    number("starts_at", "K", "the temperature a new instance of it has (0: the surroundings')", 0, 100000, 0, 8);
    // burning
    number("fuel", "", "how much of it there is to burn", 0, 1000000000, 0);
    number("burn_rate", "a step", "how fast its fuel burns", 0, 1000000, 1);
    number("heat", "K a step", "the heat its burning gives off", 0, 100000, 0, 8);
    number("oxygen", "", "what it gives a fire beside it", 0, 1000, 0);
    // water
    number("moist", "", "the wetness it holds by its nature (water's own)", 0, 1000000, 0);
    number("porous", "64ths", "how well wetness passes through it", 0, 8, 0);
    // electricity
    number("conduct", "64ths", "how well it conducts charge", 0, 8, 0);
    // what it is, and what it becomes
    out.columns.push_back({"phase", ColumnKind::phase, "phase", "how it is held together: solid, soft, granular, liquid, gas", 0, 0, 4, 0});
    becomes("frozen", "what it is when it sets: water's is ice, lava's stone");
    becomes("melted", "what it is when it melts");
    becomes("boiled", "what it is when it boils: water's is steam");
    becomes("condensed", "what it is when it condenses");
    becomes("burnt", "what is left when its fuel is gone");

    out.channels = {
        {"material", "material", "which material the instance is", 0, 0, "", false, "", ""},
        {"temperature", "K", "how hot it is", 293, 8, "starts_at", false, "", ""},
        {"wetness", "", "how much water is in it", 0, 0, "moist", false, "", ""},
        {"charge", "", "the electric charge on it", 0, 0, "", false, "", ""},
        {"burned", "", "how much of its fuel has burned", 0, 0, "", true, "", ""},
        {"burning", "0 or 1", "whether it is burning", 0, 0, "", true, "ignite", "go_out"},
        {"damage", "", "how much of its strength is gone", 0, 0, "", true, "", ""},
    };
    return out;
}

std::expected<Program, Failure> Program::prepare(const Definition& definition, Limits limits) {
    mlibs::trace::Span span("simulation.matter.prepare", "simulation");
    const auto fail = [](Errc code, std::string where, std::string message) { return std::unexpected(Failure{code, std::move(message), std::move(where), {}}); };
    if (definition.columns.size() > limits.max_columns) return fail(Errc::limit, "/columns", std::format("{} columns: at most {}", definition.columns.size(), limits.max_columns));
    if (definition.channels.size() > limits.max_channels) return fail(Errc::limit, "/channels", std::format("{} channels: at most {}", definition.channels.size(), limits.max_channels));
    if (definition.materials.size() > limits.max_materials) return fail(Errc::limit, "/materials", std::format("{} materials: at most {}", definition.materials.size(), limits.max_materials));
    if (definition.laws.size() > limits.max_laws) return fail(Errc::limit, "/laws", std::format("{} laws: at most {}", definition.laws.size(), limits.max_laws));
    if (definition.channels.empty() || definition.channels.front().name != "material")
        return fail(Errc::name, "/channels", "the first channel is `material`: which material an instance is");

    Program program;
    // Every name is one thing: a column, a channel, a material, a phase, or a word of the conditions.
    std::vector<std::string_view> taken{"near", "none"};
    taken.insert(taken.end(), phase_names.begin(), phase_names.end());
    const auto claim = [&](std::string_view name, const std::string& where) -> std::expected<void, Failure> {
        if (!is_identifier(name)) return fail(Errc::name, where, std::format("`{}` is not a name (letters, digits and _, not beginning with a digit)", name));
        if (std::ranges::find(taken, name) != taken.end()) return fail(Errc::name, where, std::format("`{}` is already a column, a channel, a material, a phase or a word of the conditions", name));
        taken.push_back(name);
        return {};
    };
    // events by name
    const auto event = [&](const std::string& name) -> std::optional<std::uint32_t> {
        if (name.empty()) return std::nullopt;
        if (const auto found = std::ranges::find(program.events_, name); found != program.events_.end()) return static_cast<std::uint32_t>(found - program.events_.begin());
        program.events_.push_back(name);
        return static_cast<std::uint32_t>(program.events_.size() - 1);
    };

    for (const Column& column : definition.columns) {
        const std::string where = "/columns/" + column.name;
        if (auto claimed = claim(column.name, where); !claimed) return std::unexpected(claimed.error());
        if (column.kind == ColumnKind::number) {
            if (column.fraction > 31 || !(column.least <= column.most) || column.least < 0 || std::ldexp(column.most, static_cast<int>(column.fraction)) > 4294967295.0)
                return fail(Errc::value, where, std::format("column `{}`: its range × 2^{} does not fit 32 bits, or is empty", column.name, column.fraction));
            if (!(column.initial >= column.least && column.initial <= column.most))
                return fail(Errc::value, where, std::format("column `{}`: its default {} is outside {} … {}", column.name, column.initial, column.least, column.most));
        }
        if (column.kind == ColumnKind::phase) program.phase_column_ = static_cast<std::uint32_t>(program.columns_.size());
        program.columns_.push_back(column);
    }
    for (const ChannelSpec& channel : definition.channels) {
        const std::string where = "/channels/" + channel.name;
        if (auto claimed = claim(channel.name, where); !claimed) return std::unexpected(claimed.error());
        for (const std::string* named : {&channel.rise, &channel.fall})
            if (!named->empty() && !is_identifier(*named)) return fail(Errc::name, where, std::format("`{}` is not an event's name", *named));
        std::optional<std::uint32_t> from;
        if (!channel.from.empty()) {
            for (std::uint32_t column = 0; column < program.columns_.size(); ++column)
                if (program.columns_[column].name == channel.from && program.columns_[column].kind == ColumnKind::number) from = column;
            if (!from) return fail(Errc::name, where + "/from", std::format("channel `{}` starts from `{}`, which is no property that is a number", channel.name, channel.from));
        }
        if (channel.fraction > 31 || channel.initial < 0 || std::ldexp(channel.initial, static_cast<int>(channel.fraction)) > 4294967295.0)
            return fail(Errc::value, where, std::format("channel `{}`: its start {} × 2^{} does not fit 32 bits", channel.name, channel.initial, channel.fraction));
        if (from && program.columns_[*from].fraction != channel.fraction)
            return fail(Errc::value, where + "/from", std::format("channel `{}` ({} bits after the point) starts from `{}` ({}): the two are one unit", channel.name, channel.fraction, channel.from,
                                                                  program.columns_[*from].fraction));
        program.channels_.push_back({channel.name, channel.unit, channel.doc, static_cast<std::uint32_t>(std::llround(std::ldexp(channel.initial, static_cast<int>(channel.fraction)))),
                                     channel.fraction, from, channel.resets, event(channel.rise), event(channel.fall)});
    }
    for (const MaterialSpec& material : definition.materials) {
        const std::string where = "/materials/" + material.name;
        if (auto claimed = claim(material.name, where); !claimed) return std::unexpected(claimed.error());
        program.materials_.push_back({material.name, material.tags});
    }
    const Names names{program.columns_, program.channels_, program.materials_};

    // Each material's row: the defaults, then what it says.
    const std::size_t width = program.columns_.size();
    program.values_.assign(program.materials_.size() * width, 0);
    program.numbers_.assign(program.materials_.size() * width, 0);
    for (std::size_t row = 0; row < definition.materials.size(); ++row) {
        const MaterialSpec& material = definition.materials[row];
        std::vector<bool> said(width, false);
        const auto store = [&](std::size_t column, double number) {
            const Column& of = program.columns_[column];
            program.numbers_[row * width + column] = number;
            program.values_[row * width + column] = of.kind == ColumnKind::number ? static_cast<std::uint32_t>(std::llround(std::ldexp(number, static_cast<int>(of.fraction))))
                                                                                  : static_cast<std::uint32_t>(number);
        };
        for (std::size_t column = 0; column < width; ++column) store(column, program.columns_[column].initial);
        for (const Property& property : material.properties) {
            const std::string where = std::format("/materials/{}/{}", material.name, property.column);
            const auto column = names.column(property.column);
            if (!column) {
                std::vector<std::string_view> options;
                for (const Column& each : program.columns_) options.push_back(each.name);
                return fail(Errc::name, where, std::format("material `{}`: no property `{}` ({})", material.name, property.column, mlibs::names::nearest(property.column, options)));
            }
            if (said[*column]) return fail(Errc::name, where, std::format("material `{}` says `{}` twice", material.name, property.column));
            said[*column] = true;
            const Column& of = program.columns_[*column];
            if (of.kind == ColumnKind::number) {
                const double* number = std::get_if<double>(&property.value);
                if (!number || !(*number >= of.least && *number <= of.most))
                    return fail(Errc::value, where, std::format("`{}` of `{}` is a number {} … {}{}", of.name, material.name, of.least, of.most, of.unit.empty() ? "" : " (" + of.unit + ")"));
                store(*column, *number);
            } else if (of.kind == ColumnKind::phase) {
                const std::string* word = std::get_if<std::string>(&property.value);
                const auto found = word ? std::ranges::find(phase_names, *word) : phase_names.end();
                if (found == phase_names.end())
                    return fail(Errc::value, where, std::format("`{}` of `{}` is a phase: {}", of.name, material.name, word ? mlibs::names::nearest(*word, phase_names) : "solid, soft, granular, liquid or gas"));
                store(*column, static_cast<double>(found - phase_names.begin()));
            } else {
                const std::string* word = std::get_if<std::string>(&property.value);
                const auto found = word ? names.material(*word) : std::nullopt;
                if (!found) {
                    std::vector<std::string_view> options;
                    for (const Material& each : program.materials_) options.push_back(each.name);
                    return fail(Errc::name, where, std::format("`{}` of `{}` is a material's name{}", of.name, material.name, word ? " (" + mlibs::names::nearest(*word, options) + ")" : ""));
                }
                store(*column, static_cast<double>(*found));
            }
        }
    }

    // The laws.
    std::vector<bool> flagged(program.channels_.size(), false), written(program.channels_.size(), false);
    for (std::size_t index = 0; index < definition.laws.size(); ++index) {
        const LawSpec& spec = definition.laws[index];
        const std::string where = std::format("/laws/{}", index);
        const auto placed = [&](Failure failure, std::string_view key) {
            failure.where = std::format("{}/{}", where, key);
            return std::unexpected(std::move(failure));
        };
        Law law;
        law.name = spec.name.empty() ? std::format("law {}", index) : spec.name;
        law.recipe = spec.recipe;
        law.text = spec.when;
        const bool transfers = !spec.transfer.empty(), adjusts = !spec.raise.empty() || !spec.lower.empty();
        const bool reacts = !spec.add.empty() || !spec.take.empty() || !spec.set.empty() || !spec.flag.empty() || !spec.become.empty();
        if (transfers + adjusts + reacts != 1)
            return fail(Errc::law, where, transfers + adjusts + reacts == 0 ? "a law does something: a `transfer`, `raise` or `lower`, or `add`, `take`, `set`, `flag`, `become`"
                                                                              : "a law is one kind: a transfer, an adjustment of properties (`raise`, `lower`), or a reaction (`add`, `take`, `set`, `flag`, `become`)");
        bool reads_near = false;
        if (!spec.when.empty()) {
            if (transfers) return fail(Errc::law, where + "/when", "a transfer has no condition: what conducts is its `by` property (0: not at all)");
            Parser parser(spec.when, names, spec.recipe, limits.max_terms);
            auto tests = parser.condition();
            if (!tests) return placed(tests.error(), "when");
            law.when = std::move(*tests);
            reads_near = parser.reads_near();
        }
        const auto writes = [&](const std::vector<Effect>& effects, Change change, std::string_view key, bool on_columns) -> std::expected<void, Failure> {
            for (const Effect& effect : effects) {
                const std::string at = std::format("{}/{}", key, effect.target);
                Write write;
                write.change = change;
                if (on_columns) {
                    const auto column = names.column(effect.target);
                    if (!column || program.columns_[*column].kind != ColumnKind::number)
                        return placed(Failure{Errc::name, std::format("`{}` is no property that is a number ({})", effect.target, names.nearest(effect.target)), {}, {}}, at);
                    write.target = *column;
                } else {
                    const auto channel = names.channel(effect.target);
                    if (!channel) return placed(Failure{Errc::name, std::format("`{}` is no channel ({})", effect.target, names.nearest(effect.target)), {}, {}}, at);
                    if (*channel == 0) return placed(Failure{Errc::law, "what a thing is changes by `become`, to what its own property names", {}, {}}, at);
                    write.target = *channel;
                    written[*channel] = true;
                }
                for (const Write& earlier : law.writes)
                    if (earlier.target == write.target) return placed(Failure{Errc::law, std::format("the law changes `{}` twice", effect.target), {}, {}}, at);
                Parser parser(effect.amount, names, spec.recipe, limits.max_terms);
                auto amount = parser.amount(on_columns ? program.columns_[write.target].fraction : program.channels_[write.target].fraction);
                if (!amount) return placed(amount.error(), at);
                write.amount = std::move(*amount);
                reads_near |= parser.reads_near();
                law.writes.push_back(std::move(write));
            }
            return {};
        };
        if (transfers) {
            law.kind = LawKind::transfer;
            const auto quantity = names.channel(spec.transfer);
            if (!quantity || *quantity == 0) return fail(Errc::name, where + "/transfer", std::format("`{}` is no channel that flows ({})", spec.transfer, names.nearest(spec.transfer)));
            law.quantity = *quantity;
            law.by = none;
            if (!spec.by.empty()) {
                const auto by = names.column(spec.by);
                if (!by || program.columns_[*by].kind != ColumnKind::number || program.columns_[*by].fraction != 0)
                    return fail(Errc::name, where + "/by", std::format("`{}` is no whole-number property to flow by ({})", spec.by, names.nearest(spec.by)));
                law.by = *by;
            }
            if (spec.shift > 31 || spec.multiply == 0) return fail(Errc::value, where, "a transfer's share is `multiply` (at least 1) over 2^`shift` (at most 31)");
            law.multiply = spec.multiply;
            law.shift = spec.shift;
            for (const std::uint32_t earlier : program.transfers_)
                if (program.laws_[earlier].quantity == law.quantity)
                    return fail(Errc::law, where + "/transfer", std::format("`{}` already flows by law {}: two flows of one quantity could together take more than there is", spec.transfer, earlier));
            program.transfers_.push_back(static_cast<std::uint32_t>(index));
        } else if (adjusts) {
            law.kind = LawKind::adjust;
            if (auto done = writes(spec.raise, Change::add, "raise", true); !done) return std::unexpected(done.error());
            if (auto done = writes(spec.lower, Change::take, "lower", true); !done) return std::unexpected(done.error());
            if (reads_near) return fail(Errc::law, where, "an adjustment reads the instance alone: its own state and properties, not what is near");
            program.adjusts_ = true;
        } else {
            if (auto done = writes(spec.add, Change::add, "add", false); !done) return std::unexpected(done.error());
            if (auto done = writes(spec.take, Change::take, "take", false); !done) return std::unexpected(done.error());
            if (auto done = writes(spec.set, Change::set, "set", false); !done) return std::unexpected(done.error());
            if (!spec.flag.empty()) {
                const auto flag = names.channel(spec.flag);
                if (!flag || *flag == 0) return fail(Errc::name, where + "/flag", std::format("`{}` is no channel to flag ({})", spec.flag, names.nearest(spec.flag)));
                law.flag = *flag;
                flagged[*flag] = true;
            }
            if (!spec.become.empty()) {
                law.kind = LawKind::conversion;
                const auto column = names.column(spec.become);
                if (!column || program.columns_[*column].kind != ColumnKind::material) {
                    if (names.material(spec.become))
                        return fail(Errc::names_material, where + "/become",
                                    std::format("`{}` is a material: a thing becomes what its own property names (`boiled`, `frozen`, `burnt` …), so that each material says what it turns into", spec.become));
                    return fail(Errc::name, where + "/become", std::format("`{}` is no property that names a material ({})", spec.become, names.nearest(spec.become)));
                }
                law.become = *column;
                if (!spec.event.empty() && !is_identifier(spec.event)) return fail(Errc::name, where + "/event", std::format("`{}` is not an event's name", spec.event));
                law.event = event(spec.event);
            } else if (!spec.event.empty()) {
                return fail(Errc::law, where + "/event", "an event names a conversion (a channel leaving 0 has its own: the channel's `rise`)");
            }
        }
        // What is near, that this law reads: a slot each.
        const auto slots = [&](Sum& sum) {
            for (Term& term : sum.terms) {
                if (term.source != Source::near_property && term.source != Source::near_channel) continue;
                const auto found = std::ranges::find_if(program.near_, [&](const Near& near) { return near.source == term.source && near.index == term.index; });
                if (found == program.near_.end()) program.near_.push_back({term.source, term.index});
            }
        };
        for (Test& test : law.when) slots(test.left), slots(test.right);
        for (Write& write : law.writes) slots(write.amount);
        program.laws_.push_back(std::move(law));
    }
    for (std::uint32_t channel = 0; channel < program.channels_.size(); ++channel) {
        if (!flagged[channel]) continue;
        if (written[channel])
            return fail(Errc::law, "/laws", std::format("`{}` is a flag (1 while a law holds): no law adds to it, takes from it or sets it", program.channels_[channel].name));
        program.flags_.push_back(channel);
    }
    return program;
}

std::optional<std::uint32_t> Program::column(std::string_view name) const noexcept {
    for (std::uint32_t index = 0; index < columns_.size(); ++index)
        if (columns_[index].name == name) return index;
    return std::nullopt;
}
std::optional<std::uint32_t> Program::channel(std::string_view name) const noexcept {
    for (std::uint32_t index = 0; index < channels_.size(); ++index)
        if (channels_[index].name == name) return index;
    return std::nullopt;
}
std::optional<MaterialId> Program::material(std::string_view name) const noexcept {
    for (MaterialId index = 0; index < materials_.size(); ++index)
        if (materials_[index].name == name) return index;
    return std::nullopt;
}
std::uint32_t Program::value(MaterialId material, std::uint32_t column) const noexcept {
    return material < materials_.size() && column < columns_.size() ? values_[material * columns_.size() + column] : 0;
}
double Program::number(MaterialId material, std::uint32_t column) const noexcept {
    return material < materials_.size() && column < columns_.size() ? numbers_[material * columns_.size() + column] : 0;
}
std::uint32_t Program::initial(MaterialId material, std::uint32_t channel) const noexcept {
    if (channel >= channels_.size()) return 0;
    if (channel == 0) return material;
    const Channel& of = channels_[channel];
    const std::uint32_t own = of.from ? value(material, *of.from) : 0;
    return own ? own : of.initial;
}
Phase Program::phase(MaterialId material) const noexcept { return static_cast<Phase>(value(material, phase_column_)); }
std::size_t Program::recipes() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(laws_, [](const Law& law) { return law.recipe; }));
}

}  // namespace xengine::matter
