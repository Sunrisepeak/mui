// The player owns time and buffers; immutable library plans own correspondence.
export module hanim.engine;
import std;
export import hanim.library;
export import hanim.effect;

namespace hanim::detail { struct EngineData; }
export namespace hanim {

struct PlaybackRef {
    std::uint64_t owner = 0;
    std::uint32_t slot = invalid_object_slot;
    std::uint64_t generation = 0;
    [[nodiscard]] explicit operator bool() const noexcept { return owner && slot != invalid_object_slot && generation; }
    friend bool operator==(const PlaybackRef& a, const PlaybackRef& b) noexcept {
        return a.owner == b.owner && a.slot == b.slot && a.generation == b.generation;
    }
};
enum class PlaybackState : std::uint8_t { playing, paused, finished, cancelled };
enum class CancelMode : std::uint8_t { hold, restore };
enum class EngineError : std::uint8_t {
    uninitialized, invalid_limits, invalid_handle, foreign_engine, stale_handle,
    invalid_time, invalid_ease, invalid_argument, capacity, binding_conflict, cancelled,
    version_exhausted, object, animation, effect, reentrant_mutation
};
struct EngineFailure {
    EngineError code;
    std::optional<ObjectError> object;
    std::optional<MorphFailure> animation;
    std::optional<EffectError> effect;
};
[[nodiscard]] std::string_view engine_error_name(EngineError error) noexcept;
struct PlayOptions {
    seconds duration{1};
    Ease easing = ease::linear;
};
struct EngineLimits {
    ObjectLimits objects;
    std::size_t max_playbacks = 1024;
    EffectLimits effects;
    std::size_t max_total_parts = 65536;
    std::size_t max_total_cubics = 1048576;
    std::size_t max_total_effect_parameters = 65536;
};
struct PlaybackInfo {
    PlaybackRef ref;
    ObjectRef object, target;
    std::uint64_t target_revision = 0, plan_revision = 0;
    PlaybackState state = PlaybackState::paused;
    seconds position{0}, duration{0};
    double rate = 1, progress = 0, sample_progress = 0;
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
};

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

    [[nodiscard]] std::expected<PlaybackRef, EngineFailure> play(
        ObjectRef object, anim::MorphTo animation, std::span<const EffectSpec> effects = {}, PlayOptions options = {});
    [[nodiscard]] std::expected<PlaybackRef, EngineFailure> play(
        ObjectRef object, anim::MorphTo animation, const EffectSpec& effect, PlayOptions options = {});
    // External hosts can publish endpoints through the same player without
    // moving their registry into the Engine or invoking their setters per frame.
    [[nodiscard]] std::expected<PlaybackRef, EngineFailure> play(
        Snapshot from, Snapshot to, std::span<const EffectSpec> effects = {}, PlayOptions options = {}, MorphOptions morph = {});
    [[nodiscard]] std::expected<void, EngineFailure> retarget(
        PlaybackRef playback, Snapshot to, PlayOptions options = {}, MorphOptions morph = {});
    [[nodiscard]] std::expected<void, EngineFailure> retarget(
        PlaybackRef playback, anim::MorphTo animation, PlayOptions options = {});
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
    [[nodiscard]] std::expected<EngineFrame, EngineFailure> frame(PlaybackRef) const noexcept;
    [[nodiscard]] std::expected<std::size_t, EngineFailure> list(std::span<PlaybackInfo> output) const noexcept;
private:
    std::unique_ptr<detail::EngineData> data_;
    bool writing_ = false;
};
} // namespace hanim
