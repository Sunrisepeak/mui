// The player owns time and buffers; immutable library plans own correspondence.
export module hanim.engine;
import std;
import mlibs.error;
export import hanim.program;
import mlibs.memory;

namespace hanim::detail { struct EngineData; }
export namespace hanim {

// A playback of an engine (mlibs.memory's OwnedHandle, tagged).
struct PlaybackTag;
using PlaybackRef = mlibs::memory::OwnedHandle<PlaybackTag>;
enum class PlaybackState : std::uint8_t { playing, paused, finished, cancelled };
enum class CancelMode : std::uint8_t { hold, restore };
enum class EngineError : std::uint8_t {
    uninitialized, invalid_limits, invalid_handle, foreign_engine, stale_handle,
    invalid_time, invalid_ease, invalid_argument, capacity, binding_conflict, cancelled,
    version_exhausted, object, animation, effect, reentrant_mutation,
    program, ambiguous_frame, unsupported_operation
};
// The codes as the error contract describes them (mlibs.error).
[[nodiscard]] constexpr mlibs::error::Description describe(EngineError e) noexcept {
    using Kind = mlibs::error::Kind;  // not a using-declaration: that is an ICE in GCC 16
    switch (e) {
        case EngineError::uninitialized: return {"hanim.engine.uninitialized", Kind::invalid_argument};
        case EngineError::invalid_limits: return {"hanim.engine.invalid-limits", Kind::invalid_argument};
        case EngineError::invalid_handle: return {"hanim.engine.invalid-handle", Kind::invalid_argument};
        case EngineError::foreign_engine: return {"hanim.engine.foreign-engine", Kind::invalid_argument};
        case EngineError::stale_handle: return {"hanim.engine.stale-handle", Kind::stale};
        case EngineError::invalid_time: return {"hanim.engine.invalid-time", Kind::invalid_argument};
        case EngineError::invalid_ease: return {"hanim.engine.invalid-ease", Kind::invalid_argument};
        case EngineError::invalid_argument: return {"hanim.engine.invalid-argument", Kind::invalid_argument};
        case EngineError::capacity: return {"hanim.engine.capacity", Kind::refused};
        case EngineError::binding_conflict: return {"hanim.engine.binding-conflict", Kind::refused};
        case EngineError::cancelled: return {"hanim.engine.cancelled", Kind::failed};
        case EngineError::version_exhausted: return {"hanim.engine.version-exhausted", Kind::refused};
        case EngineError::object: return {"hanim.engine.object", Kind::failed, {}, true};
        case EngineError::animation: return {"hanim.engine.animation", Kind::failed, {}, true};
        case EngineError::effect: return {"hanim.engine.effect", Kind::failed, {}, true};
        case EngineError::reentrant_mutation: return {"hanim.engine.reentrant-mutation", Kind::refused};
        case EngineError::program: return {"hanim.engine.program", Kind::failed, {}, true};
        case EngineError::ambiguous_frame: return {"hanim.engine.ambiguous-frame", Kind::invalid_argument};
        case EngineError::unsupported_operation: return {"hanim.engine.unsupported-operation", Kind::refused};
    }
    return {"hanim.engine.?", Kind::internal};
}
// What failed beneath an engine's error (an object, an animation, an effect,
// a program) is a cause.
using EngineFailure = mlibs::error::Error<EngineError>;
using PlayOptions = SegmentOptions;
// The engine's budget (core design §10.2): its objects' and effects' limits
// embedded; the totals bound the sum over every playback it holds (each
// program is bounded by its own ProgramLimits besides).
struct EngineLimits {
    ObjectLimits objects;
    std::size_t max_playbacks = 1024;
    EffectLimits effects;
    std::size_t max_total_parts = 65536;
    std::size_t max_total_cubics = 1048576;
    std::size_t max_total_effect_parameters = 65536;
    std::size_t max_total_route_segments = 65536;
};
struct PlaybackInfo {
    PlaybackRef ref;
    ObjectRef object, target;
    std::uint64_t target_revision = 0, plan_revision = 0;
    PlaybackState state = PlaybackState::paused;
    seconds position{0}, duration{0};
    double rate = 1, progress = 0;
    // Multi-track programs have no single eased object progress.
    std::optional<double> sample_progress;
    std::size_t track_count = 0, segment_count = 0, scope_count = 0;
};
// All spans/pointers borrow the Engine until its next mutating operation.
// Queries do not resample. Copy immutable plans to keep their preparation;
// this does not extend the lifetime of these mutable frame buffers.
struct EngineFrame {
    PlaybackInfo info;
    FrameView geometry;
    const PreparedObjectMorph* animation = nullptr;
    const PreparedEffects* effects = nullptr;
    std::span<const float> effect_values;
    std::size_t track_index = 0, segment_index = 0;
};
struct EngineProgramFrame { PlaybackInfo info; ProgramFrameView scene; };

// ── Interactive mode (L5) ───────────────────────────────────────────────
// A channel the host drives as input arrives — a hover's opacity, a dragged
// card's position — named by the host's key and a typed channel. Each lane is
// one of the engine's interactive lanes (hanim.core's Set: parallel arrays,
// evaluated without allocating); a retarget goes on from where the channel is
// and, for a spring, how fast it moves. Time is the engine's: advance() moves
// it for playbacks and driven channels alike.
//
//     auto opacity = engine.drive(node_key, ch::define<double>("ui.opacity", 0));
//     opacity->spring_to(1.0, springs::snappy);
//     engine.advance(1/60.0s); double now = opacity->value();
using DriveKey = std::uint64_t;
struct DriveTag;
using DriveRef = mlibs::memory::Handle<DriveTag>;
template <Animatable T> class Drive;

// One owning thread or external synchronization. No event loop, window,
// renderer, UI, transport or business setter. advance() is the batch boundary.
class Engine {
public:
    Engine() noexcept;
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    [[nodiscard]] static std::expected<Engine, EngineFailure> make(EngineLimits limits = {});
    [[nodiscard]] std::uint64_t owner() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::expected<ObjectRef, ObjectError> create_object(const ObjectDescription&);
    [[nodiscard]] std::expected<std::uint64_t, ObjectError> update_object(ObjectRef, const ObjectDescription&);
    [[nodiscard]] std::expected<void, ObjectError> destroy_object(ObjectRef);
    [[nodiscard]] std::expected<Snapshot, ObjectError> capture_object(ObjectRef) const noexcept;

