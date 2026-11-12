module xrender.frame;

import std;
import mlibs.rhi;

namespace xrender {

mlibs::rhi::Result<GpuFrame> GpuFrame::create(mlibs::rhi::Device& device) {
    auto renderer = GpuRenderer::create(device);
    if (!renderer) return std::unexpected(renderer.error());
    return GpuFrame(std::move(*renderer));
}

mlibs::rhi::Device& GpuFrame::device() const noexcept { return renderer_.device(); }

mlibs::rhi::Result<const FrameReport*> GpuFrame::render(std::span<const FrameLayer> layers, mlibs::rhi::Texture target, std::uint32_t width, std::uint32_t height,
                                                        const GpuFrameOptions& options) {
    report_.width = static_cast<int>(width);
    report_.height = static_cast<int>(height);
    report_.layers.resize(layers.size());
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const FrameLayer& layer = layers[index];
        LayerReport& said = report_.layers[index];
        said.name.assign(layer.name);
        said.object_ids = false;  // the device keeps none yet: a pixel's source is the reference renderer's (CpuFrame)
        said.report = {};
        if (!layer.list) continue;
        GpuOptions how;
        how.scale = layer.scale;
        how.clear = options.clear && index == 0;
        how.background = options.background;
        how.external = options.external;
        auto drawn = renderer_.render(*layer.list, target, layer.resources, how);
        if (!drawn) return std::unexpected(drawn.error());
        said.report = *drawn;
    }
    return &report_;
}

}  // namespace xrender
