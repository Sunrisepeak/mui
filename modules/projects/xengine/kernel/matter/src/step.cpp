// The reference step (D-165, the plan's §6.5): what is near is summed, what flows is counted for both
// ends of each pair, every law is judged on the step's input, and the result is committed — unsigned
// 32-bit, saturating, in an order written down (CONTRACT.md) so that a carrier's own path can be held
// to the same bits.
module xengine.matter;

import std;
import mlibs.error;
import mlibs.trace;

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
[[nodiscard]] std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char letter : text) {
        if (letter == '"' || letter == '\\') out += '\\', out += letter;
        else if (letter == '\n') out += "\\n";
        else if (static_cast<unsigned char>(letter) < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned>(letter));
        else out += letter;
    }
    return out + "\"";
}

}  // namespace

std::vector<std::uint32_t> Program::fresh(std::size_t instances, MaterialId material) const {
    std::vector<std::uint32_t> state(values(instances));
    for (std::size_t channel = 0; channel < channels_.size(); ++channel)
        std::fill_n(state.begin() + static_cast<std::ptrdiff_t>(channel * instances), instances, initial(material, static_cast<std::uint32_t>(channel)));
    return state;
}

void Program::place(std::span<std::uint32_t> state, std::size_t instances, std::size_t instance, MaterialId material) const noexcept {
    if (instance >= instances || state.size() != values(instances)) return;
    for (std::size_t channel = 0; channel < channels_.size(); ++channel) state[channel * instances + instance] = initial(material, static_cast<std::uint32_t>(channel));
}

std::uint32_t Program::evaluate(const Sum& sum, std::span<const std::uint32_t> properties, std::span<const std::uint32_t> input, std::size_t instances,
                                std::size_t instance, std::span<const std::uint32_t> near) const noexcept {
    std::uint32_t plus = 0, minus = 0;
    for (const Term& term : sum.terms) {
        std::uint32_t read = 0;
        switch (term.source) {
            case Source::constant: read = term.index; break;
            case Source::property: read = properties[term.index]; break;
            case Source::channel: read = input[term.index * instances + instance]; break;
            case Source::near_property:
            case Source::near_channel:
                for (std::size_t slot = 0; slot < near_.size(); ++slot)
                    if (near_[slot].source == term.source && near_[slot].index == term.index) read = near[slot * instances + instance];
                break;
        }
        const std::uint32_t amount = saturating_multiply(read, term.multiply) >> term.shift;
        if (term.negative) minus = saturating_add(minus, amount);
        else plus = saturating_add(plus, amount);
    }
    return plus - std::min(plus, minus);
}

bool Program::holds(const Law& law, std::span<const std::uint32_t> properties, std::span<const std::uint32_t> input, std::size_t instances, std::size_t instance,
                    std::span<const std::uint32_t> near) const noexcept {
    for (const Test& test : law.when) {
        const std::uint32_t left = evaluate(test.left, properties, input, instances, instance, near);
        const std::uint32_t right = evaluate(test.right, properties, input, instances, instance, near);
        bool passed = false;
        switch (test.compare) {
            case Compare::greater_equal: passed = left >= right; break;
            case Compare::less_equal: passed = left <= right; break;
            case Compare::greater: passed = left > right; break;
            case Compare::less: passed = left < right; break;
            case Compare::equal: passed = left == right; break;
            case Compare::not_equal: passed = left != right; break;
        }
        if (!passed) return false;
    }
    return true;
}

std::uint32_t Program::flow_of(const Law& law, std::uint32_t difference, std::uint32_t conductance, std::uint32_t weight, std::uint64_t load) noexcept {
    const std::uint32_t product = saturating_multiply(saturating_multiply(saturating_multiply(difference, conductance), weight), law.multiply);
    const std::uint64_t room = std::uint64_t{1} << law.shift;
    if (load <= room) return product >> law.shift;
    return static_cast<std::uint32_t>((std::uint64_t{product} * room / load) >> law.shift);
}

std::span<const std::uint32_t> Program::scaled(const Carrier& carrier, std::size_t instance, std::uint32_t material, std::span<std::uint32_t> row) const noexcept {
    const std::size_t width = columns_.size();
    const std::span<const std::uint32_t> written(values_.data() + material * width, width);
    const std::uint64_t held = carrier.capacity.size() > instance ? std::max(1u, carrier.capacity[instance]) : 1u;
    const std::uint64_t exposed = carrier.exposed.size() > instance ? std::clamp<std::uint64_t>(carrier.exposed[instance], 1, held) : held;
    if (extensive_.empty() || held == exposed || row.size() < width) return written;
    std::ranges::copy(written, row.begin());
    for (const std::uint32_t column : extensive_)
        row[column] = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{written[column]} * held / exposed, 0xffffffffu));
    return row.first(width);
}

