// Performance tiers in the runtime (D-94; performance tiers §4, §5): the
// device's tier as its host says it, and the frame-budget governor. When
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

namespace {

constexpr std::size_t weighed = 30;   // frames weighed before lowering
constexpr std::size_t patience = 300; // frames of room before raising

}  // namespace

void Runtime::Impl::govern(double ms) {
    costs[costs_n++ % costs.size()] = static_cast<float>(ms);
    ++cost_frames;
    if (!governing) return;
    const double budget = 1000.0 / (display_hz > 0 ? display_hz : 60.0);
    const std::size_t have = std::min(costs_n, costs.size());
    auto last = [&](std::size_t n, auto&& pred) {
        std::size_t hits = 0;
        for (std::size_t i = 0; i < std::min(n, have); ++i) hits += pred(costs[(costs_n - 1 - i) % costs.size()]);
        return hits;
    };
    Adaptation next = adaptation;
    // Two frames in three over the budget, for a weighed since the last change: one step down.
    if (cost_frames >= weighed && last(weighed, [&](float c) { return c > budget; }) * 3 >= weighed * 2) {
        if (next.motion != MotionLevel::off) {
            next.motion = static_cast<MotionLevel>(static_cast<int>(next.motion) + 1);
            next.reason = std::format("frames over their {:.1f} ms: motion {}", budget, next.motion == MotionLevel::off ? "off" : "reduced");
        } else if (next.effects > mlibs::pipeline::Tier::t0) {
            next.effects = static_cast<mlibs::pipeline::Tier>(static_cast<int>(next.effects) - 1);
            next.reason = std::format("frames over their {:.1f} ms: effects as on {}", budget, mlibs::pipeline::name(next.effects));
        }
    }
    // Every frame of a long while under half the budget: one step back up.
    else if (cost_frames >= patience && have >= patience && last(patience, [&](float c) { return c < budget / 2; }) == patience) {
        if (next.effects < mlibs::pipeline::Tier::t4) {
            next.effects = static_cast<mlibs::pipeline::Tier>(static_cast<int>(next.effects) + 1);
            next.reason = std::format("room in the frame: effects as on {}", mlibs::pipeline::name(next.effects));
        } else if (next.motion != MotionLevel::full) {
            next.motion = static_cast<MotionLevel>(static_cast<int>(next.motion) - 1);
            next.reason = std::format("room in the frame: motion {}", next.motion == MotionLevel::full ? "full" : "reduced");
        }
    }
    if (next == adaptation) return;
    constexpr Log adapt_log{"adapt"};
    adapt_log.info("{}", next.reason);
    adaptation = std::move(next);
    cost_frames = 0;
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
    impl_->cost_frames = 0;
}

void Runtime::report_frame_cost(double ms) { impl_->govern(ms); }

}  // namespace omui