    // Animation-first authoring uses the same Program compiler/player as the
    // explicit snapshot API. Preparation captures all live inputs before any
    // matching and rejects reentrant writes; it does not publish or start time.
    [[nodiscard]] std::expected<PreparedProgram, EngineFailure> prepare(const Program&, ProgramLimits limits = {});
    [[nodiscard]] std::expected<PlaybackRef, EngineFailure> play(const Program&, ProgramLimits limits = {});

    [[nodiscard]] std::expected<PlaybackRef, EngineFailure> play(
        ObjectRef object, anim::MorphTo animation, std::span<const EffectSpec> effects = {}, PlayOptions options = {});
    [[nodiscard]] std::expected<PlaybackRef, EngineFailure> play(
        ObjectRef object, anim::MorphTo animation, const EffectSpec& effect, PlayOptions options = {});
    // External hosts can publish endpoints through the same player without
    // moving their registry into the Engine or invoking their setters per frame.
    [[nodiscard]] std::expected<PlaybackRef, EngineFailure> play(
        Snapshot from, Snapshot to, std::span<const EffectSpec> effects = {}, PlayOptions options = {}, MorphOptions morph = {});
    // Preparation is explicit and immutable. All tracks use the same clock,
    // lifecycle and batch publication as the single-object convenience calls.
    [[nodiscard]] std::expected<PlaybackRef, EngineFailure> play(PreparedProgram program);
    [[nodiscard]] std::expected<void, EngineFailure> retarget(
        PlaybackRef playback, Snapshot to, PlayOptions options = {}, MorphOptions morph = {});
    [[nodiscard]] std::expected<void, EngineFailure> retarget(
        PlaybackRef playback, anim::MorphTo animation, PlayOptions options = {});
    [[nodiscard]] std::expected<void, EngineFailure> retarget(
        PlaybackRef playback, const PropertyTargets& values, PlayOptions options = {}, MorphOptions morph = {});
    template<class Targets> requires std::same_as<std::remove_cvref_t<Targets>, PropertySet>
    [[nodiscard]] std::expected<void, EngineFailure> retarget(
        PlaybackRef playback, Targets&& values, PlayOptions options = {}, MorphOptions morph = {}) {
        return retarget_properties(playback, values, options, morph);
    }
    // Retarget preserves effect order/targets, starting them at current values.
    // A different material program requires the renderer's explicit rebind.
    [[nodiscard]] std::expected<void, EngineFailure> pause(PlaybackRef);
    [[nodiscard]] std::expected<void, EngineFailure> resume(PlaybackRef);
    [[nodiscard]] std::expected<void, EngineFailure> reverse(PlaybackRef);
    [[nodiscard]] std::expected<void, EngineFailure> rate(PlaybackRef, double rate);
    [[nodiscard]] std::expected<void, EngineFailure> seek(PlaybackRef, seconds position);
    [[nodiscard]] std::expected<void, EngineFailure> seek_progress(PlaybackRef, double progress);
    [[nodiscard]] std::expected<void, EngineFailure> cancel(PlaybackRef, CancelMode = CancelMode::hold);
    [[nodiscard]] std::expected<void, EngineFailure> release(PlaybackRef);
    [[nodiscard]] std::expected<void, EngineFailure> advance(seconds elapsed);
    [[nodiscard]] std::expected<PlaybackInfo, EngineFailure> inspect(PlaybackRef) const noexcept;
    // O(1) immutable lease for tools/Agents, including convenience play().
    // Retaining it preserves author hierarchy and channel metadata after the
    // playback is released; it does not retain mutable frame buffers.
    [[nodiscard]] std::expected<PreparedProgram, EngineFailure> plan(PlaybackRef) const noexcept;
    [[nodiscard]] std::expected<EngineFrame, EngineFailure> frame(PlaybackRef) const noexcept;
    [[nodiscard]] std::expected<EngineFrame, EngineFailure> frame(PlaybackRef, std::size_t track) const noexcept;
    [[nodiscard]] std::expected<EngineProgramFrame, EngineFailure> program_frame(PlaybackRef) const noexcept;
    [[nodiscard]] std::expected<std::size_t, EngineFailure> list(std::span<PlaybackInfo> output) const noexcept;

