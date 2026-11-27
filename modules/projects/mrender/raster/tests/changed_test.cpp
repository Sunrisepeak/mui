// Where two pictures differ (changed_regions): what a design step changed on screen, as regions of tiles with how
// many of their pixels differ — the facts a show's director reads (hanim.timeline's direct, D-282).
import std;
import mrender.raster;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace raster = mrender::raster;

namespace {

raster::Surface filled(int width, int height, raster::Rgba8 colour) {
    raster::Surface surface(width, height);
    for (raster::Rgba8& pixel : surface.pixels()) pixel = colour;
    return surface;
}

void paint(raster::Surface& surface, int x, int y, int width, int height, raster::Rgba8 colour) {
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column) surface.at(column, row) = colour;
}

constexpr raster::Rgba8 white{255, 255, 255, 255}, ink{20, 20, 30, 255};

}  // namespace

UTest cases{"raster.changed", [] {
    "the same picture: nothing changed"_test = [] {
        const auto picture = filled(100, 60, white);
        expect(raster::changed_regions(picture, picture).empty());
    };

    "a change within a tile: that tile, with the pixels that differ"_test = [] {
        const auto before = filled(100, 60, white);
        auto after = before;
        paint(after, 20, 20, 5, 3, ink);
        const auto regions = raster::changed_regions(before, after);
        expect(eq(regions.size(), std::size_t{1}));
        if (!regions.empty()) expect(regions.front() == raster::ChangedRegion{16, 16, 16, 16, 15});
    };

    "a run of tiles across a row is one region, carried down while the rows below span the same tiles"_test = [] {
        const auto before = filled(100, 60, white);
        auto after = before;
        paint(after, 0, 0, 40, 40, ink);   // tiles 0–2 across, rows 0–2 down: one region
        paint(after, 96, 50, 4, 4, ink);   // the last tile on its own, clipped to the picture both ways
        const auto regions = raster::changed_regions(before, after);
        expect(eq(regions.size(), std::size_t{2})) << std::format("{} regions", regions.size());
        if (regions.size() != 2) return;
        expect(regions[0] == raster::ChangedRegion{0, 0, 48, 48, 1600}) << std::format("{} {} {} {} {}", regions[0].x, regions[0].y, regions[0].width, regions[0].height, regions[0].pixels);
        expect(regions[1] == raster::ChangedRegion{96, 48, 4, 12, 16}) << std::format("{} {} {} {} {}", regions[1].x, regions[1].y, regions[1].width, regions[1].height, regions[1].pixels);
    };

    "a run that changes its span below starts a region of its own; regions top to bottom, left to right"_test = [] {
        const auto before = filled(64, 64, white);
        auto after = before;
        paint(after, 0, 0, 32, 16, ink);   // tiles 0–1 of row 0
        paint(after, 0, 16, 16, 16, ink);  // tile 0 of row 1: another span
        paint(after, 48, 16, 16, 16, ink); // tile 3 of row 1
        const auto regions = raster::changed_regions(before, after);
        expect(eq(regions.size(), std::size_t{3}));
        if (regions.size() == 3) {
            expect(regions[0] == raster::ChangedRegion{0, 0, 32, 16, 512});
            expect(regions[1] == raster::ChangedRegion{0, 16, 16, 16, 256});
            expect(regions[2] == raster::ChangedRegion{48, 16, 16, 16, 256});
        }
    };

    "pictures of different sizes differ everywhere; the tile is the caller's"_test = [] {
        const auto small = filled(10, 10, white), large = filled(20, 12, white);
        const auto regions = raster::changed_regions(small, large);
        expect(eq(regions.size(), std::size_t{1}) && regions.front() == raster::ChangedRegion{0, 0, 20, 12, 240});
        auto after = small;
        paint(after, 3, 3, 1, 1, ink);
        const auto exact = raster::changed_regions(small, after, 1);
        expect(eq(exact.size(), std::size_t{1}) && exact.front() == raster::ChangedRegion{3, 3, 1, 1, 1});
    };
}};
