// openmui.showcase.widget_breakout:fx — what the play looks like beyond the
// state: sparks from what breaks, short trails, a ball squashed for a few
// frames after it bounces, a paddle squashed by a return, beams fading,
// pulses running along a circuit, numbers rising from a banked chain, the
// shutdown's ring, a restrained shake. All derived from the steps' events
// and states, one step at a time (so the same play draws the same frames),
// in fixed pools (nothing allocated a frame), and none of it in the
// simulation: a rewind clears it. And the ghost of a rewound shot.
export module openmui.showcase.widget_breakout:fx;

import std;
import openmui;
import openmui.showcase.widget_breakout.sim;
import :rooms;

export namespace omui::breakout {

// The colours of play: the same under every style, so a cue reads the same.
namespace ink {
inline constexpr std::uint32_t ball = 0xf5f7ff, pierce = 0xff7043, paddle = 0xe8eaf6, wide = 0x4fc3f7, magnet = 0xb388ff;
inline constexpr std::uint32_t wire = 0x3a4a6b, live = 0x64ffda, beam = 0xfff59d, core = 0xff5252, charge = 0xffd54f;
inline constexpr std::uint32_t spark = 0xffc94d, shutter = 0x8fa3c7, kicker = 0x64ffda, lost = 0xff6e6e, cache = 0xffd54f;
}  // namespace ink

// The colour sparks of a brick of this kind fly in.
[[nodiscard]] Color kind_color(Kind k);

class Fx {
public:
    struct Particle {
        float x = 0, y = 0, vx = 0, vy = 0, life = 0, span = 1, size = 2;
        Color color;
    };
    struct Trail {
        std::uint32_t            ball = 0xffffffffu, generation = 0;  // whose (an entity of the world)
        std::array<Point, 10>    at{};                                 // the latest first
        std::uint32_t            count = 0;
        float                    squash = 0;                           // just bounced: 1, back to round
        Point                    dir{0, -1};
        std::int64_t             seen = -1;                            // the step it was last seen
    };
    struct Beam {
        Point        from, to;
        float        life = 0;
        std::int32_t n = 0;
    };
    struct Pulse {
        Point from, to;
        float life = 0;
    };
    struct Floater {
        std::array<char, 40> text{};
        Point                at;
        float                life = 0, size = 18;
        Color                color;
        [[nodiscard]] std::string_view view() const noexcept { return {text.data()}; }
    };
    struct Ring {
        Point at;
        float life = 0, radius = 0, grow = 0;
        Color color;
    };

    // A step played forward: a step older, then its events and its balls.
    void react(const State& s, const RoomDef& room);
    // A step older, nothing new (a room over).
    void age();
    // Everything gone (a rewind, a new room); the ball's recent path kept for the ghost.
    void clear();
    // A rewind from frame `from` back to `to`: the path the ball took between, as a ghost.
    void ghost_from(std::int64_t to, std::int64_t from);

    std::array<Particle, 384>  particles{};
    std::array<Trail, 16>      trails{};
    std::array<Beam, 8>        beams{};
    std::array<Pulse, 16>      pulses{};
    std::array<Floater, 12>    floaters{};
    std::array<Ring, 6>        rings{};
    std::array<float, 128>     flash{};          // by brick id: just struck, 1 down to 0
    std::array<float, 2>       paddle_squash{};
    float                      shake = 0;        // pixels, dying away
    float                      chain_pop = 0;    // the chain's counter just grew
    float                      banked = 0;       // sparks just banked: the meter glows
    std::int64_t               frame = 0;
    std::array<Point, 128>     ghost{};          // a rewound shot's path
    std::size_t                ghost_count = 0;
    float                      ghost_life = 0;

private:
    void burst(Point at, Color c, int n, float speed, std::uint64_t seed, float size = 2.5f);
    void floater(Point at, Color c, float size, std::string_view text);
    void ring(Point at, Color c, float radius, float grow, float life);
    std::size_t                next_particle_ = 0, next_floater_ = 0, next_ring_ = 0, next_beam_ = 0, next_pulse_ = 0;
    std::array<Point, 512>     path_{};          // the first flying ball, by frame (frame % 512)
    std::array<std::int64_t, 512> path_frame_{};
    std::int32_t               last_combo_ = 0;
};

}  // namespace omui::breakout
