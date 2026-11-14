// Kiln as a game the runtime plays (xengine.runtime's Playable): the simulation's own state, input and step;
// what a player is shown; an agent player's actions as Mui's.
//
//   kiln::Standalone game(*kiln::start(kiln::level::first()), {.keep = 600});
export module omui.showcase.kiln.headless;

import std;
import openas;
import mlibs.agent;
import xengine.input;
import xengine.cellular.matter.agent;
import xengine.cellular.rigid.agent;
export import xengine.runtime;
export import omui.showcase.kiln.sim;

export namespace kiln {

struct Play {
    using State = kiln::State;
    using Input = kiln::Input;
    static constexpr std::int32_t steps_per_second = kiln::steps_per_second;
    static void step(State& state, const Input& input) { kiln::step(state, input); }
    [[nodiscard]] static std::uint64_t checksum(const State& state) noexcept { return kiln::checksum(state); }
    static void play(Input& input, const xengine::Actions& actions) { input.player = actions; }
    [[nodiscard]] static bool over(const State& state) noexcept { return state.outcome != Outcome::playing; }
    // What a player is shown: Mui, the exit, the cells around her by what they are; the full view adds the hash.
    [[nodiscard]] static std::string observe(const State& state, bool full) { return kiln::observe(state, full); }
    // The rules a player is told before playing.
    static constexpr std::string_view about =
        "Kiln: Mui, a cat with a wand, crosses a cavern to its exit. Everything is matter and nothing is scripted — wood burns, water puts fire out and "
        "boils, stone breaks under a bomb's pressure, lava kills — and her three spells (fire, water, a bomb; bombs are few) open the way by what they do "
        "to it. Fire, heat and lava cost health; water over her head costs breath; mana pays for spells and comes back. Reaching the exit wins; no "
        "health loses";
    // What the controls mean, and what to begin with (D-186): `world.state` lists them, `find` answers "jump".
    static constexpr std::array<xengine::runtime::Control, 6> controls{{
        {.name = "left", .summary = "Mui walks left", .axis = 0, .toward = -1},
        {.name = "right", .summary = "Mui walks right", .axis = 0, .toward = 1},
        {.name = "jump", .summary = "Jump from the ground; held in water, swim up", .button = jump_button},
        {.name = "cast", .summary = "Cast the chosen spell once, toward `aim` (with no aim: the way she faces); it costs mana", .button = cast_button},
        {.name = "next-spell", .summary = "Choose the next of her spells: fire, water, a bomb", .button = next_spell_button},
        {.name = "aim", .summary = "Where a spell is cast toward: a place in the cavern, [x, y], each 0 to 1 of it", .pointer = true},
    }};
    static constexpr std::array<xengine::runtime::Move, 3> walk_and_jump{{{.hold = {"right"}, .steps = 40}, {.hold = {"right", "jump"}, .steps = 20}, {.steps = 20}}};
    static constexpr std::array<xengine::runtime::Move, 2> cast_once{{{.hold = {"aim", "cast"}, .steps = 20, .pointer = {0.9, 0.5}}, {.steps = 30}}};
    static constexpr std::array<xengine::runtime::Task, 2> tasks{{
        {"walk-right-and-jump", "Walk right, jump while walking, and see where Mui stands", walk_and_jump},
        {"cast-a-spell", "Cast the chosen spell toward the right of the cavern and see what it left", cast_once},
    }};
};

using Standalone = xengine::runtime::Runtime<Play>;

// The matter of the game's world as a subject beside the game (xengine.cellular's matter subject on the world's
// lattice): `matter.observe`, `matter.explain` — why this sand is here, why that crate fell — of the world the
// game is at, as the game steps and seeks. Time is the game's (`world.step`, `world.seek`). What Mui's wand does
// to the world is on record as the player's.
//
//   kiln::Causes causes(game);
//   mlibs::agent::SubjectSession session(kiln::subjects(game, &causes));
//
// Made after the game and gone before it; it takes the game's one place to be told of its time (Runtime::tell).
class Causes {
public:
    explicit Causes(Standalone& game, openas::Actor player = {openas::Kind::person, "player", {}});
    ~Causes();
    Causes(const Causes&) = delete;
    Causes& operator=(const Causes&) = delete;
    [[nodiscard]] xengine::cellular::matter_agent::Matter& matter() noexcept { return matter_; }

private:
    Standalone*                               game_;
    xengine::cellular::rigid_agent::WorldLattice lattice_;
    xengine::cellular::matter_agent::Matter   matter_;
};

// The subjects a session of `game` has: the game, and — where its tier is shown everything (`assist`) — the
// matter of its world. A person's tier is shown what a person is: it cannot ask why of a cell it does not see.
[[nodiscard]] std::vector<mlibs::agent::Subject*> subjects(Standalone& game, Causes* causes);

}  // namespace kiln
