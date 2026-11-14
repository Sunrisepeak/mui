// xengine.particles — things that fly in 3D, a carrier of matter (the foundation plan's §6.4, D-166).
//
//     auto cloud = particles::Cloud::make(program, {.ambient = *program->material("air"), .ambient_kelvin = 293});
//     cloud->emit(sparks);                     // where, how fast, how big, how long, of what, how hot
//     cloud->step(world);                      // fly, meet the world's triangles, then one tick of matter's laws
//     for (const particles::Landed& one : cloud->landed()) hear(one);   // what struck something, how fast
//     auto branch = *cloud;                    // a copy is a branch: the same bits from here on
//
// A step: each particle's speed takes gravity and loses its drag, it moves, and where its path meets one of the
// world's triangles first it stops there and goes on reflected — its speed into the surface kept by its
// material's `restitution`, along it less by its `friction` — and that is a landing. Then it ages, and one that
// has lived its life is gone. Then the laws: every particle is an instance of matter, beside those within
// `near` of it (weight 1; at most `most_near`, the nearest first) and beside the air about it (a sample of the
// air for each, at `ambient_kelvin`, set again each step: the air is a bath), so a spark cools, wood near a hot one lights, water near a fire boils — no law here.
//
// Determinism: + − × ÷ and sqrt in a fixed order, no contraction; neighbours found by cells and taken in order;
// the same spawns are the same bits on every machine and every run.
export module xengine.particles;

import std;
export import mlibs.math;
export import mlibs.geom;
export import xengine.matter;

export namespace xengine::particles {

using mlibs::math::Mat4;
using mlibs::math::Vec3;

struct Options {
    Vec3          gravity{0, -9.8f, 0};  // m/s²
    float         step = 1.0f / 60.0f;  // seconds a step
    float         drag = 0.1f;          // of its speed a particle loses a second to the air
    float         near = 0.05f;         // m: particles nearer than this are beside each other
    // At most this many beside each (the nearest first): what keeps a law's flow within what a particle has —
    // matter refuses a step where the shares beside an instance could take more than it holds.
    std::uint32_t most_near = 6;
    std::uint32_t most = 16384;         // at once; past it, what is emitted is not
    // The air about them: a material and how hot it is (none: no bath, they keep their heat but among themselves).
    xengine::matter::MaterialId ambient = xengine::matter::none;
    float                       ambient_kelvin = 293;
    std::uint32_t               ambient_weight = 1;  // how much of a neighbour the air is
};
struct Spawn {
    Vec3                         at{0, 0, 0}, velocity{0, 0, 0};
    float                        size = 0.02f;  // m across (what is drawn)
    float                        life = 1;      // seconds
    xengine::matter::MaterialId  material = 0;
    std::optional<float>         kelvin;        // its temperature; none: its material's own start
};
// What the particles meet: a mesh's triangles, its tree (`mlibs.geom`'s `bvh_of`), where it is.
struct Surface {
    const mlibs::geom::Mesh* mesh = nullptr;
    const mlibs::geom::Bvh*  tree = nullptr;
    Mat4                     place;
};
// A particle that struck a surface this step: what a sound or a mark is made of.
struct Landed {
    std::uint32_t               particle = 0;  // its id
    xengine::matter::MaterialId material = 0;
    float                       speed = 0;     // m/s into the surface
    Vec3                        at{0, 0, 0}, normal{0, 1, 0};
};

class Cloud {
public:
    [[nodiscard]] static std::expected<Cloud, std::string> make(std::shared_ptr<const xengine::matter::Program> program, const Options& options = {});

    // Each spawn a particle (in order, numbered on from the last); past `most`, the rest are not. How many were.
    std::uint32_t emit(std::span<const Spawn> spawns);
    // One step against the world's surfaces.
    [[nodiscard]] std::expected<void, std::string> step(std::span<const Surface> world);

    [[nodiscard]] std::size_t size() const noexcept { return at_.size(); }
    [[nodiscard]] std::span<const Vec3> at() const noexcept { return at_; }
    [[nodiscard]] std::span<const Vec3> velocity() const noexcept { return velocity_; }
    [[nodiscard]] std::span<const float> sizes() const noexcept { return size_; }
    [[nodiscard]] std::span<const float> ages() const noexcept { return age_; }
    [[nodiscard]] std::span<const float> lives() const noexcept { return life_; }  // seconds each is given
    [[nodiscard]] std::span<const std::uint32_t> ids() const noexcept { return id_; }
    // Its state as matter keeps it: channel-major ([channel][particle]), `size()` a channel.
    [[nodiscard]] std::span<const std::uint32_t> state() const noexcept { return state_; }
    [[nodiscard]] xengine::matter::MaterialId material(std::size_t particle) const noexcept { return state_[particle]; }
    // A channel's value of a particle in its unit (temperature in kelvin).
    [[nodiscard]] double value(std::uint32_t channel, std::size_t particle) const noexcept;
    [[nodiscard]] std::span<const Landed> landed() const noexcept { return landed_; }
    // What matter's laws did this step, each naming a particle by its place (`ids()[event.instance]`).
    [[nodiscard]] std::span<const xengine::matter::Event> events() const noexcept { return events_; }
    [[nodiscard]] const xengine::matter::Program& program() const noexcept { return *program_; }
    [[nodiscard]] std::uint64_t steps() const noexcept { return steps_; }
    // Every particle's every number, hashed: two clouds that agree have the same.
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;

private:
    std::shared_ptr<const xengine::matter::Program> program_;
    Options                         options_;
    std::uint32_t                   channels_ = 0, temperature_ = 0;
    std::vector<Vec3>               at_, velocity_;
    std::vector<float>              size_, age_, life_;
    std::vector<std::uint32_t>      id_, state_;
    std::uint32_t                   next_id_ = 0;
    std::uint64_t                   steps_ = 0;
    std::vector<Landed>             landed_;
    std::vector<xengine::matter::Event> events_;
    // Room a step reuses.
    std::vector<std::uint32_t>          input_, output_;
    std::vector<xengine::matter::Pair>  pairs_;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> cells_;
    struct Candidate {
        float         apart = 0;
        std::uint32_t one = 0, other = 0;
    };
    std::vector<Candidate>             candidates_;
    struct Facing {
        const Surface* surface = nullptr;
        Mat4           back, normals;
    };
    std::vector<Facing>                facing_;
    std::vector<std::size_t>           kept_;
    std::vector<std::uint32_t>         degree_;
    xengine::matter::Workspace          workspace_;
};

}  // namespace xengine::particles
