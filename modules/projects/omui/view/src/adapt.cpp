// Performance tiers in the runtime (D-94; performance tiers §4, §5): the
// device's tier as its host says it, and the frame-budget governor — the
// general one (mlibs.pipeline.govern, D-178), with the UI's two ladders. When
// frames keep missing their budget the governor lowers motion (full, then
// reduced, then off), then effects a tier at a time; when there is room for
// long enough it raises them again, never above the device's tier nor the
// person's own setting. What it decides is an input: Runtime::adaptation()
// is what a session records, and set_adaptation what a replay applies.
module omui.view;

import std;
import omui.core;
import mlibs.pipeline;
import :impl;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
}

namespace omui {

void Runtime::Impl::govern(double ms) {
    governor.observe(ms);
    if (!governing) return;
    const double budget = 1000.0 / (display_hz > 0 ? display_hz : 60.0);
    // Where the ladders stand is the adaptation's to say (a replay, a tool may have set it since).
    governor.set(0, static_cast<std::uint8_t>(adaptation.motion));
    governor.set(1, static_cast<std::uint8_t>(4 - static_cast<int>(adaptation.effects)));
    const auto change = governor.decide(budget);
    if (!change) return;
    Adaptation next = adaptation;
    if (change->ladder == 0) {
        next.motion = static_cast<MotionLevel>(change->lowered);
        next.reason = change->down ? std::format("frames over their {:.1f} ms: motion {}", budget, next.motion == MotionLevel::off ? "off" : "reduced")
                                   : std::format("room in the frame: motion {}", next.motion == MotionLevel::full ? "full" : "reduced");
    } else {
        next.effects = static_cast<mlibs::pipeline::Tier>(4 - static_cast<int>(change->lowered));
        next.reason = change->down ? std::format("frames over their {:.1f} ms: effects as on {}", budget, mlibs::pipeline::name(next.effects))
                                   : std::format("room in the frame: effects as on {}", mlibs::pipeline::name(next.effects));
    }
    constexpr Log adapt_log{"adapt"};
    adapt_log.info("{}", next.reason);
    adaptation = std::move(next);
    ++motion_gen;
    touch(dirty_paint);
}

void Runtime::set_device_tier(mlibs::pipeline::Tier tier, std::string reason) {
    Impl& d = *impl_;
    if (d.adaptation.tier == tier) return;
    d.adaptation.tier = tier;
    d.adaptation.reason = std::move(reason);
    ++d.motion_gen;
    d.touch(dirty_paint);
}

const Adaptation& Runtime::adaptation() const noexcept { return impl_->adaptation; }

void Runtime::set_adaptation(const Adaptation& a) {
    Impl& d = *impl_;
    d.governing = false;
    if (d.adaptation == a) return;
    d.adaptation = a;
    ++d.motion_gen;
    d.touch(dirty_paint);
}

void Runtime::set_governor(bool on) {
    impl_->governing = on;
    impl_->governor.settle();
}

void Runtime::report_frame_cost(double ms) { impl_->govern(ms); }

}  // namespace omui
