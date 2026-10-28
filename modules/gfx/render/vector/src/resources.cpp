module openmui.render.vector;

import std;

namespace omui::render {

ImageRef ImageStore::add(int width, int height, std::vector<raster::Rgba8> pixels) {
    auto s = std::make_shared<raster::Surface>(width, height);
    std::ranges::copy(pixels | std::views::take(s->pixels().size()), s->pixels().begin());
    return add(std::shared_ptr<const raster::Surface>(std::move(s)));
}

ImageRef ImageStore::add(std::shared_ptr<const raster::Surface> surface) {
    std::uint32_t id = next_++;
    images_.emplace(id, std::move(surface));
    return {id};
}

void ImageStore::remove(ImageRef image) { images_.erase(image.id); }

void ImageStore::put(ImageRef image, std::shared_ptr<const raster::Surface> surface) {
    if (image.id == 0 || !surface) return;
    images_[image.id] = std::move(surface);
    next_ = std::max(next_, image.id + 1);
}

std::shared_ptr<const raster::Surface> ImageStore::shared(ImageRef image) const {
    const auto it = images_.find(image.id);
    return it == images_.end() ? nullptr : it->second;
}

const raster::Surface* ImageStore::get(ImageRef image) const noexcept {
    auto it = images_.find(image.id);
    return it == images_.end() ? nullptr : it->second.get();
}

}  // namespace omui::render
