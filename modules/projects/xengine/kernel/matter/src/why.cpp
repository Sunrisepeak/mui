// One instance's step, said (D-169): the reference step's arithmetic (step.cpp, CONTRACT.md) for a single
// instance, keeping what the step throws away — which laws held and what each read, what flowed to and from
// each neighbour, whose oxygen a near sum counted. A record of causes is made of this at a commit; nothing
// here is in a step's path.
module xengine.matter;

import std;

namespace xengine::matter {
namespace {

inline constexpr std::uint32_t largest = std::numeric_limits<std::uint32_t>::max();
[[nodiscard]] constexpr std::uint32_t saturating_add(std::uint32_t left, std::uint32_t right) noexcept {
    const std::uint32_t sum = left + right;
    return sum < left ? largest : sum;
}
[[nodiscard]] constexpr std::uint32_t saturating_multiply(std::uint32_t left, std::uint32_t right) noexcept {
    const std::uint64_t product = static_cast<std::uint64_t>(left) * right;
    return product > largest ? largest : static_cast<std::uint32_t>(product);
}

}  // namespace

Why Program::why(Carrier carrier, std::span<const std::uint32_t> input, std::size_t instance) const {
    Why out;
    const std::size_t instances = carrier.instances, channels = channels_.size(), width = columns_.size();
    if (channels_.empty() || instance >= instances || input.size() != values(instances)) return out;
    out.after.resize(channels);
    for (std::size_t channel = 0; channel < channels; ++channel) out.after[channel] = input[channel * instances + instance];
    const std::uint32_t material = input[instance];
    if (material >= materials_.size()) return out;  // not a material of this definition: nothing acts on it

    const auto beside = [&](const Pair& pair) -> std::optional<std::uint32_t> {
        if (pair.first >= instances || pair.second >= instances || pair.first == pair.second) return std::nullopt;
        if (pair.first == instance) return pair.second;
        if (pair.second == instance) return pair.first;
        return std::nullopt;
    };
    // 1 What is near, for this instance: every slot's sum, and its parts.
    std::vector<std::uint32_t> near(near_.size() * instances, 0);
    for (std::size_t slot = 0; slot < near_.size(); ++slot) {
        const Near& what = near_[slot];
        std::uint32_t& sum = near[slot * instances + instance];
        for (const Pair& pair : carrier.pairs) {
            const auto other = beside(pair);
            if (!other) continue;
            const std::uint32_t theirs = input[*other];
            if (theirs >= materials_.size()) continue;
            const std::uint32_t amount = saturating_multiply(what.source == Source::near_channel ? input[what.index * instances + *other] : values_[theirs * width + what.index], pair.weight);
            sum = saturating_add(sum, amount);
            if (amount) out.near.push_back({what.source, what.index, *other, amount});
        }
    }
    // 2 What flows, between this instance and each beside it.
    std::vector<std::uint32_t> gained(transfers_.size(), 0), lost(transfers_.size(), 0);
    for (std::size_t slot = 0; slot < transfers_.size(); ++slot) {
        const Law& law = laws_[transfers_[slot]];
        const auto conducts = [&](std::uint32_t of) -> std::uint32_t {
            const std::uint32_t theirs = input[of];
            if (theirs >= materials_.size()) return 0;
            return law.by == none ? 1 : values_[theirs * width + law.by];
        };
        const std::uint32_t* quantity = input.data() + law.quantity * instances;
        for (const Pair& pair : carrier.pairs) {
            const auto other = beside(pair);
            if (!other) continue;
            const std::uint32_t mine = quantity[instance], theirs = quantity[*other];
            if (mine == theirs) continue;
            const std::uint32_t conductance = std::min(conducts(static_cast<std::uint32_t>(instance)), conducts(*other));
            const std::uint32_t difference = mine > theirs ? mine - theirs : theirs - mine;
            const std::uint32_t flow = saturating_multiply(saturating_multiply(saturating_multiply(difference, conductance), pair.weight), law.multiply) >> law.shift;
            if (!flow) continue;
            const bool in = theirs > mine;
            (in ? gained : lost)[slot] = saturating_add((in ? gained : lost)[slot], flow);
            out.flows.push_back({transfers_[slot], *other, flow, in});
        }
    }
    // 3 Its properties as its state leaves them, every law judged, the commit.
    const std::span<const std::uint32_t> written(values_.data() + material * width, width);
    out.properties.assign(written.begin(), written.end());
    const auto judged = [&](std::uint32_t index, std::span<const std::uint32_t> properties) {
        const Law& law = laws_[index];
        Judged judgement{.law = index, .holds = true};
        const auto read = [&](const Sum& sum) {
            for (const Term& term : sum.terms) {
                if (term.source == Source::constant) continue;
                if (std::ranges::any_of(judgement.readings, [&](const Reading& seen) { return seen.source == term.source && seen.index == term.index; })) continue;
                std::uint32_t value = 0;
                if (term.source == Source::property) value = properties[term.index];
                else if (term.source == Source::channel) value = input[term.index * instances + instance];
                else
                    for (std::size_t slot = 0; slot < near_.size(); ++slot)
                        if (near_[slot].source == term.source && near_[slot].index == term.index) value = near[slot * instances + instance];
                judgement.readings.push_back({term.source, term.index, value});
            }
        };
        for (std::uint32_t test = 0; test < law.when.size(); ++test) {
            read(law.when[test].left);
            read(law.when[test].right);
            const std::uint32_t left = evaluate(law.when[test].left, properties, input, instances, instance, near);
            const std::uint32_t right = evaluate(law.when[test].right, properties, input, instances, instance, near);
            bool passed = false;
            switch (law.when[test].compare) {
                case Compare::greater_equal: passed = left >= right; break;
                case Compare::less_equal: passed = left <= right; break;
                case Compare::greater: passed = left > right; break;
                case Compare::less: passed = left < right; break;
                case Compare::equal: passed = left == right; break;
                case Compare::not_equal: passed = left != right; break;
            }
            if (!passed && judgement.holds) judgement.holds = false, judgement.failed = test;
        }
        return judgement;
    };
    if (adjusts_) {
        // The adjustments, in the laws' order, each judged on the properties as written.
        for (std::uint32_t index = 0; index < laws_.size(); ++index) {
            const Law& law = laws_[index];
            if (law.kind != LawKind::adjust) continue;
            out.laws.push_back(judged(index, written));
            if (!out.laws.back().holds) continue;
            for (const Write& write : law.writes) {
                const std::uint32_t amount = evaluate(write.amount, written, input, instances, instance, near);
                std::uint32_t& value = out.properties[write.target];
                value = write.change == Change::add ? saturating_add(value, amount) : value - std::min(value, amount);
            }
        }
    }
    const std::span<const std::uint32_t> properties(out.properties);
    std::vector<std::uint32_t> plus(channels, 0), minus(channels, 0), base(channels, 0);
    std::vector<std::uint8_t> set(channels, 0);
    for (const std::uint32_t flag : flags_) base[flag] = 0, set[flag] = 1;
    std::uint32_t becomes = none;
    for (std::uint32_t index = 0; index < laws_.size(); ++index) {
        const Law& law = laws_[index];
        if (law.kind != LawKind::reaction && law.kind != LawKind::conversion) continue;
        out.laws.push_back(judged(index, properties));
        if (!out.laws.back().holds) continue;
        for (const Write& write : law.writes) {
            const std::uint32_t amount = evaluate(write.amount, properties, input, instances, instance, near);
            if (write.change == Change::add) plus[write.target] = saturating_add(plus[write.target], amount);
            else if (write.change == Change::take) minus[write.target] = saturating_add(minus[write.target], amount);
            else if (!set[write.target]) base[write.target] = amount, set[write.target] = 1;
        }
        if (law.flag) base[*law.flag] = 1;
        if (law.become && becomes == none) {
            const std::uint32_t target = properties[*law.become];
            if (target < materials_.size() && target != material) becomes = target, out.became_by = index;
        }
    }
    std::ranges::sort(out.laws, {}, &Judged::law);
    for (std::uint32_t channel = 1; channel < channels; ++channel) {
        std::uint32_t value = set[channel] ? base[channel] : input[channel * instances + instance];
        value = saturating_add(value, plus[channel]);
        value -= std::min(value, minus[channel]);
        for (std::size_t slot = 0; slot < transfers_.size(); ++slot) {
            if (laws_[transfers_[slot]].quantity != channel) continue;
            value -= std::min(value, lost[slot]);
            value = saturating_add(value, gained[slot]);
        }
        if (becomes != none && channels_[channel].resets) value = initial(becomes, channel);
        out.after[channel] = value;
    }
    out.after[0] = becomes != none ? becomes : material;
    return out;
}

}  // namespace xengine::matter
