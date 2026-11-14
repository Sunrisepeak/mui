// xrender.particle.gpu — particles drawn on a device: xrender.particle's reference through mlibs.rhi alone.
//
//     auto particles = particle::gpu::Pass::make(device);
//     mesh_pass.record(list, world, seen, view, materials, light, depth, width, height, options);   // light: rgba16_float
//     mesh_pass.record_surface(list, world, seen, view, surface, depth, width, height);            // how far each surface is
//     particles->record(list, batches, view, light, surface, width, height);                      // over the light
//
// The same batches as the reference, laid out on the CPU in its order (every particle of every batch, the farthest
// first) as squares facing the eye: one buffer of corners a frame, drawn in runs of one batch. A pixel takes a
// particle where its middle is inside the square, its shape the reference's round spot at its place there, faded
// by how near it is along the eye's ray to the surface behind (`record_surface`'s distance). `add` adds its light;
// `over` lays it on by how much it hides (premultiplied). A batch's picture (`Batch::sprite`) is put on the device
// the first time it is seen (`xrender.mesh.gpu`'s `picture_on`, on the frame's list) and read at the size the
// reference reads it — each particle's texels a pixel, the same mix between two sizes.
export module xrender.particle.gpu;

import std;
export import xrender.particle;
export import xrender.mesh.gpu;
export import mlibs.rhi;
export import :particle_shaders;

export namespace xrender::particle::gpu {

struct Stats {
    std::uint32_t particles = 0, outside = 0, runs = 0;
    std::uint32_t pictured = 0;  // of a batch with a sprite
    std::uint32_t uploaded = 0;  // pictures put on the device this frame (each the first time it is seen)
};

class Pass {
public:
    struct Corner;  // a corner as the device takes it
    // The pipelines: added and laid over, onto an rgba16_float target (a frame's light).
    [[nodiscard]] static mlibs::rhi::Result<Pass> make(mlibs::rhi::Device& device);
    [[nodiscard]] static mlibs::rhi::ShaderFormats shader_formats() noexcept;
    Pass(Pass&& other) noexcept;
    Pass& operator=(Pass&& other) noexcept;
    ~Pass();

    // The batches over `light` (rgba16_float, a colour target: what a mesh pass drew there is kept), faded at the
    // surfaces `surface` says (rgba16_float, sampled: `xrender.mesh.gpu`'s `record_surface` of the same view),
    // both `width` × `height`. Their corners are uploaded on `list` first.
    [[nodiscard]] mlibs::rhi::Result<void> record(mlibs::rhi::CommandList& list, std::span<const Batch> batches, const scene::View& view, mlibs::rhi::Texture light,
                                                  mlibs::rhi::Texture surface, std::uint32_t width, std::uint32_t height);
    [[nodiscard]] const Stats& last() const noexcept { return last_; }
    // A picture is kept on the device by its address; one changed in place (or gone) is let go here and put there
    // again the next time it is seen.
    void forget(const mesh::Texture* sprite) noexcept;

private:
    Pass() = default;
    void release() noexcept;
    struct Held {
        mlibs::rhi::Device* device = nullptr;
        Held() = default;
        Held(mlibs::rhi::Device* on) noexcept : device(on) {}
        Held(Held&& other) noexcept : device(std::exchange(other.device, nullptr)) {}
        Held& operator=(Held&& other) noexcept {
            device = std::exchange(other.device, nullptr);
            return *this;
        }
        mlibs::rhi::Device* operator->() const noexcept { return device; }
        operator mlibs::rhi::Device*() const noexcept { return device; }
    };
    struct Run {
        std::uint32_t batch = 0, first = 0, count = 0;  // corners
    };
    Held                         device_;
    mlibs::rhi::GraphicsPipeline added_, laid_;
    mlibs::rhi::Sampler          nearest_;
    mlibs::rhi::Buffer           corners_;
    std::uint32_t                room_ = 0;  // corners the buffer holds
    std::vector<Corner>          laid_out_;
    std::vector<Run>             runs_;
    struct Sprite {
        mlibs::rhi::Texture texture;
        mlibs::rhi::Sampler sampler;
    };
    std::map<const mesh::Texture*, Sprite> sprites_;
    Stats                        last_;
};

}  // namespace xrender::particle::gpu
