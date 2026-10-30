module openmui.raster;

import std;
import openmui.hash;

namespace omui::raster {

Surface::Surface(int width, int height) { resize(width, height); }

void Surface::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.assign(static_cast<std::size_t>(width_) * height_, Rgba8{});
}

void Surface::clear(Rgba8 value) { std::ranges::fill(pixels_, value); }

std::uint64_t Surface::hash() const noexcept {
    hash::Fnv1a h;
    h.le(static_cast<std::uint64_t>(width_), 2).le(static_cast<std::uint64_t>(height_), 2);
    for (const Rgba8& p : pixels_) h.byte(p.r).byte(p.g).byte(p.b).byte(p.a);
    return h.value();
}

Mask::Mask(int width, int height, std::uint8_t fill) { resize(width, height, fill); }

void Mask::resize(int width, int height, std::uint8_t fill) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    data_.assign(static_cast<std::size_t>(width_) * height_, fill);
}

void Mask::fill(std::uint8_t v) { std::ranges::fill(data_, v); }

}  // namespace omui::raster
