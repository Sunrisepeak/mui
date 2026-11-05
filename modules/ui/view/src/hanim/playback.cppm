export module omui.hanim:playback;
import std;
export import :binding;

export namespace omui::animation {
enum class PlaybackError : std::uint8_t { binding, engine, stale, no_program, occupied, invalid_time };
struct PlaybackFailure {
    PlaybackError code;
    std::optional<BindingFailure> binding;
    std::optional<hanim::EngineFailure> engine;
};
enum class Suspension : std::uint8_t { none, hidden, background, interaction, motion_preference };
struct PlaybackGate {
    bool visible = true;
    bool foreground = true;
    bool interacting = false;
};
struct PlaybackOptions {
    bool alternate = false; // repeat forwards/backwards while live
    double rate = 1;
};
struct PlaybackStatus {
    std::optional<hanim::PlaybackInfo> native;
    Suspension suspended = Suspension::none;
    bool requested = false;
    bool demanding_frames = false;
    bool clock_paused = false;
};

// An owning UI host adapter for one selected HObject. Hanim's Engine remains
// the only player; hanim::TimeMap maps the caller's absolute clock, including
// repeats and backward seeks. No event loop, timer, window or script is owned.
// Call update() after input/regions and before bounds/paint. Controls publish
// immediately; the Runtime's next paint commits pixels and input geometry.
class Playback {
public:
    Playback() noexcept = default;
    Playback(Playback&&) noexcept = default;
    Playback& operator=(Playback&&) noexcept = default;
    Playback(const Playback&)=delete;
    Playback& operator=(const Playback&)=delete;
    [[nodiscard]] static std::expected<Playback,PlaybackFailure>
        attach(Runtime&,std::span<const Target>);
    [[nodiscard]] hanim::ObjectRef object() const noexcept {return binding_.object();}
    [[nodiscard]] bool attached() const noexcept {return binding_.attached();}
    [[nodiscard]] std::span<const Target> targets() const & noexcept {return binding_.targets();}
    std::span<const Target> targets() const && = delete;
    // A second play is refused. Stop explicitly before installing a new
    // recipe; editing business state is never part of this adapter.
    [[nodiscard]] std::expected<void,PlaybackFailure> play(const hanim::Program&,PlaybackOptions = {});
    [[nodiscard]] std::expected<void,PlaybackFailure> pause();
    [[nodiscard]] std::expected<void,PlaybackFailure> resume();
    [[nodiscard]] std::expected<void,PlaybackFailure> reverse();
    [[nodiscard]] std::expected<void,PlaybackFailure> rate(double);
    // Explicit inspection sampling pauses automatic transport and remains
    // available when automatic motion is suppressed by host preferences.
    [[nodiscard]] std::expected<void,PlaybackFailure> seek(hanim::seconds);
    [[nodiscard]] std::expected<void,PlaybackFailure> seek_progress(double);
    [[nodiscard]] std::expected<void,PlaybackFailure> stop(hanim::CancelMode = hanim::CancelMode::restore);
    [[nodiscard]] std::expected<void,PlaybackFailure> update(PlaybackGate = {});
    [[nodiscard]] PlaybackStatus status() const noexcept;
    [[nodiscard]] std::expected<hanim::PreparedProgram,PlaybackFailure> plan() const;
private:
    hanim::Engine engine_;
    Binding binding_;
    FrameDemandLease demand_;
    Runtime* runtime_ = nullptr; // dereference only while binding_.attached()
    std::optional<hanim::PlaybackRef> playback_;
    PlaybackOptions options_;
    hanim::seconds anchor_time_{0},anchor_position_{0};
    double direction_ = 1;
    Suspension suspended_ = Suspension::none;
    bool requested_ = false, demanding_ = false;
    [[nodiscard]] std::expected<void,PlaybackFailure> ready() const;
    [[nodiscard]] std::expected<void,PlaybackFailure> publish();
    [[nodiscard]] std::expected<void,PlaybackFailure> sample(hanim::seconds,double);
    void reanchor();
    void demand(bool);
};
}
