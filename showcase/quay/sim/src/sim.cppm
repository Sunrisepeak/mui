// Quay's simulation: a watchkeeper on a harbour at night, the wardens that patrol it, and the beacon at the end of
// the breakwater that has to be lit before the tide comes in.
//
//   auto state = quay::start(quay::level::harbour());
//   quay::step(state, {.player = actions});            // a 60th of a second
//
// The quay, its containers and crates are a 3D rigid world (xengine.dynamics.rigid3: Jolt under our contract); the
// watchkeeper is its character; the wardens walk the quay's navigation mesh (xengine.nav: Detour's crowd). Both
// worlds are held as values (`mlibs::simulation::Branching`), so the state is a value: a copy is a branch.
//
// The wardens know only what they hear and see. Each step's noises — a shot, running feet, a crate knocked — reach
// each warden by xengine.audio.space's `reach`, the rule the player's own speakers play by (distance, and each wall
// on the way); one loud enough is heard, and the warden goes to where it seems to have come from — further than it
// was if walls dulled it. A warden sees the watchkeeper within its sight's reach and angle when a ray from its eye
// to the watchkeeper's head meets nothing on the way. It never reads where the watchkeeper is.
//
// Coordinates: metres, y up; the quay is the ground at y = 0, the sea beyond its edges.
export module omui.showcase.quay.sim;

import std;
export import xengine.input;
export import xengine.nav;
export import xengine.dynamics.rigid3;
export import xengine.audio.space;
export import mlibs.simulation;
export import mlibs.geom;

export namespace quay {

namespace rigid3 = xengine::dynamics::rigid3;
namespace nav = xengine::nav;
using mlibs::math::Vec3;

inline constexpr std::int32_t steps_per_second = 60;
inline constexpr float step_seconds = 1.0f / steps_per_second;
// Buttons: jump, fire, use (the beacon), run. Axes: 0 strafes (right +), 1 walks (forward +), 2 turns (right +),
// 3 looks (up +).
inline constexpr int jump_button = 0, fire_button = 1, use_button = 2, run_button = 3;
inline constexpr float eye_height = 1.6f;

// A box of the level: a container, a wall, a floor (fixed), or a crate (it moves when struck or pushed).
struct Box {
    Vec3 at, half;
    bool crate = false;
};
struct Patrol {
    Vec3 from, to;
};
struct Level {
    std::string_view    name;
    std::vector<Box>    boxes;
    Vec3                start;
    float               facing = 0;  // radians: 0 looks along −z, π/2 along +x
    std::vector<Patrol> wardens;
    Vec3                beacon;
    float               tide = 180;  // seconds before the tide covers the breakwater
    std::uint32_t       rounds = 60; // the watchkeeper's
};
namespace level {
// The harbour: a quay of rows of containers with crates between them, four wardens on their rounds, and the
// breakwater out to the beacon.
[[nodiscard]] Level harbour();
}  // namespace level

enum class Mind : std::uint8_t { patrol, search, chase };
inline constexpr std::array<std::string_view, 3> mind_names{"patrol", "search", "chase"};
struct Warden {
    nav::WalkerId walker;
    Patrol        round;
    bool          outward = true;     // on its round: toward `round.to`
    std::int32_t  health = 3;
    Mind          mind = Mind::patrol;
    Vec3          at, facing{0, 0, -1};
    Vec3          heading_for;        // search: where it thinks the noise was; chase: where it last saw the watchkeeper
    std::uint32_t searching = 0;      // steps left looking about where it went
    std::uint32_t cooldown = 0;       // steps before it may fire again
    bool          sees = false;       // the watchkeeper, this step
    std::uint32_t heard = 0;          // noises it heard this step
};
struct Watchkeeper {
    rigid3::CharacterId body;
    Vec3                at;          // feet
    float               yaw = 0, pitch = 0;
    std::int32_t        health = 100;
    std::uint32_t       rounds = 0, cooldown = 0;
    std::uint32_t       stride = 0;  // steps since the last footfall
    std::uint32_t       lighting = 0;  // steps the beacon has been worked
    std::uint32_t       hurt = 600;    // steps since last hit
};
// Something heard: where, how loud within a metre (dB against full), who made it (0 the watchkeeper, n the n-th
// warden), what it was.
struct Noise {
    Vec3             at;
    float            loudness = 0;
    std::uint32_t    by = 0;
    std::string_view what;
};
// A shot this step: from, to where it stopped, what it met (a warden by number, a body, nothing).
struct Shot {
    Vec3          from, to;
    std::uint32_t by = 0;         // 0 the watchkeeper, n the n-th warden
    std::uint32_t warden = 0;     // the warden it struck (1 …), or 0
    bool          struck = false; // the watchkeeper, for a warden's shot
};
enum class Outcome : std::uint8_t { playing, won, lost };
inline constexpr std::array<std::string_view, 3> outcome_names{"playing", "won", "lost"};

// What does not change: the level's fixed boxes as one mesh with its tree, what sound passes through.
struct Statics {
    mlibs::geom::Mesh              mesh;
    mlibs::geom::Bvh               tree;
    std::vector<xengine::audio::space::Wall> walls;
};

struct State {
    mlibs::simulation::Branching<rigid3::World> bodies;
    mlibs::simulation::Branching<nav::World>     ground;
    std::shared_ptr<const Statics>               statics;
    Watchkeeper                                  keeper;
    std::vector<Warden>                          wardens;
    std::vector<rigid3::BodyId>                  crates;
    std::vector<Noise>                           noises;  // this step's
    std::vector<Shot>                            shots;   // this step's
    Vec3                                         beacon;
    float                                        tide = 0;  // seconds left
    std::uint64_t                                frame = 0;
    Outcome                                      outcome = Outcome::playing;
    std::string                                  why;  // how it ended
};
struct Input {
    xengine::Actions player;
};

[[nodiscard]] State start(const Level& level);
void step(State& state, const Input& input);
[[nodiscard]] std::uint64_t checksum(const State& state) noexcept;

// Where the watchkeeper looks: the eye and the way it looks.
[[nodiscard]] Vec3 eye(const State& state) noexcept;
[[nodiscard]] Vec3 looking(const State& state) noexcept;
// The wardens still standing.
[[nodiscard]] std::size_t standing(const State& state) noexcept;
// Whether a ray from `from` to `to` meets nothing of the world on the way.
[[nodiscard]] bool clear(const State& state, Vec3 from, Vec3 to);

}  // namespace quay
