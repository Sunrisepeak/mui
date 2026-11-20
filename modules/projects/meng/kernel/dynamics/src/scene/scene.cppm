// meng.dynamics.scene — bodies and particles in one air (the M6.5 design's T5.7): the object carrier's bodies
// and a cloud of particles as two groups of a field's guests in a single step — each particle beside the cell it
// is in, and beside the body whose surface it lies against (a drop on a burning crate takes its heat), the wind
// where each is carrying the particles.
//
//     rigid3::Scene scene;
//     scene.advance(world, objects, cloud, air);   // the whole tick: pushes, the world, the drops, matter
//
// or its parts, where a consumer puts something between them (its own surfaces, a wall that is no body):
//
//     world.step();
//     cloud.fly(scene.surfaces(world), scene.winds(air, cloud));        // the drops meet the bodies where they are
//     scene.step(world, objects, cloud, scene.surface_bodies(), air);  // one tick of matter for all of it
//
// `surface_bodies[s]` is the body the cloud's surface `s` is (one not carried — the ground, a wall — is met and
// has no matter). A scene is a value: copied with a branch, it shares the bodies' meshes, which never change.
export module meng.dynamics.scene;

import std;
import mlibs.geom;
export import meng.dynamics.objects;
export import meng.particles;
export import meng.field;

export namespace meng::dynamics::rigid3 {

class Scene {
public:
    // The wind where each particle is, for `Cloud::fly`.
    [[nodiscard]] std::span<const mlibs::math::Vec3> winds(const meng::field::Field& air, const meng::particles::Cloud& cloud);
    // One tick of matter: the bodies (group 0) and the cloud (group 1) as guests of the field, each particle beside
    // its cell and beside the carried body it lies against. Events and causes as each carrier keeps them.
    [[nodiscard]] std::expected<meng::matter::Report, meng::matter::Failure> step(const World& world, Objects& objects, meng::particles::Cloud& cloud,
                                                                                        std::span<const BodyId> surface_bodies, meng::field::Field& air);
    // The pairs between a particle and a body of the last step.
    [[nodiscard]] std::span<const meng::field::Between> between() const noexcept { return between_; }
    // What made a body or a particle what it is, back to where it began, across both (a drop boiled by the crate
    // it lay on, the crate lit by …): matter::Causes::explain over each carrier's causes.
    [[nodiscard]] static std::vector<meng::matter::Explained> explain(meng::matter::Who who, const Objects& objects, const meng::particles::Cloud& cloud, std::size_t depth = 32,
                                                                         std::optional<std::uint32_t> about = std::nullopt);

    // The surfaces a cloud's particles meet: every body of the world by the very shape it collides by
    // (`World::triangles`, made into a mesh and its tree once a body and shared, unchanged, by every copy of the
    // scene), where it is now; `surface_bodies()` says which body each is. A compound whose parts were moved
    // (`World::move_part`) is taken again after `forget(body)`.
    [[nodiscard]] std::span<const meng::particles::Surface> surfaces(const World& world);
    [[nodiscard]] std::span<const BodyId> surface_bodies() const noexcept { return surface_bodies_; }
    void forget(BodyId body) { shaped_.erase(body); }
    // One whole tick of a scene: the field's wind and any blast pushing the bodies (`Objects::blow`, `thrown`),
    // the world's step, the particles' flight in the wind against the bodies where they are now, then one tick of
    // matter for all of it. Nothing allocated on a steady tick (no body new since the last).
    [[nodiscard]] std::expected<meng::matter::Report, meng::matter::Failure> advance(World& world, Objects& objects, meng::particles::Cloud& cloud, meng::field::Field& air);

private:
    struct Shaped {
        mlibs::geom::Mesh mesh;
        mlibs::geom::Bvh  tree;
    };
    std::vector<mlibs::math::Vec3>                    winds_;
    std::vector<meng::field::Between>              between_;
    std::map<BodyId, std::shared_ptr<const Shaped>>   shaped_;
    std::vector<meng::particles::Surface>          surfaces_;
    std::vector<BodyId>                               surface_bodies_;
    std::vector<mlibs::math::Vec3>                    corners_;
};

}  // namespace meng::dynamics::rigid3
