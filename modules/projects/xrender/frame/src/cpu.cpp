module xrender.frame;

import std;

namespace xrender {

const FrameReport& CpuFrame::render(std::span<const FrameLayer> layers, raster::Surface& target, const FrameOptions& options) {
    if (renderers_.size() < layers.size()) renderers_.resize(layers.size());
    report_.width = target.width();
    report_.height = target.height();
    report_.layers.resize(layers.size());
    pictures_.assign(layers.size(), nullptr);
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const FrameLayer& layer = layers[index];
        LayerReport& said = report_.layers[index];
        said.name.assign(layer.name);
        said.object_ids = layer.object_ids && layer.list;
        said.report = {};
        if (const PictureLayer* picture = layer.picture; picture && !layer.list) {
            // A picture of another size is not this frame's: not drawn, and it says so by drawing no row.
            if (picture->width != target.width() || picture->height != target.height() ||
                picture->rgba.size() != static_cast<std::size_t>(picture->width) * static_cast<std::size_t>(picture->height) * 4)
                continue;
            pictures_[index] = picture;
            said.object_ids = picture->objects.size() == static_cast<std::size_t>(picture->width) * static_cast<std::size_t>(picture->height);
            const bool under = options.clear && index == 0;  // the first layer: over the background
            const auto lay = [&](int top, int bottom) {
                for (int y = std::max(top, 0); y < std::min(bottom, target.height()); ++y) {
                    raster::Rgba8* row = target.row(y);
                    const std::uint8_t* from = picture->rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(picture->width) * 4;
                    for (int x = 0; x < picture->width; ++x, from += 4) {
                        const std::uint32_t alpha = from[3];
                        const raster::Rgba8 below = under ? raster::Rgba8{} : row[x];
                        const auto over = [&](std::uint32_t source, std::uint32_t destination) {
                            return static_cast<std::uint8_t>((source * alpha + 127) / 255 + (destination * (255 - alpha) + 127) / 255);
                        };
                        row[x] = {over(from[0], below.r), over(from[1], below.g), over(from[2], below.b), static_cast<std::uint8_t>(alpha + (below.a * (255 - alpha) + 127) / 255)};
                    }
                    ++said.report.rows;
                }
            };
            if (options.rows.empty()) lay(0, target.height());
            else
                for (const RowSpan rows : options.rows) lay(rows.top, rows.bottom);
            continue;
        }
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
    if (pictures_[layer]) return pictures_[layer]->objects;
    return renderers_[layer].object_ids();
}

void CpuFrame::describe(std::size_t layer, int x, int y, std::string& members) const {
    if (layer < pictures_.size() && pictures_[layer] && pictures_[layer]->describe && x >= 0 && y >= 0 && x < report_.width && y < report_.height) pictures_[layer]->describe(x, y, members);
}

PixelSource CpuFrame::at(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= report_.width || y >= report_.height) return {};
    for (std::size_t index = report_.layers.size(); index-- > 0;) {
        if (!report_.layers[index].object_ids) continue;
        if (const PictureLayer* picture = pictures_[index]) {
            if (const std::uint32_t object = picture->objects[static_cast<std::size_t>(y) * static_cast<std::size_t>(report_.width) + static_cast<std::size_t>(x)])
                return {static_cast<std::int32_t>(index), object};
            continue;
        }
        if (const std::uint32_t object = renderers_[index].object_at(x, y, report_.width)) return {static_cast<std::int32_t>(index), object};
    }
    return {};
}

}  // namespace xrender
