// meng.dynamics.objects — matter carried by 3D bodies (the M6.5 design's T5.1; the foundation plan's §6.4: the
// object carrier). Each body given a material is one instance of meng.matter; two bodies touching now are a
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
// is meng.matter's reference — the same bits everywhere.
export module meng.dynamics.objects;

import std;
export import meng.dynamics.rigid3;
export import meng.matter;
export import meng.field;

export namespace meng::dynamics::rigid3 {

namespace matter = meng::matter;

struct ObjectsOptions {
    float         margin = 0.02f;  // m: bodies this close touch (resting ones sit a hair apart)
    // A pair's weight from the surface it shares: `unit` m² a weight of 1, at most `most`. The laws' rates are made
    // for a lattice's cell (a face a neighbour, weight 1, a handful of neighbours), and the kernel refuses a step
    // where what is beside an instance could take more than it has — so the weight stays 1 unless a definition's
    // rates leave room, and a body touching many (the ground under a yard) is not one instance: the field carrier
    // and terrain patches carry it (T5.2, T5.6).
    float         unit = 0.25f;
    std::uint32_t most = 1;
    // The air about each body, as meng.particles has it: a sample of a material at a temperature beside every
    // body, set again each step (a bath) — so a fire has its oxygen and a hot thing cools. None: no bath. The
    // field carrier (meng.field, T5.2) gives each body the air where it is instead.
    matter::MaterialId ambient = matter::none;
    float              ambient_kelvin = 293;
    std::uint32_t      ambient_weight = 1;
    // How much a body holds (matter's capacity): its mass over this, at least 1 — a 600 kg crate warms twelve times
    // slower than a cell of the laws' scale does; a tuft of grass as fast. 0: every body 1.
    float              capacity_unit = 50;  // kg
    // How the wind pushes a body (`blow`): F = ½ ρ C A |w − v| (w − v), A the area its box shows to the wind.
    float              air_density = 1.2f;  // kg/m³
    float              drag = 1.0f;         // C
};

// The bodies as matter's `Who`: carrier 1, a body by its id — what their causes are kept by (matter::Causes),
// so that an explanation runs on from a body into a particle or a cell of the air and back.
inline constexpr std::uint32_t bodies_carrier = 1;
[[nodiscard]] inline matter::Who who(BodyId body) noexcept { return {bodies_carrier, body.value}; }
[[nodiscard]] inline BodyId body_of(matter::Who who) noexcept { return who.carrier == bodies_carrier ? BodyId{static_cast<decltype(BodyId::value)>(who.id)} : BodyId{}; }

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
    // A quantity applied to a body (heat, water, charge): its channel set, kept as a cause (`applied`); false where
    // `get` would say none.
    bool set(BodyId body, std::uint32_t channel, std::uint32_t value);
    [[nodiscard]] std::optional<matter::MaterialId> material(BodyId body) const noexcept;

