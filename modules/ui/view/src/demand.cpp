// Frame demand (D-70): merging what asks for frames, the interval it makes,
// and a component's lease on it.
module openmui.view;

import std;

namespace omui {

FrameDemand merge(const FrameDemand& a, const FrameDemand& b) {
    if (a.idle()) return b;
    if (b.idle()) return a;
    FrameDemand m;
    m.min_hz = std::max(a.min_hz, b.min_hz);
    m.max_hz = std::max(a.max_hz, b.max_hz);
    const double wa = a.wanted(), wb = b.wanted();
    m.preferred_hz = std::max(wa, wb);
    m.reason = wa >= wb ? a.reason : b.reason;
    return m;
}

double frame_interval(const FrameDemand& d, double display_hz) noexcept {
    if (d.idle()) return 0;
    const double top = display_hz > 0 ? display_hz : 60;
    const double rate = std::clamp(d.wanted(), std::min(d.min_hz, top), top);
    return rate > 0 ? 1.0 / rate : 0;
}

FrameDemandLease& FrameDemandLease::operator=(FrameDemandLease&& o) noexcept {
    if (this != &o) {
        release();
        services_ = std::exchange(o.services_, nullptr);
        token_ = std::exchange(o.token_, 0);
        life_ = std::move(o.life_);
    }
    return *this;
}

void FrameDemandLease::release() noexcept {
    if (services_ && token_ && !life_.expired()) services_->release_frames(token_);
    services_ = nullptr;
    token_ = 0;
}

}  // namespace omui