std::expected<Report, Failure> Program::step(Carrier carrier, std::span<const std::uint32_t> input, std::span<std::uint32_t> output, Workspace& workspace,
                                             std::vector<Event>* events) const noexcept {
    mlibs::trace::Span span("simulation.matter.step", "simulation");
    const auto fail = [](Errc code, std::string where, std::string message) { return std::unexpected(Failure{code, std::move(message), std::move(where), {}}); };
    if (channels_.empty()) return fail(Errc::unprepared, "", "the program was not prepared");
    const std::size_t instances = carrier.instances, channels = channels_.size(), width = columns_.size();
    if (input.size() != values(instances) || output.size() != input.size())
        return fail(Errc::shape, "", std::format("{} instances of {} channels are {} values: {} given, {} to write", instances, channels, values(instances), input.size(), output.size()));
    if (!input.empty() && std::less<>{}(input.data(), output.data() + output.size()) && std::less<>{}(output.data(), input.data() + input.size()))
        return fail(Errc::shape, "", "the input and the output overlap: a step reads one and writes the other");
    for (std::size_t index = 0; index < carrier.pairs.size(); ++index) {
        const Pair& pair = carrier.pairs[index];
        if (pair.first >= instances || pair.second >= instances || pair.first == pair.second)
            return fail(Errc::pair, std::format("/pairs/{}", index), std::format("pair {}: {} beside {}, of {} instances", index, pair.first, pair.second, instances));
    }

    if (!carrier.capacity.empty() && carrier.capacity.size() != instances)
        return fail(Errc::shape, "/capacity", std::format("{} capacities for {} instances: one each, or none", carrier.capacity.size(), instances));
    if (!carrier.residue.empty() && carrier.residue.size() != transfers_.size() * instances)
        return fail(Errc::shape, "/residue", std::format("{} residues for {} transfers × {} instances: one each, or none", carrier.residue.size(), transfers_.size(), instances));
    if (!carrier.exposed.empty() && carrier.exposed.size() != instances)
        return fail(Errc::shape, "/exposed", std::format("{} exposures for {} instances: one each, or none", carrier.exposed.size(), instances));
    // How much is beside each instance: what a transfer's share must leave room for.
    workspace.degree_.assign(instances, 0);
    for (const Pair& pair : carrier.pairs) {
        workspace.degree_[pair.first] = saturating_add(workspace.degree_[pair.first], pair.weight);
        workspace.degree_[pair.second] = saturating_add(workspace.degree_[pair.second], pair.weight);
    }
    // A flow out of an instance is at most the difference times the smaller of the two conductances — never more
    // than its own — so its own material's conductance bounds what all that is beside it can take: air, which
    // conducts little, may have many beside it; iron few.
    for (const std::uint32_t index : transfers_) {
        if (carrier.crowded) break;  // shared, not refused
        const Law& law = laws_[index];
        for (std::size_t instance = 0; instance < instances; ++instance) {
            const std::uint32_t material = input[instance];
            if (material >= materials_.size()) continue;  // conducts nothing: takes no part
            const std::uint64_t most = law.by == none ? 1 : static_cast<std::uint64_t>(values_[material * width + law.by]);
            // What it holds (its capacity) is what it can give: a crate of twelve units has twelve times the room.
            const std::uint64_t held = carrier.capacity.size() > instance ? std::max(1u, carrier.capacity[instance]) : 1u;
            if (most * law.multiply * workspace.degree_[instance] > (std::uint64_t{1} << law.shift) * held)
                return fail(Errc::unstable, std::format("/laws/{}", index),
                            std::format("`{}`: instance {} has {} beside it; with a share of {} × {} / {} to each, they could together take more than it has", law.name, instance,
                                        workspace.degree_[instance], most, law.multiply, std::uint64_t{1} << law.shift));
        }
    }

    const auto material_of = [&](std::size_t instance) { return input[instance]; };
    // 1 What is near: each thing some law reads, summed over an instance's neighbours by weight.
    workspace.near_.assign(near_.size() * instances, 0);
    for (std::size_t slot = 0; slot < near_.size(); ++slot) {
        const Near& what = near_[slot];
        // What is not a material of this definition is nothing to its neighbours: neither its row nor its state.
        const auto of = [&](std::uint32_t instance) -> std::uint32_t {
            const std::uint32_t material = material_of(instance);
            if (material >= materials_.size()) return 0;
            return what.source == Source::near_channel ? input[what.index * instances + instance] : values_[material * width + what.index];
        };
        std::uint32_t* sums = workspace.near_.data() + slot * instances;
        for (const Pair& pair : carrier.pairs) {
            sums[pair.first] = saturating_add(sums[pair.first], saturating_multiply(of(pair.second), pair.weight));
            sums[pair.second] = saturating_add(sums[pair.second], saturating_multiply(of(pair.first), pair.weight));
        }
    }
    // 2 What flows: for each pair, the same amount counted at both ends.
    workspace.gained_.assign(transfers_.size() * instances, 0);
    workspace.lost_.assign(transfers_.size() * instances, 0);
    const bool kept = !carrier.residue.empty();
    if (kept) workspace.net_.assign(instances, 0);
    for (std::size_t slot = 0; slot < transfers_.size(); ++slot) {
        const Law& law = laws_[transfers_[slot]];
        // What is not a material of this definition conducts nothing: it takes no part.
        const auto conducts = [&](std::uint32_t instance) -> std::uint32_t {
            const std::uint32_t material = material_of(instance);
            if (material >= materials_.size()) return 0;
            return law.by == none ? 1 : values_[material * width + law.by];
        };
        std::uint32_t* gained = workspace.gained_.data() + slot * instances;
        std::uint32_t* lost = workspace.lost_.data() + slot * instances;
        const std::uint32_t* quantity = input.data() + law.quantity * instances;
        for (const Pair& pair : carrier.pairs) {
            const std::uint32_t one = quantity[pair.first], other = quantity[pair.second];
            if (one == other) continue;
            const std::uint32_t conductance = std::min(conducts(pair.first), conducts(pair.second));
            const std::uint32_t difference = one > other ? one - other : other - one;
            std::uint64_t load = 0;
            if (carrier.crowded) {
                // An end's load: its conductance × the share × all beside it, over what it holds (rounded up) — a crate
                // of twelve units with 383 drops on it is as crowded as a cell with 32.
                const auto load_of = [&](std::uint32_t end) {
                    const std::uint64_t held = carrier.capacity.size() > end ? std::max(1u, carrier.capacity[end]) : 1u;
                    return (std::uint64_t{conducts(end)} * law.multiply * workspace.degree_[end] + held - 1) / held;
                };
                load = std::max(load_of(pair.first), load_of(pair.second));
            }
            const std::uint32_t flow = flow_of(law, difference, conductance, pair.weight, load);
            const std::uint32_t from = one > other ? pair.first : pair.second, to = one > other ? pair.second : pair.first;
            if (kept) {
                workspace.net_[from] -= flow, workspace.net_[to] += flow;
                continue;
            }
            const auto held = [&](std::uint32_t end) { return carrier.capacity.empty() ? 1u : std::max(1u, carrier.capacity[end]); };
            lost[from] = saturating_add(lost[from], flow / held(from));
            gained[to] = saturating_add(gained[to], flow / held(to));
        }
        if (!kept) continue;
        // Kept: each instance's net flow and what it had left over, in whole units of its own (floored), the rest
        // kept for the next step — exact: Σ (value × capacity + residue) moves only by what the laws say.
        std::int32_t* residue = carrier.residue.data() + slot * instances;
        for (std::size_t instance = 0; instance < instances; ++instance) {
            const std::int64_t held = carrier.capacity.empty() ? 1 : std::max<std::int64_t>(1, carrier.capacity[instance]);
            const std::int64_t total = workspace.net_[instance] + residue[instance];
            const std::int64_t whole = total >= 0 ? total / held : -((-total + held - 1) / held);
            residue[instance] = static_cast<std::int32_t>(total - whole * held);
            if (whole > 0) gained[instance] = static_cast<std::uint32_t>(std::min<std::int64_t>(whole, 0xffffffffLL));
            else lost[instance] = static_cast<std::uint32_t>(std::min<std::int64_t>(-whole, 0xffffffffLL));
            workspace.net_[instance] = 0;
        }
    }

    // 3 Each instance: its properties as its state leaves them, the laws that hold, the commit.
    workspace.row_.assign(width, 0);
    workspace.plus_.assign(channels, 0);
    workspace.minus_.assign(channels, 0);
    workspace.base_.assign(channels, 0);
    workspace.set_.assign(channels, 0);
    Report report{.instances = instances, .pairs = carrier.pairs.size()};
    const std::span<const std::uint32_t> near(workspace.near_);
    for (std::size_t instance = 0; instance < instances; ++instance) {
        const std::uint32_t material = material_of(instance);
        if (material >= materials_.size()) {
            // Not a material of this definition: nothing acts on it.
            for (std::size_t channel = 0; channel < channels; ++channel) output[channel * instances + instance] = input[channel * instances + instance];
            continue;
        }
        std::span<const std::uint32_t> properties = scaled(carrier, instance, material, workspace.row_);
        if (adjusts_) {
            // The adjustments, in the laws' order, each judged on the properties as written.
            if (properties.data() != workspace.row_.data()) std::ranges::copy(properties, workspace.row_.begin());
            for (const Law& law : laws_) {
                if (law.kind != LawKind::adjust || !holds(law, properties, input, instances, instance, near)) continue;
                for (const Write& write : law.writes) {
                    const std::uint32_t amount = evaluate(write.amount, properties, input, instances, instance, near);
                    std::uint32_t& value = workspace.row_[write.target];
                    value = write.change == Change::add ? saturating_add(value, amount) : value - std::min(value, amount);
                }
            }
            properties = workspace.row_;
        }
        std::ranges::fill(workspace.plus_, 0u);
        std::ranges::fill(workspace.minus_, 0u);
        std::ranges::fill(workspace.set_, std::uint8_t{0});
        for (const std::uint32_t flag : flags_) {
            workspace.base_[flag] = 0;
            workspace.set_[flag] = 1;
        }
        std::uint32_t becomes = none, by_law = 0;
        for (std::uint32_t index = 0; index < laws_.size(); ++index) {
            const Law& law = laws_[index];
            if ((law.kind != LawKind::reaction && law.kind != LawKind::conversion) || !holds(law, properties, input, instances, instance, near)) continue;
            ++report.held;
            for (const Write& write : law.writes) {
                const std::uint32_t amount = evaluate(write.amount, properties, input, instances, instance, near);
                if (write.change == Change::add) workspace.plus_[write.target] = saturating_add(workspace.plus_[write.target], amount);
                else if (write.change == Change::take) workspace.minus_[write.target] = saturating_add(workspace.minus_[write.target], amount);
                else if (!workspace.set_[write.target]) workspace.base_[write.target] = amount, workspace.set_[write.target] = 1;  // the first law that sets it
            }
            if (law.flag) workspace.base_[*law.flag] = 1;
            if (law.become && becomes == none) {
                const std::uint32_t target = properties[*law.become];
                if (target < materials_.size() && target != material) becomes = target, by_law = index;
            }
        }
        bool changed = false;
        for (std::uint32_t channel = 1; channel < channels; ++channel) {
            const std::uint32_t old = input[channel * instances + instance];
            std::uint32_t value = workspace.set_[channel] ? workspace.base_[channel] : old;
            value = saturating_add(value, workspace.plus_[channel]);
            value -= std::min(value, workspace.minus_[channel]);
            for (std::size_t slot = 0; slot < transfers_.size(); ++slot) {
                if (laws_[transfers_[slot]].quantity != channel) continue;
                value -= std::min(value, workspace.lost_[slot * instances + instance]);
                value = saturating_add(value, workspace.gained_[slot * instances + instance]);
            }
            if (becomes != none && channels_[channel].resets) value = initial(becomes, channel);
            output[channel * instances + instance] = value;
            changed |= value != old;
            if (events && (old == 0) != (value == 0)) {
                const Channel& of = channels_[channel];
                const auto named = value != 0 ? of.rise : of.fall;
                if (named) events->push_back({value != 0 ? EventKind::rose : EventKind::fell, static_cast<std::uint32_t>(instance), *named, 0, channel, old, value, material});
            }
        }
        output[instance] = becomes != none ? becomes : material;
        if (becomes != none) {
            ++report.became;
            changed = true;
            if (events) events->push_back({EventKind::became, static_cast<std::uint32_t>(instance), laws_[by_law].event.value_or(largest), by_law, 0, material, becomes, material});
        }
        report.changed += changed;
    }
    mlibs::trace::count("simulation.matter.held", static_cast<double>(report.held), "simulation");
    mlibs::trace::count("simulation.matter.became", static_cast<double>(report.became), "simulation");
    return report;
}

