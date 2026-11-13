// mlibs.pipeline.govern — the frame-budget governor (performance tiers §4.2, D-94; D-178): what a frame is made
// of is lowered a step at a time while frames keep missing their budget, and raised again when there is room
// for long enough. General: it knows ladders — each a name and how many steps down it has — and nothing of what
// a step means. Whoever owns a ladder says what its steps are (omui: motion full → reduced → off, then effects a
// tier at a time; a world's presentation: a state's recipe from its richest form to a baked one), reads how far
// down each is, and words the reason.
//
//     mlibs::pipeline::Governor governor({{"motion", 2}, {"effects", 4}});
//     if (auto change = governor.frame(cost_ms, 1000.0 / display_hz)) apply(*change);   // each frame
//
// Lowering takes the ladders in the order given (the first to its foot, then the next); raising takes them back
// in the reverse order. What it decides is an input to whoever draws: recorded with a session and set again by a
// replay (`set`), so a replay draws as the recording did. The judgement is a function (`Policy`) and can be
// another; `set` with the governor not asked (`observe` alone) pins a ladder.
//
// A frame allocates nothing and calls nothing virtual: a ring of the last frames' costs, and the policy's call.
export module mlibs.pipeline.govern;

import std;

export namespace mlibs::pipeline {

struct Ladder {
    std::string_view name;      // outlives the governor: a literal, or interned
    std::uint8_t steps = 0;     // how far down it can go
    std::uint8_t lowered = 0;   // how far down it is now (0: as rich as it gets)
};

enum class Verdict : std::int8_t { lower = -1, hold = 0, raise = 1 };

// What the default judgement weighs: `weighed` frames before lowering (two in three of them over the budget),
// `patience` frames of room before raising (every one under half the budget).
struct GovernorOptions {
    std::size_t weighed = 30;
    std::size_t patience = 300;
};

// The judgement: the last frames' costs (`recent`, the newest last; at most the ring's), the budget of one, and
// how many frames since the governor last changed anything.
using Policy = Verdict (*)(std::span<const float> recent, double budget_ms, std::uint64_t since_change, const GovernorOptions& options) noexcept;

// Performance tiers §4.2: two frames in three of the last `weighed` over the budget — lower; every frame of the
// last `patience` under half of it — raise.
[[nodiscard]] inline Verdict weigh(std::span<const float> recent, double budget_ms, std::uint64_t since_change, const GovernorOptions& options) noexcept {
    const auto last = [&](std::size_t count, auto&& counted) {
        std::size_t hits = 0;
        for (std::size_t index = 0; index < std::min(count, recent.size()); ++index) hits += counted(recent[recent.size() - 1 - index]);
        return hits;
    };
    if (since_change >= options.weighed && last(options.weighed, [&](float cost) { return cost > budget_ms; }) * 3 >= options.weighed * 2) return Verdict::lower;
    if (since_change >= options.patience && recent.size() >= options.patience && last(options.patience, [&](float cost) { return cost < budget_ms / 2; }) == options.patience)
        return Verdict::raise;
    return Verdict::hold;
}

class Governor {
public:
    static constexpr std::size_t most_ladders = 16, remembered = 300;
    // A ladder that changed: which, how far down it is now, and which way it went.
    struct Change {
        std::size_t ladder = 0;
        std::uint8_t lowered = 0;
        bool down = false;
    };

    Governor() = default;
    // The ladders in the order they are lowered (past `most_ladders`: left out).
    explicit Governor(std::initializer_list<Ladder> ladders, GovernorOptions options = {}, Policy policy = weigh) noexcept : options_(options), policy_(policy ? policy : weigh) {
        options_.patience = std::min(options_.patience, remembered);
        options_.weighed = std::min(options_.weighed, remembered);
        for (const Ladder& ladder : ladders)
            if (count_ < most_ladders) ladders_[count_++] = {ladder.name, ladder.steps, std::min(ladder.lowered, ladder.steps)};
    }

    [[nodiscard]] std::span<const Ladder> ladders() const noexcept { return {ladders_.data(), count_}; }
    [[nodiscard]] const GovernorOptions& options() const noexcept { return options_; }

    // A frame's cost, weighed from now on; nothing is decided.
    void observe(double cost_ms) noexcept {
        costs_[head_] = static_cast<float>(cost_ms);
        head_ = (head_ + 1) % remembered;
        held_ = std::min(held_ + 1, remembered);
        ++since_;
    }
    // The judgement on what was observed, against one frame's budget: a ladder a step down or up, or nothing.
    [[nodiscard]] std::optional<Change> decide(double budget_ms) noexcept {
        // The ring in the order it was filled, the newest last.
        std::array<float, remembered> recent;
        for (std::size_t index = 0; index < held_; ++index) recent[index] = costs_[(head_ + remembered - held_ + index) % remembered];
        const Verdict verdict = policy_({recent.data(), held_}, budget_ms, since_, options_);
        if (verdict == Verdict::lower) {
            for (std::size_t index = 0; index < count_; ++index)
                if (ladders_[index].lowered < ladders_[index].steps) return changed(index, static_cast<std::uint8_t>(ladders_[index].lowered + 1), true);
        } else if (verdict == Verdict::raise) {
            for (std::size_t index = count_; index-- > 0;)
                if (ladders_[index].lowered > 0) return changed(index, static_cast<std::uint8_t>(ladders_[index].lowered - 1), false);
        }
        return std::nullopt;
    }
    // Both: a frame as a host that is governed reports it.
    [[nodiscard]] std::optional<Change> frame(double cost_ms, double budget_ms) noexcept {
        observe(cost_ms);
        return decide(budget_ms);
    }
    // A ladder put where something else says — a recording, a person's pin, the owner's own change. The count of
    // frames since a change is not touched (`settle` starts it again).
    void set(std::size_t ladder, std::uint8_t lowered) noexcept {
        if (ladder < count_) ladders_[ladder].lowered = std::min(lowered, ladders_[ladder].steps);
    }
    // Weighing starts again from here: turned on, or after a change made from outside.
    void settle() noexcept { since_ = 0; }
    [[nodiscard]] std::uint64_t since_change() const noexcept { return since_; }

private:
    [[nodiscard]] Change changed(std::size_t ladder, std::uint8_t lowered, bool down) noexcept {
        ladders_[ladder].lowered = lowered;
        since_ = 0;
        return {ladder, lowered, down};
    }

    std::array<Ladder, most_ladders> ladders_{};
    std::size_t count_ = 0;
    GovernorOptions options_;
    Policy policy_ = weigh;
    std::array<float, remembered> costs_{};
    std::size_t head_ = 0, held_ = 0;
    std::uint64_t since_ = 0;
};

}  // namespace mlibs::pipeline
