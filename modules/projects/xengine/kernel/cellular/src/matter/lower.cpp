module xengine.cellular.matter;

import std;

namespace xengine::cellular {
namespace {

namespace matter = xengine::matter;

[[nodiscard]] Compare compare_of(matter::Compare compare) noexcept {
    switch (compare) {
        case matter::Compare::greater_equal: return Compare::greater_equal;
        case matter::Compare::less_equal: return Compare::less_equal;
        case matter::Compare::greater: return Compare::greater;
        case matter::Compare::less: return Compare::less;
        case matter::Compare::equal: return Compare::equal;
        case matter::Compare::not_equal: return Compare::not_equal;
    }
    return Compare::equal;
}

}  // namespace

std::expected<Program, Failure> lower(const xengine::matter::Program& plan, const Lowering& options) {
    const auto columns = plan.columns();
    const auto channels = plan.channels();
    if (channels.empty()) return std::unexpected(Failure{Errc::unprepared});
    const std::uint32_t width = static_cast<std::uint32_t>(columns.size());
    const std::uint32_t mobility_column = width;  // one more column than matter's: how a material may move

    Reactions reactions;
    reactions.table.key = 0;
    reactions.table.columns = width + 1;
    for (matter::MaterialId material = 0; material < plan.materials().size(); ++material) {
        for (std::uint32_t column = 0; column < width; ++column) reactions.table.values.push_back(plan.value(material, column));
        reactions.table.values.push_back(mobility_of(plan.phase(material)));
    }
    reactions.near = options.neighbourhood;
    // The properties some law adjusts are derived values; every other is its row's as written.
    std::vector<std::optional<std::uint32_t>> derived_of(width);
    for (const matter::Law& law : plan.laws())
        if (law.kind == matter::LawKind::adjust)
            for (const matter::Write& write : law.writes)
                if (!derived_of[write.target]) {
                    derived_of[write.target] = static_cast<std::uint32_t>(reactions.derived.size());
                    reactions.derived.push_back(write.target);
                }
    // A sum's terms; `plain`: an adjustment's, of the row as written.
    const auto amount = [&](const matter::Sum& sum, bool plain) {
        Amount out;
        for (const matter::Term& term : sum.terms) {
            Operand operand{.index = term.index, .multiply = term.multiply, .shift = term.shift, .negative = term.negative};
            switch (term.source) {
                case matter::Source::constant: operand.read = Read::constant; break;
                case matter::Source::channel: operand.read = Read::channel; break;
                case matter::Source::near_channel: operand.read = Read::near_channel; break;
                case matter::Source::near_property: operand.read = Read::near_lookup; break;
                case matter::Source::property:
                    if (!plain && derived_of[term.index]) {
                        operand.read = Read::derived;
                        operand.index = *derived_of[term.index];
                    } else {
                        operand.read = Read::lookup;
                    }
                    break;
            }
            out.push_back(operand);
        }
        return out;
    };
    const auto conditions = [&](const std::vector<matter::Test>& tests, bool plain) {
        std::vector<Comparison> out;
        for (const matter::Test& test : tests) out.push_back({amount(test.left, plain), amount(test.right, plain), compare_of(test.compare)});
        return out;
    };

    std::vector<Transfer> transfers;
    for (const matter::Law& law : plan.laws()) {
        switch (law.kind) {
            case matter::LawKind::transfer: {
                Transfer transfer{.channel = law.quantity, .offsets = options.neighbourhood, .multiply = law.multiply, .shift = law.shift};
                if (law.by != matter::none) {
                    transfer.by_column = law.by;
                    transfer.by_limit = static_cast<std::uint32_t>(columns[law.by].most);
                }
                transfers.push_back(std::move(transfer));
                break;
            }
            case matter::LawKind::adjust:
                // One law's writes: each a derived value, all under its one condition.
                for (const matter::Write& write : law.writes)
                    reactions.adjustments.push_back({conditions(law.when, true), *derived_of[write.target], write.change == matter::Change::add, amount(write.amount, true)});
                break;
            case matter::LawKind::motion:
                break;  // not the lattice's: a lattice has no velocities — what flies and the bodies do it
            case matter::LawKind::reaction:
            case matter::LawKind::conversion: {
                Reaction reaction{.when = conditions(law.when, false), .flag = law.flag, .become = law.become};
                for (const matter::Write& write : law.writes)
                    reaction.changes.push_back({write.target, write.change == matter::Change::add ? ChangeKind::add : write.change == matter::Change::take ? ChangeKind::take : ChangeKind::set,
                                                amount(write.amount, false)});
                reactions.reactions.push_back(std::move(reaction));
                break;
            }
        }
    }
    for (std::uint32_t channel = 1; channel < channels.size(); ++channel)
        if (channels[channel].resets) reactions.resets.push_back({channel, channels[channel].initial, channels[channel].from});

    // Cells change places by how heavy a volume of them is and how their phase moves.
    std::vector<Exchange> exchanges;
    if (options.moves)
        if (const auto density = plan.column("density")) exchanges.push_back({.weight = *density, .mobility = mobility_column, .carried = {}, .seed = options.seed, .by_table = true});

    Limits limits = options.limits;
    limits.max_channels = std::max(limits.max_channels, static_cast<std::uint32_t>(channels.size()));
    return Program::prepare(static_cast<std::uint32_t>(channels.size()), Stages{.rules = {}, .transfers = transfers, .exchanges = exchanges, .reactions = &reactions}, limits);
}

void events_between(const xengine::matter::Program& plan, std::size_t cells, std::span<const std::uint32_t> input, std::span<const std::uint32_t> output,
                    std::vector<xengine::matter::Event>& events) {
    const auto channels = plan.channels();
    if (input.size() != cells * channels.size() || output.size() != input.size()) return;
    constexpr std::uint32_t unnamed = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t cell = 0; cell < cells; ++cell) {
        for (std::uint32_t channel = 1; channel < channels.size(); ++channel) {
            const std::uint32_t old = input[channel * cells + cell], now = output[channel * cells + cell];
            if ((old == 0) == (now == 0)) continue;
            const auto named = now != 0 ? channels[channel].rise : channels[channel].fall;
            if (named) events.push_back({now != 0 ? matter::EventKind::rose : matter::EventKind::fell, static_cast<std::uint32_t>(cell), *named, 0, channel, old, now});
        }
        if (input[cell] != output[cell]) events.push_back({matter::EventKind::became, static_cast<std::uint32_t>(cell), unnamed, unnamed, 0, input[cell], output[cell]});
    }
}

}  // namespace xengine::cellular
