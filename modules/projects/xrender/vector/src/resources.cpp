module xrender.vector.api;

import std;
import xrender.raster;
import mlibs.geom;

namespace xrender {

ImageRef ImageStore::add(int width, int height, std::vector<xrender::raster::Rgba8> pixels) {
    auto s = std::make_shared<xrender::raster::Surface>(width, height);
    std::ranges::copy(pixels | std::views::take(s->pixels().size()), s->pixels().begin());
    return add(std::shared_ptr<const xrender::raster::Surface>(std::move(s)));
}

ImageRef ImageStore::add(std::shared_ptr<const xrender::raster::Surface> surface) {
    std::uint32_t id = next_++;
    images_.emplace(id, std::move(surface));
    return {id};
}

void ImageStore::remove(ImageRef image) {
    images_.erase(image.id);
    updates_.erase(image.id);
}

void ImageStore::put(ImageRef image, std::shared_ptr<const xrender::raster::Surface> surface, std::uint64_t revision) {
    if (image.id == 0 || !surface) return;
    images_[image.id] = std::move(surface);
    // A snapshot knows the revision, not the rectangles: a renderer behind it redraws all of it once.
    if (revision) updates_[image.id] = Updates{revision, {}};
    else updates_.erase(image.id);
    next_ = std::max(next_, image.id + 1);
}

bool ImageStore::update(ImageRef image, std::shared_ptr<const xrender::raster::Surface> surface, std::span<const mlibs::geom::PixelRect> changed) {
    const auto it = images_.find(image.id);
    if (it == images_.end() || !surface || surface->width() != it->second->width() || surface->height() != it->second->height()) return false;
    std::vector<mlibs::geom::PixelRect> rects;
    for (const auto& r : changed) {
        const std::int32_t x0 = std::max(0, r.x), y0 = std::max(0, r.y);
        const std::int32_t x1 = std::min(surface->width(), r.x + r.width), y1 = std::min(surface->height(), r.y + r.height);
        if (x1 > x0 && y1 > y0) rects.push_back({x0, y0, x1 - x0, y1 - y0});
    }
    if (changed.empty()) rects.push_back({0, 0, surface->width(), surface->height()});
    it->second = std::move(surface);
    Updates& u = updates_[image.id];
    u.recent.emplace_back(++u.revision, std::move(rects));
    while (u.recent.size() > remembered) u.recent.pop_front();
    return true;
}

std::uint64_t ImageStore::revision(ImageRef image) const noexcept {
    const auto it = updates_.find(image.id);
    return it == updates_.end() ? 0 : it->second.revision;
}

bool ImageStore::changed_since(ImageRef image, std::uint64_t revision, std::vector<mlibs::geom::PixelRect>& out) const {
    const auto it = updates_.find(image.id);
    if (it == updates_.end()) return revision == 0;
    const Updates& u = it->second;
    if (revision >= u.revision) return true;
    // Every revision after `revision` must still be remembered.
    if (u.recent.empty() || u.recent.front().first > revision + 1) return false;
    for (const auto& [at, rects] : u.recent)
        if (at > revision) out.insert(out.end(), rects.begin(), rects.end());
    return true;
}

std::shared_ptr<const xrender::raster::Surface> ImageStore::shared(ImageRef image) const {
    const auto it = images_.find(image.id);
    return it == images_.end() ? nullptr : it->second;
}

const xrender::raster::Surface* ImageStore::get(ImageRef image) const noexcept {
    auto it = images_.find(image.id);
    return it == images_.end() ? nullptr : it->second.get();
}

}  // namespace xrender
