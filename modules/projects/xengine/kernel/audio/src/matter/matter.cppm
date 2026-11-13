// xengine.audio.matter — what matter sounds like, made from what it is (the package's feature `matter`; the
// foundation plan's §6.15, D-183). A world's events have names — a thing lands, shatters, catches fire, boils —
// and a material has properties; a recipe says which shape of sound an event has, and each material's sound of
// that shape is made from its own numbers: the pitch it rings at and how soon that dies (`ring`, `damping`:
// audio's pack of acoustics, or a work's own), or, of a material that says neither, its phase and its density.
// No work writes "when wood lands play this": a new material sounds without a sound written for it.
//
//     auto sounds = *audio::matter::Sounds::make(program, mixer);               // a sound an event a material
//     sounds.hear({.event = "land", .material = iron, .strength = 0.8f, .pan = -0.2f, .at = mixer.clock()});
//
// It is of the presentation: it reads the world and changes nothing of it. The sounds are made once, when it is
// made; hearing an event plays one, and allocates nothing.
export module xengine.audio.matter;

import std;
export import xengine.audio;
export import xengine.matter;

export namespace xengine::audio::matter {

// The shape of sound an event has. `strike`: one thing meets another — a click and the thing's ring. `crumble`:
// it breaks — a burst that falls, over what is left of its ring. `sizzle`: it burns or boils — a hiss.
enum class Shape : std::uint8_t { strike, crumble, sizzle };
struct Recipe {
    std::string event;
    Shape       shape = Shape::strike;
    float       gain = 1;
    float       again = 0;  // heard from a world's steps: the least seconds between two of it for one material
};
// The events of the standard definition and of a world's journal: land, strike, shatter, ignite, boil, freeze, melt.
[[nodiscard]] std::span<const Recipe> standard_recipes() noexcept;

struct Heard {
    std::string_view event;
    std::uint32_t    material = 0;
    float            strength = 1;  // 0 … 1: how hard (a speed, a pressure), by whoever tells of it
    float            pan = 0;       // −1 left … 1 right
    std::int64_t     at = 0;        // the mixer's sample it is heard at
};

// Where a step's events are heard from: a lattice `width` cells wide, an ear over the cell column `at`, what is
// `reach` cells to a side wholly on that side; `when` the mixer's sample.
struct Ear {
    std::uint32_t width = 1;
    float         at = 0, reach = 32;
    std::int64_t  when = 0;
};

// The sound of one shape for one material, as data: what `Sounds` adds to the mixer, and what a tool shows.
[[nodiscard]] Sound sound_of(const xengine::matter::Program& program, std::uint32_t material, const Recipe& recipe);

class Sounds {
public:
    // Every recipe's sound for every material, added to the mixer (named `matter/<event>/<material>`).
    [[nodiscard]] static std::expected<Sounds, std::string> make(const xengine::matter::Program& program, Mixer& mixer, std::span<const Recipe> recipes = standard_recipes());
    // Plays it. False: no recipe for the event, no such material, or the mixer's queue is full.
    bool hear(const Heard& heard) noexcept;
    // A step's events as its carrier says them (a world's `events`): the ones that have a recipe, each event of
    // each material one sound — louder the more of them there were, from where they were on average. A wall that
    // shatters is one crumble, not forty. How many sounds it played. Allocates nothing.
    // What goes on — wood burning, lava setting — says so every frame: a recipe's `again` keeps it from being a
    // sound a frame.
    std::uint32_t hear(std::span<const xengine::matter::Event> events, const Ear& ear) noexcept;
    // A step's landings (a world's `impacts`), as the event `land`: a material's is one sound, as loud as the
    // fastest of them — `full` the speed that is as loud as it gets.
    std::uint32_t hear(std::span<const xengine::matter::Impact> impacts, const Ear& ear, float full = 60) noexcept;
    [[nodiscard]] std::int32_t sound(std::string_view event, std::uint32_t material) const noexcept;  // the mixer's index; −1: none
    [[nodiscard]] std::size_t count() const noexcept { return sounds_.size(); }

private:
    Mixer*                    mixer_ = nullptr;
    std::vector<std::string>  events_;
    std::size_t               materials_ = 0;
    std::vector<std::int32_t> sounds_;  // event × material
    std::vector<std::int32_t> named_;   // the program's event names → a recipe's row; −1: none
    std::vector<std::uint32_t> counts_; // a step's scratch, event × material
    std::vector<float>         places_, most_;
    std::vector<std::int64_t>  heard_at_;  // event × material: the sample it last began at, from a world's steps
    std::vector<std::int64_t>  again_;     // an event: samples
};

}  // namespace xengine::audio::matter
