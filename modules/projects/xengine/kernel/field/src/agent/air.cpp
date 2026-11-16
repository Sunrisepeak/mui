module xengine.field.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.json;
import xengine.field;

namespace xengine::field::agent {

namespace {

// A stored value in its channel's unit (× 2^-fraction).
double in_unit(const matter::Channel& channel, std::uint32_t value) { return std::ldexp(static_cast<double>(value), -static_cast<int>(channel.fraction)); }

std::optional<Vec3> place_of(const mlibs::json::Node& node) {
    const auto parts = node.items();
    if (parts.size() != 3) return std::nullopt;
    std::array<float, 3> out{};
    for (std::size_t index = 0; index < 3; ++index) {
        const auto value = parts[index].number();
        if (!value || !std::isfinite(*value)) return std::nullopt;
        out[index] = static_cast<float>(*value);
    }
    return Vec3{out[0], out[1], out[2]};
}

std::string vector_text(Vec3 value) { return std::format("[{},{},{}]", value.x, value.y, value.z); }

}  // namespace

void AirModel::step(State& state, const Input& input) {
    for (std::uint32_t index = 0; index < input.count; ++index) (void)state.field.set(input.applied[index].cell, input.applied[index].channel, input.applied[index].value);
    if (auto stepped = state.field.step(); !stepped) state.failed = stepped.error().text();
}

Air::Air(Field initial, Options options) : options_(options), timeline_(AirModel::State{std::move(initial), {}}, options_.keep) {}

namespace {

AirModel::Input input_of(std::span<const Applied> held, std::span<const Applied> once) {
    AirModel::Input out;
    for (const auto* list : {&held, &once})
        for (const Applied& applied : *list)
            if (out.count < AirModel::most_applied) out.applied[out.count++] = applied;
    return out;
}

}  // namespace

AirModel::Input Air::input(bool clear_once) {
    const AirModel::Input out = input_of(held_, once_);
    if (clear_once) once_.clear();
    return out;
}

void Air::step() { timeline_.step(input(true)); }

void Air::hold(Applied applied) {
    std::erase_if(held_, [&](const Applied& kept) { return kept.cell == applied.cell && kept.channel == applied.channel; });
    if (held_.size() + once_.size() < AirModel::most_applied) held_.push_back(applied);
}

std::expected<Air::Setting, std::string> Air::setting_of(const Field& field, const mlibs::json::Node& given) {
    const auto at = place_of(given["at"]);
    if (!at) return std::unexpected(std::string("`at` is [x, y, z], three numbers (metres)"));
    const auto cell = field.cell_at(*at);
    if (!cell) return std::unexpected(std::format("no cell at {}: the air has cells only where it was covered (air.observe says how many)", vector_text(*at)));
    const matter::Program& program = field.program();
    const auto name = given["channel"].text();
    const auto channel = name ? program.channel(*name) : std::nullopt;
    if (!channel || *channel == 0) {
        std::string known;
        for (std::size_t index = 1; index < program.channels().size(); ++index) known += std::format("{}{}", known.empty() ? "" : ", ", program.channels()[index].name);
        return std::unexpected(std::format("`channel` is one of the program's: {}", known));
    }
    const auto value = given["value"].number();
    if (!value || !std::isfinite(*value) || *value < 0) return std::unexpected(std::string("`value` is a number of at least 0, in the channel's unit"));
    const double stored = std::ldexp(*value, static_cast<int>(program.channels()[*channel].fraction));
    if (stored > std::numeric_limits<std::uint32_t>::max()) return std::unexpected(std::string("`value` is more than the channel holds"));
    return Setting{{*cell, *channel, static_cast<std::uint32_t>(std::lround(stored))}, given["hold"].boolean().value_or(false)};
}

std::string Air::changes_for(std::uint32_t cell) const {
    // Who made the steps that reached this frame, and who applied a quantity to this cell before them: the latest
    // first, at most eight.
    std::string out = "[";
    std::size_t said = 0;
    for (auto change = changes_.rbegin(); change != changes_.rend() && said < 8; ++change) {
        if (change->applied && change->applied->cell != cell) continue;
        out += std::format("{}{{\"op\":{},\"actor\":{},\"from\":{},\"to\":{}{}}}", said ? "," : "", mlibs::agent::json_text(change->op), mlibs::agent::json_text(change->actor),
                           change->from, change->to,
                           change->applied ? std::format(",\"channel\":{},\"value\":{},\"hold\":{}", change->applied->channel, change->applied->value, change->hold) : std::string());
        ++said;
    }
    return out + "]";
}

std::string Air::status() const {
    const auto& state = timeline_.state();
    return std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"earliest\":{},\"latest\":{}{}}}", timeline_.frame(), state.field.state_hash(), timeline_.earliest(),
                       timeline_.latest(), state.failed.empty() ? std::string() : std::format(",\"failed\":{}", mlibs::agent::json_text(state.failed)));
}

