// xengine.dynamics.scene — bodies and particles in one air (the M6.5 design's T5.7): the object carrier's bodies
// and a cloud of particles as two groups of a field's guests in a single step — each particle beside the cell it
// is in, and beside the body whose surface it lies against (a drop on a burning crate takes its heat), the wind
// where each is carrying the particles.
//
//     rigid3::Scene scene;
//     world.step();
//     cloud.fly(surfaces, scene.winds(air, cloud));         // the step's flight in the field's wind
//     scene.step(world, objects, cloud, surface_bodies, air);  // one tick of matter for all of it
//
// `surface_bodies[s]` is the body the cloud's surface `s` is (none: the ground, a wall — not carried).
export module xengine.dynamics.scene;

import std;
export import xengine.dynamics.objects;
export import xengine.particles;
export import xengine.field;

export namespace xengine::dynamics::rigid3 {

class Scene {
public:
    // The wind where each particle is, for `Cloud::fly`.
    [[nodiscard]] std::span<const mlibs::math::Vec3> winds(const xengine::field::Field& air, const xengine::particles::Cloud& cloud);
    // One tick of matter: the bodies (group 0) and the cloud (group 1) as guests of the field, each particle beside
    // its cell and beside the carried body it lies against. Events and causes as each carrier keeps them.
    [[nodiscard]] std::expected<xengine::matter::Report, xengine::matter::Failure> step(const World& world, Objects& objects, xengine::particles::Cloud& cloud,
                                                                                        std::span<const BodyId> surface_bodies, xengine::field::Field& air);
    // The pairs between a particle and a body of the last step.
    [[nodiscard]] std::span<const xengine::field::Between> between() const noexcept { return between_; }
    // What made a body or a particle what it is, back to where it began, across both (a drop boiled by the crate
    // it lay on, the crate lit by …): matter::Causes::explain over each carrier's causes.
    [[nodiscard]] static std::vector<xengine::matter::Explained> explain(xengine::matter::Who who, const Objects& objects, const xengine::particles::Cloud& cloud, std::size_t depth = 32);

private:
    std::vector<mlibs::math::Vec3>      winds_;
    std::vector<xengine::field::Between> between_;
};

}  // namespace xengine::dynamics::rigid3
