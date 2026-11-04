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
struct Playback {
    PreparedObjectMorph animation;
    PreparedEffects effects;
    FrameStorage current, scratch;
    std::vector<float> values, next_values;
    PlayOptions options;
    double position = 0, rate = 1, progress = 0, sample_progress = 0;
    double pending_position = 0, pending_progress = 0, pending_linear_progress = 0;
    bool paused = false, cancelled = false, anchor_source = false;
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
struct WriteScope {
    bool& writing;
    explicit WriteScope(bool& value) noexcept : writing(value) { writing = true; }
    ~WriteScope() { writing = false; }
};
bool valid_options(PlayOptions options) noexcept {
    if (!std::isfinite(options.duration.count()) || options.duration.count() < 0) return false;
    if (options.easing.kind > EaseKind::steps) return false;
    for (double value : options.easing.p) if (!std::isfinite(value)) return false;
    if (options.easing.kind == EaseKind::cubic_bezier &&
        (options.easing.p[0] < 0 || options.easing.p[0] > 1 || options.easing.p[2] < 0 || options.easing.p[2] > 1)) return false;
    if (options.easing.kind == EaseKind::steps &&
        (options.easing.p[0] < 1 || options.easing.p[0] > 1000000 || static_cast<double>(static_cast<std::uint32_t>(options.easing.p[0])) != options.easing.p[0] ||
         options.easing.p[1] < 0 || options.easing.p[1] > 3 || static_cast<double>(static_cast<std::uint32_t>(options.easing.p[1])) != options.easing.p[1])) return false;
    return true;
}
PlaybackState state(const detail::Playback& playback) noexcept {
    if (playback.cancelled) return PlaybackState::cancelled;
    if (playback.paused) return PlaybackState::paused;
    if (playback.options.duration.count() == 0 || (playback.rate > 0 ? playback.position >= playback.options.duration.count() : playback.position <= 0))
        return PlaybackState::finished;
    return PlaybackState::playing;
}
FrameView geometry(const detail::Playback& playback) noexcept {
    return {.parts = playback.current.parts, .contours = playback.animation.contours(), .cubics = playback.current.cubics,
        .progress = playback.sample_progress, .source_object = playback.animation.source_object(),
        .source_revision = playback.animation.source_revision(), .preparation = playback.animation.identity()};
}
PlaybackInfo info(const detail::EngineData& data, detail::PlaybackKey key, const detail::Playback& playback) noexcept {
    return {.ref = {data.objects.owner(), key.index, key.generation}, .object = playback.animation.source_object(),
        .target = playback.animation.to().object(), .target_revision = playback.animation.to().revision(),
        .plan_revision = playback.revision, .state = state(playback), .position = seconds(playback.position),
        .duration = playback.options.duration, .rate = playback.rate,
        .progress = playback.progress, .sample_progress = playback.sample_progress};
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
    const auto duration = playback.options.duration.count();
    const auto linear = exact_source ? 0 : duration == 0 ? 1 : position / duration;
    const bool anchored = exact_source || (playback.anchor_source && duration > 0 && linear == 0);
    double progress = anchored ? 0 : playback.options.easing(linear);
    if (!std::isfinite(progress)) return std::unexpected(fail(EngineError::invalid_ease));
    // This contour-domain plan uses the core shape sampler's bounded progress.
    // Geometry, fill, transform and effects share exactly this same progress.
    progress = std::clamp(progress, 0.0, 1.0);
    const auto frame = playback.animation.sample(progress, playback.scratch.output());
    if (!frame) return std::unexpected(animation_failure(frame.error()));
    const auto effects = playback.effects.sample(progress, playback.next_values);
    if (!effects) return std::unexpected(effect_failure(effects.error()));
    playback.pending_position = position; playback.pending_progress = progress; playback.pending_linear_progress = linear;
    return {};
}
void publish(detail::Playback& playback) noexcept {
    playback.current.swap(playback.scratch); playback.values.swap(playback.next_values);
    playback.position = playback.pending_position; playback.sample_progress = playback.pending_progress; playback.progress = playback.pending_linear_progress;
}
std::expected<detail::Playback, EngineFailure> make_playback(PreparedObjectMorph animation, PreparedEffects effects, PlayOptions options, bool anchor_source = false) {
    detail::Playback playback;
    playback.animation = std::move(animation); playback.effects = std::move(effects); playback.options = options;
    playback.anchor_source = anchor_source;
    const auto count = playback.animation.requirements();
    playback.current.resize(count); playback.scratch.resize(count);
    playback.values.resize(playback.effects.parameter_count()); playback.next_values.resize(playback.values.size());
    auto sampled = sample_next(playback, 0);
    if (!sampled) return std::unexpected(sampled.error());
    publish(playback);
    return playback;
}
bool within_budget(const detail::EngineData& data, const PreparedObjectMorph& animation, const PreparedEffects& effects,
    const detail::Playback* previous = nullptr) noexcept {
    const auto count = animation.requirements();
    const auto old = previous ? previous->animation.requirements() : FrameRequirements{};
    return count.parts <= data.limits.max_total_parts - (data.parts - old.parts) &&
        count.cubics <= data.limits.max_total_cubics - (data.cubics - old.cubics) &&
        effects.parameter_count() <= data.limits.max_total_effect_parameters -
            (data.parameters - (previous ? previous->effects.parameter_count() : 0));
}
void account(detail::EngineData& data, const detail::Playback& playback, bool add) noexcept {
    const auto count = playback.animation.requirements(); const auto params = playback.effects.parameter_count();
    if (add) { data.parts += count.parts; data.cubics += count.cubics; data.parameters += params; }
    else { data.parts -= count.parts; data.cubics -= count.cubics; data.parameters -= params; }
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
    Effects effects; effects.reserve(playback.effects.effects().size());
    const auto parameters = playback.effects.parameters();
    for (const auto& entry : playback.effects.effects()) {
        EffectSpec effect{.id = entry.id}; effect.parameters.reserve(entry.count);
        for (std::size_t i = entry.offset; i < entry.offset + entry.count; ++i)
            effect.parameters.push_back({parameters[i].name, playback.values[i], parameters[i].to});
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
    if (!valid_options(options)) return std::unexpected(fail(!std::isfinite(options.duration.count()) || options.duration.count() < 0 ? EngineError::invalid_time : EngineError::invalid_ease));
    bool bound = false;
    data_->playbacks.for_each([&](auto, const detail::Playback& playback) {
        bound |= playback.animation.source_object() == from.object();
    });
    if (bound) return std::unexpected(fail(EngineError::binding_conflict));
    const auto inserted = data_->playbacks.try_insert([&](detail::PlaybackKey) -> std::expected<detail::Playback, EngineFailure> {
        auto animation = PreparedObjectMorph::prepare(std::move(from), std::move(to), morph);
        if (!animation) return std::unexpected(animation_failure(animation.error()));
        auto effect = PreparedEffects::prepare(effects, data_->limits.effects);
        if (!effect) return std::unexpected(effect_failure(effect.error()));
        if (!within_budget(*data_, *animation, *effect)) return std::unexpected(fail(EngineError::capacity));
        return make_playback(std::move(*animation), std::move(*effect), options);
    }, fail(EngineError::capacity));
    if (!inserted) return std::unexpected(inserted.error());
    account(*data_, *data_->playbacks.get(*inserted), true);
    engine_log.debug("play owner={} slot={} generation={}", owner(), inserted->index, inserted->generation);
    return PlaybackRef{owner(), inserted->index, inserted->generation};
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
        if (!valid_options(options)) return std::unexpected(fail(!std::isfinite(options.duration.count()) || options.duration.count() < 0 ? EngineError::invalid_time : EngineError::invalid_ease));
        if (old.revision == std::numeric_limits<std::uint64_t>::max()) return std::unexpected(fail(EngineError::version_exhausted));
        auto animation = PreparedObjectMorph::retarget(geometry(old), std::move(to), morph);
        if (!animation) return std::unexpected(animation_failure(animation.error()));
        const auto description = current_effects(old);
        auto effect = PreparedEffects::prepare(description, data_->limits.effects);
        if (!effect) return std::unexpected(effect_failure(effect.error()));
        if (!within_budget(*data_, *animation, *effect, &old)) return std::unexpected(fail(EngineError::capacity));
        auto next = make_playback(std::move(*animation), std::move(*effect), options, true);
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
        auto result = sample_next(playback, std::clamp(position.count(), 0.0, playback.options.duration.count()));
        if (!result) return result;
        publish(playback); return {};
    });
}
Status Engine::seek_progress(PlaybackRef ref, double progress) {
    if (writing_) return std::unexpected(fail(EngineError::reentrant_mutation));
    const auto playback = resolve(data_.get(), ref); if (!playback) return std::unexpected(playback.error());
    if (!std::isfinite(progress)) return std::unexpected(fail(EngineError::invalid_time));
    return seek(ref, (*playback)->options.duration * std::clamp(progress, 0.0, 1.0));
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
        const auto distance = forward ? playback.options.duration.count() - playback.position : playback.position;
        const auto speed = std::abs(playback.rate);
        // Saturate before multiplying, including huge finite rates/deltas.
        const auto position = elapsed.count() >= distance / speed ? (forward ? playback.options.duration.count() : 0) :
            std::clamp(playback.position + elapsed.count() * playback.rate, 0.0, playback.options.duration.count());
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
    const auto& value = **playback;
    return EngineFrame{info(*data_, {ref.slot, ref.generation}, value), geometry(value), &value.animation, &value.effects, value.values};
}
std::expected<std::size_t, EngineFailure> Engine::list(std::span<PlaybackInfo> output) const noexcept {
    if (!data_) return std::unexpected(fail(EngineError::uninitialized));
    if (output.size() < data_->playbacks.size()) return std::unexpected(fail(EngineError::capacity));
    std::size_t count = 0;
    data_->playbacks.for_each([&](detail::PlaybackKey key, const detail::Playback& playback) { output[count++] = info(*data_, key, playback); });
    return count;
}
} // namespace hanim
