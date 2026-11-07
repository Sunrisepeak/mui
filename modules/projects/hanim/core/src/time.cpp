module hanim.core;

import std;

namespace hanim {

Clock Clock::interactive() {
    Clock c;
    c.kind_ = ClockKind::interactive;
    return c;
}

Clock Clock::virtual_time(seconds start) {
    Clock c;
    c.kind_ = ClockKind::virtual_time;
    c.now_  = start;
    return c;
}

Clock Clock::render(mlibs::time::Rate rate) {
    Clock c;
    c.kind_           = ClockKind::render;
    c.rate_of_frames_ = rate;
    c.now_            = rate.time_of(0);
    return c;
}

void Clock::begin_frame(std::chrono::steady_clock::time_point wall) noexcept {
    if (kind_ != ClockKind::interactive) return;
    if (!started_) {
        started_     = true;
        anchor_wall_ = wall;
        anchor_time_ = now_;
        return;
    }
    if (paused_) return;
    auto elapsed = std::chrono::duration_cast<seconds>(wall - anchor_wall_);
    if (elapsed.count() < 0) return;  // a host that reports an earlier frame time keeps the latch
    now_ = anchor_time_ + elapsed * rate_;
}

void Clock::set_rate(double rate) noexcept {
    if (kind_ != ClockKind::interactive || !(rate >= 0)) return;
    // Re-anchor at the latched time so the change does not jump.
    anchor_time_ = now_;
    anchor_wall_ = std::chrono::steady_clock::now();
    rate_        = rate;
}

void Clock::pause() noexcept {
    if (kind_ != ClockKind::interactive) return;
    paused_ = true;
}

void Clock::resume() noexcept {
    if (kind_ != ClockKind::interactive || !paused_) return;
    paused_      = false;
    anchor_time_ = now_;
    anchor_wall_ = std::chrono::steady_clock::now();
}

void Clock::advance(seconds dt) noexcept {
    if (kind_ == ClockKind::virtual_time) now_ += dt;
}

void Clock::set(seconds t) noexcept {
    if (kind_ == ClockKind::virtual_time) now_ = t;
}

void Clock::seek(std::int64_t frame) noexcept {
    if (kind_ != ClockKind::render) return;
    frame_ = frame;
    now_   = rate_of_frames_.time_of(frame);
}

seconds Clock::subframe(int i, int n, double shutter) const noexcept {
    if (n <= 1) return now_;
    double open  = rate_of_frames_.frame_duration().count() * shutter;
    double slice = open / static_cast<double>(n);
    return now_ + seconds(slice * (static_cast<double>(i) + 0.5));
}

}  // namespace hanim