    // Interactive mode: the channel `channel` of `key`, at its rest the first
    // time; the same channel after. One key's channel has one lane count.
    template <Animatable T>
    [[nodiscard]] std::expected<Drive<T>, EngineFailure> drive(DriveKey key, const ch::Channel<T>& channel) {
        std::array<double, ch::max_lanes> rest{};
        std::ranges::copy(lanes<T>::to(channel.rest), rest.begin());
        auto ref = drive_lanes(key, channel.id, std::span<const double>(rest).first(channel.id.lanes));
        if (!ref) return std::unexpected(ref.error());
        return Drive<T>(this, *ref);
    }
    // The engine's time (the sum of advance()'s steps).
    [[nodiscard]] seconds now() const noexcept;
    // Driven channels still moving at now(): whether a host needs another frame.
    [[nodiscard]] std::size_t moving() const noexcept;
    // Every driven lane's value at now(), out[lane] (Drive::lane), for a host
    // reading them all at once; returns how many lanes are still moving.
    [[nodiscard]] std::expected<std::size_t, EngineFailure> evaluate_driven(std::span<double> out) const noexcept;
    [[nodiscard]] std::size_t driven_lane_capacity() const noexcept;
    // The user asked for reduced motion: a driven channel shows its target.
    void reduce_motion(bool on) noexcept;

private:
    template <Animatable T> friend class Drive;
    [[nodiscard]] std::expected<DriveRef, EngineFailure> drive_lanes(DriveKey, ch::Id, std::span<const double> rest);
    enum class DriveVerb : std::uint8_t { spring, tween, jump };
    [[nodiscard]] std::expected<void, EngineFailure> drive_to(DriveRef, DriveVerb, std::span<const double> target, SpringParams, seconds, Ease);
    [[nodiscard]] std::expected<void, EngineFailure> drive_read(DriveRef, std::span<double> value, std::span<double> velocity) const noexcept;
    [[nodiscard]] std::expected<bool, EngineFailure> drive_settled(DriveRef) const noexcept;
    [[nodiscard]] std::expected<std::uint32_t, EngineFailure> drive_lane(DriveRef, std::size_t lane) const noexcept;
    [[nodiscard]] std::expected<void, EngineFailure> drive_release(DriveRef);
    [[nodiscard]] std::expected<void, EngineFailure> retarget_properties(
        PlaybackRef, std::span<const PropertyTargets>, PlayOptions, MorphOptions);
    std::unique_ptr<detail::EngineData> data_;
    bool writing_ = false;
};

// A driven channel, typed. It names the engine it came from: moving the engine
// leaves it naming the old place (take it again with drive()).
template <Animatable T>
class Drive {
public:
    static constexpr std::size_t lane_count = static_cast<std::size_t>(lanes<T>::count);
    // Toward `target` as a spring, from where the channel is and how fast it moves.
    std::expected<void, EngineFailure> spring_to(const T& target, SpringParams params = springs::standard) {
        return engine_->drive_to(ref_, Engine::DriveVerb::spring, lanes_of(target), params, {}, {});
    }
    // Toward `target` over `duration` along `easing`, from where the channel is.
    std::expected<void, EngineFailure> tween_to(const T& target, seconds duration, Ease easing = ease::out_cubic) {
        return engine_->drive_to(ref_, Engine::DriveVerb::tween, lanes_of(target), {}, duration, easing);
    }
    // There, now, at rest.
    std::expected<void, EngineFailure> jump(const T& value) {
        return engine_->drive_to(ref_, Engine::DriveVerb::jump, lanes_of(value), {}, {}, {});
    }
    // Where it is at the engine's now (its target under reduced motion).
    [[nodiscard]] std::expected<T, EngineFailure> value() const noexcept {
        std::array<double, lanes<T>::count> v{}, d{};
        if (auto r = engine_->drive_read(ref_, v, d); !r) return std::unexpected(r.error());
        return lanes<T>::from(v);
    }
    // How fast each lane moves (units a second).
    [[nodiscard]] std::expected<std::array<double, lanes<T>::count>, EngineFailure> velocity() const noexcept {
        std::array<double, lanes<T>::count> v{}, d{};
        if (auto r = engine_->drive_read(ref_, v, d); !r) return std::unexpected(r.error());
        return d;
    }
    [[nodiscard]] std::expected<bool, EngineFailure> settled() const noexcept { return engine_->drive_settled(ref_); }
    // Lane i's place in evaluate_driven()'s output.
    [[nodiscard]] std::expected<std::uint32_t, EngineFailure> lane(std::size_t i) const noexcept { return engine_->drive_lane(ref_, i); }
    std::expected<void, EngineFailure> release() { return engine_->drive_release(ref_); }
    [[nodiscard]] DriveRef ref() const noexcept { return ref_; }

private:
    friend class Engine;
    Drive(Engine* engine, DriveRef ref) noexcept : engine_(engine), ref_(ref) {}
    static std::array<double, lanes<T>::count> lanes_of(const T& v) { return lanes<T>::to(v); }
    Engine*  engine_;
    DriveRef ref_;
};
} // namespace hanim
