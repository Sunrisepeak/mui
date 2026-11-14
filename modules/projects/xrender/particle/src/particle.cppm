// xrender.particle — particles drawn: the CPU reference (the foundation plan's §5.1, M6.4; D-184).
//
//     const std::array<particle::Batch, 2> batches{
//         particle::Batch{.particles = sparks, .blend = particle::Blend::add, .soft = 0.05f, .id = 40},
//         particle::Batch{.particles = smoke, .blend = particle::Blend::over, .soft = 0.3f, .sprite = &puff, .id = 41}};
//     mesh_pass.draw(world, seen, view, assets, frame);   // the meshes first: their light and depth
//     particle::draw(batches, view, frame);               // then these, over the light, faded at the surfaces
//     glow.show(frame.light, …);                           // then shown
//
// A particle is a square facing the eye, `size` across, turned about the way to the eye by `turn`; its shape is
// a round spot soft at its edge, or a picture's (`Batch::sprite`: its light times the particle's colour, its
// cover how much is there). A batch's particles are added to the light (`add`: sparks, fire, glints — their
// order does not change what is seen) or laid over it by how much they hide (`over`: smoke, dust — the farthest
// first, then nearer; at one distance, in the batch's order). Where a particle comes within `soft` of what is
// behind it (the frame's depth) it fades to nothing, so a puff meeting a wall has no hard line. Nothing is
// written but `light` and `clear`: particles are not a surface — they leave the depth, the normal, the velocity
// and who drew a pixel to the meshes.
//
// Determinism: + − × ÷ and sqrt in a fixed order, no contraction; a pixel takes a particle where its middle is
// inside the particle's square; the same bits on every machine.
export module xrender.particle;

import std;
export import xrender.mesh;

export namespace xrender::particle {

using mlibs::math::Vec3;
using mlibs::math::Vec4;

struct Particle {
    Vec3          at{0, 0, 0};    // its middle, in the world
    float         size = 0.1f;    // across, in the world's units
    Vec4          colour{1, 1, 1, 1};  // its light (linear, not held to 1: a spark brighter than white glows); w: how much it hides (`over`)
    float         turn = 0;       // radians about the way to the eye
    std::uint32_t id = 0;         // its carrier's number for it, said back by `pick`
};
enum class Blend : std::uint8_t { add, over };
struct Batch {
    std::span<const Particle> particles;
    Blend                     blend = Blend::add;
    float                     soft = 0.1f;  // world units: nearer than this to what is behind it, it fades (0: cut where it goes in)
    const mesh::Texture*      sprite = nullptr;  // its shape and light; none: a round spot
    scene::Id                 id = 0;        // the batch's: an emitter's, said back by `pick`
};
struct Drawn {
    std::uint32_t particles = 0;  // looked at
    std::uint32_t outside = 0;    // behind the eye or wholly off the frame
    std::uint64_t pixels = 0;     // pixels a particle reached
};
// The batches drawn over `frame`'s light (`frame` as a mesh pass left it: its depth is what they fade at; a
// frame with no depth is as empty as far as they see). `view` is the frame's.
Drawn draw(std::span<const Batch> batches, const scene::View& view, mesh::Targets& frame);

// Which particle is at a pixel of a view (`x`, `y` in pixels; a pixel's middle at +0.5), found without a frame:
// the nearest whose square holds the point and whose shape is there (more than half its cover). What an agent
// asks of a spark it sees.
struct Picked {
    scene::Id     batch = 0;
    std::uint32_t particle = 0;  // its id
    float         distance = 0;  // from the eye
};
[[nodiscard]] std::optional<Picked> pick(std::span<const Batch> batches, const scene::View& view, float x, float y, int width, int height);

}  // namespace xrender::particle
