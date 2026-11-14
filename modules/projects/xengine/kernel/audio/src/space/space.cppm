// xengine.audio.space — sound in a place, and what the ears hear of it (the foundation plan's M6.4, S2 and S5).
//
//     auto space = audio::space::Space::make(mixer).value();      // a source of the mixer's: it adds into a bus
//     mixer.add_source(space);
//     const auto shot = space->add(gunshot);                       // made one channel once (`audio::mono`)
//     space->walls(world);                                         // the world's triangles: what a sound passes through
//     space->listen({.at = head, .forward = looking});
//     const auto id = space->emit(shot, muzzle, 1, "rifle fired by player 2").value();
//     mixer.render(block);
//     for (const audio::space::Heard& one : space->heard()) …      // what reached each ear, from where, how loud, through what
//
// One way sound goes from a place to an ear, and everything else follows from it: each ear is a receiver at its
// own place, and hears at each moment the sound as it left the emitter when it was as far from the ear as sound
// goes in the time between — the moment τ with t − τ = |emitter(τ) − ear(t)| ÷ speed, found from where the emitter
// has been (exactly, at each block's two ends; between them, a straight line). So a sound far off arrives late; one coming nearer is read faster than it was made and is higher
// (Doppler), one going away lower; and the ear on the far side, a head's width further, hears it a fraction of a
// millisecond later than the near one (the difference between the ears' times, by which a person tells left from
// right). None of the three is a separate effect. Then, for each ear: the level falls as the distance grows
// (`reference` over the distance, past `reference`); the air takes the highs of a far sound; the head shades the
// far ear (its highs, more as the sound comes from the other side); each surface the sound passes through on its
// way lets `through` of it by, and above its `bright` 6 dB less an octave (the mass law). The levels and filters move smoothly across a block from
// where they were to where they are, so a moving sound does not click.
//
// Hearing is data (S5): `heard()` says, for each sound in the place, its name and cause (what made it, as the one
// who emitted it said), how far and from which way (azimuth to the right, elevation up, in the listener's frame),
// how late it arrives, its pitch against the one made (Doppler), how many surfaces it came through, and how loud
// it is at each ear this block — measured from the samples that reached the ear, not computed beside them.
//
// Determinism: + − × ÷ and sqrt in a fixed order, `mlibs.math`'s det for exp and atan2, no contraction: the same
// emissions and the same listener are the same samples on every machine. One thread: the owner calls `emit`,
// `move`, `listen` and `walls` between the blocks the mixer renders (a game's step and its audio on one thread, or
// offline) — what moves it is the owner's step, not the audio thread.
export module xengine.audio.space;

import std;
export import xengine.audio;
export import mlibs.math;
export import mlibs.geom;

export namespace xengine::audio::space {

using mlibs::math::Mat4;
using mlibs::math::Vec3;

struct Listener {
    Vec3  at{0, 0, 0};
    Vec3  forward{0, 0, -1}, up{0, 1, 0};
    float head = 0.0875f;  // m: from the middle of the head to each ear
};
// What a sound passes through: a mesh's triangles, its tree (`mlibs.geom`'s `bvh_of`), where it is, and how much of
// a sound's amplitude each of its surfaces lets by.
struct Wall {
    const mlibs::geom::Mesh* mesh = nullptr;
    const mlibs::geom::Bvh*  tree = nullptr;
    Mat4                     place;
    float                    through = 0.1f;  // 0.1: −20 dB a surface
    // Above this a surface lets by 6 dB less an octave (the mass law: a heavier wall, a lower one); each further
    // surface on the way halves it.
    float                    bright = 1000;   // Hz
};
struct Options {
    std::string   bus = "sfx";   // the mixer's bus it adds into
    float         speed = 343;   // m/s
    float         reference = 1; // m: nearer than this, a sound is at its own level
    float         air = 100;     // m: at this distance the air has halved the highest the ear hears (20 kHz → 10 kHz)
    std::uint32_t most = 64;     // sounds at once; past it an emission is refused
    std::uint32_t most_walls = 8; // surfaces counted along a sound's way
};
using EmitterId = std::uint32_t;  // 1, 2, 3 … in the order emitted

// How a sound at `at` reaches a listener, by the rule `Space` plays sounds by — a step's numbers, no samples: what a
// game's own ears use (a guard that hears a shot), the same as what reaches a player's.
struct Reach {
    float         distance = 0;    // m, from the middle of the head
    float         azimuth = 0;     // degrees: 0 ahead, 90 right, −90 left, ±180 behind
    float         elevation = 0;   // degrees: up
    float         delay = 0;       // s
    std::uint32_t walls = 0;       // surfaces between
    float         through = 1;     // of its amplitude they let by
    float         bright = 20000;  // Hz: the highest they let by whole
    float         level = 0;       // dB against the sound's own level within `reference`: distance and walls
};
[[nodiscard]] Reach reach(const Listener& listener, Vec3 at, std::span<const Wall> walls, const Options& options = {}) noexcept;

// What the ears heard of one sound, the last block.
struct Heard {
    EmitterId        id = 0;
    std::uint32_t    sound = 0;
    std::string_view name, cause;
    float            distance = 0;          // m, from the middle of the head
    float            azimuth = 0;           // degrees: 0 ahead, 90 right, −90 left, ±180 behind
    float            elevation = 0;         // degrees: up
    float            delay = 0;             // s: how long it takes to arrive
    float            doppler = 1;           // its pitch against the one it was made at
    std::uint32_t    walls = 0;             // surfaces between it and the listener
    float            left = -200, right = -200;  // dBFS: how loud at each ear this block (−200: nothing)
    bool             arrived = false;       // whether any of it has reached an ear
};

class Space final : public Source {
public:
    // On `mixer`'s bus `options.bus`, at its rate.
    [[nodiscard]] static std::expected<std::shared_ptr<Space>, std::string> make(Mixer& mixer, Options options = {});

