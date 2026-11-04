module hanim.engine;
import std;
import openmui.memory;
import openmui.log;
import openmui.trace;

namespace hanim::detail {
struct PlaybackTag;
using PlaybackKey = omui::SlotKey<PlaybackTag>;
struct FrameStorage {
    std::vector<FramePart> parts;
    std::vector<ShapeCubic> cubics;
    void resize(FrameRequirements count) { parts.resize(count.parts); cubics.resize(count.cubics); }
    FrameBuffer output() noexcept { return {parts, cubics}; }
    void swap(FrameStorage& other) noexcept { parts.swap(other.parts); cubics.swap(other.cubics); }
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
struct EngineData {
    ObjectRegistry objects;
    EngineLimits limits;
    omui::SlotStorage<Playback, PlaybackKey> playbacks;
    std::size_t parts = 0, cubics = 0, parameters = 0;
    EngineData(ObjectRegistry registry, EngineLimits budget)
        : objects(std::move(registry)), limits(budget), playbacks(budget.max_playbacks) {}
};
}

namespace hanim {
namespace {
constexpr omui::Log engine_log{"hanim.engine"};
using Status = std::expected<void, EngineFailure>;
EngineFailure fail(EngineError code) noexcept { return {.code = code}; }
EngineFailure object_failure(ObjectError error) noexcept { return {.code = EngineError::object, .object = error}; }
EngineFailure animation_failure(MorphFailure error) noexcept { return {.code = EngineError::animation, .animation = error}; }
EngineFailure effect_failure(EffectError error) noexcept { return {.code = EngineError::effect, .effect = error}; }
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
        case ProgramError::effect_parameter_budget: case ProgramError::scope_budget: code = EngineError::capacity; break;
        default: break;
    }
    return {.code = code, .object = error.object, .animation = error.animation, .effect = error.effect, .program = error};
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
        .source_revision = animation.source_revision(), .preparation = animation.identity()};
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
        result.target_revision = animation.to().revision(); result.sample_progress = current.progress;
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
    playback.pending_linear_progress = exact_source ? 0 : duration == 0 ? 1 : position / duration;
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
struct Counts { std::size_t parts = 0, cubics = 0, parameters = 0; };
Counts counts(const PreparedProgram& program) noexcept {
    Counts result{.parameters=program.scope_parameter_count()};
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
        count.parameters <= data.limits.max_total_effect_parameters - (data.parameters - old.parameters);
}
void account(detail::EngineData& data, const detail::Playback& playback, bool add) noexcept {
    const auto count = counts(playback.program);
    if (add) { data.parts += count.parts; data.cubics += count.cubics; data.parameters += count.parameters; }
    else { data.parts -= count.parts; data.cubics -= count.cubics; data.parameters -= count.parameters; }
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
    engine_log.debug("play owner={} slot={} generation={}", data->objects.owner(), inserted->index, inserted->generation);
    return PlaybackRef{data->objects.owner(), inserted->index, inserted->generation};
}
template <class F>
Status mutate(detail::EngineData* data, bool& writing, PlaybackRef ref, const char* operation, F&& action) {
    if (writing) return std::unexpected(fail(EngineError::reentrant_mutation));
    WriteScope write(writing); omui::trace::Span span(operation, "hanim.engine");
    auto playback = resolve(data, ref);
    if (!playback) return std::unexpected(playback.error());
    const auto result = action(**playback);
    if (result) engine_log.debug("{} owner={} slot={} generation={}", operation, ref.owner, ref.slot, ref.generation);
    else engine_log.warn("{} refused: {}", operation, engine_error_name(result.error().code));
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
}

std::string_view engine_error_name(EngineError error) noexcept {
    switch (error) {
        case EngineError::uninitialized: return "uninitialized";
        case EngineError::invalid_limits: return "invalid-limits";
        case EngineError::invalid_handle: return "invalid-handle";
        case EngineError::foreign_engine: return "foreign-engine";
        case EngineError::stale_handle: return "stale-handle";
        case EngineError::invalid_time: return "invalid-time";
        case EngineError::invalid_ease: return "invalid-ease";
        case EngineError::invalid_argument: return "invalid-argument";
        case EngineError::capacity: return "capacity";
        case EngineError::binding_conflict: return "binding-conflict";
        case EngineError::cancelled: return "cancelled";
        case EngineError::version_exhausted: return "version-exhausted";
        case EngineError::object: return "object";
        case EngineError::animation: return "animation";
        case EngineError::effect: return "effect";
        case EngineError::reentrant_mutation: return "reentrant-mutation";
        case EngineError::program: return "program";
        case EngineError::ambiguous_frame: return "ambiguous-frame";
        case EngineError::unsupported_operation: return "unsupported-operation";
    }
    return "unknown";
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
    WriteScope write(writing_); omui::trace::Span span("prepare", "hanim.engine");
    auto program = PreparedProgram::prepare(description, data_->objects, limits);
    if (!program) return std::unexpected(program_failure(program.error()));
    return std::move(*program);
}
std::expected<PlaybackRef, EngineFailure> Engine::play(const Program& description, ProgramLimits limits) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    WriteScope write(writing_); omui::trace::Span span("play-animation", "hanim.engine");
    auto program = PreparedProgram::prepare(description, data_->objects, limits);
    if (!program) return std::unexpected(program_failure(program.error()));
    return insert_playback(data_.get(), std::move(*program));
}
std::expected<PlaybackRef, EngineFailure> Engine::play(ObjectRef object, anim::MorphTo animation, const EffectSpec& effect, PlayOptions options) {
    return play(object, animation, std::span<const EffectSpec>(&effect, 1), options);
}
std::expected<PlaybackRef, EngineFailure> Engine::play(ObjectRef object, anim::MorphTo animation, std::span<const EffectSpec> effects, PlayOptions options) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    const auto from = capture_object(object), to = capture_object(animation.target);
    if (!from) return std::unexpected(object_failure(from.error()));
    if (!to) return std::unexpected(object_failure(to.error()));
    return play(*from, *to, effects, options, animation.options);
}
std::expected<PlaybackRef, EngineFailure> Engine::play(Snapshot from, Snapshot to, std::span<const EffectSpec> effects, PlayOptions options, MorphOptions morph) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    WriteScope write(writing_); omui::trace::Span span("play", "hanim.engine");
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    auto animation = PreparedObjectMorph::prepare(std::move(from), std::move(to), morph);
    if (!animation) return std::unexpected(animation_failure(animation.error()));
    auto effect = PreparedEffects::prepare(effects, data_->limits.effects);
    if (!effect) return std::unexpected(effect_failure(effect.error()));
    auto program = PreparedProgram::single(std::move(*animation), std::move(*effect), options, false,
        {.effects = data_->limits.effects});
    if (!program) return std::unexpected(program_failure(program.error()));
    return insert_playback(data_.get(), std::move(*program));
}
std::expected<PlaybackRef, EngineFailure> Engine::play(PreparedProgram program) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    WriteScope write(writing_); omui::trace::Span span("play-program", "hanim.engine");
    return insert_playback(data_.get(), std::move(program));
}
Status Engine::retarget(PlaybackRef ref, anim::MorphTo animation, PlayOptions options) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    const auto to = capture_object(animation.target);
    if (!to) return std::unexpected(object_failure(to.error()));
    return retarget(ref, *to, options, animation.options);
}
Status Engine::retarget(PlaybackRef ref, Snapshot to, PlayOptions options, MorphOptions morph) {
    return mutate(data_.get(), writing_, ref, "retarget", [&](detail::Playback& old) -> Status {
        if (auto result = active(old); !result) return result;
        if (old.tracks.size() != 1 || !old.program.scopes().empty()) return std::unexpected(fail(EngineError::unsupported_operation));
        if (old.revision == std::numeric_limits<std::uint64_t>::max()) return std::unexpected(fail(EngineError::version_exhausted));
        auto animation = PreparedObjectMorph::retarget(geometry(old, 0), std::move(to), morph);
        if (!animation) return std::unexpected(animation_failure(animation.error()));
        const auto description = current_effects(old);
        auto effect = PreparedEffects::prepare(description, data_->limits.effects);
        if (!effect) return std::unexpected(effect_failure(effect.error()));
        auto program = PreparedProgram::single(std::move(*animation), std::move(*effect), options, true,
            {.effects = data_->limits.effects});
        if (!program) return std::unexpected(program_failure(program.error()));
        if (!within_budget(*data_, *program, &old)) return std::unexpected(fail(EngineError::capacity));
        auto next = make_playback(std::move(*program));
        if (!next) return std::unexpected(next.error());
        next->paused = old.paused; next->rate = std::abs(old.rate); next->revision = old.revision + 1;
        account(*data_, old, false); old = std::move(*next); account(*data_, old, true);
        return {};
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
    WriteScope write(writing_); omui::trace::Span span("advance", "hanim.engine");
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    if (!std::isfinite(elapsed.count()) || elapsed.count() < 0) return std::unexpected(fail(EngineError::invalid_time));
    if (elapsed.count() == 0) return {};
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
    return {};
}
std::expected<PlaybackInfo, EngineFailure> Engine::inspect(PlaybackRef ref) const noexcept {
    const auto playback = resolve(data_.get(), ref); if (!playback) return std::unexpected(playback.error());
    return info(*data_, {ref.slot, ref.generation}, **playback);
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
} // namespace hanim
