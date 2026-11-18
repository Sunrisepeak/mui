// Kiln's simulation: Mui the pixel cat in caverns of living matter.
//
//   auto state = kiln::start(kiln::level::first());
//   kiln::step(state, {.player = actions});            // a 60th of a second
//
// The world is the standard definition of matter on a lattice of 0.1 m cells, with rigid bodies made of its
// cells and cells in flight (meng.cellular.rigid's MatterWorld). Mui reads it: what is solid or a powder
// holds her up, a liquid is swum, heat and fire hurt, water over her head takes her breath. Her wand casts
// fire (it heats where it lands: what burns catches by the laws), water (it pours where it lands), and bombs
// (they blast what is there, stone too, and heat it). Every puzzle is what the laws do.
//
// Coordinates are the field's, in cells: x across, y down. Mui's position is the middle of her feet.
export module omui.showcase.kiln.sim;
import std;
export import meng.input;
export import meng.cellular.rigid;
export import :first_world;   // levels/first.world.xdef.ts, embedded as its JSON (kiln::level::first_world)
export import :flawed_world;  // levels/flawed.world.xdef.ts

export namespace kiln {
namespace rigid = meng::cellular::rigid;

inline constexpr std::int32_t steps_per_second = 60;
inline constexpr float step_seconds = 1.0f / steps_per_second;
// Buttons: jump (and swim up), cast, the next spell. Axis 0 walks; the pointer aims (0 … 1 of the field).
inline constexpr int jump_button = 0, cast_button = 1, next_spell_button = 2;
inline constexpr std::uint32_t cat_width = 3, cat_height = 5;

enum class Spell : std::uint8_t { fire, water, bomb };
inline constexpr std::array<std::string_view, 3> spell_names{"fire", "water", "bomb"};
inline constexpr std::array<std::int32_t, 3> spell_cost{20, 15, 50};

struct Cat {
    float x = 0, y = 0;            // the middle of her feet, in cells
    float vx = 0, vy = 0;          // cells a second
    std::int32_t health = 100, mana = 100, breath = 180;
    std::uint32_t bombs = 0;       // charges: a cavern gives so many
    Spell spell = Spell::fire;
    bool facing_right = true, on_ground = false, swimming = false;
    std::uint32_t hurt = 600;      // frames since she was last hurt (for a picture's flash, and when she mends); at the start: long ago
};
struct Bolt {
    float x = 0, y = 0, vx = 0, vy = 0;
    Spell spell = Spell::fire;
};
enum class Outcome : std::uint8_t { playing, won, lost };

struct State {
    mlibs::simulation::Branching<rigid::MatterWorld> world;
    Cat cat;
    std::vector<Bolt> bolts;
    meng::cellular::Tile exit;
    std::uint64_t frame = 0;
    Outcome outcome = Outcome::playing;
    std::uint32_t casts = 0;
};
struct Input {
    meng::Actions player;
};

// A level: rows of characters, one a cell, and what each is (`legend`): a material of the definition, or a role —
// `start` where Mui stands (her feet), `exit` (any number of them: the region they span), `body:<material>` that
// material as a body (a crate; each connected piece one body). A character the legend does not name is air.
// The levels are TypeScript data (`levels/*.world.xdef.ts`, omui/define's `defineWorld`), carried in the program
// as the JSON they evaluate to and read by `level::read`.
// Scenery: matter that is only shown (D-178) — the simulation never has it, so it is no part of the game; a
// picture that has room simulates it beside the world (it catches from the world's fires), one that has not
// draws it as placed.
struct Scenery {
    meng::cellular::Tile region;
    std::string             material;  // a solid of the definition
};
struct Level {
    std::string                               name;
    std::vector<std::pair<char, std::string>> legend;
    std::vector<std::string>                  rows;
    std::uint32_t                             bombs = 2;  // the wand's bomb charges in this cavern (`numbers.bombs`)
    std::vector<Scenery>                      scenery;
};
namespace level {
// A level from its snapshot (`{"world": …}`, what a `.world.xdef.ts` evaluates to); a problem names its place.
[[nodiscard]] std::expected<Level, std::string> read(std::string_view json, std::string_view name);
// The first cavern: a barricade of wood to burn, a pool with a crate afloat, a stone wall to blast, lava to
// cool into a floor, the exit beyond. Two bombs.
[[nodiscard]] Level first();
// The first cavern with a mistake in it, for a playtest to find: a second wall of stone past the lava, and
// still one bomb — fire and water open no stone, so it cannot be finished.
[[nodiscard]] Level flawed();
// The levels the program carries, by their file's stem ("first", "flawed").
[[nodiscard]] std::optional<Level> named(std::string_view stem);
}  // namespace level

// The level's world, Mui at her start; or why it cannot be made.
[[nodiscard]] std::expected<State, std::string> start(const Level& level);
void step(State& state, const Input& input);
[[nodiscard]] std::uint64_t checksum(const State& state) noexcept;
// What a player is shown, as JSON: Mui (where, health, mana, breath, the spell), the exit, what is around her
// (a screen's worth of cells, a letter each), the bodies near, what flies; `full`: every channel there too.
[[nodiscard]] std::string observe(const State& state, bool full);
// The standard definition of matter, loaded once (what every world of Kiln is made of).
[[nodiscard]] const meng::matter::Program& matter();

}  // namespace kiln
