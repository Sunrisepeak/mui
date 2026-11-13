// The matter subject of a field (D-169): the record of causes made at each commit, and the operations that
// read a field as matter. Everything here runs between requests and steps; a step's own path is the kernel's.
module xengine.cellular.matter.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.json;

namespace xengine::cellular::matter_agent {
namespace {

namespace matter = xengine::matter;
using mlibs::agent::json_text;

[[nodiscard]] std::string number(double value) { return std::format("{:.6g}", value); }
[[nodiscard]] double in_units(std::uint32_t value, std::uint32_t fraction) { return std::ldexp(static_cast<double>(value), -static_cast<int>(fraction)); }
[[nodiscard]] std::string at(Shape shape, std::uint32_t cell) {
    return cell == outside ? std::string("\"outside\"") : std::format("[{},{}]", cell % shape.width, cell / shape.width);
}
[[nodiscard]] std::string region_text(Tile region) { return std::format("[{},{},{},{}]", region.x, region.y, region.width, region.height); }
[[nodiscard]] bool within(Tile region, Shape shape, std::uint32_t cell) {
    const std::uint32_t x = cell % shape.width, y = cell / shape.width;
    return x >= region.x && y >= region.y && x < region.x + region.width && y < region.y + region.height;
}
// A material's name, or what a cell that is none holds.
[[nodiscard]] std::string material_text(const matter::Program& program, std::uint32_t material) {
    return material < program.materials().size() ? json_text(program.materials()[material].name) : std::string("null");
}
[[nodiscard]] std::string channel_value(const matter::Program& program, std::uint32_t channel, std::uint32_t value) {
    return channel == 0 ? material_text(program, value) : number(in_units(value, program.channels()[channel].fraction));
}
[[nodiscard]] std::string property_value(const matter::Program& program, std::uint32_t column, std::uint32_t value) {
    const matter::Column& of = program.columns()[column];
    if (of.kind == matter::ColumnKind::phase) return value < matter::phase_names.size() ? json_text(matter::phase_names[value]) : std::string("null");
    if (of.kind == matter::ColumnKind::material) return material_text(program, value);
    return number(in_units(value, of.fraction));
}
// What a law read, by its name as a law writes it, and its value in its unit.
[[nodiscard]] std::string read_name(const matter::Program& program, const Read& read) {
    switch (read.source) {
        case matter::Source::property: return program.columns()[read.index].name;
        case matter::Source::channel: return program.channels()[read.index].name;
        case matter::Source::near_property: return "near." + program.columns()[read.index].name;
        case matter::Source::near_channel: return "near." + program.channels()[read.index].name;
        case matter::Source::constant: break;
    }
    return "?";
}
[[nodiscard]] std::string read_value(const matter::Program& program, const Read& read) {
    const bool column = read.source == matter::Source::property || read.source == matter::Source::near_property;
    return column ? property_value(program, read.index, read.value) : channel_value(program, read.index, read.value);
}
// The comparison of a condition as written that `index` names: its text between the `&&`s.
[[nodiscard]] std::string comparison(std::string_view when, std::uint32_t index) {
    std::uint32_t seen = 0;
    for (const auto part : std::views::split(when, std::string_view("&&"))) {
        if (seen++ != index) continue;
        std::string_view text(part.begin(), part.end());
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
        return std::string(text);
    }
    return std::string(when);
}
// Whether more of what a term reads helps a comparison hold (+1), less does (−1), or neither can be said (0):
// by the side it is on, its sign, and the comparison.
[[nodiscard]] int wants(const matter::Test& test, matter::Source source, std::uint32_t index) {
    int direction = 0;
    for (const auto& [side, sign] : {std::pair{&test.left, 1}, std::pair{&test.right, -1}})
        for (const matter::Term& term : side->terms) {
            if (term.source != source || term.index != index) continue;
            const int here = term.negative ? -sign : sign;
            if (direction != 0 && direction != here) return 0;
            direction = here;
        }
    switch (test.compare) {
        case matter::Compare::greater_equal:
        case matter::Compare::greater: return direction;
        case matter::Compare::less_equal:
        case matter::Compare::less: return -direction;
        case matter::Compare::equal:
        case matter::Compare::not_equal: break;
    }
    return 0;
}

[[nodiscard]] std::string law_text(const matter::Program& program, std::uint32_t law, bool with_condition) {
    if (law >= program.laws().size()) return "null";
    const matter::Law& of = program.laws()[law];
    std::string out = std::format("{{\"name\":{},\"at\":\"/laws/{}\"", json_text(of.name), law);
    if (with_condition && !of.text.empty()) out += std::format(",\"when\":{}", json_text(of.text));
    return out + "}";
}

// What an actor did over a region, not what happened at a cell.
constexpr bool of_a_region(RecordKind kind) noexcept { return kind == RecordKind::placed || kind == RecordKind::applied || kind == RecordKind::blast; }
constexpr std::array<std::string_view, 3> thrown_by{"flung", "pressure", "blast"};

}  // namespace

Matter::Matter(std::shared_ptr<const matter::Program> program, agent::Lattice& field, Lowering lowering, std::size_t records_kept, std::int64_t most_steps)
    : program_(std::move(program)), field_(&field), lowering_(std::move(lowering)), records_kept_(std::max<std::size_t>(16, records_kept)), most_steps_(most_steps) {
    const auto channels = program_->channels();
    for (std::uint32_t channel = 1; channel < channels.size(); ++channel)
        if (channels[channel].rise || channels[channel].fall) event_channels_.push_back(channel);
    field_->lattice_watch(this);
}

Matter::~Matter() { field_->lattice_unwatch(this); }

std::optional<agent::Edit> Matter::placing(matter::MaterialId material, Tile region) const {
    const auto channels = program_->channels();
    if (material >= program_->materials().size() || channels.size() > 16) return std::nullopt;
    agent::Edit edit{.kind = agent::EditKind::set, .count = static_cast<std::uint32_t>(channels.size()), .region = region};
    for (std::uint32_t channel = 0; channel < channels.size(); ++channel) edit.channels[channel] = channel, edit.values[channel] = program_->initial(material, channel);
    return edit;
}

bool Matter::place(matter::MaterialId material, Tile region, const openas::Actor& actor) {
    const auto edit = placing(material, region);
    return edit && field_->lattice_edit(*edit, actor);
}

bool Matter::apply(std::uint32_t channel, agent::EditKind kind, std::uint32_t amount, Tile region, const openas::Actor& actor) {
    if (channel == 0 || channel >= program_->channels().size() || kind == agent::EditKind::step) return false;
    agent::Edit edit{.kind = kind, .count = 1, .region = region};
    edit.channels[0] = channel;
    edit.values[0] = amount;
    return field_->lattice_edit(edit, actor);
}

matter::Why Matter::why(std::span<const std::uint32_t> cells, std::uint32_t cell, std::vector<std::uint32_t>* beside) const {
    const agent::FieldState& state = field_->lattice_state();
    const Shape shape = state.shape;
    const std::size_t area = static_cast<std::size_t>(shape.width) * shape.height, channels = program_->channels().size();
    if (cell >= area || cells.size() != area * channels) return {};
    const std::size_t instances = 1 + lowering_.neighbourhood.size();
    std::vector<std::uint32_t> local(instances * channels, 0);
    std::vector<matter::Pair> pairs;
    if (beside) beside->assign(lowering_.neighbourhood.size(), outside);
    for (std::size_t channel = 0; channel < channels; ++channel) local[channel * instances] = cells[channel * area + cell];
    const std::int64_t x = cell % shape.width, y = cell / shape.width;
    for (std::size_t index = 0; index < lowering_.neighbourhood.size(); ++index) {
        std::int64_t nx = x + lowering_.neighbourhood[index].x, ny = y + lowering_.neighbourhood[index].y;
        const bool beyond = nx < 0 || ny < 0 || nx >= shape.width || ny >= shape.height;
        std::optional<std::uint32_t> from;
        if (!beyond) {
            from = static_cast<std::uint32_t>(ny * shape.width + nx);
        } else if (state.boundary.edge == Edge::wrap) {
            nx = ((nx % shape.width) + shape.width) % shape.width;
            ny = ((ny % shape.height) + shape.height) % shape.height;
            from = static_cast<std::uint32_t>(ny * shape.width + nx);
        } else if (state.boundary.edge == Edge::clamp) {
            from = static_cast<std::uint32_t>(std::clamp<std::int64_t>(ny, 0, shape.height - 1) * shape.width + std::clamp<std::int64_t>(nx, 0, shape.width - 1));
        }
        for (std::size_t channel = 0; channel < channels; ++channel)
            local[channel * instances + 1 + index] = from ? cells[channel * area + *from]
                                                          : state.boundary.values.empty() ? state.boundary.value : state.boundary.values[channel];
        if (beside && (!beyond || state.boundary.edge != Edge::constant)) (*beside)[index] = *from;
        pairs.push_back({0, static_cast<std::uint32_t>(1 + index), 1});
    }
    return program_->why({instances, pairs}, local, 0);
}

void Matter::judge(const agent::FieldState& now, std::span<const std::uint32_t> before, std::span<const std::uint8_t> activity_before, std::int64_t frame,
                   std::vector<Record>& out) const {
    const Shape shape = now.shape;
    const std::size_t area = static_cast<std::size_t>(shape.width) * shape.height;
    const auto laws = program_->laws();
    if (before.size() != now.cells.size() || area == 0) return;
    std::vector<std::uint32_t> beside;
    const auto states_of = [&](std::uint32_t cell) {
        std::uint32_t bits = 0;
        if (cell == outside) return bits;
        for (std::size_t index = 0; index < event_channels_.size() && index < 32; ++index)
            if (before[event_channels_[index] * area + cell]) bits |= 1u << index;
        return bits;
    };
    const auto consider = [&](std::uint32_t cell) {
        const matter::Why why = this->why(before, cell, &beside);
        if (why.after.empty() || why.laws.empty()) return;
        const auto record_of = [&](RecordKind kind, std::uint32_t law) {
            Record record{.frame = frame, .step = now.steps, .kind = kind, .cell = cell, .law = law};
            const auto judged = std::ranges::find(why.laws, law, &matter::Judged::law);
            if (judged == why.laws.end()) return record;
            record.failed = judged->failed;
            for (const matter::Reading& reading : judged->readings)
                if (record.read_count < record.reads.size()) record.reads[record.read_count++] = {reading.source, reading.index, reading.value};
            // The neighbours' parts. Where the law needed more of a channel, who gave it: the greatest flows toward
            // the cell. Where less made it so — a law that asks for less, or one that stopped holding for want of
            // it — who took it: the greatest flows away. Then the parts of the near sums the law needed; and, where
            // it stopped holding because a near sum fell short, what was beside it.
            const matter::Law& law_of = laws[law];
            const auto wanted = [&](std::uint32_t test, matter::Source source, std::uint32_t index) { return test < law_of.when.size() ? wants(law_of.when[test], source, index) : 0; };
            const auto direction = [&](matter::Source source, std::uint32_t index) {
                if (kind == RecordKind::fell) return -wanted(judged->failed, source, index);
                int all = 0;
                for (std::uint32_t test = 0; test < law_of.when.size(); ++test)
                    if (const int one = wanted(test, source, index); one != 0 && all == 0) all = one;
                return all;
            };
            std::vector<Gave> givers;
            for (const matter::Flowed& flow : why.flows) {
                const std::uint32_t quantity = laws[flow.law].quantity;
                const int needed = direction(matter::Source::channel, quantity);
                if (needed == 0 || (needed > 0) != flow.in) continue;
                const std::uint32_t from = beside[flow.beside - 1];
                givers.push_back({flow.in ? Part::gave : Part::took, from, matter::Source::channel, quantity, flow.amount, from == outside ? matter::none : before[from], states_of(from)});
            }
            std::ranges::stable_sort(givers, std::greater{}, &Gave::amount);
            if (givers.size() > 3) givers.resize(3);
            if (kind == RecordKind::fell && !law_of.when.empty()) {
                // The comparison that failed read a near sum that is too small now: what is beside the cell, each with its part.
                const matter::Test& failed = law_of.when[std::min<std::size_t>(judged->failed, law_of.when.size() - 1)];
                for (const matter::Sum* side : {&failed.left, &failed.right})
                    for (const matter::Term& term : side->terms) {
                        if (term.source != matter::Source::near_property && term.source != matter::Source::near_channel) continue;
                        if (wants(failed, term.source, term.index) <= 0 || !givers.empty()) continue;
                        for (std::size_t index = 0; index < beside.size() && givers.size() < record.gave.size(); ++index) {
                            const std::uint32_t from = beside[index];
                            std::uint32_t amount = 0;
                            for (const matter::Summed& part : why.near)
                                if (part.source == term.source && part.index == term.index && part.beside == index + 1) amount = part.amount;
                            givers.push_back({Part::beside, from, term.source, term.index, amount, from == outside ? matter::none : before[from], states_of(from)});
                        }
                    }
            }
            for (const matter::Summed& part : why.near) {
                if (kind == RecordKind::fell || givers.size() >= record.gave.size()) break;
                if (direction(part.source, part.index) <= 0) continue;
                const std::uint32_t from = beside[part.beside - 1];
                givers.push_back({Part::gave, from, part.source, part.index, part.amount, from == outside ? matter::none : before[from], states_of(from)});
            }
            for (const Gave& giver : givers)
                if (record.gave_count < record.gave.size()) record.gave[record.gave_count++] = giver;
            return record;
        };
        if (why.became_by) {
            Record record = record_of(RecordKind::became, *why.became_by);
            record.from = before[cell];
            record.to = why.after[0];
            out.push_back(record);
        }
        for (const std::uint32_t channel : event_channels_) {
            const std::uint32_t old = before[channel * area + cell], value = why.after[channel];
            if ((old == 0) == (value == 0)) continue;
            const bool rose = value != 0;
            // The law that raises this channel as a flag: the one that holds now, or the one that held and does not.
            std::uint32_t law = matter::none;
            for (const matter::Judged& judged : why.laws)
                if (laws[judged.law].flag && *laws[judged.law].flag == channel && judged.holds == rose && law == matter::none) law = judged.law;
            Record record = record_of(rose ? RecordKind::rose : RecordKind::fell, law);
            record.channel = channel;
            record.from = old;
            record.to = value;
            out.push_back(record);
        }
    };
    // Each cell against the cell it was a step ago, where it was then: what changed did so there. A tile that
    // slept through the step was not run — its cells are the input's — and is not looked at.
    if (from_.size() != area) from_.resize(area);
    const Shape tiles_of = tiles(shape, now.tile);
    if (activity_before.size() != static_cast<std::size_t>(tiles_of.width) * tiles_of.height) activity_before = {};
    now.program->origins(shape, before, {.tile = now.tile, .boundary = now.boundary, .tick = static_cast<std::uint64_t>(now.steps - 1)}, from_, activity_before);
    for (std::uint32_t tile_y = 0; tile_y < tiles_of.height; ++tile_y)
        for (std::uint32_t tile_x = 0; tile_x < tiles_of.width; ++tile_x) {
            if (!activity_before.empty() && asleep(activity_before[static_cast<std::size_t>(tile_y) * tiles_of.width + tile_x])) continue;
            const std::uint32_t right = std::min(shape.width, (tile_x + 1) * now.tile.width), bottom = std::min(shape.height, (tile_y + 1) * now.tile.height);
            for (std::uint32_t y = tile_y * now.tile.height; y < bottom; ++y)
                for (std::uint32_t cell = y * shape.width + tile_x * now.tile.width, end = y * shape.width + right; cell < end; ++cell) {
                    const std::uint32_t was = from_[cell];
                    bool changed = before[was] != now.cells[cell];
                    for (std::size_t index = 0; !changed && index < event_channels_.size(); ++index)
                        changed = (before[event_channels_[index] * area + was] == 0) != (now.cells[event_channels_[index] * area + cell] == 0);
                    if (changed) consider(was);
                }
        }
}

void Matter::keep(Record record) {
    if (records_.size() >= records_kept_) records_.erase(records_.begin(), records_.begin() + static_cast<std::ptrdiff_t>(std::max<std::size_t>(1, records_kept_ / 8)));
    records_.push_back(record);
}

std::uint32_t Matter::actor_of(const openas::Actor& actor) {
    const auto found = std::ranges::find(actors_, actor);
    if (found != actors_.end()) return static_cast<std::uint32_t>(found - actors_.begin());
    actors_.push_back(actor);
    return static_cast<std::uint32_t>(actors_.size() - 1);
}

void Matter::stepped(const agent::FieldState& now, std::span<const std::uint32_t> before, std::span<const std::uint8_t> activity_before, std::int64_t frame) {
    std::vector<Record> happened;
    judge(now, before, activity_before, frame, happened);
    for (const Record& record : happened) keep(record);
    // What landed goes where the laws then move it (sand that comes down on a slope slides): its landing is
    // asked of the cell it is in now. `from_` is this step's, as `judge` left it, for the tiles that were awake.
    constexpr std::size_t origins_kept = 16384;
    if (landings_.size() > origins_kept) std::erase_if(landings_, [frame](const auto& entry) { return entry.second.frame < frame - 600; });
    if (landings_.size() > origins_kept) landings_.clear();
    for (const Record& record : happened)
        if (record.kind == RecordKind::became) landings_[record.cell] = {record.cell, frame, RecordKind::became};
    if (landings_.empty()) return;
    const Shape shape = now.shape, tiles_of = tiles(shape, now.tile);
    const bool every = activity_before.size() != static_cast<std::size_t>(tiles_of.width) * tiles_of.height;
    moved_.clear();
    for (std::uint32_t tile_y = 0; tile_y < tiles_of.height; ++tile_y)
        for (std::uint32_t tile_x = 0; tile_x < tiles_of.width; ++tile_x) {
            if (!every && asleep(activity_before[static_cast<std::size_t>(tile_y) * tiles_of.width + tile_x])) continue;
            const std::uint32_t right = std::min(shape.width, (tile_x + 1) * now.tile.width), bottom = std::min(shape.height, (tile_y + 1) * now.tile.height);
            for (std::uint32_t y = tile_y * now.tile.height; y < bottom; ++y)
                for (std::uint32_t x = tile_x * now.tile.width; x < right; ++x) {
                    const std::uint32_t cell = y * shape.width + x, was = from_[cell];
                    if (was == cell) continue;
                    if (const auto found = landings_.find(was); found != landings_.end()) moved_.emplace_back(cell, found->second);
                }
        }
    for (const auto& [cell, landing] : moved_) landings_.erase(from_[cell]);
    for (const auto& [cell, landing] : moved_) landings_[cell] = landing;
}

void Matter::edited(const agent::FieldState& now, const agent::Edited& edit) {
    const std::uint32_t channels = static_cast<std::uint32_t>(program_->channels().size());
    Record record{.frame = edit.frame, .step = now.steps, .cell = edit.edit.region.y * now.shape.width + edit.edit.region.x, .region = edit.edit.region, .edit = edit.edit.kind};
    record.actor = actor_of(edit.actor);
    // Every channel set, the material first: a material placed. Anything else: a quantity applied.
    bool whole = edit.edit.kind == agent::EditKind::set && edit.edit.count == channels;
    for (std::uint32_t written = 0; whole && written < channels; ++written) whole = edit.edit.channels[written] == written;
    record.kind = whole ? RecordKind::placed : RecordKind::applied;
    record.channel = whole ? 0 : edit.edit.channels[0];
    record.to = edit.edit.values[0];
    keep(record);
}

void Matter::wrote(const agent::FieldState& now, std::span<const agent::Wrote> what, std::int64_t frame, std::span<const openas::Actor> actors) {
    for (const agent::Wrote& one : what) {
        Record record{.frame = frame, .step = now.steps, .cell = one.cell};
        switch (one.kind) {
            case agent::Wrote::Kind::blast:
                record.kind = RecordKind::blast;
                record.region = one.region;
                record.cell = one.region.y * now.shape.width + one.region.x;
                record.actor = actor_of(one.actor < actors.size() ? actors[one.actor] : agent::Field::owner());
                break;
            case agent::Wrote::Kind::thrown:
                record.kind = RecordKind::thrown;
                record.from = one.material;
                record.to = static_cast<std::uint32_t>(one.by);
                if (const auto origin = landings_.find(one.cell); origin != landings_.end()) {
                    record.failed = origin->second.kind == RecordKind::landed ? 2 : 1;
                    record.channel = origin->second.cell;
                    record.since = origin->second.frame;
                    landings_.erase(origin);
                }
                break;
            case agent::Wrote::Kind::landed:
                record.kind = RecordKind::landed;
                record.to = one.material;
                record.from = one.from;
                record.since = one.thrown_at;
                landings_[one.cell] = {one.cell, frame, RecordKind::landed};
                break;
            case agent::Wrote::Kind::left:
                record.kind = RecordKind::left;
                record.from = one.material;
                record.body = one.body;
                break;
            case agent::Wrote::Kind::dropped:
                record.kind = RecordKind::dropped;
                record.step = frame;  // told a few frames on, of the frame it began to move
                record.body = one.body;
                record.support = one.support;
                break;
        }
        keep(record);
    }
}

void Matter::rewound(std::int64_t frame) {
    std::erase_if(records_, [frame](const Record& record) { return record.frame > frame; });
    // Where what landed has gone since is of the frames after: not followed back.
    landings_.clear();
}

std::string Matter::happened_as(const Record& record) const {
    const matter::Program& program = *program_;
    const auto names = program.event_names();
    const auto laws = program.laws();
    const auto channels = program.channels();
    switch (record.kind) {
        case RecordKind::placed: return "placed";
        case RecordKind::applied: return "applied";
        case RecordKind::blast: return "blast";
        case RecordKind::thrown: return "thrown";
        case RecordKind::landed: return "landed";
        case RecordKind::left: return "left";
        case RecordKind::dropped: return "dropped";
        case RecordKind::became: {
            const auto event = record.law < laws.size() ? laws[record.law].event : std::nullopt;
            return event && *event < names.size() ? names[*event] : "became";
        }
        case RecordKind::rose:
        case RecordKind::fell: {
            const auto event = record.kind == RecordKind::rose ? channels[record.channel].rise : channels[record.channel].fall;
            return event && *event < names.size() ? names[*event] : record.kind == RecordKind::rose ? "rose" : "fell";
        }
    }
    return "?";
}

std::string Matter::said(const Record& record, std::span<const Record> among, int depth) const {
    const matter::Program& program = *program_;
    const Shape shape = field_->lattice_state().shape;
    const auto names = program.event_names();
    const bool edit = of_a_region(record.kind);
    std::string out = std::format("{{\"frame\":{},\"step\":{}", record.frame, record.step);
    if (!edit) out += std::format(",\"cell\":{}", at(shape, record.cell));
    const auto by = [&] {
        const openas::Actor& actor = actors_[record.actor];
        return std::format(",\"region\":{},\"by\":{{\"kind\":\"{}\",\"name\":{}}}", region_text(record.region), openas::name(actor.kind), json_text(actor.name));
    };
    switch (record.kind) {
        case RecordKind::placed: return out + std::format(",\"what\":\"placed\",\"material\":{}", material_text(program, record.to)) + by() + "}";
        case RecordKind::applied:
            return out + std::format(",\"what\":\"applied\",\"channel\":{},\"{}\":{}", json_text(program.channels()[record.channel].name),
                                     agent::edit_kind_names[static_cast<std::size_t>(record.edit)], channel_value(program, record.channel, record.to)) + by() + "}";
        case RecordKind::blast: return out + ",\"what\":\"blast\"" + by() + "}";
        case RecordKind::left: {
            out += std::format(",\"what\":\"left\",\"material\":{},\"body\":{}", material_text(program, record.from), record.body);
            // Why it is no longer solid: what it became, by which law.
            if (depth > 0)
                for (auto earlier = among.rbegin(); earlier != among.rend(); ++earlier)
                    if (earlier->kind == RecordKind::became && earlier->cell == record.cell && earlier->frame <= record.frame) {
                        out += std::format(",\"because\":{}", said(*earlier, among, depth - 1));
                        break;
                    }
            return out + "}";
        }
        case RecordKind::dropped: {
            out += std::format(",\"what\":\"dropped\",\"body\":{}", record.body);
            if (!record.support) return out + "}";
            out += std::format(",\"support\":{}", record.support);
            // Why what it rested on gave way: the cells that had left that body by then, the latest first.
            int told = 0;
            for (auto earlier = among.rbegin(); earlier != among.rend() && told < 3 && depth > 0; ++earlier) {
                if (earlier->kind != RecordKind::left || earlier->body != record.support || earlier->frame > record.frame) continue;
                out += (told ? "," : ",\"because\":[") + said(*earlier, among, depth - 1);
                ++told;
            }
            if (told) out += "]";
            return out + "}";
        }
        case RecordKind::thrown: {
            out += std::format(",\"what\":\"thrown\",\"material\":{},\"by\":\"{}\"", material_text(program, record.from), thrown_by[std::min<std::size_t>(record.to, 2)]);
            // By a blast: the one whose square held it, no later than this.
            if (depth > 0 && record.to == static_cast<std::uint32_t>(agent::Wrote::By::blast))
                for (auto earlier = among.rbegin(); earlier != among.rend(); ++earlier)
                    if (earlier->kind == RecordKind::blast && earlier->frame <= record.frame && within(earlier->region, shape, record.cell)) {
                        out += std::format(",\"because\":{}", said(*earlier, among, depth - 1));
                        break;
                    }
            // By the pressure: what last happened to the cell it was — what it became and by which law (with
            // what an actor applied of what that law read) — or, where nothing did, what an actor applied there.
            if (depth > 0 && record.to == static_cast<std::uint32_t>(agent::Wrote::By::pressure)) {
                // A pressure that lasts throws the same grain again and again: its flights before this one are
                // counted, not each told, and the cause is how it came to be there before the first.
                const Record* at = &record;
                const Record* cause = nullptr;
                int flights = 0;
                while (at->failed != 0) {
                    const RecordKind kind = at->failed == 2 ? RecordKind::landed : RecordKind::became;
                    const Record* origin = nullptr;
                    for (auto earlier = among.rbegin(); earlier != among.rend() && !origin; ++earlier)
                        if (earlier->kind == kind && earlier->cell == at->channel && earlier->frame == at->since) origin = &*earlier;
                    if (!origin) break;
                    cause = origin;
                    if (origin->kind != RecordKind::landed) break;
                    const Record* threw = nullptr;
                    for (auto earlier = among.rbegin(); earlier != among.rend() && !threw; ++earlier)
                        if (earlier->kind == RecordKind::thrown && earlier->cell == origin->from && earlier->frame == origin->since) threw = &*earlier;
                    // It came by a blast, or its throw is no longer on record: that landing says what is known.
                    if (!threw || threw->to != static_cast<std::uint32_t>(agent::Wrote::By::pressure) || threw->frame >= at->frame) break;
                    at = threw, cause = nullptr, ++flights;
                }
                for (auto earlier = among.rbegin(); earlier != among.rend() && !cause; ++earlier)
                    if (earlier->frame <= at->frame && earlier->cell == at->cell && (earlier->kind == RecordKind::became || earlier->kind == RecordKind::rose)) cause = &*earlier;
                for (auto earlier = among.rbegin(); earlier != among.rend() && !cause; ++earlier)
                    if (earlier->frame <= at->frame && earlier->kind == RecordKind::applied && within(earlier->region, shape, at->cell)) cause = &*earlier;
                if (flights) out += std::format(",\"flights_before\":{}", flights);
                if (cause) out += std::format(",\"because\":{}", said(*cause, among, depth - 1));
            }
            return out + "}";
        }
        case RecordKind::landed: {
            out += std::format(",\"what\":\"landed\",\"material\":{},\"from\":{},\"thrown_at\":{}", material_text(program, record.to), at(shape, record.from), record.since);
            // Why it flew: its being thrown, where and when it says.
            if (depth > 0)
                for (auto earlier = among.rbegin(); earlier != among.rend(); ++earlier)
                    if (earlier->kind == RecordKind::thrown && earlier->cell == record.from && earlier->frame == record.since) {
                        out += std::format(",\"because\":{}", said(*earlier, among, depth - 1));
                        break;
                    }
            return out + "}";
        }
        case RecordKind::became: {
            const auto event = record.law < program.laws().size() ? program.laws()[record.law].event : std::nullopt;
            out += std::format(",\"what\":{},\"from\":{},\"to\":{}", json_text(event && *event < names.size() ? names[*event] : "became"), material_text(program, record.from),
                               material_text(program, record.to));
            break;
        }
        case RecordKind::rose:
        case RecordKind::fell: {
            const matter::Channel& channel = program.channels()[record.channel];
            const auto event = record.kind == RecordKind::rose ? channel.rise : channel.fall;
            out += std::format(",\"what\":{},\"channel\":{}", json_text(event && *event < names.size() ? names[*event] : record.kind == RecordKind::rose ? "rose" : "fell"),
                               json_text(channel.name));
            break;
        }
    }
    if (record.law < program.laws().size()) {
        out += std::format(",\"law\":{}", law_text(program, record.law, true));
        if (record.kind == RecordKind::fell) out += std::format(",\"failed\":{}", json_text(comparison(program.laws()[record.law].text, record.failed)));
    }
    if (record.read_count) {
        out += ",\"read\":{";
        for (std::uint8_t index = 0; index < record.read_count; ++index)
            out += std::format("{}{}:{}", index ? "," : "", json_text(read_name(program, record.reads[index])), read_value(program, record.reads[index]));
        out += "}";
    }
    // What an actor applied to it of what the law read: the latest such edit, no later than this.
    for (auto earlier = among.rbegin(); earlier != among.rend(); ++earlier) {
        if (earlier->frame > record.frame || earlier->kind != RecordKind::applied || !within(earlier->region, shape, record.cell)) continue;
        if (std::ranges::none_of(std::span(record.reads).first(record.read_count),
                                 [&](const Read& read) { return read.source == matter::Source::channel && read.index == earlier->channel; }))
            continue;
        out += std::format(",\"applied\":{}", said(*earlier, {}, 0));
        break;
    }
    // The neighbours' parts, each kind a list: who gave, who took, what was beside.
    for (const auto& [part, key] : {std::pair{Part::gave, "gave"}, std::pair{Part::took, "took"}, std::pair{Part::beside, "beside"}}) {
        bool any = false;
        for (std::uint8_t index = 0; index < record.gave_count; ++index) {
            const Gave& giver = record.gave[index];
            if (giver.part != part) continue;
            const Read given{giver.source, giver.index, giver.amount};
            out += std::format("{}{{\"cell\":{},\"what\":{},\"amount\":{},\"was\":{}", any ? "," : std::format(",\"{}\":[", key), at(shape, giver.cell),
                               json_text(read_name(program, given)), read_value(program, given), material_text(program, giver.material));
            any = true;
            if (giver.states) {
                out += ",\"states\":[";
                bool first = true;
                for (std::size_t bit = 0; bit < event_channels_.size() && bit < 32; ++bit)
                    if (giver.states & (1u << bit)) out += std::format("{}{}", first ? "" : ",", json_text(program.channels()[event_channels_[bit]].name)), first = false;
                out += "]";
            }
            // Why it could: the latest thing on record of it, no later than this.
            if (depth > 0 && giver.cell != outside) {
                const Record* cause = nullptr;
                for (auto earlier = among.rbegin(); earlier != among.rend() && !cause; ++earlier) {
                    if (earlier->frame > record.frame || &*earlier == &record) continue;
                    const bool edit = of_a_region(earlier->kind);
                    if (edit ? within(earlier->region, shape, giver.cell) : earlier->cell == giver.cell) cause = &*earlier;
                }
                if (cause) out += std::format(",\"because\":{}", said(*cause, among, depth - 1));
            }
            out += "}";
        }
        if (any) out += "]";
    }
    return out + "}";
}

void Matter::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };
    const matter::Program& program = *program_;
    const agent::FieldState& state = field_->lattice_state();
    const Shape shape = state.shape;
    const std::size_t area = static_cast<std::size_t>(shape.width) * shape.height;
    const auto channels = program.channels();
    const auto materials = program.materials();
    const auto laws = program.laws();
    const auto cell_of = [&]() -> std::optional<std::uint32_t> {
        const auto parts = request["cell"].items();
        const auto x = parts.size() == 2 ? parts[0].integer() : std::nullopt, y = parts.size() == 2 ? parts[1].integer() : std::nullopt;
        if (!x || !y || *x < 0 || *y < 0 || *x >= shape.width || *y >= shape.height) return std::nullopt;
        return static_cast<std::uint32_t>(*y * shape.width + *x);
    };
    const std::string no_cell = std::format("`cell` is [x, y] within the field ({} × {})", shape.width, shape.height);
    const auto names_of = [&](auto&& range) {
        std::string out;
        for (const auto& each : range) out += std::format("{}`{}`", out.empty() ? "" : ", ", each.name);
        return out;
    };
    // A material placed: every channel as a new instance of it has.
    const auto placement = [&](mlibs::json::Node material_node, mlibs::json::Node region_node) -> std::expected<agent::Edit, std::string> {
        const auto name = material_node.text();
        const auto material = name ? program.material(*name) : std::nullopt;
        if (!material) return std::unexpected(std::format("`material` is one of {}", names_of(materials)));
        const auto region = field_->region_of(region_node, "region", true);
        if (!region) return std::unexpected(region.error());
        const auto edit = placing(*material, *region);
        if (!edit) return std::unexpected(std::string("a definition of more than 16 channels is placed channel by channel (field.place)"));
        return *edit;
    };
    // A quantity applied: one channel set to, raised by or lowered by an amount in its unit.
    const auto application = [&](mlibs::json::Node channel_node, mlibs::json::Node set, mlibs::json::Node add, mlibs::json::Node take,
                                 mlibs::json::Node region_node) -> std::expected<agent::Edit, std::string> {
        const auto name = channel_node.text();
        const auto channel = name ? program.channel(*name) : std::nullopt;
        if (!channel) return std::unexpected(std::format("`channel` is one of {}", names_of(channels.subspan(1))));
        if (*channel == 0) return std::unexpected(std::string("what a cell is made of is placed (matter.place), not applied"));
        const int said = (set ? 1 : 0) + (add ? 1 : 0) + (take ? 1 : 0);
        if (said != 1) return std::unexpected(std::string("one of `set`, `add`, `take`: an amount in the channel's unit"));
        const auto amount = (set ? set : add ? add : take).number();
        const double most = in_units(std::numeric_limits<std::uint32_t>::max(), channels[*channel].fraction);
        if (!amount || !(*amount >= 0) || *amount > most) return std::unexpected(std::format("an amount of `{}` is 0 … {}{}", *name, number(most), channels[*channel].unit.empty() ? "" : " " + channels[*channel].unit));
        const auto region = field_->region_of(region_node, "region", true);
        if (!region) return std::unexpected(region.error());
        agent::Edit edit{.kind = set ? agent::EditKind::set : add ? agent::EditKind::add : agent::EditKind::take, .count = 1, .region = *region};
        edit.channels[0] = *channel;
        edit.values[0] = static_cast<std::uint32_t>(std::llround(std::ldexp(*amount, static_cast<int>(channels[*channel].fraction))));
        return edit;
    };
    const auto status = [&] {
        const agent::FieldState& current = field_->lattice_state();
        return std::format("{{\"frame\":{},\"steps\":{},\"hash\":\"{:016x}\"}}", field_->lattice_frame(), current.steps, field_->lattice_hash());
    };
    // How many cells of each material a region of `cells` holds (the last: what is no material).
    const auto count_materials = [&](std::span<const std::uint32_t> cells, Tile region) {
        std::vector<std::uint64_t> counts(materials.size() + 1, 0);
        for (std::uint32_t y = region.y; y < region.y + region.height; ++y)
            for (std::uint32_t x = region.x; x < region.x + region.width; ++x) ++counts[std::min<std::size_t>(cells[static_cast<std::size_t>(y) * shape.width + x], materials.size())];
        return counts;
    };
    // A record in brief: what an event stream carries.
    const auto brief = [&](const Record& record) { return said(record, {}, 0); };
    const auto event_name = [&](const Record& record) { return happened_as(record); };

    if (operation == "matter.describe") {
        if (const auto name = request["material"].text()) {
            const auto material = program.material(*name);
            if (!material) return invalid(std::format("`material` is one of {}", names_of(materials)));
            std::string out = std::format("{{\"material\":{},\"id\":{},\"phase\":\"{}\",\"properties\":{{", json_text(*name), *material,
                                          matter::phase_names[static_cast<std::size_t>(program.phase(*material))]);
            bool first = true;
            for (std::uint32_t column = 0; column < program.columns().size(); ++column) {
                const matter::Column& of = program.columns()[column];
                if (of.kind == matter::ColumnKind::phase) continue;
                const std::uint32_t value = program.value(*material, column);
                if (of.kind == matter::ColumnKind::material ? value >= materials.size() : program.number(*material, column) == of.initial) continue;
                out += std::format("{}{}:{}", first ? "" : ",", json_text(of.name),
                                   of.kind == matter::ColumnKind::material ? material_text(program, value) : number(program.number(*material, column)));
                first = false;
            }
            return answer.data(out + "}}");
        }
        if (const auto name = request["law"].text()) {
            const auto found = std::ranges::find(laws, *name, &matter::Law::name);
            if (found == laws.end()) return invalid(std::format("`law` is one of {}", names_of(laws)));
            const auto index = static_cast<std::uint32_t>(found - laws.begin());
            std::string out = std::format("{{\"name\":{},\"kind\":\"{}\",\"at\":\"/laws/{}\"", json_text(found->name), matter::law_kind_names[static_cast<std::size_t>(found->kind)], index);
            if (!found->text.empty()) out += std::format(",\"when\":{}", json_text(found->text));
            if (found->kind == matter::LawKind::transfer)
                out += std::format(",\"transfer\":{},\"by\":{},\"share\":\"{} / {}\"", json_text(channels[found->quantity].name),
                                   found->by == matter::none ? std::string("null") : json_text(program.columns()[found->by].name), found->multiply, std::uint64_t{1} << found->shift);
            if (!found->writes.empty()) {
                out += ",\"changes\":[";
                for (std::size_t write = 0; write < found->writes.size(); ++write) {
                    const std::uint32_t target = found->writes[write].target;
                    out += std::format("{}{{\"{}\":{}}}", write ? "," : "",
                                       found->writes[write].change == matter::Change::add ? (found->kind == matter::LawKind::adjust ? "raise" : "add")
                                       : found->writes[write].change == matter::Change::take ? (found->kind == matter::LawKind::adjust ? "lower" : "take") : "set",
                                       json_text(found->kind == matter::LawKind::adjust ? program.columns()[target].name : channels[target].name));
                }
                out += "]";
            }
            if (found->flag) out += std::format(",\"flag\":{}", json_text(channels[*found->flag].name));
            if (found->become) out += std::format(",\"become\":{}", json_text(program.columns()[*found->become].name));
            if (found->event) out += std::format(",\"event\":{}", json_text(program.event_names()[*found->event]));
            if (found->recipe) out += ",\"recipe\":true";
            return answer.data(out + "}");
        }
        std::string out = "{\"materials\":[";
        for (std::size_t material = 0; material < materials.size(); ++material) out += (material ? "," : "") + json_text(materials[material].name);
        out += "],\"channels\":[";
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            out += std::format("{}{{\"name\":{},\"unit\":{}}}", channel ? "," : "", json_text(channels[channel].name), json_text(channels[channel].unit));
        out += "],\"laws\":[";
        for (std::size_t law = 0; law < laws.size(); ++law)
            out += std::format("{}{{\"name\":{},\"kind\":\"{}\",\"at\":\"/laws/{}\"}}", law ? "," : "", json_text(laws[law].name), matter::law_kind_names[static_cast<std::size_t>(laws[law].kind)], law);
        return answer.data(out + std::format("],\"recipes\":{}}}", program.recipes()));
    }
    if (operation == "matter.observe") {
        const auto region = field_->region_of(request["region"], "region", false);
        if (!region) return invalid(region.error());
        const auto budget = request["budget"].integer();
        const std::uint64_t cells = static_cast<std::uint64_t>(region->width) * region->height;
        const std::string head = std::format("{{\"frame\":{},\"steps\":{},\"region\":{},\"cells\":{}", field_->lattice_frame(), state.steps, region_text(*region), cells);
        // The materials there are, the most first.
        const auto counts = count_materials(state.cells, *region);
        std::vector<std::uint32_t> present;
        for (std::uint32_t material = 0; material < counts.size(); ++material)
            if (counts[material]) present.push_back(material);
        std::ranges::stable_sort(present, std::greater{}, [&](std::uint32_t material) { return counts[material]; });
        std::vector<std::string> material_parts, channel_parts;
        for (const std::uint32_t material : present)
            material_parts.push_back(std::format("{}:{}", material < materials.size() ? json_text(materials[material].name) : std::string("\"(none)\""), counts[material]));
        std::string states;
        for (std::uint32_t channel = 1; channel < channels.size(); ++channel) {
            std::uint32_t least = std::numeric_limits<std::uint32_t>::max(), most = 0;
            std::uint64_t sum = 0, filled = 0;
            for (std::uint32_t y = region->y; y < region->y + region->height; ++y) {
                const std::uint32_t* row = state.cells.data() + channel * area + static_cast<std::size_t>(y) * shape.width + region->x;
                for (std::uint32_t x = 0; x < region->width; ++x) least = std::min(least, row[x]), most = std::max(most, row[x]), sum += row[x], filled += row[x] != 0;
            }
            const std::uint32_t fraction = channels[channel].fraction;
            channel_parts.push_back(std::format("{{\"name\":{},\"unit\":{},\"least\":{},\"most\":{},\"mean\":{}}}", json_text(channels[channel].name), json_text(channels[channel].unit),
                                                number(in_units(least, fraction)), number(in_units(most, fraction)),
                                                number(std::ldexp(static_cast<double>(sum) / static_cast<double>(cells), -static_cast<int>(fraction)))));
            if (std::ranges::contains(event_channels_, channel)) states += std::format("{}{}:{}", states.empty() ? "" : ",", json_text(channels[channel].name), filled);
        }
        // Within a budget: channels are left out from the last, then materials from the fewest.
        const auto built = [&](std::size_t kept_materials, std::size_t kept_channels) {
            std::string out = head + ",\"materials\":{";
            for (std::size_t index = 0; index < kept_materials; ++index) out += (index ? "," : "") + material_parts[index];
            out += std::format("}},\"states\":{{{}}},\"channels\":[", states);
            for (std::size_t index = 0; index < kept_channels; ++index) out += (index ? "," : "") + channel_parts[index];
            return out + std::format("],\"more\":{}}}", (material_parts.size() - kept_materials) + (channel_parts.size() - kept_channels));
        };
        std::size_t kept_materials = material_parts.size(), kept_channels = channel_parts.size();
        if (budget) {
            const auto most = static_cast<std::size_t>(std::max<std::int64_t>(0, *budget));
            while (kept_channels > 0 && built(kept_materials, kept_channels).size() > most) --kept_channels;
            while (kept_materials > 0 && built(kept_materials, kept_channels).size() > most) --kept_materials;
            if (built(kept_materials, kept_channels).size() > most)
                return invalid(std::format("`budget` {}: the least this answers, with everything left out, is {} bytes", *budget, built(0, 0).size()));
        }
        return answer.data(built(kept_materials, kept_channels));
    }
    if (operation == "matter.at") {
        const auto cell = cell_of();
        if (!cell) return invalid(no_cell);
        const std::uint32_t material = state.cells[*cell];
        std::string out = std::format("{{\"cell\":{},\"frame\":{},\"material\":{}", at(shape, *cell), field_->lattice_frame(), material_text(program, material));
        if (material < materials.size()) out += std::format(",\"phase\":\"{}\"", matter::phase_names[static_cast<std::size_t>(program.phase(material))]);
        out += ",\"channels\":{";
        for (std::uint32_t channel = 1; channel < channels.size(); ++channel)
            out += std::format("{}{}:{}", channel > 1 ? "," : "", json_text(channels[channel].name), channel_value(program, channel, state.cells[channel * area + *cell]));
        out += "},\"adjusted\":{";
        // Its properties where its state leaves them other than written.
        if (material < materials.size()) {
            const matter::Why now = why(state.cells, *cell);
            bool first = true;
            for (std::uint32_t column = 0; column < now.properties.size(); ++column) {
                if (now.properties[column] == program.value(material, column)) continue;
                out += std::format("{}{}:{{\"written\":{},\"now\":{}}}", first ? "" : ",", json_text(program.columns()[column].name),
                                   property_value(program, column, program.value(material, column)), property_value(program, column, now.properties[column]));
                first = false;
            }
        }
        return answer.data(out + "}}");
    }
    if (operation == "matter.place") {
        const auto edit = placement(request["material"], request["region"]);
        if (!edit) return invalid(edit.error());
        if (!field_->lattice_edit(*edit, request.actor)) return invalid("the field does not take this edit");
        return answer.data(status());
    }
    if (operation == "matter.apply") {
        const auto edit = application(request["channel"], request["set"], request["add"], request["take"], request["region"]);
        if (!edit) return invalid(edit.error());
        if (!field_->lattice_edit(*edit, request.actor)) return invalid("the field does not take this edit");
        return answer.data(status());
    }
    if (operation == "matter.explain") {
        const auto cell = cell_of();
        if (!cell) return invalid(no_cell);
        const std::int64_t depth = request["depth"].integer().value_or(2);
        if (depth < 0 || depth > 6) return invalid("`depth` is 0 … 6");
        const auto budget = request["budget"].integer();
        const std::uint32_t material = state.cells[*cell];
        std::string out = std::format("{{\"cell\":{},\"frame\":{},\"steps\":{},\"now\":{{\"material\":{}", at(shape, *cell), field_->lattice_frame(), state.steps, material_text(program, material));
        for (std::uint32_t channel = 1; channel < channels.size(); ++channel)
            if (const std::uint32_t value = state.cells[channel * area + *cell]) out += std::format(",{}:{}", json_text(channels[channel].name), channel_value(program, channel, value));
        out += "}";
        if (const auto name = request["law"].text()) {
            // A law judged for the cell as it is now: held, or the comparison that fails, with what it read.
            const auto found = std::ranges::find(laws, *name, &matter::Law::name);
            const auto can_be_judged = [](const matter::Law& law) { return law.kind != matter::LawKind::transfer && law.kind != matter::LawKind::motion; };
            if (found == laws.end() || !can_be_judged(*found)) {
                std::string judgeable;
                for (const matter::Law& law : laws)
                    if (can_be_judged(law)) judgeable += std::format("{}`{}`", judgeable.empty() ? "" : ", ", law.name);
                return invalid(std::format("`law` is one of {} (a transfer is not judged: it flows wherever two cells differ; nor a law of motion: it has no condition)", judgeable));
            }
            const auto index = static_cast<std::uint32_t>(found - laws.begin());
            const matter::Why now = why(state.cells, *cell);
            const auto judged = std::ranges::find(now.laws, index, &matter::Judged::law);
            out += std::format(",\"judged\":{{\"law\":{}", law_text(program, index, true));
            if (judged == now.laws.end()) {
                out += ",\"holds\":false,\"because\":\"what is here is no material of this definition: no law acts on it\"}";
            } else {
                out += std::format(",\"holds\":{}", judged->holds);
                if (!judged->holds) out += std::format(",\"fails\":{}", json_text(comparison(found->text, judged->failed)));
                out += ",\"read\":{";
                for (std::size_t reading = 0; reading < judged->readings.size(); ++reading) {
                    const Read read{judged->readings[reading].source, judged->readings[reading].index, judged->readings[reading].value};
                    out += std::format("{}{}:{}", reading ? "," : "", json_text(read_name(program, read)), read_value(program, read));
                }
                out += "}}";
            }
        }
        // What is on record of it, the latest first: what happened to the cell, and the edits whose region holds it.
        std::vector<const Record*> own;
        for (auto record = records_.rbegin(); record != records_.rend(); ++record) {
            const bool edit = of_a_region(record->kind);
            if (edit ? within(record->region, shape, *cell) : record->cell == *cell) own.push_back(&*record);
        }
        // What is here landed or became what it is somewhere else and the laws moved it here: its landing is of it too.
        if (const auto landing = landings_.find(*cell); landing != landings_.end() && landing->second.cell != *cell) {
            const auto found = std::ranges::find_if(records_, [&](const Record& record) {
                return record.kind == landing->second.kind && record.cell == landing->second.cell && record.frame == landing->second.frame;
            });
            if (found != records_.end()) own.insert(std::ranges::find_if(own, [&](const Record* record) { return record->frame <= found->frame; }), &*found);
        }
        // A body's cell: what is on record of the body — when it came loose, and of what.
        if (const std::uint64_t body = field_->lattice_body(*cell)) {
            out += std::format(",\"body\":{{\"id\":{},\"records\":[", body);
            int said_of = 0;
            for (auto record = records_.rbegin(); record != records_.rend() && said_of < 4; ++record)
                if (record->kind == RecordKind::dropped && record->body == body) out += (said_of++ ? "," : "") + said(*record, records_, static_cast<int>(depth));
            out += "]}";
        }
        out += ",\"records\":[";
        std::size_t told = 0;
        for (; told < own.size() && told < 8; ++told) {
            const std::string next = said(*own[told], records_, static_cast<int>(depth));
            if (budget && out.size() + next.size() + 24 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
            out += (told ? "," : "") + next;
        }
        out += std::format("],\"more\":{}}}", own.size() - told);
        if (budget && out.size() > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget)))
            return invalid(std::format("`budget` {}: the least this answers, with every record left out, is {} bytes", *budget, out.size()));
        return answer.data(out);
    }
    if (operation == "matter.events") {
        const std::int64_t since = request["since"].integer().value_or(-1);
        const auto wanted = request["name"].text();
        const auto region = field_->region_of(request["region"], "region", false);
        if (!region) return invalid(region.error());
        const bool everywhere = !request["region"];
        const auto budget = request["budget"].integer();
        std::vector<const Record*> chosen;
        for (const Record& record : records_) {
            if (record.frame <= since) continue;
            if (wanted && event_name(record) != *wanted) continue;
            if (!everywhere && !within(*region, shape, record.cell)) continue;
            chosen.push_back(&record);
        }
        // Within a budget the latest are kept.
        std::vector<std::string> parts;
        std::size_t size = 48;
        for (auto record = chosen.rbegin(); record != chosen.rend(); ++record) {
            std::string part = brief(**record);
            if (budget && size + part.size() + 1 > static_cast<std::size_t>(std::max<std::int64_t>(0, *budget))) break;
            size += part.size() + 1;
            parts.push_back(std::move(part));
        }
        std::string out = "{\"events\":[";
        for (auto part = parts.rbegin(); part != parts.rend(); ++part) out += (part == parts.rbegin() ? "" : ",") + *part;
        return answer.data(out + std::format("],\"more\":{},\"kept\":{}}}", chosen.size() - parts.size(), records_.size()));
    }
    if (operation == "matter.branch") {
        const std::int64_t steps = request["steps"].integer().value_or(1);
        if (steps < 1 || steps > most_steps_) return invalid(std::format("`steps` is 1 … {}", most_steps_));
        const auto region = field_->region_of(request["region"], "region", false);
        if (!region) return invalid(region.error());
        std::vector<agent::Edit> edits;
        if (const auto place = request["place"]) {
            const auto edit = placement(place["material"], place["region"]);
            if (!edit) return invalid("`place`: " + edit.error());
            edits.push_back(*edit);
        }
        if (const auto apply = request["apply"]) {
            const auto edit = application(apply["channel"], apply["set"], apply["add"], apply["take"], apply["region"]);
            if (!edit) return invalid("`apply`: " + edit.error());
            edits.push_back(*edit);
        }
        std::vector<Record> happened;
        // A copy run ahead: the lattice is not touched.
        const agent::Lattice::Ahead ahead = field_->lattice_ahead(
            edits, steps, [&](const agent::FieldState& now, std::span<const std::uint32_t> before, std::span<const std::uint8_t> slept, std::int64_t frames_on) {
                judge(now, before, slept, field_->lattice_frame() + frames_on, happened);
            });
        const agent::FieldState& future = ahead.state;
        const std::int64_t frames = ahead.frames;
        const auto then = count_materials(state.cells, *region), now = count_materials(future.cells, *region);
        std::string out = std::format("{{\"frame\":{},\"steps\":{},\"hash\":\"{:016x}\",\"region\":{},\"materials\":{{", field_->lattice_frame() + frames, future.steps, ahead.hash,
                                      region_text(*region));
        bool first = true;
        for (std::size_t material = 0; material < now.size(); ++material) {
            if (!now[material] && !then[material]) continue;
            out += std::format("{}{}:{{\"now\":{},\"would\":{}}}", first ? "" : ",", material < materials.size() ? json_text(materials[material].name) : std::string("\"(none)\""),
                               then[material], now[material]);
            first = false;
        }
        // What would happen, counted by name; and the first few, to look at.
        std::vector<std::pair<std::string, std::size_t>> counted;
        for (const Record& record : happened) {
            const std::string name = event_name(record);
            const auto found = std::ranges::find(counted, name, &std::pair<std::string, std::size_t>::first);
            if (found == counted.end()) counted.emplace_back(name, 1);
            else ++found->second;
        }
        out += "},\"events\":{";
        for (std::size_t index = 0; index < counted.size(); ++index) out += std::format("{}{}:{}", index ? "," : "", json_text(counted[index].first), counted[index].second);
        out += "},\"first\":[";
        for (std::size_t index = 0; index < happened.size() && index < 6; ++index) out += (index ? "," : "") + brief(happened[index]);
        return answer.data(out + "]}");
    }
}

std::expected<World, std::string> World::make(matter::Program program, Shape shape, std::string_view filled, Options options) {
    const auto material = program.material(filled);
    if (!material) return std::unexpected(std::format("`{}` is no material of the definition", filled));
    auto lattice = lower(program, options.lowering);
    if (!lattice) return std::unexpected(std::format("the definition is not lowered to the lattice: {}", lattice.error().description().text));
    const std::size_t area = static_cast<std::size_t>(shape.width) * shape.height;
    auto field = agent::Field::make(std::move(*lattice), shape, program.fresh(area, *material), options.field);
    if (!field) return std::unexpected(field.error());
    World world;
    world.field = std::make_unique<agent::Field>(std::move(*field));
    world.matter = std::make_unique<Matter>(std::make_shared<const matter::Program>(std::move(program)), *world.field, std::move(options.lowering), options.records_kept, options.most_steps);
    return world;
}

}  // namespace xengine::cellular::matter_agent
