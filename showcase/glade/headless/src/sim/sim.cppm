// Glade's simulation: a pocket valley — a stream and a footbridge, grass on both banks, a campfire, a crate on the
// far bank, a block of ice — made of matter, poked by a few verbs, answering by the laws.
//
//   auto state = glade::start(glade::level::glade());
//   glade::torch(state, {-3, 0.2f, 0}, 2);    // a torch held there for two seconds
//   glade::step(state);                       // a 60th of a second
//   glade::explain(state, "crate");           // why it is what it is, back to where it began
//
// Every body is a body of xengine.dynamics' rigid3 world and an instance of matter (rigid3::Objects); the air is a
// field (xengine.field) with its wind; water poured is particles that take room (xengine.particles); all of it one
// step (rigid3::Scene). Nothing here names what fire, water or wind do: the standard laws do, with Glade's own
// materials (glade.matter.omui.ts: grass and bark, rows of properties). A verb is a quantity applied from outside
// and kept as a cause. The state is a value — the rigid world held by `mlibs::simulation::Branching` — so a copy
// is a branch: an agent tries a verb on one before doing it.
//
// Coordinates: metres, y up; the near bank is x < 0, the stream runs along z between x = 0 and x = 2, the far bank
// beyond; the ground's top is y = 0.
export module omui.showcase.glade.sim;

import std;
export import xengine.dynamics.scene;
export import xengine.matter.json;
export import mlibs.simulation;

export namespace glade {

namespace rigid3 = xengine::dynamics::rigid3;
namespace matter = xengine::matter;
namespace field = xengine::field;
namespace particles = xengine::particles;
using mlibs::math::Vec3;

inline constexpr int steps_per_second = 60;

// The standard definition with Glade's pack laid over it.
[[nodiscard]] std::shared_ptr<const matter::Program> program();

// A thing of a level: a box of a material, fixed (the ground, grass, a bridge) or loose (a crate, the ice).
struct Thing {
    std::string name;
    Vec3        at, half;
    std::string material;
    bool        fixed = true;
    bool        carried = true;  // its matter is carried (the ground's is not: it touches everything)
    float       density = 0;     // kg/m³; 0: the material's
};
// The ground as heights on a grid (a fixed body of rigid3's mesh shape, carrying no matter): `heights` row by
// row along z, (across + 1) × (along + 1) of them, from `origin` (the corner at the lowest x and z).
struct Terrain {
    Vec3               origin{0, 0, 0};
    float              cell = 0.5f;
    std::uint32_t      across = 0, along = 0;
    std::vector<float> heights;
    std::string        material = "stone";
    [[nodiscard]] float height(float x, float z) const noexcept;  // between the grid's heights, as its triangles are
};
struct Level {
    std::string        name;
    Terrain            ground;
    std::vector<Thing> things;
    Vec3               wind{0, 0, 0};
    float              stream_from = 0, stream_to = 2;  // x: the stream between
};
namespace level {
// The valley: a hill behind the near bank, the near bank with a strip of grass to the bridge, the stream in its
// channel and the footbridge over it, the far bank's grass to a wooden crate; the campfire's logs in a ring of
// stones on the near bank, a block of ice by it; trees (bark, a crown of dry leaves) on both banks.
[[nodiscard]] Level glade();
}  // namespace level

// What a person or an agent is shown of a thing: what it is and the state it is in (the semantic projection).
struct Seen {
    std::string name, material;
    Vec3        at;
    float       kelvin = 0;
    bool        burning = false;
    float       wetness = 0;
    bool        gone = false;  // burned out: it is what it became (`material`)
    bool        open = false;  // nothing above it: rain reaches it, lightning may
};

struct Held {
    rigid3::BodyId body;
    std::int64_t   until = 0;  // the step it is let go at
    float          kelvin = 900;
};
struct State {
    std::shared_ptr<const matter::Program>      program;
    mlibs::simulation::Branching<rigid3::World> world;
    std::optional<rigid3::Objects>              objects;
    std::optional<field::Field>                 air;
    std::optional<particles::Cloud>             cloud;
    rigid3::Scene                               scene;
    std::vector<std::pair<std::string, rigid3::BodyId>> names;  // in the order made
    std::vector<rigid3::BodyId>                 ground;          // fixed and not carried: what particles meet but has no matter
    std::vector<Held>                           torches;
    bool                                        raining = false;
    Vec3                                        wind{0, 0, 0};
    std::int64_t                                steps = 0;
    std::vector<std::string>                    done;            // each verb, said, with its step
    float                                       stream_from = 0, stream_to = 2;
};

[[nodiscard]] State start(const Level& level);
// One step: the torches held, the rain, then the scene's whole tick (rigid3::Scene::advance): the wind's and blasts'
// pushes, the bodies, the drops against them, matter for all.
void step(State& state);
void run(State& state, int steps);

// ── verbs: each a quantity applied from outside, kept as a cause ──
// A torch held at a place for a time: the carried thing nearest it (within 0.6 m) is held at `kelvin`.
bool torch(State& state, Vec3 at, float seconds, float kelvin = 900);
// Every torch held let go now (a hand that held one opened).
void let_go(State& state);
// Water poured at a place: `drops` drops of it (2 cm, a unit each) let fall from 0.5 m above, over a second.
void pour(State& state, Vec3 at, int drops);
// A thing put down (loose, or fixed as it says).
bool drop(State& state, const Thing& thing);
// A thing pushed (an impulse, N·s).
bool push(State& state, std::string_view name, Vec3 impulse);
// The wind from now on (the field's far wind); rain on or off; lightning, now (on the highest open thing that
// conducts — none: it strikes nothing).
void wind(State& state, Vec3 far);
void rain(State& state, bool on);
std::optional<std::string> strike(State& state);

// ── what is seen and said ──
[[nodiscard]] std::optional<rigid3::BodyId> body(const State& state, std::string_view name);
[[nodiscard]] std::optional<std::string_view> name_of(const State& state, rigid3::BodyId body);
// The carried thing nearest a place (by its box) within `reach`, and its name.
[[nodiscard]] std::optional<std::string_view> nearest_to(const State& state, Vec3 at, float reach);
[[nodiscard]] std::vector<Seen> observe(const State& state);
[[nodiscard]] std::optional<Seen> seen(const State& state, std::string_view name);
// Why a thing is what it is, back to where it began, said in words (a link a line: who, when, by which law, from
// whom); the links themselves by `chain`.
[[nodiscard]] std::vector<matter::Explained> chain(const State& state, std::string_view name);
[[nodiscard]] std::string explain(const State& state, std::string_view name);
[[nodiscard]] std::uint64_t checksum(const State& state);

// ── tasks: problems the environment solves ──
enum class Verb : std::uint8_t { torch, pour, drop, push, wind, rain, strike };
struct Task {
    std::string                               name, goal;
    std::vector<Verb>                         verbs;       // the verbs it may be solved with
    std::function<bool(const State&)>         done;
    std::function<bool(Vec3)>                 may_act_at;  // where verbs may be done (the near bank only, …)
    std::string                               where;       // the same, said
    std::vector<std::string>                  about;       // the things the goal is about
    std::optional<float>                      x_below;     // the same, as a bound: verbs only where x is below it
};
[[nodiscard]] std::vector<Task> tasks();

}  // namespace glade
