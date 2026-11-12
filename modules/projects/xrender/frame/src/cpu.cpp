module xrender.frame;

import std;

namespace xrender {

const FrameReport& CpuFrame::render(std::span<const FrameLayer> layers, raster::Surface& target, const FrameOptions& options) {
    if (renderers_.size() < layers.size()) renderers_.resize(layers.size());
    report_.width = target.width();
    report_.height = target.height();
    report_.layers.resize(layers.size());
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const FrameLayer& layer = layers[index];
        LayerReport& said = report_.layers[index];
        said.name.assign(layer.name);
        said.object_ids = layer.object_ids && layer.list;
        said.report = {};
        if (!layer.list) continue;
        CpuOptions how;
        how.scale = layer.scale;
        how.object_ids = layer.object_ids;
        how.clear = options.clear && index == 0;
        how.background = options.background;
        how.rows = options.rows;
        how.parallel = options.parallel;
        how.rows_per_task = options.rows_per_task;
        how.measure = options.measure;
        how.transform = layer.transform;
        how.effect_scale = layer.effect_scale;
        said.report = renderers_[index].render(*layer.list, target, layer.resources, how);
    }
    return report_;
}

std::span<const std::uint32_t> CpuFrame::object_ids(std::size_t layer) const noexcept {
    if (layer >= report_.layers.size() || !report_.layers[layer].object_ids) return {};
    return renderers_[layer].object_ids();
}

PixelSource CpuFrame::at(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= report_.width || y >= report_.height) return {};
    for (std::size_t index = report_.layers.size(); index-- > 0;) {
        if (!report_.layers[index].object_ids) continue;
        if (const std::uint32_t object = renderers_[index].object_at(x, y, report_.width)) return {static_cast<std::int32_t>(index), object};
    }
    return {};
}

}  // namespace xrender