    // One tick of matter between the bodies touching now in `world` (after its step). What became of each body
    // is in `events`, by body (`body_of`).
    [[nodiscard]] std::expected<matter::Report, matter::Failure> step(const World& world);
    // The same, in the air of a field instead of a bath: each body a guest of the cell it is in (cells made about
    // it as needed), the field's laws and wind stepped with it — what is about a body is the air where it is.
    [[nodiscard]] std::expected<matter::Report, matter::Failure> step(const World& world, meng::field::Field& air);
    // That step in two parts, for a field step that has other guests too (particles: meng.dynamics.scene):
    // the bodies as guests (cells made about them), and — after the field's step — what became of them, as
    // group `group` of its guests.
    [[nodiscard]] meng::field::Guests guests_in(const World& world, meng::field::Field& air);
    // `others`: who another guest of that step is (a particle), so a cause can name it.
    void joined(const meng::field::Field& air, std::size_t group, const std::function<matter::Who(std::uint32_t instance)>& others = {});
    // The wind's push on each body for the world's next step (before `world.step()`): the field's wind where it is
    // against how it moves — an updraft lifts a light thing, a gale shoves a crate.
    void blow(World& world, const meng::field::Field& air) const;
    // What the motion laws say moves things down a channel's slope (the standard's `pressure throws`), on bodies:
    // each pushed by the difference of that channel across it (the air just outside each pair of opposite faces,
    // above the law's `from`) times the face's area — for a pressure, pascals × m² = newtons — times the law's
    // `force` where it says one. A blast beside a crate throws it. Before `world.step()`, as `blow`.
    void thrown(World& world, const meng::field::Field& air) const;

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
    // Whether nothing is above a body for `sky` metres (a ray up from its top): what rain and lightning ask.
    [[nodiscard]] bool open_to_sky(const World& world, BodyId body, float sky = 60) const noexcept;
    [[nodiscard]] std::span<const matter::Event> events() const noexcept { return events_; }
    // The body an instance of the last step is (none: the air beside one).
    [[nodiscard]] BodyId body_of(std::uint32_t instance) const noexcept { return instance < bodies_before_.size() ? bodies_before_[instance] : BodyId{}; }
    // What met something in the world's last step (contacts that began, sensors aside), each carried body that
    // did, its material and how fast it closed (m/s), into `out` (emptied first): what a sound of matter hears
    // as landings (`meng.audio.matter`'s `hear(impacts, …)`) — a new material has its own without new code.
    void impacts(const World& world, std::vector<matter::Impact>& out) const;
    // The pairs the last step read, by instance: who was touching whom, and how much.
    [[nodiscard]] std::span<const matter::Pair> pairs() const noexcept { return pairs_; }
    // Why a body is what it is after the last step: every law judged, every flow named by the body it came from
    // (instances: `body_of`). None before a step, or for a body not carried then.
    [[nodiscard]] std::optional<matter::Why> why(BodyId body) const;
    // After a step in a field: asked of that field (the state holds no pointer to it — a copy is a branch).
    [[nodiscard]] std::optional<matter::Why> why(BodyId body, const meng::field::Field& air) const;

    // What made a body what it is now, back to where it began: its latest cause, then the cause of the body that
    // gave it what it needed (before that step), and so on — at most `depth` links, none twice. Across carriers
    // (a body warmed by a particle): matter::Causes::explain with each carrier's `causes`.
    [[nodiscard]] std::vector<matter::Explained> explain(BodyId body, std::size_t depth = 16) const;
    [[nodiscard]] std::span<const matter::Cause> causes(BodyId body) const noexcept { return causes_.of(who(body)); }
    [[nodiscard]] const matter::Causes& causes() const noexcept { return causes_; }
    [[nodiscard]] std::uint64_t steps() const noexcept { return steps_; }

    [[nodiscard]] std::uint64_t state_hash() const noexcept;
    [[nodiscard]] const matter::Program& program() const noexcept { return *program_; }

private:
    [[nodiscard]] std::optional<std::size_t> index(BodyId body) const noexcept;
    void weigh(const World& world, std::size_t instances);  // capacity_: the bodies' by mass, then 1 each
    // After a step: each event of a body judged again, its cause kept (`why` gives the law and the inflows).
    void witness(const std::function<std::optional<matter::Why>(std::uint32_t instance)>& why_of, const std::function<matter::CameFrom(std::uint32_t instance)>& source_of);

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
    std::vector<meng::field::Vec3> at_;          // where each body is (a field step's guests)
    std::vector<std::int32_t>  residue_, step_residue_;  // [transfer][body]: what transfers left each short of a whole unit (state); a step's with the bath
    std::vector<std::uint32_t> capacity_, exposed_;  // a step's: each instance's (exposed: of it, what reactions run over)
    bool                         in_field_ = false; // the last step was a field's (its why is the field's, asked with it)
    std::uint32_t                group_ = 0;        // which group of its guests the bodies were
    std::uint64_t              steps_ = 0;
    matter::Causes             causes_;
};

}  // namespace meng::dynamics::rigid3
