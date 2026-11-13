// The frame-budget governor is general: ladders by name and steps, lowered in their order while frames miss
// their budget and raised in the reverse order when there is room; its judgement is a function that can be
// another; a ladder is put where a recording or a pin says; a frame allocates nothing.
import std;
import mlibs.pipeline;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace pl = mlibs::pipeline;

namespace {
// Frames of one cost until something changes, at most `most`: the change, and how many frames it took.
std::pair<std::optional<pl::Governor::Change>, int> until_change(pl::Governor& governor, double cost_ms, double budget_ms, int most) {
    for (int frame = 1; frame <= most; ++frame)
        if (auto change = governor.frame(cost_ms, budget_ms)) return {change, frame};
    return {std::nullopt, most};
}
}  // namespace

UTest cases{"pipeline.govern", [] {
    "frames over their budget lower the ladders in their order, a step at a time; then nothing is left to lower"_test = [] {
        pl::Governor governor({{"detail", 2}, {"light", 1}});
        const double budget = 1000.0 / 60.0;
        expect(!until_change(governor, 5.0, budget, 29).first) << "under the budget: nothing";
        const std::array<std::pair<std::size_t, std::uint8_t>, 3> expected{{{0, 1}, {0, 2}, {1, 1}}};
        for (const auto& [ladder, lowered] : expected) {
            const auto [change, frames] = until_change(governor, 30.0, budget, 200);
            expect(fatal(change.has_value()));
            expect(change->ladder == ladder && change->lowered == lowered && change->down) << std::format("ladder {} at {}", change->ladder, change->lowered);
            expect(frames <= 30) << std::format("{} frames", frames);
        }
        expect(!until_change(governor, 30.0, budget, 200).first) << "every ladder at its foot";
        expect(governor.ladders()[0].lowered == 2 && governor.ladders()[1].lowered == 1);
    };

    "a change is weighed anew: the frames before it do not count against the next"_test = [] {
        pl::Governor governor({{"detail", 3}});
        const double budget = 10.0;
        const auto [first, frames] = until_change(governor, 20.0, budget, 100);
        expect(fatal(first.has_value()));
        expect(eq(frames, 30));
        // 29 more frames over the budget: not yet (30 since the change are weighed).
        expect(!until_change(governor, 20.0, budget, 29).first);
        expect(governor.frame(20.0, budget).has_value());
    };

    "room for long enough raises them in the reverse order; a frame without room starts the wait again"_test = [] {
        pl::Governor governor({{"detail", 2, 1}, {"light", 1, 1}});
        const double budget = 16.0;
        expect(!until_change(governor, 4.0, budget, 299).first) << "299 frames of room: not yet";
        const auto raised = governor.frame(4.0, budget);
        expect(fatal(raised.has_value()));
        expect(raised->ladder == 1 && raised->lowered == 0 && !raised->down) << "the last lowered is the first raised";
        // Room again, but one frame at 9 ms (over half the budget) among them: 300 more are needed after it.
        expect(!until_change(governor, 4.0, budget, 200).first);
        expect(!governor.frame(9.0, budget));
        expect(!until_change(governor, 4.0, budget, 299).first);
        const auto next = governor.frame(4.0, budget);
        expect(fatal(next.has_value()));
        expect(next->ladder == 0 && next->lowered == 0);
        expect(!until_change(governor, 4.0, budget, 400).first) << "nothing left to raise";
    };

    "between over and room nothing changes"_test = [] {
        pl::Governor governor({{"detail", 2, 1}});
        expect(!until_change(governor, 12.0, 16.0, 1000).first) << "under the budget, over half of it";
    };

    "a ladder is put where a recording or a pin says; observed alone, nothing is decided"_test = [] {
        pl::Governor governor({{"detail", 2}, {"light", 4}});
        governor.set(1, 3);
        governor.set(0, 9);
        expect(governor.ladders()[1].lowered == 3);
        expect(governor.ladders()[0].lowered == 2) << "no further than its foot";
        governor.set(7, 1);  // no such ladder: nothing
        for (int frame = 0; frame < 100; ++frame) governor.observe(40.0);
        expect(governor.ladders()[1].lowered == 3) << "observed, not decided: a pin holds";
        governor.settle();
        expect(eq(governor.since_change(), std::uint64_t{0}));
    };

    "the judgement can be another: here, lower on any frame over twice the budget"_test = [] {
        const pl::Policy strict = [](std::span<const float> recent, double budget_ms, std::uint64_t, const pl::GovernorOptions&) noexcept {
            return !recent.empty() && recent.back() > 2 * budget_ms ? pl::Verdict::lower : pl::Verdict::hold;
        };
        pl::Governor governor({{"detail", 2}}, {}, strict);
        expect(!governor.frame(19.0, 10.0));
        const auto change = governor.frame(21.0, 10.0);
        expect(fatal(change.has_value()));
        expect(change->lowered == 1 && change->down);
    };

    "the default judgement by its numbers; other numbers are the options'"_test = [] {
        const pl::GovernorOptions options{.weighed = 6, .patience = 12};
        const std::array<float, 6> mostly{20, 20, 5, 20, 20, 5};  // four of six over 10 ms
        expect(pl::weigh(mostly, 10.0, 6, options) == pl::Verdict::lower);
        expect(pl::weigh(mostly, 10.0, 5, options) == pl::Verdict::hold) << "too soon after a change";
        const std::array<float, 6> half{20, 5, 5, 20, 5, 20};     // three of six
        expect(pl::weigh(half, 10.0, 6, options) == pl::Verdict::hold);
        std::array<float, 12> room;
        room.fill(4.0f);
        expect(pl::weigh(room, 10.0, 12, options) == pl::Verdict::raise);
        room[3] = 6.0f;
        expect(pl::weigh(room, 10.0, 12, options) == pl::Verdict::hold);
        pl::Governor governor({{"detail", 1}}, options);
        expect(eq(until_change(governor, 20.0, 10.0, 100).second, 6));
    };
}};