    // A sound, made one channel once (its pans set aside); its number here.
    std::uint32_t add(const Sound& sound);
    // A sound at a place from the next block, at `gain`; `cause`: what made it (said back by `heard`). Refused past
    // `most` at once, or for a sound not added.
    [[nodiscard]] std::expected<EmitterId, std::string> emit(std::uint32_t sound, Vec3 at, float gain = 1, std::string cause = {});
    // Where a sound is now (a moving one is moved each step); false: no such sound sounding.
    bool move(EmitterId id, Vec3 at) noexcept;
    void listen(const Listener& listener) noexcept { listener_ = listener; }
    // The surfaces sounds pass through (the owner keeps them alive).
    void walls(std::span<const Wall> walls) noexcept { walls_ = walls; }

    void render(std::int64_t clock, std::int32_t frames, std::span<const BusBlock> buses) noexcept override;

    // What was heard in the last block, in the order emitted; valid until the next `emit` or block.
    [[nodiscard]] std::span<const Heard> heard() const noexcept { return heard_; }
    [[nodiscard]] std::size_t sounding() const noexcept;
    [[nodiscard]] std::int32_t rate() const noexcept { return rate_; }

    Space(Options options, std::int32_t rate, std::int32_t bus) : options_(std::move(options)), rate_(rate), bus_(bus) {}

private:
    struct Ear {
        double read = 0;                         // where the last block ended: the sample of the sound heard (from its start)
        float  distance = 0, gain = 0, pull = 0; // how far it was when it left, how loud, the filter's step
        float  held = 0;                         // the filter's last output
    };
    // Where an emitter has been: each place the owner gave it, at the clock it was given for (the next block's
    // start), the latest last (a ring).
    static constexpr std::size_t kept = 128;
    struct Emitter {
        bool          active = false, started = false, moved = true;
        EmitterId     id = 0;
        std::uint32_t sound = 0;
        std::int64_t  start = 0;  // the clock's sample it was made at
        Vec3          at{0, 0, 0};
        float         gain = 1;
        std::string   cause;
        std::array<Ear, 2> ears;
        std::array<std::pair<std::int64_t, Vec3>, kept> been{};
        std::size_t   count = 0;  // places kept (at most `kept`)
    };
    // Where the emitter was at sample `when` (between the places kept; before the first, the first; after the last,
    // on at the speed between the last two for as long as they were apart, then still).
    [[nodiscard]] static Vec3 where(const Emitter& emitter, double when) noexcept;
    // The sample of the sound an ear at `ear` hears at sample `now`, and how far it was when it left.
    [[nodiscard]] std::pair<double, float> heard_at(const Emitter& emitter, Vec3 ear, std::int64_t now) const noexcept;
    Options                         options_;
    std::int32_t                    rate_ = 48000, bus_ = 0;
    std::vector<std::vector<float>> sounds_;
    std::vector<std::string>        names_;
    std::vector<Emitter>            emitters_;
    std::vector<Heard>              heard_;
    std::span<const Wall>           walls_;
    Listener                        listener_;
    EmitterId                       last_ = 0;
};

}  // namespace xengine::audio::space