std::string Air::observed(const Field& field, std::int64_t frame, std::optional<std::int64_t> budget) const {
    const matter::Program& program = field.program();
    const auto channels = program.channels();
    // The strongest wind at a cell's centre.
    float strongest = 0;
    Vec3 strongest_at{0, 0, 0};
    for (std::uint32_t cell = 0; cell < field.cells(); ++cell) {
        const Vec3 centre = field.centre(cell), wind = field.wind_at(centre);
        const float speed = std::sqrt(wind.x * wind.x + wind.y * wind.y + wind.z * wind.z);
        if (speed > strongest) strongest = speed, strongest_at = centre;
    }
    std::string out = std::format("{{\"frame\":{},\"hash\":\"{:016x}\",\"cells\":{},\"parked\":{},\"wind\":{{\"strongest\":{},\"at\":{}}},\"channels\":[", frame,
                                  field.state_hash(), field.cells(), field.parked(), strongest, vector_text(strongest_at));
    std::size_t shown = 0;
    for (std::uint32_t channel = 1; channel < channels.size(); ++channel, ++shown) {
        std::uint32_t least = std::numeric_limits<std::uint32_t>::max(), most = 0;
        Vec3 most_at{0, 0, 0};
        for (std::uint32_t cell = 0; cell < field.cells(); ++cell) {
            const std::uint32_t value = field.get(cell, channel).value_or(0);
            least = std::min(least, value);
            if (value > most) most = value, most_at = field.centre(cell);
        }
        if (field.cells() == 0) least = 0;
        const std::string next =
            std::format("{}{{\"name\":{},\"unit\":{},\"least\":{},\"most\":{},\"most_at\":{},\"total\":{}}}", shown ? "," : "", mlibs::agent::json_text(channels[channel].name),
                        mlibs::agent::json_text(channels[channel].unit), in_unit(channels[channel], least), in_unit(channels[channel], most), vector_text(most_at),
                        std::ldexp(static_cast<double>(field.total(channel)), -static_cast<int>(channels[channel].fraction)));
        // What closes the answer (`],"more":N}`) still has to fit.
        if (budget && out.size() + next.size() + 22 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
        out += next;
    }
    const std::size_t total = channels.empty() ? 0 : channels.size() - 1;
    return out + std::format("],\"more\":{}}}", total - shown);
}

void Air::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };
    const auto steps_of = [&]() -> std::optional<std::int64_t> {
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > options_.most_steps) return std::nullopt;
        return steps;
    };
    const std::string no_steps = std::format("`steps` is 1 … {}", options_.most_steps);
    const Field& field = timeline_.state().field;
    const matter::Program& program = field.program();
    const auto channels = program.channels();

    if (operation == "air.observe") {
        const auto budget = request["budget"].integer();
        const std::string seen = observed(field, timeline_.frame(), budget);
        if (budget && seen.size() > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget)))
            return invalid(std::format("`budget` {}: the least this answers, with every channel left out, is {} bytes", *budget, seen.size()));
        return answer.data(seen);
    }
    if (operation == "air.at") {
        const auto at = place_of(request["at"]);
        if (!at) return invalid("`at` is [x, y, z], three numbers (metres)");
        const auto cell = field.cell_at(*at);
        std::string out = std::format("{{\"at\":{},\"cell\":{},\"kelvin\":{},\"wind\":{},\"channels\":{{", vector_text(*at), cell ? std::to_string(*cell) : std::string("null"),
                                      field.kelvin_at(*at), vector_text(field.wind_at(*at)));
        for (std::uint32_t channel = 1; channel < channels.size(); ++channel)
            out += std::format("{}{}:{}", channel > 1 ? "," : "", mlibs::agent::json_text(channels[channel].name), field.value_at(*at, channel));
        return answer.data(out + "}}");
    }
    if (operation == "air.set") {
        const auto setting = setting_of(field, request.document->root());
        if (!setting) return invalid(setting.error());
        const auto same = [&](const Applied& applied) { return applied.cell == setting->applied.cell && applied.channel == setting->applied.channel; };
        std::erase_if(held_, same);
        std::erase_if(once_, same);
        if (held_.size() + once_.size() >= AirModel::most_applied)
            return invalid(std::format("at most {} quantities are applied at once: let one go (air.set it with hold false) or step first", AirModel::most_applied));
        (setting->hold ? held_ : once_).push_back(setting->applied);
        changes_.push_back({std::string(operation), openas::format(request.actor), timeline_.frame(), timeline_.frame(), setting->applied, setting->hold});
        // The step it is first applied in makes the frame after this one.
        return answer.data(std::format("{{\"effective\":{},\"frame\":{},\"cell\":{},\"held\":{},\"once\":{}}}", timeline_.frame() + 1, timeline_.frame(), setting->applied.cell,
                                       held_.size(), once_.size()));
    }
    if (operation == "air.step") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        const std::int64_t from = timeline_.frame();
        for (std::int64_t played = 0; played < *steps; ++played) step();
        changes_.push_back({std::string(operation), openas::format(request.actor), from, timeline_.frame(), std::nullopt, false});
        if (!timeline_.state().failed.empty())
            return answer.fail(openas::error_code::failed, std::format("the air could not step: {}", timeline_.state().failed), openas::exit_code::failed,
                               R"({"op":"air.seek","frame":0})");
        return answer.data(status());
    }
    if (operation == "air.seek") {
        const auto frame = request["frame"].integer();
        if (!frame) return invalid("`frame`: the frame to return to");
        if (!timeline_.seek(*frame))
            return answer.fail(openas::error_code::stale, std::format("frame {} is outside the history kept ({} … {})", *frame, timeline_.earliest(), timeline_.latest()));
        timeline_.branch();
        held_.clear(), once_.clear();  // what was applied belonged to the steps left behind
        std::erase_if(changes_, [&](const Change& change) { return change.to > *frame || (change.applied && change.from >= *frame); });
        return answer.data(status());
    }
    if (operation == "air.branch") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        std::vector<Applied> held = held_, once = once_;
        if (const auto given = request["set"]) {
            const auto setting = setting_of(field, given);
            if (!setting) return invalid(std::format("`set`: {}", setting.error()));
            if (held.size() + once.size() >= AirModel::most_applied) return invalid(std::format("`set`: at most {} quantities are applied at once", AirModel::most_applied));
            (setting->hold ? held : once).push_back(setting->applied);
        }
        AirModel::State future = timeline_.state();  // a copy: the air itself is not touched
        for (std::int64_t played = 0; played < *steps; ++played) {
            AirModel::step(future, input_of(held, played == 0 ? std::span<const Applied>(once) : std::span<const Applied>()));
        }
        return answer.data(observed(future.field, timeline_.frame() + *steps, std::nullopt));
    }
    if (operation == "air.explain") {
        const auto at = place_of(request["at"]);
        if (!at) return invalid("`at` is [x, y, z], three numbers (metres)");
        const auto cell = field.cell_at(*at);
        if (!cell) return invalid(std::format("no cell at {}", vector_text(*at)));
        const auto why = field.why(*cell);
        if (!why) return answer.fail(openas::error_code::failed, "nothing is on record: the air has not stepped yet", openas::exit_code::failed, R"({"op":"air.step"})");
        const auto laws = program.laws();
        std::string out = std::format("{{\"cell\":{},\"centre\":{},\"frame\":{},\"held\":[", *cell, vector_text(field.centre(*cell)), timeline_.frame());
        bool first = true;
        for (const matter::Judged& judged : why->laws)
            if (judged.holds) {
                out += std::format("{}{}", first ? "" : ",", mlibs::agent::json_text(laws[judged.law].name));
                first = false;
            }
        out += "],\"flows\":[";
        first = true;
        for (const matter::Flowed& flow : why->flows) {
            // A neighbour is a cell of the field (guests come after the cells).
            const std::string from = flow.beside < field.cells() ? vector_text(field.centre(flow.beside)) : std::string("null");
            out += std::format("{}{{\"law\":{},\"direction\":\"{}\",\"beside\":{},\"beside_at\":{},\"amount\":{}}}", first ? "" : ",", mlibs::agent::json_text(laws[flow.law].name),
                               flow.in ? "in" : "out", flow.beside, from, flow.amount);
            first = false;
        }
        out += "],\"after\":{";
        for (std::uint32_t channel = 1; channel < channels.size() && channel < why->after.size(); ++channel)
            out += std::format("{}{}:{}", channel > 1 ? "," : "", mlibs::agent::json_text(channels[channel].name), in_unit(channels[channel], why->after[channel]));
        out += "}";
        if (why->became_by) out += std::format(",\"became_by\":{}", mlibs::agent::json_text(laws[*why->became_by].name));
        return answer.data(out + ",\"by\":" + changes_for(*cell) + "}");
    }
}

}  // namespace xengine::field::agent
