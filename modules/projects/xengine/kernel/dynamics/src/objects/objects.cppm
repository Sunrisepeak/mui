// xengine.dynamics.objects — matter carried by 3D bodies (the M6.5 design's T5.1; the foundation plan's §6.4: the
// object carrier). Each body given a material is one instance of xengine.matter; two bodies touching now are a
// pair, weighted by how much surface they share. The laws are the standard definition's, the same as on a lattice
// or on particles: a burning crate heats the crate it leans on, a wet one dries against a warm stone, a block of
// ice beside a fire melts — nothing here names a material or a law.
//
//     auto objects = rigid3::Objects::make(program).value();
//     objects.add(crate, *program.material("wood"));
//     objects.set(crate, *program.channel("temperature"), hot);   // a quantity applied
//     world.step();
//     objects.step(world);                                         // matter across what touches now
//     objects.why(crate);                                          // the last step, for one body, said
//
// The state is a value (copy it with the world: a branch); its hash is part of a world's. Determinism: the pairs
// come in the bodies' id order (`World::touching`), each weight from an area by + × ÷ and rounding, and the step
// is xengine.matter's reference — the same bits everywhere.
export module xengine.dynamics.objects;

import std;
export import xengine.dynamics.rigid3;
export import xengine.matter;
export import xengine.field;

export namespace xengine::dynamics::rigid3 {

namespace matter = xengine::matter;

struct ObjectsOptions {
    float         margin = 0.02f;  // m: bodies this close touch (resting ones sit a hair apart)
    // A pair's weight from the surface it shares: `unit` m² a weight of 1, at most `most`. The laws' rates are made
    // for a lattice's cell (a face a neighbour, weight 1, a handful of neighbours), and the kernel refuses a step
    // where what is beside an instance could take more than it has — so the weight stays 1 unless a definition's
    // rates leave room, and a body touching many (the ground under a yard) is not one instance: the field carrier
    // and terrain patches carry it (T5.2, T5.6).
    float         unit = 0.25f;
    std::uint32_t most = 1;
    // The air about each body, as xengine.particles has it: a sample of a material at a temperature beside every
    // body, set again each step (a bath) — so a fire has its oxygen and a hot thing cools. None: no bath. The
    // field carrier (xengine.field, T5.2) gives each body the air where it is instead.
    matter::MaterialId ambient = matter::none;
    float              ambient_kelvin = 293;
    std::uint32_t      ambient_weight = 1;
    // How the wind pushes a body (`blow`): F = ½ ρ C A |w − v| (w − v), A the area its box shows to the wind.
    float              air_density = 1.2f;  // kg/m³
    float              drag = 1.0f;         // C
};

class Objects {
public:
    [[nodiscard]] static std::expected<Objects, std::string> make(const matter::Program& program, ObjectsOptions options = {});

    // A body made of a material (every channel at its initial value), from the next step; false: already carried,
    // or no such material.
    bool add(BodyId body, matter::MaterialId material);
    bool remove(BodyId body);
    [[nodiscard]] std::size_t size() const noexcept { return bodies_.size(); }
    [[nodiscard]] std::span<const BodyId> bodies() const noexcept { return bodies_; }

    // A channel of a body, as stored (× 2^fraction); none: not carried, or no such channel.
    [[nodiscard]] std::optional<std::uint32_t> get(BodyId body, std::uint32_t channel) const noexcept;
    // A quantity applied to a body (heat, water, charge): its channel set; false where `get` would say none.
    bool set(BodyId body, std::uint32_t channel, std::uint32_t value) noexcept;
    [[nodiscard]] std::optional<matter::MaterialId> material(BodyId body) const noexcept;

    // One tick of matter between the bodies touching now in `world` (after its step). What became of each body
    // is in `events`, by body (`body_of`).
    [[nodiscard]] std::expected<matter::Report, matter::Failure> step(const World& world);
    // The same, in the air of a field instead of a bath: each body a guest of the cell it is in (cells made about
    // it as needed), the field's laws and wind stepped with it — what is about a body is the air where it is.
    [[nodiscard]] std::expected<matter::Report, matter::Failure> step(const World& world, xengine::field::Field& air);
    // The wind's push on each body for the world's next step (before `world.step()`): the field's wind where it is
    // against how it moves — an updraft lifts a light thing, a gale shoves a crate.
    void blow(World& world, const xengine::field::Field& air) const;

    // Weather, each by what things are and where (no material named): rain wets each body open to the sky (a ray
    // straight up from its top meets nothing) by `wetness` (as stored) a step, up to `most` (none: as wet as the
    // wettest material holds — water's `moist`); lightning strikes the highest body open to
    // the sky among those whose material conducts (its `conduct` above 0), giving it `charge` and `kelvin` of heat
    // — none where nothing conducts (it goes to the ground). The body struck, or none.
    void rain(const World& world, std::uint32_t wetness, std::optional<std::uint32_t> most = std::nullopt);
    struct Strike {
        std::uint32_t charge = 1000;
        float         kelvin = 800;   // heat added
        float         sky = 60;       // m: how far up a body's way to the sky must be clear
    };
    std::optional<BodyId> lightning(const World& world, Strike strike);
    std::optional<BodyId> lightning(const World& world) { return lightning(world, Strike{}); }
    [[nodiscard]] std::span<const matter::Event> events() const noexcept { return events_; }
    // The body an instance of the last step is (none: the air beside one).
    [[nodiscard]] BodyId body_of(std::uint32_t instance) const noexcept { return instance < bodies_before_.size() ? bodies_before_[instance] : BodyId{}; }
    // The pairs the last step read, by instance: who was touching whom, and how much.
    [[nodiscard]] std::span<const matter::Pair> pairs() const noexcept { return pairs_; }
    // Why a body is what it is after the last step: every law judged, every flow named by the body it came from
    // (instances: `body_of`). None before a step, or for a body not carried then.
    [[nodiscard]] std::optional<matter::Why> why(BodyId body) const;

    [[nodiscard]] std::uint64_t state_hash() const noexcept;
    [[nodiscard]] const matter::Program& program() const noexcept { return *program_; }

private:
    [[nodiscard]] std::optional<std::size_t> index(BodyId body) const noexcept;
    [[nodiscard]] bool open_to_sky(const World& world, BodyId body, float sky) const noexcept;

    const matter::Program*     program_ = nullptr;
    ObjectsOptions             options_;
    std::vector<BodyId>        bodies_;           // instance order: the bodies' ids, ascending
    std::vector<std::uint32_t> state_;            // channel-major, [channel][instance]: the bodies'
    std::vector<std::uint32_t> input_, output_;   // a step's: the bodies, then their air samples
    std::vector<std::uint32_t> before_;           // the last step's input (for `why`)
    std::vector<BodyId>        bodies_before_;    // and its bodies
    std::vector<matter::Pair>  pairs_;
    std::vector<Touch>         touches_;
    std::vector<matter::Event> events_;
    matter::Workspace          workspace_;
    std::vector<xengine::field::Vec3> at_;          // where each body is (a field step's guests)
    const xengine::field::Field* joined_ = nullptr; // the field of the last step (none: a bath)
};

}  // namespace xengine::dynamics::rigid3
