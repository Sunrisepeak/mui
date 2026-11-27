module mrender.raster.api;

import std;
import mlibs.hash;

namespace mrender::raster {

Surface::Surface(int width, int height) { resize(width, height); }

void Surface::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.assign(static_cast<std::size_t>(width_) * height_, Rgba8{});
}

void Surface::clear(Rgba8 value) { std::ranges::fill(pixels_, value); }

std::uint64_t Surface::hash() const noexcept {
    mlibs::hash::Fnv1a h;
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

std::vector<ChangedRegion> changed_regions(const Surface& before, const Surface& after, int tile) {
    const int width = after.width(), height = after.height();
    if (before.width() != width || before.height() != height)
        return width > 0 && height > 0 ? std::vector<ChangedRegion>{{0, 0, width, height, static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height)}}
                                       : std::vector<ChangedRegion>{};
    tile = std::max(1, tile);
    const int columns = (width + tile - 1) / tile, rows = (height + tile - 1) / tile;
    std::vector<std::uint64_t> differing(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows), 0);
    for (int y = 0; y < height; ++y) {
        const Rgba8* was = before.row(y);
        const Rgba8* now = after.row(y);
        std::uint64_t* counts = differing.data() + static_cast<std::size_t>(y / tile) * static_cast<std::size_t>(columns);
        for (int x = 0; x < width; ++x)
            if (!(was[x] == now[x])) ++counts[x / tile];
    }
    // A run of differing tiles across a row; carried down while the next row has a run of the same span.
    struct Run {
        int           first = 0, last = 0, top = 0, bottom = 0;
        std::uint64_t pixels = 0;
    };
    std::vector<ChangedRegion> regions;
    const auto close = [&](const Run& run) {
        const int left = run.first * tile, top = run.top * tile;
        regions.push_back({left, top, std::min((run.last + 1) * tile, width) - left, std::min((run.bottom + 1) * tile, height) - top, run.pixels});
    };
    std::vector<Run> open, next;
    for (int row = 0; row < rows; ++row) {
        next.clear();
        const std::uint64_t* counts = differing.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(columns);
        for (int column = 0; column < columns;) {
            if (counts[column] == 0) {
                ++column;
                continue;
            }
            Run run{.first = column, .top = row, .bottom = row};
            for (; column < columns && counts[column] != 0; ++column) run.pixels += counts[column];
            run.last = column - 1;
            if (const auto above = std::ranges::find_if(open, [&](const Run& was) { return was.first == run.first && was.last == run.last; }); above != open.end()) {
                run.top = above->top;
                run.pixels += above->pixels;
                open.erase(above);
            }
            next.push_back(run);
        }
        for (const Run& ended : open) close(ended);
        open.swap(next);
    }
    for (const Run& ended : open) close(ended);
    std::ranges::sort(regions, [](const ChangedRegion& a, const ChangedRegion& b) { return std::tie(a.y, a.x) < std::tie(b.y, b.x); });
    return regions;
}

}  // namespace mrender::raster
