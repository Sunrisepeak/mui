module hanim.engine;
import std;
import mlibs.memory;
import mlibs.log;
import mlibs.trace;

namespace hanim::detail {
struct PlaybackTag;
using PlaybackKey = mlibs::memory::SlotKey<PlaybackTag>;
struct FrameStorage {
    std::vector<FramePart> parts;
    std::vector<ShapeCubic> cubics;
    std::vector<double> values;
    void resize(FrameRequirements count) { parts.resize(count.parts); cubics.resize(count.cubics); values.resize(count.values); }
    FrameBuffer output() noexcept { return {parts, cubics, values}; }
    void swap(FrameStorage& other) noexcept { parts.swap(other.parts); cubics.swap(other.cubics); values.swap(other.values); }
};
struct TrackPlayback {
    FrameStorage current, scratch;
    std::vector<float> values, next_values;
    std::size_t segment = 0, pending_segment = 0;
    double progress = 0, pending_progress = 0;
};
struct Playback {
    PreparedProgram program;
    std::vector<TrackPlayback> tracks;
    std::vector<ProgramTrackView> views;
    std::vector<float> scope_values, next_scope_values;
    double position = 0, rate = 1, progress = 0;
    double pending_position = 0, pending_linear_progress = 0;
    bool paused = false, cancelled = false;
    std::uint64_t revision = 1;
};
// A driven channel: its key and channel, and its lanes in the interactive set.
struct DriveSlot {
    DriveKey key = 0;
    ch::Id id;
    std::array<Set::Channel, ch::max_lanes> lanes{};
    std::uint32_t generation = 1;
    bool live = false;
    // A playback driving the channel (timeline takeover), its last value and
    // how fast it moved there.
    std::optional<PlaybackRef> follows;
    std::size_t track = 0, part = 0;
    std::array<double, ch::max_lanes> last{}, speed{};
    double last_at = 0;
    bool seen = false;
};
struct EngineData {
    ObjectRegistry objects;
    EngineLimits limits;
    mlibs::memory::SlotStorage<Playback, PlaybackKey> playbacks;
    std::size_t parts = 0, cubics = 0, parameters = 0, routes = 0;
    // Interactive mode: one set of lanes for every driven channel, on the engine's time.
    Set drives;
    std::vector<DriveSlot> drive_slots;
    std::vector<std::uint32_t> drive_free;
    std::unordered_map<DriveKey, std::vector<std::uint32_t>> drive_keys;
    double now = 0;
    Policy policy;
    // Constraints, and the order they apply in (each after those it reads).
    struct ConstraintSlot { Constraint value; std::uint32_t generation = 1; bool live = false; };
    std::vector<ConstraintSlot> constraints;
    std::vector<std::uint32_t> constraint_order;
    EngineData(ObjectRegistry registry, EngineLimits budget)
        : objects(std::move(registry)), limits(budget), playbacks(budget.max_playbacks) {}
};
}

