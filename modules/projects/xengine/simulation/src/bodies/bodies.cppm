// xengine.simulation.bodies — soft bodies in 2D, deterministic (plan §6.9:
// the particle and constraint solver animation and physics share).
//
// A body is a ring of particles held by its edges' lengths, its enclosed
// area and the shape it was made in (shape matching), stepped by small-step
// XPBD under gravity against half-plane colliders, with restitution and
// friction. One step is a pure function of the state and the input (a pin,
// a dragging hand), so the body runs in a Timeline: checkpoints, replay and
// seeking come with it.
//
//     auto body = xengine::simulation::SoftBody::ring(outline, {.mass = 1}, {{.normal_y = -1,
//     .offset = -400}}); xengine::simulation::Timeline<xengine::simulation::SoftBodyModel>
//     history(*body); history.step({});                                         // 1/120 s on
export module xengine.simulation.bodies;

import std;

export namespace xengine::simulation {

// The half-plane normal · p ≥ offset (a ground at y = 400 on a y-down
// screen: normal (0, −1), offset −400).
struct Plane {
    double normal_x = 0, normal_y = -1;
    double offset = 0;
};
struct BodyParams {
    double mass = 1;  // the whole body's
    double gravity_x = 0, gravity_y = 980;
    double step = 1.0 / 120;  // seconds a step lasts
    int substeps = 8;
    double edge_compliance = 0;     // XPBD compliance (m/N); 0 is rigid
    double area_compliance = 0;     // the enclosed area's (0: incompressible)
    double shape_stiffness = 0.05;  // per substep, 0 to 1: how fast it springs back to its shape
    double upright = 0;             // 0 to 1: how much the shape it springs back to stays unturned (a creature on its feet)
    double restitution = 0.4;       // of the normal speed, off a collider
    double friction = 0.3;          // of the tangential speed lost on contact
    double damping = 0.2;           // of the velocity a second
};
// A dragging hand: one particle pulled to (x, y) while active.
struct Pin {
    std::uint32_t particle = 0;
    double x = 0, y = 0;
    bool active = false;
};
// A floor from above only, between `left` and `right` at `top` (a card's top
// edge on a y-down page): what comes down onto it from above stands on it;
// what is below it or beside it passes by.
struct Ledge {
    double left = 0, right = 0, top = 0;
};
inline constexpr std::size_t max_ledges = 32;
// A step's input: the hand, the ledges near the body (a page's surfaces, as
// many as max_ledges, chosen by whoever drives it), the speed its feet go
// at along what it stands on (walking: friction holds what touches a floor
// to that speed, not to stillness; 0 standing), an acceleration that lasts
// (in the air too) and a velocity added once at the step's start (a hop).
// Fixed size: a history of inputs allocates nothing.
struct BodyInput {
    Pin pin;
    std::array<Ledge, max_ledges> ledges{};
    std::uint32_t ledge_count = 0;
    double walk = 0;
    double push_x = 0, push_y = 0;
    double kick_x = 0, kick_y = 0;
};

enum class BodyError : std::uint8_t {
    too_few_points,
    too_many_points,
    nonfinite,
    degenerate,
    invalid_params
};

// The model's state: plain owned data, copied by snapshots.
struct SoftBody {
    std::vector<double> x, y, velocity_x, velocity_y;
    std::vector<double> rest_x, rest_y;  // the shape made in, about its centre
    std::vector<double> edge_rest;       // edge i: particle i to i + 1
    double rest_area = 0;
    double inverse_mass = 0;  // each particle's
    BodyParams params;
    std::vector<Plane> planes;
    // A step's working space, sized once: stepping allocates nothing.
    std::vector<double> scratch_x, scratch_y, moved_x, moved_y;
    std::vector<std::uint8_t> touching;

    // A body of the closed outline `points` (3 to 4096, in order round it).
    [[nodiscard]] static std::expected<SoftBody, BodyError>
    ring(std::span<const std::array<double, 2>> points, BodyParams params = {},
         std::span<const Plane> planes = {});
    [[nodiscard]] std::size_t size() const noexcept {
        return x.size();
    }
    // The enclosed area now (signed: positive counter-clockwise in a y-up sense).
    [[nodiscard]] double area() const noexcept;
    [[nodiscard]] std::array<double, 2> centre() const noexcept;
};

struct SoftBodyModel {
    using State = SoftBody;
    using Input = BodyInput;
    static void step(State& body, const Input& input) noexcept;
};

}  // namespace xengine::simulation
