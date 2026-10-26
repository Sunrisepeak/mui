module openmui.raster;

import std;

namespace omui::raster {

Surface::Surface(int width, int height) { resize(width, height); }

void Surface::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.assign(static_cast<std::size_t>(width_) * height_, Rgba8{});
}

void Surface::clear(Rgba8 value) { std::ranges::fill(pixels_, value); }

std::uint64_t Surface::hash() const noexcept {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](std::uint8_t b) {
        h ^= b;
        h *= 0x100000001b3ull;
    };
    mix(static_cast<std::uint8_t>(width_));
    mix(static_cast<std::uint8_t>(width_ >> 8));
    mix(static_cast<std::uint8_t>(height_));
    mix(static_cast<std::uint8_t>(height_ >> 8));
    for (const Rgba8& p : pixels_) {
        mix(p.r);
        mix(p.g);
        mix(p.b);
        mix(p.a);
    }
    return h;
}

Mask::Mask(int width, int height, std::uint8_t fill) { resize(width, height, fill); }

void Mask::resize(int width, int height, std::uint8_t fill) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    data_.assign(static_cast<std::size_t>(width_) * height_, fill);
}

void Mask::fill(std::uint8_t v) { std::ranges::fill(data_, v); }

}  // namespace omui::raster