namespace hanim {
namespace {
constexpr mlibs::log::Log engine_log{"hanim.engine"};
using Status = std::expected<void, EngineFailure>;
EngineFailure fail(EngineError code) noexcept { return {.code = code}; }
EngineFailure object_failure(ObjectError error) noexcept { return EngineFailure{EngineError::object}.because(error); }
EngineFailure animation_failure(const MorphFailure& error) noexcept { return EngineFailure{EngineError::animation}.because(error); }
EngineFailure effect_failure(EffectError error) noexcept { return EngineFailure{EngineError::effect}.because(error); }
EngineFailure program_failure(ProgramFailure error) noexcept {
    EngineError code = EngineError::program;
    switch (error.code) {
        case ProgramError::invalid_time: code = EngineError::invalid_time; break;
        case ProgramError::invalid_ease: code = EngineError::invalid_ease; break;
        case ProgramError::binding_conflict: code = EngineError::binding_conflict; break;
        case ProgramError::animation: code = EngineError::animation; break;
        case ProgramError::effect: code = EngineError::effect; break;
        case ProgramError::object: code = EngineError::object; break;
        case ProgramError::node_budget: case ProgramError::depth_budget: case ProgramError::track_budget:
        case ProgramError::segment_budget: case ProgramError::part_budget: case ProgramError::cubic_budget:
        case ProgramError::effect_parameter_budget: case ProgramError::scope_budget:
        case ProgramError::channel_budget: code = EngineError::capacity; break;
        case ProgramError::scope_conflict: code = EngineError::unsupported_operation; break;
        default: break;
    }
    return EngineFailure{code}.because(error);
}
struct WriteScope {
    bool& writing;
    explicit WriteScope(bool& value) noexcept : writing(value) { writing = true; }
    ~WriteScope() { writing = false; }
};
PlaybackState state(const detail::Playback& playback) noexcept {
    if (playback.cancelled) return PlaybackState::cancelled;
    if (playback.paused) return PlaybackState::paused;
    const auto duration = playback.program.duration().count();
    if (duration == 0 || (playback.rate > 0 ? playback.position >= duration : playback.position <= 0))
        return PlaybackState::finished;
    return PlaybackState::playing;
}
FrameView geometry(const detail::Playback& playback, std::size_t track) noexcept {
    const auto& current = playback.tracks[track];
    const auto& animation = playback.program.segments()[current.segment].animation;
    const auto count = animation.requirements();
    return {.parts = std::span<const FramePart>(current.current.parts).first(count.parts), .contours = animation.contours(),
        .cubics = std::span<const ShapeCubic>(current.current.cubics).first(count.cubics),
        .progress = current.progress, .source_object = animation.source_object(),
        .source_revision = animation.source_revision(), .preparation = animation.identity(),
        .values = std::span<const double>(current.current.values).first(count.values), .value_slots = animation.value_slots()};
}
PlaybackInfo info(const detail::EngineData& data, detail::PlaybackKey key, const detail::Playback& playback) noexcept {
    PlaybackInfo result{.ref = {data.objects.owner(), key.index, key.generation}, .plan_revision = playback.revision,
        .state = state(playback), .position = seconds(playback.position), .duration = playback.program.duration(),
        .rate = playback.rate, .progress = playback.progress,
        .track_count = playback.tracks.size(), .segment_count = playback.program.segments().size(),
        .scope_count = playback.program.scopes().size()};
    if (playback.tracks.size() == 1) {
        const auto& current = playback.tracks[0];
        const auto& animation = playback.program.segments()[current.segment].animation;
        result.object = animation.source_object(); result.target = animation.to().object();
        result.target_revision = animation.to().revision();
        if(animation.channels().empty())result.sample_progress = current.progress;
    }
    return result;
}
std::expected<detail::Playback*, EngineFailure> resolve(detail::EngineData* data, PlaybackRef ref) noexcept {
    if (!data) return std::unexpected(fail(EngineError::uninitialized));
    if (!ref) return std::unexpected(fail(EngineError::invalid_handle));
    if (ref.owner != data->objects.owner()) return std::unexpected(fail(EngineError::foreign_engine));
    if (ref.slot >= data->playbacks.slot_count()) return std::unexpected(fail(EngineError::invalid_handle));
    auto* playback = data->playbacks.get({ref.slot, ref.generation});
    if (!playback) return std::unexpected(fail(EngineError::stale_handle));
    return playback;
}
Status sample_next(detail::Playback& playback, double position, bool exact_source = false) noexcept {
    if(!playback.program.scopes().empty()) {
        const auto sampled=playback.program.sample_scopes(seconds(position),playback.next_scope_values,exact_source);
        if(!sampled)return std::unexpected(program_failure(sampled.error()));
    }
    for (std::size_t i = 0; i < playback.tracks.size(); ++i) {
        auto& track = playback.tracks[i];
        auto sampled = playback.program.sample(i, seconds(position), track.scratch.output(), track.next_values, exact_source);
        if (!sampled) return std::unexpected(program_failure(sampled.error()));
        track.pending_segment = sampled->segment_index;
        track.pending_progress = sampled->sample_progress;
    }
    playback.pending_position = position;
    const auto duration = playback.program.duration().count();
    // Forever: the fraction of the current course.
    const auto natural = playback.program.natural_duration().count();
    playback.pending_linear_progress = exact_source ? 0 : duration == 0 ? 1 :
        std::isfinite(duration) ? position / duration : natural > 0 ? std::fmod(position, natural) / natural : 1;
    return {};
}
void publish(detail::Playback& playback) noexcept {
    for (auto& track : playback.tracks) {
        track.current.swap(track.scratch); track.values.swap(track.next_values);
        track.segment = track.pending_segment; track.progress = track.pending_progress;
    }
    playback.scope_values.swap(playback.next_scope_values);
    for(std::size_t i=0;i<playback.tracks.size();++i) {
        const auto& track=playback.tracks[i];const auto& segment=playback.program.segments()[track.segment];
        playback.views[i]={geometry(playback,i),track.segment,std::span<const float>(track.values).first(segment.effects.parameter_count())};
    }
    playback.position = playback.pending_position; playback.progress = playback.pending_linear_progress;
}
std::expected<detail::Playback, EngineFailure> make_playback(PreparedProgram program) {
    detail::Playback playback;
    playback.program = std::move(program);
    playback.tracks.resize(playback.program.tracks().size());
    playback.views.resize(playback.tracks.size());
    playback.scope_values.resize(playback.program.scope_parameter_count());
    playback.next_scope_values.resize(playback.program.scope_parameter_count());
    for (std::size_t i = 0; i < playback.tracks.size(); ++i) {
        auto& track = playback.tracks[i]; const auto& required = playback.program.tracks()[i];
        track.current.resize(required.requirements); track.scratch.resize(required.requirements);
        track.values.resize(required.max_effect_parameters); track.next_values.resize(required.max_effect_parameters);
    }
    auto sampled = sample_next(playback, 0);
    if (!sampled) return std::unexpected(sampled.error());
    publish(playback);
    return playback;
}
struct Counts { std::size_t parts = 0, cubics = 0, parameters = 0, routes = 0; };
Counts counts(const PreparedProgram& program) noexcept {
    Counts result{.parameters=program.scope_parameter_count(),.routes=program.route_segment_count()};
    for (const auto& track : program.tracks()) {
        result.parts += track.requirements.parts; result.cubics += track.requirements.cubics;
        result.parameters += track.max_effect_parameters;
    }
    return result;
}
bool within_budget(const detail::EngineData& data, const PreparedProgram& program, const detail::Playback* previous = nullptr) noexcept {
    const auto count = counts(program); const auto old = previous ? counts(previous->program) : Counts{};
    return count.parts <= data.limits.max_total_parts - (data.parts - old.parts) &&
        count.cubics <= data.limits.max_total_cubics - (data.cubics - old.cubics) &&
        count.parameters <= data.limits.max_total_effect_parameters - (data.parameters - old.parameters) &&
        count.routes <= data.limits.max_total_route_segments - (data.routes - old.routes);
}
void account(detail::EngineData& data, const detail::Playback& playback, bool add) noexcept {
    const auto count = counts(playback.program);
    if (add) { data.parts += count.parts; data.cubics += count.cubics; data.parameters += count.parameters; data.routes += count.routes; }
    else { data.parts -= count.parts; data.cubics -= count.cubics; data.parameters -= count.parameters; data.routes -= count.routes; }
}
// ── Constraints ──
// Where a part shows: the published frame of the playback showing its object.
struct Shown {
    std::span<FramePart> parts;
    std::span<double> values;
    std::span<const ValueSlot> slots;
    std::size_t index = 0;
};
std::optional<Shown> shown(detail::EngineData& data, const PartAddress& address) noexcept {
    std::optional<Shown> found;
    data.playbacks.for_each([&](auto, detail::Playback& playback) {
        if (found) return;
        for (auto& track : playback.tracks) {
            const auto& animation = playback.program.segments()[track.segment].animation;
            const auto count = animation.requirements();
            const auto parts = std::span<FramePart>(track.current.parts).first(count.parts);
            for (std::size_t index = 0; index < parts.size(); ++index)
                if (parts[index].info.from_ref.object == address.object && parts[index].info.id == address.part) {
                    found = Shown{parts, std::span<double>(track.current.values).first(count.values), animation.value_slots(), index};
                    return;
                }
        }
    });
    return found;
}
mlibs::geom::AffineD world_of(const Shown& place, std::size_t index) noexcept {
    auto world = mlibs::geom::AffineD::from(place.parts[index].local.matrix());
    for (auto parent = place.parts[index].info.parent; parent != invalid_object_slot && parent < place.parts.size();
         parent = place.parts[parent].info.parent)
        world = mlibs::geom::AffineD::from(place.parts[parent].local.matrix()) * world;
    return world;
}
// A part at rest, as its object describes it: its local transform and world.
struct Rest { Transform2D local; mlibs::geom::AffineD world; };
std::optional<Rest> at_rest(const detail::EngineData& data, const PartAddress& address) noexcept {
    const auto snapshot = data.objects.capture(address.object);
    if (!snapshot) return std::nullopt;
    const auto ref = snapshot->find(address.part);
    if (!ref) return std::nullopt;
    auto part = snapshot->get(*ref);
    if (!part) return std::nullopt;
    Rest rest{(*part)->local, mlibs::geom::AffineD::from((*part)->local.matrix())};
    for (auto parent = (*part)->parent; parent;) {
        const auto above = snapshot->get(*parent);
        if (!above) break;
        rest.world = mlibs::geom::AffineD::from((*above)->local.matrix()) * rest.world;
        parent = (*above)->parent;
    }
    return rest;
}
std::size_t channel_lanes(std::string_view channel) noexcept { return channel == "rotation" ? 1 : 2; }
// A channel's lanes on a shown part (its value channel's, when it has one).
std::optional<std::array<double, ch::max_lanes>> read(const Shown& place, std::string_view channel, std::size_t& count) noexcept {
    std::array<double, ch::max_lanes> lanes{};
    const auto& local = place.parts[place.index].local;
    if (channel == "position") { lanes[0] = local.tx; lanes[1] = local.ty; count = 2; return lanes; }
    if (channel == "rotation") { lanes[0] = local.rotate; count = 1; return lanes; }
    if (channel == "scale") { lanes[0] = local.sx; lanes[1] = local.sy; count = 2; return lanes; }
    for (const auto& slot : place.slots)
        if (slot.part == place.index && slot.id.name == channel) {
            std::copy_n(place.values.begin() + static_cast<std::ptrdiff_t>(slot.offset), slot.id.lanes, lanes.begin());
            count = slot.id.lanes; return lanes;
        }
    return std::nullopt;
}
void write(Shown& place, std::string_view channel, const std::array<double, ch::max_lanes>& lanes, std::size_t count) noexcept {
    auto& local = place.parts[place.index].local;
    if (channel == "position") { local.tx = lanes[0]; local.ty = lanes[1]; return; }
    if (channel == "rotation") { local.rotate = lanes[0]; return; }
    if (channel == "scale") { local.sx = lanes[0]; local.sy = lanes[1]; return; }
    for (const auto& slot : place.slots)
        if (slot.part == place.index && slot.id.name == channel)
            std::copy_n(lanes.begin(), std::min<std::size_t>(count, slot.id.lanes), place.values.begin() + static_cast<std::ptrdiff_t>(slot.offset));
}
void apply(detail::EngineData& data, const Constraint& constraint) noexcept {
    auto target = shown(data, constraint.target);
    if (!target) return;  // nothing draws it
    const auto source = shown(data, constraint.source);
    const auto rest = source ? std::nullopt : at_rest(data, constraint.source);
    if (!source && !rest) return;
    const auto source_world = source ? world_of(*source, source->index) : rest->world;
    const auto parent = target->parts[target->index].info.parent;
    const auto parent_world = parent == invalid_object_slot || parent >= target->parts.size() ? mlibs::geom::AffineD{} : world_of(*target, parent);
    auto& local = target->parts[target->index].local;
    switch (constraint.kind) {
        case ConstraintKind::follow: {
            const auto origin = source_world.apply(0, 0);
            const auto inverse = parent_world.inverse();
            if (!inverse) return;
            const auto placed = inverse->apply(origin[0] + constraint.offset.x, origin[1] + constraint.offset.y);
            if (std::isfinite(placed[0]) && std::isfinite(placed[1])) { local.tx = placed[0]; local.ty = placed[1]; }
            return;
        }
        case ConstraintKind::aim: {
            const auto from = (parent_world * mlibs::geom::AffineD::from(local.matrix())).apply(0, 0);
            const auto to = source_world.apply(0, 0);
            if (from == to) return;
            const double facing = mlibs::math::det::atan2(to[1] - from[1], to[0] - from[0]);
            const double parent_turn = mlibs::math::det::atan2(parent_world.b, parent_world.a);
            local.rotate = facing - parent_turn + constraint.angle;
            return;
        }
        case ConstraintKind::relate: {
            std::size_t count = 0;
            std::optional<std::array<double, ch::max_lanes>> lanes;
            if (source) lanes = read(*source, constraint.source_channel, count);
            else if (constraint.source_channel == "position") { lanes = std::array<double, ch::max_lanes>{rest->local.tx, rest->local.ty}; count = 2; }
            else if (constraint.source_channel == "rotation") { lanes = std::array<double, ch::max_lanes>{rest->local.rotate}; count = 1; }
            else if (constraint.source_channel == "scale") { lanes = std::array<double, ch::max_lanes>{rest->local.sx, rest->local.sy}; count = 2; }
            if (!lanes) return;
            for (std::size_t lane = 0; lane < count; ++lane)
                (*lanes)[lane] = constraint.scale * (*lanes)[lane] + (lane == 0 ? constraint.offset.x : lane == 1 ? constraint.offset.y : 0);
            if (std::ranges::all_of(*lanes, [](double lane) { return std::isfinite(lane); })) write(*target, constraint.target_channel, *lanes, count);
            return;
        }
    }
}
void apply_constraints(detail::EngineData& data) noexcept {
    for (const auto index : data.constraint_order)
        if (data.constraints[index].live) apply(data, data.constraints[index].value);
}
std::string_view written(const Constraint& constraint) noexcept {
    return constraint.kind == ConstraintKind::follow ? "position" : constraint.kind == ConstraintKind::aim ? "rotation" : std::string_view(constraint.target_channel);
}
bool same_part(const PartAddress& left, const PartAddress& right) noexcept { return left.object == right.object && left.part == right.part; }
// The constraints in an order where each comes after those whose target it
// reads; nothing when they make a cycle.
std::optional<std::vector<std::uint32_t>> order_constraints(const std::vector<detail::EngineData::ConstraintSlot>& slots) {
    std::vector<std::uint32_t> live;
    for (std::uint32_t index = 0; index < slots.size(); ++index) if (slots[index].live) live.push_back(index);
    std::vector<std::size_t> waiting(slots.size(), 0);
    for (const auto reader : live)
        for (const auto writer : live)
            if (reader != writer && same_part(slots[writer].value.target, slots[reader].value.source)) ++waiting[reader];
    std::vector<std::uint32_t> order, ready;
    for (const auto index : live) if (!waiting[index]) ready.push_back(index);
    while (!ready.empty()) {
        const auto index = ready.front(); ready.erase(ready.begin());
        order.push_back(index);
        for (const auto reader : live)
            if (reader != index && same_part(slots[index].value.target, slots[reader].value.source) && --waiting[reader] == 0) ready.push_back(reader);
    }
    if (order.size() != live.size()) return std::nullopt;
    return order;
}

std::expected<PlaybackRef, EngineFailure> insert_playback(detail::EngineData* data, PreparedProgram program) {
    if (!data) return std::unexpected(fail(EngineError::uninitialized));
    if (!program) return std::unexpected(program_failure({ProgramError::unprepared}));
    for (const auto& segment : program.segments()) {
        if (segment.effects.effects().size() > data->limits.effects.max_effects ||
            segment.effects.parameter_count() > data->limits.effects.max_parameters)
            return std::unexpected(effect_failure(EffectError::capacity));
    }
    for(const auto& scope:program.scopes()) {
        if(scope.effects.effects().size()>data->limits.effects.max_effects ||
            scope.effects.parameter_count()>data->limits.effects.max_parameters)
            return std::unexpected(effect_failure(EffectError::capacity));
    }
    bool bound = false;
    data->playbacks.for_each([&](auto, const detail::Playback& old) {
        for (const auto& existing : old.program.tracks()) for (const auto& added : program.tracks())
            bound |= existing.object == added.object;
    });
    if (bound) return std::unexpected(fail(EngineError::binding_conflict));
    if (!within_budget(*data, program)) return std::unexpected(fail(EngineError::capacity));
    const auto inserted = data->playbacks.try_insert([&](detail::PlaybackKey) {
        return make_playback(std::move(program));
    }, fail(EngineError::capacity));
    if (!inserted) return std::unexpected(inserted.error());
    account(*data, *data->playbacks.get(*inserted), true);
    apply_constraints(*data);
    engine_log.debug("play owner={} slot={} generation={}", data->objects.owner(), inserted->index, inserted->generation);
    return PlaybackRef{data->objects.owner(), inserted->index, inserted->generation};
}
template <class F>
Status mutate(detail::EngineData* data, bool& writing, PlaybackRef ref, const char* operation, F&& action) {
    if (writing) return std::unexpected(fail(EngineError::reentrant_mutation));
    WriteScope write(writing); mlibs::trace::Span span(operation, "hanim.engine");
    auto playback = resolve(data, ref);
    if (!playback) return std::unexpected(playback.error());
    const auto result = action(**playback);
    if (result) apply_constraints(*data);
    if (result) engine_log.debug("{} owner={} slot={} generation={}", operation, ref.owner, ref.slot, ref.generation);
    else engine_log.warn("{} refused: {}", operation, result.error().text());
    return result;
}
Status active(const detail::Playback& playback) noexcept {
    return playback.cancelled ? Status(std::unexpected(fail(EngineError::cancelled))) : Status{};
}
Effects current_effects(const detail::Playback& playback) {
    const auto& track = playback.tracks[0]; const auto& prepared = playback.program.segments()[track.segment].effects;
    Effects effects; effects.reserve(prepared.effects().size());
    const auto parameters = prepared.parameters();
    for (const auto& entry : prepared.effects()) {
        EffectSpec effect{.id = entry.id}; effect.parameters.reserve(entry.count);
        for (std::size_t i = entry.offset; i < entry.offset + entry.count; ++i)
            effect.parameters.push_back({parameters[i].name, track.values[i], parameters[i].to});
        effects.push_back(std::move(effect));
    }
    return effects;
}
template<class Prepare>
Status replace_plan(detail::EngineData& data,detail::Playback& old,PlayOptions options,Prepare&& prepare) {
    if (auto result=active(old); !result) return result;
    if (old.tracks.size()!=1 || !old.program.scopes().empty()) return std::unexpected(fail(EngineError::unsupported_operation));
    if (old.revision==std::numeric_limits<std::uint64_t>::max()) return std::unexpected(fail(EngineError::version_exhausted));
    auto animation=prepare(geometry(old,0));
    if (!animation) return std::unexpected(animation_failure(animation.error()));
    auto effect=PreparedEffects::prepare(current_effects(old),data.limits.effects);
    if (!effect) return std::unexpected(effect_failure(effect.error()));
    auto program=PreparedProgram::single(std::move(*animation),std::move(*effect),options,true,{.effects=data.limits.effects});
    if (!program) return std::unexpected(program_failure(program.error()));
    if (!within_budget(data,*program,&old)) return std::unexpected(fail(EngineError::capacity));
    auto next=make_playback(std::move(*program));if(!next)return std::unexpected(next.error());
    next->paused=old.paused;next->rate=std::abs(old.rate);next->revision=old.revision+1;
    account(data,old,false);old=std::move(*next);account(data,old,true);
    return {};
}
}

Engine::Engine() noexcept = default;
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;
std::expected<Engine, EngineFailure> Engine::make(EngineLimits limits) {
    if (!limits.max_playbacks || limits.max_playbacks > invalid_object_slot || !limits.max_total_parts)
        return std::unexpected(fail(EngineError::invalid_limits));
    auto objects = ObjectRegistry::make(limits.objects);
    if (!objects) return std::unexpected(object_failure(objects.error()));
    auto effects = PreparedEffects::prepare({}, limits.effects);
    if (!effects) return std::unexpected(effect_failure(effects.error()));
    Engine engine; engine.data_ = std::make_unique<detail::EngineData>(std::move(*objects), limits);
    return engine;
}
std::uint64_t Engine::owner() const noexcept { return data_ ? data_->objects.owner() : 0; }
std::size_t Engine::size() const noexcept { return data_ ? data_->playbacks.size() : 0; }
std::expected<ObjectRef, ObjectError> Engine::create_object(const ObjectDescription& description) {
    if (writing_) return std::unexpected(ObjectError::reentrant_mutation);
    if (!data_) return std::unexpected(ObjectError::uninitialized);
    WriteScope write(writing_); return data_->objects.create(description);
}
std::expected<std::uint64_t, ObjectError> Engine::update_object(ObjectRef ref, const ObjectDescription& description) {
    if (writing_) return std::unexpected(ObjectError::reentrant_mutation);
    if (!data_) return std::unexpected(ObjectError::uninitialized);
    WriteScope write(writing_); return data_->objects.update(ref, description);
}
std::expected<void, ObjectError> Engine::destroy_object(ObjectRef ref) {
    if (writing_) return std::unexpected(ObjectError::reentrant_mutation);
    if (!data_) return std::unexpected(ObjectError::uninitialized);
    WriteScope write(writing_); return data_->objects.destroy(ref);
}
std::expected<Snapshot, ObjectError> Engine::capture_object(ObjectRef ref) const noexcept {
    return data_ ? data_->objects.capture(ref) : std::expected<Snapshot, ObjectError>(std::unexpected(ObjectError::uninitialized));
}
std::expected<PreparedProgram, EngineFailure> Engine::prepare(const Program& description, ProgramLimits limits) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    WriteScope write(writing_); mlibs::trace::Span span("prepare", "hanim.engine");
    auto program = PreparedProgram::prepare(description, data_->objects, limits);
    if (!program) return std::unexpected(program_failure(program.error()));
    return std::move(*program);
}
std::expected<PlaybackRef, EngineFailure> Engine::play(const Program& description, ProgramLimits limits) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    WriteScope write(writing_); mlibs::trace::Span span("play-animation", "hanim.engine");
    auto program = PreparedProgram::prepare(description, data_->objects, limits);
    if (!program) return std::unexpected(program_failure(program.error()));
    return insert_playback(data_.get(), std::move(*program));
}
std::expected<PlaybackRef, EngineFailure> Engine::play(PreparedProgram program) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    WriteScope write(writing_); mlibs::trace::Span span("play-program", "hanim.engine");
    return insert_playback(data_.get(), std::move(program));
}
Status Engine::retarget(PlaybackRef ref, Snapshot to, PlayOptions options, MorphOptions morph) {
    return mutate(data_.get(), writing_, ref, "retarget", [&](detail::Playback& old) -> Status {
        return replace_plan(*data_,old,options,[&](FrameView current) {
            return PreparedObjectAnimation::retarget(current,std::move(to),morph);
        });
    });
}
Status Engine::retarget(PlaybackRef ref, const PropertyTargets& values, PlayOptions options, MorphOptions morph) {
    return retarget_properties(ref,std::span(&values,1),options,morph);
}
Status Engine::retarget_properties(PlaybackRef ref,std::span<const PropertyTargets> values,PlayOptions options,MorphOptions morph) {
    return mutate(data_.get(),writing_,ref,"retarget-properties",[&](detail::Playback& old) -> Status {
        return replace_plan(*data_,old,options,[&](FrameView current) {
            return PreparedObjectAnimation::properties(current,values,morph);
        });
    });
}
Status Engine::pause(PlaybackRef ref) {
    return mutate(data_.get(), writing_, ref, "pause", [](detail::Playback& playback) -> Status {
        if (auto result = active(playback); !result) return result;
        playback.paused = true; return {};
    });
}
Status Engine::resume(PlaybackRef ref) {
    return mutate(data_.get(), writing_, ref, "resume", [](detail::Playback& playback) -> Status {
        if (auto result = active(playback); !result) return result;
        playback.paused = false; return {};
    });
}
Status Engine::reverse(PlaybackRef ref) {
    return mutate(data_.get(), writing_, ref, "reverse", [](detail::Playback& playback) -> Status {
        if (auto result = active(playback); !result) return result;
        playback.rate = -playback.rate; return {};
    });
}
Status Engine::rate(PlaybackRef ref, double rate) {
    return mutate(data_.get(), writing_, ref, "rate", [rate](detail::Playback& playback) -> Status {
        if (auto result = active(playback); !result) return result;
        if (!std::isfinite(rate) || rate == 0) return std::unexpected(fail(EngineError::invalid_time));
        playback.rate = rate; return {};
    });
}
Status Engine::seek(PlaybackRef ref, seconds position) {
    return mutate(data_.get(), writing_, ref, "seek", [position](detail::Playback& playback) -> Status {
        if (auto result = active(playback); !result) return result;
        if (!std::isfinite(position.count())) return std::unexpected(fail(EngineError::invalid_time));
        auto result = sample_next(playback, std::clamp(position.count(), 0.0, playback.program.duration().count()));
        if (!result) return result;
        publish(playback); return {};
    });
}
Status Engine::seek_progress(PlaybackRef ref, double progress) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    const auto playback = resolve(data_.get(), ref); if (!playback) return std::unexpected(playback.error());
    if (!std::isfinite(progress)) return std::unexpected(fail(EngineError::invalid_time));
    // A program that loops forever has no fraction of its length: seek a time.
    if (!std::isfinite((*playback)->program.duration().count())) return std::unexpected(fail(EngineError::unsupported_operation));
    return seek(ref, (*playback)->program.duration() * std::clamp(progress, 0.0, 1.0));
}
Status Engine::cancel(PlaybackRef ref, CancelMode mode) {
    return mutate(data_.get(), writing_, ref, "cancel", [mode](detail::Playback& playback) -> Status {
        if (mode != CancelMode::hold && mode != CancelMode::restore) return std::unexpected(fail(EngineError::invalid_argument));
        if (mode == CancelMode::restore) {
            auto result = sample_next(playback, 0, true); if (!result) return result;
            publish(playback);
        }
        playback.cancelled = true; return {};
    });
}
Status Engine::release(PlaybackRef ref) {
    return mutate(data_.get(), writing_, ref, "release", [&](detail::Playback& playback) -> Status {
        account(*data_, playback, false); data_->playbacks.erase({ref.slot, ref.generation}); return {};
    });
}
Status Engine::advance(seconds elapsed) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    WriteScope write(writing_); mlibs::trace::Span span("advance", "hanim.engine");
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    if (!std::isfinite(elapsed.count()) || elapsed.count() < 0) return std::unexpected(fail(EngineError::invalid_time));
    if (elapsed.count() == 0) return {};
    data_->now += elapsed.count();
    std::optional<EngineFailure> error;
    data_->playbacks.for_each([&](auto, detail::Playback& playback) {
        if (error || state(playback) != PlaybackState::playing) return;
        const bool forward = playback.rate > 0;
        const auto distance = forward ? playback.program.duration().count() - playback.position : playback.position;
        const auto speed = std::abs(playback.rate);
        // Saturate before multiplying, including huge finite rates/deltas.
        const auto position = elapsed.count() >= distance / speed ? (forward ? playback.program.duration().count() : 0) :
            std::clamp(playback.position + elapsed.count() * playback.rate, 0.0, playback.program.duration().count());
        auto result = sample_next(playback, position);
        if (!result) error = result.error();
    });
    if (error) return std::unexpected(*error);
    data_->playbacks.for_each([](auto, detail::Playback& playback) {
        if (state(playback) == PlaybackState::playing) publish(playback);
    });
    apply_constraints(*data_);
    follow_playbacks();
    return {};
}
std::expected<PlaybackInfo, EngineFailure> Engine::inspect(PlaybackRef ref) const noexcept {
    const auto playback = resolve(data_.get(), ref); if (!playback) return std::unexpected(playback.error());
    return info(*data_, {ref.slot, ref.generation}, **playback);
}
std::expected<PreparedProgram, EngineFailure> Engine::plan(PlaybackRef ref) const noexcept {
    const auto playback=resolve(data_.get(),ref);if(!playback)return std::unexpected(playback.error());
    return (*playback)->program;
}
std::expected<EngineFrame, EngineFailure> Engine::frame(PlaybackRef ref) const noexcept {
    const auto playback = resolve(data_.get(), ref); if (!playback) return std::unexpected(playback.error());
    if ((*playback)->tracks.size() != 1) return std::unexpected(fail(EngineError::ambiguous_frame));
    return frame(ref, 0);
}
std::expected<EngineFrame, EngineFailure> Engine::frame(PlaybackRef ref, std::size_t track) const noexcept {
    const auto playback = resolve(data_.get(), ref); if (!playback) return std::unexpected(playback.error());
    const auto& value = **playback;
    if (track >= value.tracks.size()) return std::unexpected(fail(EngineError::invalid_argument));
    const auto& current = value.tracks[track]; const auto& selected = value.program.segments()[current.segment];
    return EngineFrame{info(*data_, {ref.slot, ref.generation}, value), geometry(value, track),
        &selected.animation, &selected.effects, std::span<const float>(current.values).first(selected.effects.parameter_count()),
        track, current.segment};
}
std::expected<EngineProgramFrame, EngineFailure> Engine::program_frame(PlaybackRef ref) const noexcept {
    const auto playback=resolve(data_.get(),ref);if(!playback)return std::unexpected(playback.error());
    const auto& value=**playback;
    return EngineProgramFrame{info(*data_,{ref.slot,ref.generation},value),
        {value.views,value.scope_values,seconds(value.position),value.program.identity()}};
}
std::expected<std::size_t, EngineFailure> Engine::list(std::span<PlaybackInfo> output) const noexcept {
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    if (output.size() < data_->playbacks.size()) return std::unexpected(fail(EngineError::capacity));
    std::size_t count = 0;
    data_->playbacks.for_each([&](detail::PlaybackKey key, const detail::Playback& playback) { output[count++] = info(*data_, key, playback); });
    return count;
}

