module xengine.cellular.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.hash;
import mlibs.json;

namespace xengine::cellular::agent {

void FieldModel::step(State& state, const Input& input) noexcept {
    const std::size_t area = static_cast<std::size_t>(state.shape.width) * state.shape.height;
    if (input.kind != EditKind::step) {
        for (std::uint32_t written = 0; written < input.count; ++written) {
            std::uint32_t* channel = state.cells.data() + static_cast<std::size_t>(input.channels[written]) * area;
            const std::uint32_t value = input.values[written];
            for (std::uint32_t y = input.region.y; y < input.region.y + input.region.height; ++y) {
                std::uint32_t* row = channel + static_cast<std::size_t>(y) * state.shape.width + input.region.x;
                if (input.kind == EditKind::set) {
                    std::fill_n(row, input.region.width, value);
                    continue;
                }
                for (std::uint32_t x = 0; x < input.region.width; ++x) {
                    if (input.kind == EditKind::add) row[x] = row[x] + value < row[x] ? std::numeric_limits<std::uint32_t>::max() : row[x] + value;
                    else row[x] -= std::min(row[x], value);
                }
            }
        }
        state.program->wake(state.shape, state.tile, input.region, state.activity, state.boundary.edge);
        return;
    }
    // Shaped when the field was made: a step cannot fail here. The two buffers take turns, so a tile asleep costs nothing.
    if (state.program->step(state.shape, state.cells, state.next,
                            {.tile = state.tile, .boundary = state.boundary, .tick = static_cast<std::uint64_t>(state.steps), .activity = state.activity, .alternate = true})) {
        state.cells.swap(state.next);
        ++state.steps;
    }
}

std::expected<Field, std::string> Field::make(Program program, Shape shape, std::vector<std::uint32_t> cells, Options options) {
    const auto values = program.values(shape);
    if (!values) return std::unexpected(std::format("a field of {} × {} cells: {}", shape.width, shape.height, values.error().description().text));
    if (cells.empty()) cells.assign(*values, 0);
    if (cells.size() != *values)
        return std::unexpected(std::format("{} values for a field of {} × {} cells and {} channels ({} values)", cells.size(), shape.width, shape.height,
                                           program.channels(), *values));
    if (!options.tile.width || !options.tile.height) return std::unexpected(std::string("a field's tiles are at least a cell each way"));
    FieldState state{std::make_shared<const Program>(std::move(program)), shape, options.boundary, std::move(cells), {}, 0, options.tile, {}};
    state.next.assign(state.cells.size(), 0);
    const Shape grid = tiles(shape, options.tile);
    state.activity.assign(static_cast<std::size_t>(grid.width) * grid.height, 0);
    // A step on a copy, to say now what a step would say later.
    FieldState trial = state;
    if (const auto stepped = trial.program->step(trial.shape, trial.cells, trial.next, {.boundary = trial.boundary}); !stepped)
        return std::unexpected(std::string(stepped.error().description().text));
    return Field(state, options);
}

const openas::Actor& Field::owner() noexcept {
    static const openas::Actor actor{openas::Kind::program, "owner", {}};
    return actor;
}

void Field::step() {
    if (witness_) activity_before_ = timeline_.state().activity;  // the same size every step: no allocation after the first
    timeline_.step({});
    const FieldState& state = timeline_.state();
    if (witness_) witness_->stepped(state, state.next, activity_before_, timeline_.frame());
}

bool Field::edit(const Edit& edit, const openas::Actor& actor) {
    const FieldState& state = timeline_.state();
    if (edit.kind == EditKind::step || edit.count < 1 || edit.count > edit.channels.size()) return false;
    for (std::uint32_t written = 0; written < edit.count; ++written)
        if (edit.channels[written] >= state.program->channels()) return false;
    const Tile region = edit.region;
    if (!region.width || !region.height || region.x >= state.shape.width || region.y >= state.shape.height || region.width > state.shape.width - region.x ||
        region.height > state.shape.height - region.y)
        return false;
    timeline_.step(edit);
    if (edits_.size() >= std::max<std::size_t>(1, options_.edits_kept)) edits_.erase(edits_.begin());
    edits_.push_back({timeline_.frame(), edit, actor});
    if (witness_) witness_->edited(timeline_.state(), edits_.back());
    return true;
}

bool Field::seek(std::int64_t frame) {
    if (!timeline_.seek(frame)) return false;
    timeline_.branch();
    std::erase_if(edits_, [frame](const Edited& edit) { return edit.frame > frame; });
    if (witness_) witness_->rewound(frame);
    return true;
}

std::uint64_t Field::hash_of(const FieldState& state) noexcept {
    mlibs::hash::Fnv1a hash;
    hash.u64(state.shape.width).u64(state.shape.height).u64(state.program->channels()).i64(state.steps);
    for (const std::uint32_t cell : state.cells) hash.word(cell);
    return hash.value();
}

std::expected<Tile, std::string> Field::region_of(mlibs::json::Node node, std::string_view name, bool required) const {
    const Shape shape = timeline_.state().shape;
    if (!node) {
        if (required) return std::unexpected(std::format("`{}`: [x, y, width, height] in cells", name));
        return Tile{0, 0, shape.width, shape.height};
    }
    const auto parts = node.items();
    std::array<std::int64_t, 4> at{};
    for (std::size_t index = 0; index < 4; ++index) {
        const auto value = parts.size() == 4 ? parts[index].integer() : std::nullopt;
        if (!value || *value < 0 || *value > std::numeric_limits<std::uint32_t>::max()) return std::unexpected(std::format("`{}` is [x, y, width, height], whole cells", name));
        at[index] = *value;
    }
    if (at[2] < 1 || at[3] < 1 || at[0] + at[2] > shape.width || at[1] + at[3] > shape.height)
        return std::unexpected(std::format("`{}` [{}, {}, {}, {}] is not within the field ({} × {})", name, at[0], at[1], at[2], at[3], shape.width, shape.height));
    return Tile{static_cast<std::uint32_t>(at[0]), static_cast<std::uint32_t>(at[1]), static_cast<std::uint32_t>(at[2]), static_cast<std::uint32_t>(at[3])};
}

std::string Field::status() const {
    const FieldState& state = timeline_.state();
    return std::format("{{\"frame\":{},\"steps\":{},\"hash\":\"{:016x}\",\"earliest\":{},\"latest\":{},\"tiles\":{},\"asleep\":{}}}", timeline_.frame(), state.steps,
                       hash_of(state), timeline_.earliest(), timeline_.latest(), state.activity.size(),
                       std::ranges::count_if(state.activity, [](std::uint8_t byte) { return asleep(byte); }));
}

std::string Field::observed(const FieldState& state, std::int64_t frame, Tile region, std::optional<std::int64_t> budget) const {
    const std::size_t area = static_cast<std::size_t>(state.shape.width) * state.shape.height;
    const std::uint32_t channels = state.program->channels();
    std::string out = std::format("{{\"frame\":{},\"steps\":{},\"hash\":\"{:016x}\",\"shape\":[{},{}],\"region\":[{},{},{},{}],\"channels\":[", frame, state.steps,
                                  hash_of(state), state.shape.width, state.shape.height, region.x, region.y, region.width, region.height);
    std::uint32_t shown = 0;
    for (; shown < channels; ++shown) {
        std::uint32_t least = std::numeric_limits<std::uint32_t>::max(), most = 0;
        std::uint64_t sum = 0, filled = 0;
        const std::uint32_t* channel = state.cells.data() + static_cast<std::size_t>(shown) * area;
        for (std::uint32_t y = region.y; y < region.y + region.height; ++y) {
            const std::uint32_t* row = channel + static_cast<std::size_t>(y) * state.shape.width + region.x;
            for (std::uint32_t x = 0; x < region.width; ++x) {
                least = std::min(least, row[x]);
                most = std::max(most, row[x]);
                sum += row[x];
                filled += row[x] != 0;
            }
        }
        const std::string next = std::format("{}{{\"channel\":{},\"least\":{},\"most\":{},\"sum\":{},\"filled\":{}}}", shown ? "," : "", shown, least, most, sum, filled);
        // What closes the answer (`],"more":N}`) still has to fit.
        if (budget && out.size() + next.size() + 22 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
        out += next;
    }
    return out + std::format("],\"more\":{}}}", channels - shown);
}

void Field::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };
    const FieldState& state = timeline_.state();
    const auto channel_of = [&]() -> std::optional<std::uint32_t> {
        const auto channel = request["channel"].integer();
        if (!channel || *channel < 0 || *channel >= state.program->channels()) return std::nullopt;
        return static_cast<std::uint32_t>(*channel);
    };
    const auto value_of = [&]() -> std::optional<std::uint32_t> {
        const auto value = request["value"].integer();
        if (!value || *value < 0 || *value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
        return static_cast<std::uint32_t>(*value);
    };
    const auto steps_of = [&]() -> std::optional<std::int64_t> {
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > options_.most_steps) return std::nullopt;
        return steps;
    };
    const std::string no_channel = std::format("`channel` is 0 … {}", state.program->channels() - 1);
    const std::string no_steps = std::format("`steps` is 1 … {}", options_.most_steps);

