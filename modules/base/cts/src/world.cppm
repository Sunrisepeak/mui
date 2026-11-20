// mlibs.cts:world — two contracts every simulated world is held to, whatever its dimension and
// whatever it is made of (the foundation plan's §6.13: a world of three dimensions passes the cases
// a world of two passes today, through an adapter of its own), written with no coordinate in them.
//
// The world contract (`world.*`): a world is a deterministic value. The same steps give the same
// hash; taking an image changes nothing (and, of a world that says its image is canonical, the same
// state is the same bytes); an image restored runs on as the uninterrupted run, bit for bit; a
// branch is a copy that runs alike and shares nothing with what it was made from; an image cut
// short is refused and leaves the world as it was; reading it changes nothing.
//
// The presentation contract (`presentation.*`, D-178): whatever shows a world reads it and never
// writes it. The world's hashes are the same wherever each ladder of the presentation stands and
// however the ladders move while it runs; every ladder has a name of its own; where each stands,
// and why, is said.
//
// A subject is an adapter (the suite calls nothing of the world itself), given by the world's
// owner.
export module mlibs.cts:world;

import std;
import :core;

export namespace mlibs::cts::world {

// A world as the cases see it. `step` is one frame of the adapter's own scene and inputs (the same
// each time); `disturb` is something only a writer does (a push, a cell placed) that the next
// frames show. What is stepped and has a hash: all a presentation's cases need of what it shows (a
// game's state as well as a world).
template <class A>
concept Stepped = requires(A& adapter, typename A::State& state, const typename A::State& fixed) {
    { adapter.make() } -> std::same_as<std::unique_ptr<typename A::State>>;
    adapter.step(state);
    { adapter.hash(fixed) } -> std::convertible_to<std::uint64_t>;
};
template <class A>
concept World =
    Stepped<A> && requires(A& adapter, typename A::State& state, const typename A::State& fixed,
                           std::span<const std::uint8_t> image) {
        adapter.disturb(state);
        { adapter.save(fixed) } -> std::convertible_to<std::vector<std::uint8_t>>;
        { adapter.restore(state, image) } -> std::convertible_to<bool>;
        { adapter.branch(fixed) } -> std::same_as<std::unique_ptr<typename A::State>>;
    };

// How long the cases run a world, and the frame at which they save and branch.
struct Run {
    int frames = 120;
    int at = 60;
};

namespace detail {
template <class A>
std::vector<std::uint64_t> straight(A& adapter, int frames) {
    auto state = adapter.make();
    std::vector<std::uint64_t> hashes;
    if (!state)
        return hashes;
    hashes.reserve(static_cast<std::size_t>(frames) + 1);
    hashes.push_back(adapter.hash(*state));
    for (int frame = 0; frame < frames; ++frame)
        adapter.step(*state), hashes.push_back(adapter.hash(*state));
    return hashes;
}
// Steps `state` from `from` to `to`, each frame's hash against `expected`; false (said) at the
// first that parts.
template <class A>
bool follows(Check& check, A& adapter, typename A::State& state,
             const std::vector<std::uint64_t>& expected, int from, int to, std::string_view what) {
    for (int frame = from; frame < to; ++frame) {
        adapter.step(state);
        if (adapter.hash(state) != expected[static_cast<std::size_t>(frame) + 1])
            return check.that(false, std::format("{}: frame {} parts from the uninterrupted run",
                                                 what, frame + 1));
    }
    return true;
}
}  // namespace detail

// The world contract's cases against `adapter`, checking `contract` (its id and version).
template <World A>
void declare(A adapter, std::string_view contract, Run run = {}) {
    const std::string id(contract);
    const auto subject = std::make_shared<A>(std::move(adapter));
    const auto made = [subject](Check& check) {
        auto state = subject->make();
        check.that(state != nullptr, "the adapter makes a world");
        return state;
    };
    mlibs::cts::declare(
        {"world.same-steps-same-hash", id, Level::exact, {}, [subject, run](Check& check) {
             const auto first = detail::straight(*subject, run.frames),
                        second = detail::straight(*subject, run.frames);
             if (!check.that(first.size() == static_cast<std::size_t>(run.frames) + 1,
                             "the adapter makes a world"))
                 return;
             check.that(first == second,
                        "two worlds made alike and stepped alike have every frame's hash alike");
             check.that(first.front() != first.back(), "the scene does something: the hash moves");
             check.evidence("state", std::format("{:016x}", first.back()));
         }});
    mlibs::cts::declare(
        {"world.image-restored-runs-on", id, Level::exact, {}, [subject, run, made](Check& check) {
             const auto expected = detail::straight(*subject, run.frames);
             auto state = made(check);
             if (!state)
                 return;
             for (int frame = 0; frame < run.at; ++frame)
                 subject->step(*state);
             const std::vector<std::uint8_t> image = subject->save(*state);
             check.that(!image.empty(), "an image has bytes");
             check.equal(std::uint64_t{subject->hash(*state)},
                         expected[static_cast<std::size_t>(run.at)],
                         "taking an image leaves the hash");
             if (!detail::follows(check, *subject, *state, expected, run.at, run.frames,
                                  "after an image was taken"))
                 return;
             if (!check.that(subject->restore(*state, image), "the image is restored"))
                 return;
             check.equal(std::uint64_t{subject->hash(*state)},
                         expected[static_cast<std::size_t>(run.at)],
                         "restored: the frame it was taken at");
             detail::follows(check, *subject, *state, expected, run.at, run.frames, "restored");
             check.evidence("image-bytes", std::to_string(image.size()));
         }});
    mlibs::cts::declare({"world.image-taken-twice-the-same",
                         id,
                         Level::exact,
                         {},
                         [subject, run, made](Check& check) {
                             auto state = made(check);
                             if (!state)
                                 return;
                             for (int frame = 0; frame < run.at; ++frame)
                                 subject->step(*state);
                             const std::vector<std::uint8_t> first = subject->save(*state),
                                                             again = subject->save(*state);
                             check.that(first == again,
                                        "one world's image taken twice is the same bytes");
                         }});
    // Needs `canonical-image`: a world whose image is a function of its state alone (no unused byte
    // of memory in it), so images can be compared and kept by content. A world that embeds a
    // solver's own image may lack it.
    mlibs::cts::declare({"world.same-state-same-image",
                         id,
                         Level::exact,
                         {"canonical-image"},
                         [subject, run, made](Check& check) {
                             auto one = made(check);
                             if (!one)
                                 return;
                             for (int frame = 0; frame < run.at; ++frame)
                                 subject->step(*one);
                             // The other made after worlds have come and gone: what memory was used
                             // for before must not show.
                             {
                                 auto gone = made(check);
                                 if (!gone)
                                     return;
                                 for (int frame = 0; frame < run.at; ++frame)
                                     subject->step(*gone);
                                 const std::vector<std::uint8_t> image = subject->save(*gone);
                                 auto branch = subject->branch(*gone);
                                 if (branch)
                                     subject->step(*branch);
                                 (void)subject->restore(*gone, image);
                             }
                             auto other = made(check);
                             if (!other)
                                 return;
                             for (int frame = 0; frame < run.at; ++frame)
                                 subject->step(*other);
                             check.equal(std::uint64_t{subject->hash(*one)},
                                         std::uint64_t{subject->hash(*other)},
                                         "the two at the same state");
                             check.that(subject->save(*one) == subject->save(*other),
                                        "two worlds at the same state have the same image");
                         }});
    mlibs::cts::declare(
        {"world.image-cut-short-refused", id, Level::exact, {}, [subject, run, made](Check& check) {
             auto state = made(check);
             if (!state)
                 return;
             for (int frame = 0; frame < run.at; ++frame)
                 subject->step(*state);
             const std::vector<std::uint8_t> image = subject->save(*state);
             for (int frame = run.at; frame < run.frames; ++frame)
                 subject->step(*state);
             const std::uint64_t before = subject->hash(*state);
             for (const std::size_t kept : {std::size_t{0}, image.size() / 3, image.size() - 1}) {
                 check.that(!subject->restore(*state, std::span(image).first(kept)),
                            std::format("an image of {} of its {} bytes is refused", kept,
                                        image.size()));
                 check.equal(std::uint64_t{subject->hash(*state)}, before,
                             std::format("refused at {} bytes: the world as it was", kept));
             }
         }});
    mlibs::cts::declare(
        {"world.branch-runs-alike", id, Level::exact, {}, [subject, run, made](Check& check) {
             const auto expected = detail::straight(*subject, run.frames);
             auto state = made(check);
             if (!state)
                 return;
             for (int frame = 0; frame < run.at; ++frame)
                 subject->step(*state);
             auto branch = subject->branch(*state);
             if (!check.that(branch != nullptr, "a branch is made"))
                 return;
             check.equal(std::uint64_t{subject->hash(*branch)},
                         expected[static_cast<std::size_t>(run.at)],
                         "the branch at the frame it was made");
             // The branch first, to its end; then the original: neither's steps are the other's.
             detail::follows(check, *subject, *branch, expected, run.at, run.frames, "the branch");
             detail::follows(check, *subject, *state, expected, run.at, run.frames,
                             "the original, after its branch ran");
         }});
    mlibs::cts::declare(
        {"world.branch-shares-nothing", id, Level::exact, {}, [subject, run, made](Check& check) {
             const auto expected = detail::straight(*subject, run.frames);
             auto state = made(check);
             if (!state)
                 return;
             for (int frame = 0; frame < run.at; ++frame)
                 subject->step(*state);
             auto branch = subject->branch(*state);
             if (!check.that(branch != nullptr, "a branch is made"))
                 return;
             subject->disturb(*branch);
             bool parted = false;
             for (int frame = run.at; frame < run.frames; ++frame) {
                 subject->step(*branch);
                 parted = parted ||
                          subject->hash(*branch) != expected[static_cast<std::size_t>(frame) + 1];
             }
             check.that(parted, "the branch, disturbed, goes its own way (the adapter's "
                                "disturbance is seen)");
             detail::follows(check, *subject, *state, expected, run.at, run.frames,
                             "the original, its branch disturbed");
             // And the other way: the original disturbed, a branch taken before it is not.
             auto kept = subject->branch(*state);
             if (!check.that(kept != nullptr, "a second branch is made"))
                 return;
             const std::uint64_t before = subject->hash(*kept);
             subject->disturb(*state);
             subject->step(*state);
             check.equal(std::uint64_t{subject->hash(*kept)}, before,
                         "the original disturbed and stepped: its branch as it was");
         }});
    if constexpr (requires(A& a, const typename A::State& fixed) { a.read(fixed); }) {
        mlibs::cts::declare(
            {"world.reading-changes-nothing",
             id,
             Level::exact,
             {},
             [subject, run, made](Check& check) {
                 const auto expected = detail::straight(*subject, run.frames);
                 auto state = made(check);
                 if (!state)
                     return;
                 for (int frame = 0; frame < run.frames; ++frame) {
                     subject->read(std::as_const(*state));
                     subject->step(*state);
                     if (subject->hash(*state) != expected[static_cast<std::size_t>(frame) + 1]) {
                         check.that(false, std::format("read every frame: frame {} parts from the "
                                                       "run that was not read",
                                                       frame + 1));
                         return;
                     }
                 }
             }});
    }
}
// How many cases `declare` makes for `A`.
template <World A>
[[nodiscard]] constexpr std::size_t case_count() noexcept {
    return requires(A& a, const typename A::State& fixed) { a.read(fixed); } ? 8 : 7;
}

// What shows a world, as the cases see it: `worlds` is the adapter of what it shows (stepped, with
// a hash); a ladder has a `name` and `steps` (how many it can be lowered); `set` puts ladder
// `index` at `lowered` steps down, with why.
template <class P>
concept Presentation =
    Stepped<typename P::Worlds> &&
    requires(P& adapter, typename P::Shown& shown, const typename P::Worlds::State& fixed,
             std::size_t index, std::uint8_t lowered, std::string why) {
        { adapter.worlds() } -> std::same_as<typename P::Worlds&>;
        { adapter.make() } -> std::same_as<std::unique_ptr<typename P::Shown>>;
        requires std::ranges::range<decltype(adapter.ladders(shown))>;
        {
            std::string_view(
                std::declval<const std::ranges::range_value_t<decltype(adapter.ladders(shown))>&>()
                    .name)
        };
        {
            static_cast<std::size_t>(
                std::declval<const std::ranges::range_value_t<decltype(adapter.ladders(shown))>&>()
                    .steps)
        };
        adapter.set(shown, index, lowered, why);
        adapter.show(shown, fixed);
        { adapter.describe(std::as_const(shown)) } -> std::convertible_to<std::string>;
    };

// The presentation contract's cases against `adapter`, checking `contract`.
template <Presentation P>
void declare_presentation(P adapter, std::string_view contract,
                          Run run = {.frames = 60, .at = 30}) {
    const std::string id(contract);
    const auto subject = std::make_shared<P>(std::move(adapter));
    struct Rung {
        std::string name;
        std::size_t steps = 0;
    };
    const auto rungs = [subject](typename P::Shown& shown) {
        std::vector<Rung> list;
        for (const auto& ladder : subject->ladders(shown))
            list.push_back({std::string(std::string_view(ladder.name)),
                            static_cast<std::size_t>(ladder.steps)});
        return list;
    };
    mlibs::cts::declare(
        {"presentation.ladders-named", id, Level::semantic, {}, [subject, rungs](Check& check) {
             auto shown = subject->make();
             if (!check.that(shown != nullptr, "the adapter makes a presentation"))
                 return;
             const auto list = rungs(*shown);
             check.that(!list.empty(), "a presentation has a ladder");
             std::set<std::string> names;
             for (const Rung& rung : list) {
                 check.that(!rung.name.empty(), "a ladder has a name");
                 check.that(names.insert(rung.name).second,
                            std::format("the name {} is one ladder's", rung.name));
                 check.that(rung.steps >= 1, std::format("{} can be lowered", rung.name));
             }
         }});
    mlibs::cts::declare(
        {"presentation.world-the-same-at-every-step",
         id,
         Level::exact,
         {},
         [subject, rungs, run](Check& check) {
             auto& worlds = subject->worlds();
             const auto expected = detail::straight(worlds, run.frames);
             if (!check.that(!expected.empty(), "the adapter makes a world"))
                 return;
             auto probe = subject->make();
             if (!check.that(probe != nullptr, "the adapter makes a presentation"))
                 return;
             const auto list = rungs(*probe);
             probe.reset();
             int runs = 0;
             // Everything at its richest, then each ladder alone at each step down, then everything
             // at its lowest.
             const auto one = [&](const std::vector<std::size_t>& lowered, std::string_view what) {
                 auto shown = subject->make();
                 auto state = worlds.make();
                 if (!check.that(shown && state, "a presentation and a world"))
                     return;
                 for (std::size_t index = 0; index < list.size(); ++index)
                     subject->set(*shown, index, static_cast<std::uint8_t>(lowered[index]), "cts");
                 for (int frame = 0; frame < run.frames; ++frame) {
                     subject->show(*shown, std::as_const(*state));
                     worlds.step(*state);
                     if (worlds.hash(*state) != expected[static_cast<std::size_t>(frame) + 1]) {
                         check.that(false, std::format("{}: frame {} of the world parts from the "
                                                       "world that was not shown",
                                                       what, frame + 1));
                         return;
                     }
                 }
                 ++runs;
             };
             std::vector<std::size_t> lowered(list.size(), 0);
             one(lowered, "every ladder at its richest");
             for (std::size_t index = 0; index < list.size(); ++index)
                 for (std::size_t step = 1; step <= list[index].steps; ++step) {
                     lowered.assign(list.size(), 0);
                     lowered[index] = step;
                     one(lowered, std::format("{} {} down", list[index].name, step));
                 }
             for (std::size_t index = 0; index < list.size(); ++index)
                 lowered[index] = list[index].steps;
             one(lowered, "every ladder at its lowest");
             check.evidence("runs", std::to_string(runs));
         }});
    mlibs::cts::declare(
        {"presentation.world-the-same-while-ladders-move",
         id,
         Level::exact,
         {},
         [subject, rungs, run](Check& check) {
             auto& worlds = subject->worlds();
             const auto expected = detail::straight(worlds, run.frames);
             auto shown = subject->make();
             auto state = worlds.make();
             if (!check.that(shown && state && !expected.empty(), "a presentation and a world"))
                 return;
             const auto list = rungs(*shown);
             std::vector<std::size_t> lowered(list.size(), 0);
             for (int frame = 0; frame < run.frames; ++frame) {
                 // A ladder a step every few frames, down to its lowest and round to its richest:
                 // as a governor moves them.
                 if (frame % 5 == 4 && !list.empty()) {
                     const std::size_t index = static_cast<std::size_t>(frame / 5) % list.size();
                     lowered[index] = (lowered[index] + 1) % (list[index].steps + 1);
                     subject->set(*shown, index, static_cast<std::uint8_t>(lowered[index]),
                                  "cts: moved");
                 }
                 subject->show(*shown, std::as_const(*state));
                 worlds.step(*state);
                 if (worlds.hash(*state) != expected[static_cast<std::size_t>(frame) + 1]) {
                     check.that(false, std::format("frame {} of the world parts from the world "
                                                   "that was not shown",
                                                   frame + 1));
                     return;
                 }
             }
         }});
    mlibs::cts::declare(
        {"presentation.says-where-and-why",
         id,
         Level::semantic,
         {},
         [subject, rungs](Check& check) {
             auto shown = subject->make();
             auto state = subject->worlds().make();
             if (!check.that(shown && state, "a presentation and a world"))
                 return;
             const auto list = rungs(*shown);
             subject->show(*shown, std::as_const(*state));
             const std::string richest = subject->describe(std::as_const(*shown));
             check.that(!richest.empty(), "it describes itself");
             for (std::size_t index = 0; index < list.size(); ++index) {
                 const std::string why = std::format("cts: {} pinned", list[index].name);
                 subject->set(*shown, index, static_cast<std::uint8_t>(list[index].steps), why);
                 subject->show(*shown, std::as_const(*state));
                 const std::string lowest = subject->describe(std::as_const(*shown));
                 check
                     .that(lowest != richest,
                           std::
                               format("{} at its lowest is described otherwise than at its richest",
                                      list[index].name));
                 check.that(lowest.contains(why),
                            std::format("why {} stands where it does is said back",
                                        list[index].name));
                 subject->set(*shown, index, 0, "cts: back");
             }
         }});
}
inline constexpr std::size_t presentation_case_count = 4;

}  // namespace mlibs::cts::world
