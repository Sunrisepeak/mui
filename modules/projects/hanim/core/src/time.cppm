// hanim.core:time — what "now" is, for an interactive program, a test, and a
// video render.
//
// Every animation in openmui is a function of a time value; the clock decides
// which time value a frame sees. Three clocks, one interface:
//
//   interactive  real time, latched once per frame so every animation in one
//                frame sees the same instant; can run slower, faster, or pause
//   virtual      a test advances it by hand; nothing depends on the wall clock
//   render       a video frame number over a frame rate: frame n is at n/fps,
//                computed exactly, so frame 1234 is the same instant whether
//                it is rendered first, last, or alone
export module hanim.core:time;

import std;
export import mlibs.time;

export namespace hanim {

// Time and frame rates are mlibs.time's; `seconds` is hanim's word for its
// continuous time.
using seconds = mlibs::time::seconds;

enum class ClockKind : std::uint8_t { interactive, virtual_time, render };

class Clock {
public:
    // Real time, starting at zero when created.
    [[nodiscard]] static Clock interactive();
    // Time that moves only when the owner says so.
    [[nodiscard]] static Clock virtual_time(seconds start = seconds{0});
    // Frame numbers of a video at `rate`, starting at frame 0.
    [[nodiscard]] static Clock render(mlibs::time::Rate rate);

    [[nodiscard]] ClockKind kind() const noexcept { return kind_; }

    // The time of the current frame. It does not move between two
    // `begin_frame` calls, whatever the wall clock does.
    [[nodiscard]] seconds now() const noexcept { return now_; }

    // Interactive: latch the time of a new frame (normally the presentation
    // time the host predicts); a no-op on the other clocks.
    void begin_frame(std::chrono::steady_clock::time_point wall) noexcept;
    void begin_frame() noexcept { begin_frame(std::chrono::steady_clock::now()); }

    // Interactive: speed (slow motion < 1, fast forward > 1) and pause. Time
    // stays continuous across a change: only its rate of change changes.
    void set_rate(double rate) noexcept;
    [[nodiscard]] double rate() const noexcept { return rate_; }
    void pause() noexcept;
    void resume() noexcept;
    [[nodiscard]] bool paused() const noexcept { return paused_; }

    // Virtual: move time.
    void advance(seconds dt) noexcept;
    void set(seconds t) noexcept;

    // Render: go to a frame. Any frame, in any order.
    void seek(std::int64_t frame) noexcept;
    [[nodiscard]] std::int64_t frame() const noexcept { return frame_; }
    [[nodiscard]] mlibs::time::Rate frame_rate() const noexcept { return rate_of_frames_; }

    // Render: the instant of sub-sample `i` of `n` within the current frame,
    // for motion blur. `shutter` is the open fraction of the frame (0.5 is a
    // 180° shutter); samples are centred in equal slices of the open interval.
    [[nodiscard]] seconds subframe(int i, int n, double shutter = 0.5) const noexcept;

private:
    ClockKind kind_ = ClockKind::virtual_time;
    seconds   now_{0};

    // interactive
    std::chrono::steady_clock::time_point anchor_wall_{};
    seconds anchor_time_{0};
    double  rate_    = 1.0;
    bool    paused_  = false;
    bool    started_ = false;

    // render
    std::int64_t frame_ = 0;
    mlibs::time::Rate    rate_of_frames_{};
};

}  // namespace hanim
