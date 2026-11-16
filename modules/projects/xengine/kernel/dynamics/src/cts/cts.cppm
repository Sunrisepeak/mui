// xengine.dynamics.cts — every solver family's cases (contract ids
// xengine.dynamics.collision@1, .xpbd@1, .material@2) on a subject. The CPU
// reference is the subject today; a SIMD or GPU tier will be another subject
// held to the same cases (within its declared tolerance), and a family it
// does not implement is unsupported, never passed.
export module xengine.dynamics.cts;

import std;
export import mlibs.cts;

export namespace xengine::dynamics::cts {

// Which implementation runs the cases, and what it has ("collision", "xpbd", "material").
struct Subject {
    std::string            name = "cpu-reference";
    std::vector<std::string> families{"collision", "xpbd", "material"};
};

// Every case against `subject`: mtest's conformance cases `<subject>/<family>.<case>` (in a tally, counted). What
// it has is its families; `who` may name it otherwise and say what the run requires.
void declare(Subject subject = {}, mlibs::cts::Against who = {}, std::source_location at = std::source_location::current());
// How many cases the suite has.
[[nodiscard]] std::size_t case_count() noexcept;


// A rigid family's hard constraints (the foundation plan's §6.11, §6.13; contract xengine.dynamics.rigid@1): what
// rollback and world.branch need of any solver put under this package — Box2D for two dimensions today, another
// for three. The family is a world (mlibs.cts:world: the same steps the same hash, an image restored runs on bit
// for bit, a branch is a copy that shares nothing) whose bodies keep their names: an id names the same body, at
// the same place, across an image restored and in a branch. The cases call an adapter and carry no coordinate.
template <class F>
concept RigidFamily = mlibs::cts::world::World<F> && std::equality_comparable<typename F::Body> &&
    requires(F& family, typename F::State& world, const typename F::State& fixed, typename F::Body body) {
        { family.bodies(fixed) } -> std::convertible_to<std::vector<typename F::Body>>;
        { family.add(world) } -> std::same_as<typename F::Body>;       // a body put in from outside
        family.remove(world, body);
        { family.has(fixed, body) } -> std::convertible_to<bool>;
        { family.where(fixed, body) } -> std::convertible_to<std::uint64_t>;  // its pose's bits, as one number
    };
inline constexpr std::string_view rigid_contract = "xengine.dynamics.rigid@1";

template <RigidFamily F>
void declare_rigid(F family, mlibs::cts::world::Run run = {}) {
    mlibs::cts::world::declare(family, rigid_contract, run);
    const auto subject = std::make_shared<F>(std::move(family));
    mlibs::cts::declare({"rigid.ids-across-an-image", std::string(rigid_contract), mlibs::cts::Level::exact, {}, [subject, run](mlibs::cts::Check& check) {
        auto world = subject->make();
        if (!check.that(world != nullptr, "the adapter makes a world")) return;
        for (int frame = 0; frame < run.at; ++frame) subject->step(*world);
        const std::vector<typename F::Body> before = subject->bodies(*world);
        if (!check.that(before.size() >= 2, "the scene has bodies")) return;
        std::vector<std::uint64_t> places;
        for (const auto& body : before) places.push_back(subject->where(*world, body));
        const std::vector<std::uint8_t> image = subject->save(*world);
        const auto gone = before[before.size() / 2];
        subject->remove(*world, gone);
        check.that(!subject->has(*world, gone), "a body removed is not in the world");
        const auto added = subject->add(*world);
        check.that(subject->has(*world, added), "a body added is");
        for (int frame = 0; frame < 5; ++frame) subject->step(*world);
        if (!check.that(subject->restore(*world, image), "the image is restored")) return;
        check.that(subject->has(*world, gone), "restored: the body removed after the image is back");
        check.that(!subject->has(*world, added), "restored: the body added after the image is gone");
        const std::vector<typename F::Body> after = subject->bodies(*world);
        check.that(after == before, "restored: the same bodies by the same ids, in the same order");
        std::size_t moved = 0;
        for (std::size_t index = 0; index < before.size() && index < after.size(); ++index) moved += subject->has(*world, before[index]) && subject->where(*world, before[index]) != places[index];
        check.equal(moved, std::size_t{0}, "restored: bodies whose id names another place");
        check.evidence("bodies", std::to_string(before.size()));
    }});
    mlibs::cts::declare({"rigid.ids-in-a-branch", std::string(rigid_contract), mlibs::cts::Level::exact, {}, [subject, run](mlibs::cts::Check& check) {
        auto world = subject->make();
        if (!check.that(world != nullptr, "the adapter makes a world")) return;
        for (int frame = 0; frame < run.at; ++frame) subject->step(*world);
        auto branch = subject->branch(*world);
        if (!check.that(branch != nullptr, "a branch is made")) return;
        const std::vector<typename F::Body> here = subject->bodies(*world);
        check.that(subject->bodies(*branch) == here, "the branch has the same bodies by the same ids");
        std::size_t elsewhere = 0;
        for (const auto& body : here) elsewhere += !subject->has(*branch, body) || subject->where(*branch, body) != subject->where(*world, body);
        check.equal(elsewhere, std::size_t{0}, "bodies an id names at another place in the branch");
        // A body removed from the branch is still the original's.
        if (here.empty()) return;
        subject->remove(*branch, here.front());
        check.that(subject->has(*world, here.front()), "removed from the branch: still in the original");
    }});
}
// How many cases `declare_rigid` makes for `F`.
template <RigidFamily F>
[[nodiscard]] constexpr std::size_t rigid_case_count() noexcept { return mlibs::cts::world::case_count<F>() + 2; }

}  // namespace xengine::dynamics::cts