    if (operation == "field.observe") {
        const auto region = region_of(request["region"], "region", false);
        if (!region) return invalid(region.error());
        const auto budget = request["budget"].integer();
        const std::string seen = observed(state, timeline_.frame(), *region, budget);
        if (budget && seen.size() > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget)))
            return invalid(std::format("`budget` {}: the least this answers, with every channel left out, is {} bytes", *budget, seen.size()));
        return answer.data(seen);
    }
    if (operation == "field.cells") {
        const auto channel = channel_of();
        if (!channel) return invalid(no_channel);
        const auto region = region_of(request["region"], "region", true);
        if (!region) return invalid(region.error());
        const std::size_t count = static_cast<std::size_t>(region->width) * region->height;
        if (count > options_.most_cells)
            return invalid(std::format("{} cells asked, and a request reads at most {}: ask for a smaller region, or field.observe for its numbers", count, options_.most_cells));
        const std::size_t area = static_cast<std::size_t>(state.shape.width) * state.shape.height;
        std::string out = std::format("{{\"channel\":{},\"region\":[{},{},{},{}],\"rows\":[", *channel, region->x, region->y, region->width, region->height);
        for (std::uint32_t y = 0; y < region->height; ++y) {
            out += y ? ",[" : "[";
            const std::uint32_t* row = state.cells.data() + *channel * area + static_cast<std::size_t>(region->y + y) * state.shape.width + region->x;
            for (std::uint32_t x = 0; x < region->width; ++x) out += std::format("{}{}", x ? "," : "", row[x]);
            out += "]";
        }
        return answer.data(out + "]}");
    }
    if (operation == "field.place") {
        const auto channel = channel_of();
        if (!channel) return invalid(no_channel);
        const auto value = value_of();
        if (!value) return invalid("`value` is 0 … 4294967295");
        const auto region = region_of(request["region"], "region", true);
        if (!region) return invalid(region.error());
        place(*channel, *value, *region, request.actor);
        return answer.data(status());
    }
    if (operation == "field.step") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        for (std::int64_t played = 0; played < *steps; ++played) step();
        return answer.data(status());
    }
    if (operation == "field.seek") {
        const auto frame = request["frame"].integer();
        if (!frame) return invalid("`frame`: the frame to return to");
        if (!seek(*frame))
            return answer.fail(openas::error_code::stale, std::format("frame {} is outside the history kept ({} … {})", *frame, timeline_.earliest(), timeline_.latest()));
        return answer.data(status());
    }
    if (operation == "field.branch") {
        const auto steps = steps_of();
        if (!steps) return invalid(no_steps);
        const auto region = region_of(request["region"], "region", false);
        if (!region) return invalid(region.error());
        FieldState future = state;  // a copy: the field is not touched
        std::int64_t frames = 0;
        if (request["place"] || request["value"] || request["channel"]) {
            const auto channel = channel_of();
            if (!channel) return invalid(no_channel);
            const auto value = value_of();
            if (!value) return invalid("`value` is 0 … 4294967295");
            const auto placed = region_of(request["place"], "place", true);
            if (!placed) return invalid(placed.error());
            FieldModel::step(future, Edit::place(*channel, *value, *placed));
            ++frames;
        }
        for (std::int64_t played = 0; played < *steps; ++played, ++frames) FieldModel::step(future, {});
        return answer.data(observed(future, timeline_.frame() + frames, *region, std::nullopt));
    }
    if (operation == "field.explain") {
        const auto at = request["cell"].items();
        const auto x = at.size() == 2 ? at[0].integer() : std::nullopt, y = at.size() == 2 ? at[1].integer() : std::nullopt;
        if (!x || !y || *x < 0 || *y < 0 || *x >= state.shape.width || *y >= state.shape.height)
            return invalid(std::format("`cell` is [x, y] within the field ({} × {})", state.shape.width, state.shape.height));
        const std::size_t area = static_cast<std::size_t>(state.shape.width) * state.shape.height, index = static_cast<std::size_t>(*y) * state.shape.width + static_cast<std::size_t>(*x);
        std::string out = std::format("{{\"cell\":[{},{}],\"frame\":{},\"steps\":{},\"values\":[", *x, *y, timeline_.frame(), state.steps);
        for (std::uint32_t channel = 0; channel < state.program->channels(); ++channel) out += std::format("{}{}", channel ? "," : "", state.cells[channel * area + index]);
        // Whether the program changed it in the last frame, when that frame was a step: its cells of a step ago are kept.
        const bool edited_last = !edits_.empty() && edits_.back().frame == timeline_.frame();
        if (state.steps > 0 && !edited_last) {
            std::string changed;
            for (std::uint32_t channel = 0; channel < state.program->channels(); ++channel)
                if (state.cells[channel * area + index] != state.next[channel * area + index])
                    changed += std::format("{}{{\"channel\":{},\"from\":{},\"to\":{}}}", changed.empty() ? "" : ",", channel, state.next[channel * area + index], state.cells[channel * area + index]);
            out += std::format("],\"last_step\":{{\"frame\":{},\"changed\":[{}]}},\"edits\":[", timeline_.frame(), changed);
        } else {
            out += "],\"edits\":[";
        }
        // The edits on record that cover it, the latest first: what was written, and by whom.
        std::size_t said = 0;
        for (auto edit = edits_.rbegin(); edit != edits_.rend() && said < 8; ++edit) {
            const Tile region = edit->edit.region;
            if (*x < region.x || *y < region.y || *x >= region.x + region.width || *y >= region.y + region.height) continue;
            std::string wrote;
            for (std::uint32_t written = 0; written < edit->edit.count; ++written)
                wrote += std::format("{}{{\"channel\":{},\"value\":{}}}", written ? "," : "", edit->edit.channels[written], edit->edit.values[written]);
            out += std::format("{}{{\"frame\":{},\"kind\":\"{}\",\"wrote\":[{}],\"region\":[{},{},{},{}],\"by\":{{\"kind\":\"{}\",\"name\":{}}}}}", said ? "," : "", edit->frame,
                               edit_kind_names[static_cast<std::size_t>(edit->edit.kind)], wrote, region.x, region.y, region.width, region.height, openas::name(edit->actor.kind),
                               mlibs::agent::json_text(edit->actor.name));
            ++said;
        }
        return answer.data(out + std::format("],\"kept\":{}}}", edits_.size()));
    }
}

}  // namespace xengine::cellular::agent