// ── Interactive mode ──
namespace {
std::expected<const detail::DriveSlot*, EngineFailure> drive_slot(const detail::EngineData* data, DriveRef ref) noexcept {
    if (!data) return std::unexpected(fail(EngineError::uninitialized));
    if (!ref || ref.index >= data->drive_slots.size()) return std::unexpected(fail(EngineError::invalid_handle));
    const auto& slot = data->drive_slots[ref.index];
    if (!slot.live || slot.generation != ref.generation) return std::unexpected(fail(EngineError::stale_handle));
    return &slot;
}
}  // namespace
seconds Engine::now() const noexcept { return seconds(data_ ? data_->now : 0); }
void Engine::reduce_motion(bool on) noexcept { if (data_) data_->policy.reduce_motion = on; }
std::size_t Engine::driven_lane_capacity() const noexcept { return data_ ? data_->drives.capacity() : 0; }
std::expected<DriveRef, EngineFailure> Engine::drive_lanes(DriveKey key, ch::Id id, std::span<const double> rest) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    if (id.name.empty() || !id.lanes || id.lanes > ch::max_lanes || rest.size() != id.lanes ||
        !std::ranges::all_of(rest, [](double v) { return std::isfinite(v); }))
        return std::unexpected(fail(EngineError::invalid_argument));
    auto& keyed = data_->drive_keys[key];
    for (const auto index : keyed) {
        const auto& slot = data_->drive_slots[index];
        if (slot.id.name != id.name) continue;
        // One key's channel has one lane count.
        if (!(slot.id == id)) return std::unexpected(fail(EngineError::invalid_argument));
        return DriveRef{index, slot.generation};
    }
    std::uint32_t index;
    if (!data_->drive_free.empty()) { index = data_->drive_free.back(); data_->drive_free.pop_back(); }
    else { index = static_cast<std::uint32_t>(data_->drive_slots.size()); data_->drive_slots.emplace_back(); }
    auto& slot = data_->drive_slots[index];
    slot.key = key; slot.id = id; slot.live = true;
    for (std::size_t i = 0; i < id.lanes; ++i) slot.lanes[i] = data_->drives.hold(rest[i]);
    keyed.push_back(index);
    return DriveRef{index, slot.generation};
}
std::expected<void, EngineFailure> Engine::drive_to(DriveRef ref, DriveVerb verb, std::span<const double> target, const DriveMotion& motion) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    const auto slot = drive_slot(data_.get(), ref); if (!slot) return std::unexpected(slot.error());
    if (target.size() != (*slot)->id.lanes || !std::ranges::all_of(target, [](double v) { return std::isfinite(v); }))
        return std::unexpected(fail(EngineError::invalid_argument));
    const auto& p = motion.spring;
    if (verb == DriveVerb::spring && !(p.stiffness > 0 && p.mass > 0 && p.damping >= 0 && std::isfinite(p.stiffness) &&
                                       std::isfinite(p.damping) && std::isfinite(p.mass)))
        return std::unexpected(fail(EngineError::invalid_argument));
    if (verb == DriveVerb::tween && (!std::isfinite(motion.duration.count()) || motion.duration.count() < 0)) return std::unexpected(fail(EngineError::invalid_time));
    if (verb == DriveVerb::tween && !motion.easing.valid()) return std::unexpected(fail(EngineError::invalid_ease));
    if (verb == DriveVerb::glide && !(motion.decay.rate > 0 && motion.decay.rate < 1)) return std::unexpected(fail(EngineError::invalid_argument));
    if (motion.at && !std::isfinite(motion.at->count())) return std::unexpected(fail(EngineError::invalid_time));
    const seconds at = motion.at.value_or(seconds(data_->now));
    for (std::size_t i = 0; i < target.size(); ++i) {
        const auto lane = (*slot)->lanes[i];
        switch (verb) {
            case DriveVerb::spring: data_->drives.spring_to(lane, at, target[i], p); break;
            case DriveVerb::tween: data_->drives.tween_to(lane, at, target[i], motion.duration, motion.easing); break;
            case DriveVerb::jump: data_->drives.jump(lane, target[i]); break;
            case DriveVerb::glide: data_->drives.glide(lane, at, target[i], motion.decay); break;
        }
    }
    return {};
}
std::expected<void, EngineFailure> Engine::drive_read(DriveRef ref, std::optional<seconds> at, std::span<double> value,
                                                      std::span<double> velocity) const noexcept {
    const auto slot = drive_slot(data_.get(), ref); if (!slot) return std::unexpected(slot.error());
    if (value.size() < (*slot)->id.lanes || velocity.size() < (*slot)->id.lanes) return std::unexpected(fail(EngineError::invalid_argument));
    const seconds when = at.value_or(seconds(data_->now));
    for (std::size_t i = 0; i < (*slot)->id.lanes; ++i) {
        value[i] = data_->drives.value((*slot)->lanes[i], when, data_->policy);
        velocity[i] = data_->policy.reduce_motion ? 0 : (*slot)->follows ? (*slot)->speed[i] : data_->drives.velocity((*slot)->lanes[i], when);
    }
    return {};
}
std::expected<bool, EngineFailure> Engine::drive_settled(DriveRef ref, std::optional<seconds> at) const noexcept {
    const auto slot = drive_slot(data_.get(), ref); if (!slot) return std::unexpected(slot.error());
    if (data_->policy.reduce_motion) return true;
    const seconds when = at.value_or(seconds(data_->now));
    for (std::size_t i = 0; i < (*slot)->id.lanes; ++i)
        if (!data_->drives.settled((*slot)->lanes[i], when)) return false;
    return true;
}
std::expected<void, EngineFailure> Engine::drive_target(DriveRef ref, std::span<double> target) const noexcept {
    const auto slot = drive_slot(data_.get(), ref); if (!slot) return std::unexpected(slot.error());
    if (target.size() < (*slot)->id.lanes) return std::unexpected(fail(EngineError::invalid_argument));
    for (std::size_t i = 0; i < (*slot)->id.lanes; ++i) target[i] = data_->drives.target((*slot)->lanes[i]);
    return {};
}
std::expected<seconds, EngineFailure> Engine::drive_settle_time(DriveRef ref) const noexcept {
    const auto slot = drive_slot(data_.get(), ref); if (!slot) return std::unexpected(slot.error());
    seconds latest{0};
    for (std::size_t i = 0; i < (*slot)->id.lanes; ++i) latest = std::max(latest, data_->drives.settle_time((*slot)->lanes[i]));
    return latest;
}
std::expected<std::uint32_t, EngineFailure> Engine::drive_lane(DriveRef ref, std::size_t lane) const noexcept {
    const auto slot = drive_slot(data_.get(), ref); if (!slot) return std::unexpected(slot.error());
    if (lane >= (*slot)->id.lanes) return std::unexpected(fail(EngineError::invalid_argument));
    return (*slot)->lanes[lane].index;
}
std::expected<void, EngineFailure> Engine::drive_release(DriveRef ref) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    const auto found = drive_slot(data_.get(), ref); if (!found) return std::unexpected(found.error());
    auto& slot = data_->drive_slots[ref.index];
    for (std::size_t i = 0; i < slot.id.lanes; ++i) data_->drives.remove(slot.lanes[i]);
    auto& keyed = data_->drive_keys[slot.key];
    std::erase(keyed, ref.index);
    if (keyed.empty()) data_->drive_keys.erase(slot.key);
    slot.live = false; ++slot.generation; if (slot.generation == 0) slot.generation = 1;
    data_->drive_free.push_back(ref.index);
    return {};
}
std::expected<void, EngineFailure> Engine::drive_follow(DriveRef ref, PlaybackRef playback, std::size_t track, std::size_t part) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    const auto found = drive_slot(data_.get(), ref); if (!found) return std::unexpected(found.error());
    const auto info = inspect(playback); if (!info) return std::unexpected(info.error());
    if (track >= info->track_count) return std::unexpected(fail(EngineError::invalid_argument));
    auto& slot = data_->drive_slots[ref.index];
    slot.follows = playback; slot.track = track; slot.part = part; slot.seen = false;
    follow_playbacks();
    return {};
}
// Each followed playback's value lands on its channel; one that ended hands
// the channel back with its momentum.
void Engine::follow_playbacks() noexcept {
    if (!data_) return;
    const seconds now(data_->now);
    for (auto& slot : data_->drive_slots) {
        if (!slot.live || !slot.follows) continue;
        const auto info = inspect(*slot.follows);
        const bool playing = info && (info->state == PlaybackState::playing || info->state == PlaybackState::paused);
        std::optional<std::span<const double>> value;
        if (info) {
            const auto frame = this->frame(*slot.follows, slot.track);
            if (frame) value = frame->geometry.value(slot.part, slot.id);
        }
        if (value && value->size() == slot.id.lanes) {
            for (std::size_t i = 0; i < slot.id.lanes; ++i) {
                const double x = (*value)[i], dt = data_->now - slot.last_at;
                slot.speed[i] = slot.seen && dt > 0 ? (x - slot.last[i]) / dt : 0;
                slot.last[i] = x;
                data_->drives.jump(slot.lanes[i], x);
            }
            slot.last_at = data_->now; slot.seen = true;
        }
        if (!playing) {
            // Handed back: from where the timeline left it, as fast as it moved.
            if (slot.seen)
                for (std::size_t i = 0; i < slot.id.lanes; ++i) data_->drives.carry(slot.lanes[i], now, slot.last[i], slot.speed[i]);
            slot.follows.reset(); slot.seen = false;
        }
    }
}
std::size_t Engine::moving() const noexcept {
    if (!data_ || data_->policy.reduce_motion) return 0;
    std::size_t count = 0;
    for (const auto& slot : data_->drive_slots) {
        if (!slot.live) continue;
        for (std::size_t i = 0; i < slot.id.lanes; ++i)
            if (!data_->drives.settled(slot.lanes[i], seconds(data_->now))) { ++count; break; }
    }
    return count;
}
std::expected<std::size_t, EngineFailure> Engine::evaluate_driven(std::span<double> out) const noexcept {
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    if (out.size() < data_->drives.capacity()) return std::unexpected(fail(EngineError::invalid_argument));
    return evaluate(data_->drives, seconds(data_->now), out, data_->policy);
}
} // namespace hanim