std::string Program::describe() const {
    std::string out = "{\"columns\":[";
    for (std::size_t index = 0; index < columns_.size(); ++index) {
        const Column& column = columns_[index];
        out += std::format("{}{{\"name\":{},\"kind\":\"{}\",\"unit\":{},\"doc\":{}", index ? "," : "", json_text(column.name),
                           column.kind == ColumnKind::number ? "number" : column.kind == ColumnKind::phase ? "phase" : "material", json_text(column.unit), json_text(column.doc));
        if (column.kind == ColumnKind::number) out += std::format(",\"least\":{},\"most\":{},\"fraction\":{}", column.least, column.most, column.fraction);
        if (column.extensive) out += ",\"extensive\":true";
        out += "}";
    }
    out += "],\"channels\":[";
    for (std::size_t index = 0; index < channels_.size(); ++index) {
        const Channel& channel = channels_[index];
        out += std::format("{}{{\"name\":{},\"unit\":{},\"doc\":{},\"initial\":{},\"fraction\":{},\"resets\":{}", index ? "," : "", json_text(channel.name), json_text(channel.unit),
                           json_text(channel.doc), std::ldexp(static_cast<double>(channel.initial), -static_cast<int>(channel.fraction)), channel.fraction, channel.resets);
        if (channel.from) out += std::format(",\"from\":{}", json_text(columns_[*channel.from].name));
        if (channel.rise) out += std::format(",\"rise\":{}", json_text(events_[*channel.rise]));
        if (channel.fall) out += std::format(",\"fall\":{}", json_text(events_[*channel.fall]));
        out += "}";
    }
    out += "],\"materials\":[";
    for (std::size_t row = 0; row < materials_.size(); ++row) {
        out += std::format("{}{{\"name\":{},\"id\":{}", row ? "," : "", json_text(materials_[row].name), row);
        if (!materials_[row].tags.empty()) {
            out += ",\"tags\":[";
            for (std::size_t tag = 0; tag < materials_[row].tags.size(); ++tag) out += (tag ? "," : "") + json_text(materials_[row].tags[tag]);
            out += "]";
        }
        // What differs from the columns' defaults: the rest is said once, above.
        for (std::size_t column = 0; column < columns_.size(); ++column) {
            const double number = numbers_[row * columns_.size() + column];
            const Column& of = columns_[column];
            if (number == of.initial) continue;
            if (of.kind == ColumnKind::number) out += std::format(",{}:{}", json_text(of.name), number);
            else if (of.kind == ColumnKind::phase) out += std::format(",{}:{}", json_text(of.name), json_text(phase_names[static_cast<std::size_t>(number)]));
            else out += std::format(",{}:{}", json_text(of.name), json_text(materials_[static_cast<std::size_t>(number)].name));
        }
        out += "}";
    }
    out += "],\"laws\":[";
    for (std::size_t index = 0; index < laws_.size(); ++index) {
        const Law& law = laws_[index];
        out += std::format("{}{{\"name\":{},\"kind\":\"{}\"", index ? "," : "", json_text(law.name), law_kind_names[static_cast<std::size_t>(law.kind)]);
        if (!law.text.empty()) out += std::format(",\"when\":{}", json_text(law.text));
        if (law.kind == LawKind::motion) {
            if (law.along) {
                out += std::format(",\"{}\":{},\"from\":{},\"speed\":{},\"force\":{}", law.up ? "up" : "down", json_text(channels_[*law.along].name), law.from, law.speed, law.force);
                if (law.scale) out += std::format(",\"by\":{}", json_text(columns_[*law.scale].name));
            }
            else out += std::format(",\"toward\":[{},{}],\"accel\":{}", law.toward_x, law.toward_y, law.accel);
        }
        if (law.kind == LawKind::transfer) {
            out += std::format(",\"transfer\":{},\"share\":\"{} / {}\"", json_text(channels_[law.quantity].name), law.multiply, std::uint64_t{1} << law.shift);
            if (law.by != none) out += std::format(",\"by\":{}", json_text(columns_[law.by].name));
        }
        if (!law.writes.empty()) {
            out += ",\"changes\":[";
            for (std::size_t write = 0; write < law.writes.size(); ++write)
                out += (write ? "," : "") + json_text(law.kind == LawKind::adjust ? columns_[law.writes[write].target].name : channels_[law.writes[write].target].name);
            out += "]";
        }
        if (law.flag) out += std::format(",\"flag\":{}", json_text(channels_[*law.flag].name));
        if (law.become) out += std::format(",\"become\":{}", json_text(columns_[*law.become].name));
        if (law.event) out += std::format(",\"event\":{}", json_text(events_[*law.event]));
        if (law.recipe) out += ",\"recipe\":true";
        out += "}";
    }
    out += std::format("],\"recipes\":{}}}", recipes());
    return out;
}

}  // namespace xengine::matter
