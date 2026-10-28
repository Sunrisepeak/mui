module mgame.sim;

import std;

namespace mgame {

Clock::Clock(double steps_per_second, std::int32_t catch_up) noexcept
    : step_(1.0 / std::max(1.0, steps_per_second)), catch_up_(std::max(1, catch_up)) {}

void Clock::reset(double now) noexcept {
    last_ = now;
    owed_ = 0;
}

std::int32_t Clock::advance(double now) noexcept {
    if (last_ < 0) {
        reset(now);
        return 0;
    }
    owed_ += std::max(0.0, now - last_) * scale / step_;
    last_ = now;
    auto due = static_cast<std::int32_t>(std::floor(owed_ + 1e-9));
    if (due > catch_up_) {  // a stall: what is past the limit is not owed
        owed_ = catch_up_;
        due = catch_up_;
    }
    owed_ -= due;
    return due;
}

}  // namespace mgame
