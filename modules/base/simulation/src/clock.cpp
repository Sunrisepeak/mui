module openmui.simulation;
import std;

namespace omui::simulation {
Clock::Clock(double steps_per_second, std::int32_t catch_up) noexcept
    : step_(1.0 / (std::isfinite(steps_per_second) ? std::max(1.0, steps_per_second) : 1.0)),
      catch_up_(std::max(1, catch_up)) {}

void Clock::reset(double now) noexcept {
    if (!std::isfinite(now)) return;
    last_ = now; owed_ = 0; initialized_ = true;
}

std::int32_t Clock::advance(double now) noexcept {
    if (!std::isfinite(now)) return 0;
    if (!initialized_) { reset(now); return 0; }
    const double elapsed = std::max(0.0, now - last_);
    last_ = now;
    if (!std::isfinite(scale) || scale <= 0) return 0;
    owed_ += elapsed * scale / step_;
    // Bound in floating point before converting, including overflowing stalls.
    if (!std::isfinite(owed_) || owed_ >= catch_up_) { owed_ = 0; return catch_up_; }
    const auto due = static_cast<std::int32_t>(std::floor(owed_ + 1e-9));
    owed_ = std::max(0.0, owed_ - due);
    return due;
}
}