namespace hanim {
std::expected<ConstraintRef, EngineFailure> Engine::constrain(Constraint constraint) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    WriteScope scope(writing_);
    for (const auto* address : {&constraint.target, &constraint.source}) {
        const auto snapshot = data_->objects.capture(address->object);
        if (!snapshot) return std::unexpected(object_failure(snapshot.error()));
        if (!snapshot->find(address->part)) return std::unexpected(object_failure(ObjectError::part_not_found));
    }
    const auto valid_channel = [](std::string_view channel) { return !channel.empty() && channel.size() <= 128 && channel != "fill"; };
    if (same_part(constraint.target, constraint.source) || constraint.kind > ConstraintKind::relate || !std::isfinite(constraint.offset.x) ||
        !std::isfinite(constraint.offset.y) || !std::isfinite(constraint.angle) || !std::isfinite(constraint.scale) ||
        (constraint.kind == ConstraintKind::relate && (!valid_channel(constraint.target_channel) || !valid_channel(constraint.source_channel) ||
                                                       (ch::builtin(constraint.target_channel) && ch::builtin(constraint.source_channel) &&
                                                        channel_lanes(constraint.target_channel) != channel_lanes(constraint.source_channel)))))
        return std::unexpected(fail(EngineError::invalid_argument));
    for (const auto& slot : data_->constraints)
        if (slot.live && same_part(slot.value.target, constraint.target) && written(slot.value) == written(constraint))
            return std::unexpected(fail(EngineError::binding_conflict));
    auto slots = data_->constraints;
    std::uint32_t index = 0;
    while (index < slots.size() && slots[index].live) ++index;
    if (index == slots.size()) slots.push_back({});
    slots[index].value = std::move(constraint);
    slots[index].live = true;
    auto order = order_constraints(slots);
    if (!order) return std::unexpected(fail(EngineError::constraint_cycle));
    data_->constraints = std::move(slots);
    data_->constraint_order = std::move(*order);
    apply_constraints(*data_);
    return ConstraintRef{index, data_->constraints[index].generation};
}
std::expected<void, EngineFailure> Engine::unconstrain(ConstraintRef constraint) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    if (constraint.index >= data_->constraints.size() || !data_->constraints[constraint.index].live ||
        data_->constraints[constraint.index].generation != constraint.generation)
        return std::unexpected(fail(EngineError::stale_handle));
    auto& slot = data_->constraints[constraint.index];
    slot.live = false;
    ++slot.generation;
    auto order = order_constraints(data_->constraints);
    data_->constraint_order = order ? std::move(*order) : std::vector<std::uint32_t>{};
    return {};
}
std::size_t Engine::constraint_count() const noexcept {
    return data_ ? static_cast<std::size_t>(std::ranges::count_if(data_->constraints, [](const auto& slot) { return slot.live; })) : 0;
}
}  // namespace hanim
