// Two surfaces take turns: the one the store holds is never written (images
// are immutable while drawn); the other is brought up to date — the tiles
// this present changed and those it missed while the first was shown — and
// becomes the image, named with this present's rectangles.
module xengine.simulation.cellular.present;

import std;

namespace xengine::simulation::cellular {

namespace {
using Surface = xrender::raster::Surface;
using Pixel = xrender::raster::Rgba8;
}  // namespace

struct FieldPresenter::Data {
    xrender::ImageStore* store = nullptr;
    xrender::ImageRef ref;
    Shape shape, tile, grid;
    std::uint32_t channels = 0;
    Palette palette;
    std::vector<Pixel> colors;                      // the swatches' premultiplied pixels
    Pixel fallback{};
    std::array<std::shared_ptr<Surface>, 2> surfaces;
    int shown = 0;                                  // the one the store holds
    std::vector<std::uint8_t> missed;               // tiles the other surface has not caught up with
    std::vector<std::uint8_t> dirty;                // scratch: tiles to recolour now
    std::vector<std::uint32_t> last;                // the cells presented last (comparing presents)
    bool everything = true;                         // the next present recolours every tile
    std::vector<mlibs::geom::PixelRect> rectangles;

    [[nodiscard]] std::size_t area() const noexcept { return static_cast<std::size_t>(shape.width) * shape.height; }
    [[nodiscard]] Pixel pixel(std::span<const std::uint32_t> cells, std::size_t index) const noexcept {
        const std::size_t cells_area = area();
        for (std::size_t k = 0; k < palette.swatches.size(); ++k) {
            const auto& when = palette.swatches[k].when;
            if (std::ranges::all_of(when, [&](const Test& test) {
                    const auto value = cells[test.channel * cells_area + index];
                    return value >= test.value.low && value <= test.value.high;
                }))
                return colors[k];
        }
        return fallback;
    }
    void paint(Surface& surface, std::span<const std::uint32_t> cells, std::uint32_t tx, std::uint32_t ty) const {
        const std::uint32_t x0 = tx * tile.width, y0 = ty * tile.height;
        const std::uint32_t x1 = std::min(shape.width, x0 + tile.width), y1 = std::min(shape.height, y0 + tile.height);
        for (std::uint32_t y = y0; y < y1; ++y) {
            Pixel* row = surface.row(static_cast<int>(y));
            for (std::uint32_t x = x0; x < x1; ++x) row[x] = pixel(cells, static_cast<std::size_t>(y) * shape.width + x);
        }
    }
    std::expected<void, std::string> prepare(const Palette& next) {
        for (const auto& swatch : next.swatches)
            for (const auto& test : swatch.when)
                if (test.channel >= channels) return std::unexpected(std::format("a swatch tests channel {}; the field has {}", test.channel, channels));
        palette = next;
        colors.clear();
        for (const auto& swatch : palette.swatches) colors.push_back(xrender::raster::premultiply(swatch.color));
        fallback = xrender::raster::premultiply(palette.fallback);
        everything = true;
        return {};
    }
};

FieldPresenter::FieldPresenter(std::unique_ptr<Data> data) noexcept : data_(std::move(data)) {}
FieldPresenter::FieldPresenter(FieldPresenter&&) noexcept = default;
FieldPresenter& FieldPresenter::operator=(FieldPresenter&&) noexcept = default;
FieldPresenter::~FieldPresenter() = default;
void FieldPresenter::release() {
    if (data_ && data_->store) data_->store->remove(data_->ref);
    data_.reset();
}

std::expected<FieldPresenter, std::string> FieldPresenter::make(xrender::ImageStore& store, Shape shape, std::uint32_t channels, Palette palette,
                                                                Shape tile) {
    if (!shape.width || !shape.height || shape.width > 16384 || shape.height > 16384) return std::unexpected(std::string("a field of 1 … 16384 cells a side"));
    if (!channels) return std::unexpected(std::string("a field has a channel at least"));
    if (!tile.width || !tile.height) return std::unexpected(std::string("an empty tile"));
    auto data = std::make_unique<Data>();
    data->store = &store;
    data->shape = shape;
    data->tile = tile;
    data->grid = cellular::tiles(shape, tile);
    data->channels = channels;
    if (auto prepared = data->prepare(palette); !prepared) return std::unexpected(prepared.error());
    for (auto& surface : data->surfaces) {
        surface = std::make_shared<Surface>(static_cast<int>(shape.width), static_cast<int>(shape.height));
        std::ranges::fill(surface->pixels(), data->fallback);
    }
    const std::size_t count = static_cast<std::size_t>(data->grid.width) * data->grid.height;
    data->missed.assign(count, 1);
    data->dirty.assign(count, 0);
    data->ref = store.add(std::shared_ptr<const Surface>(data->surfaces[0]));
    return FieldPresenter(std::move(data));
}

std::expected<PresentReport, std::string> FieldPresenter::present(std::span<const std::uint32_t> cells, std::span<const std::uint8_t> changed) {
    Data& d = *data_;
    if (cells.size() != d.area() * d.channels) return std::unexpected(std::format("{} values; the field has {}", cells.size(), d.area() * d.channels));
    if (changed.size() != d.dirty.size()) return std::unexpected(std::format("a mask of {} tiles; the field has {}", changed.size(), d.dirty.size()));
    PresentReport report{.whole = d.everything};
    for (std::size_t k = 0; k < d.dirty.size(); ++k) d.dirty[k] = d.everything || changed[k] ? 1 : 0;
    d.everything = false;
    if (std::ranges::none_of(d.dirty, [](std::uint8_t v) { return v != 0; })) return report;
    // What comparing presents compares with: the changed tiles' cells only.
    if (!d.last.empty())
        for (std::uint32_t ty = 0; ty < d.grid.height; ++ty)
            for (std::uint32_t tx = 0; tx < d.grid.width; ++tx) {
                if (!d.dirty[static_cast<std::size_t>(ty) * d.grid.width + tx]) continue;
                const std::uint32_t x0 = tx * d.tile.width, x1 = std::min(d.shape.width, x0 + d.tile.width);
                for (std::uint32_t channel = 0; channel < d.channels; ++channel)
                    for (std::uint32_t y = ty * d.tile.height; y < std::min(d.shape.height, (ty + 1) * d.tile.height); ++y) {
                        const std::size_t row = channel * d.area() + static_cast<std::size_t>(y) * d.shape.width;
                        std::copy(cells.begin() + static_cast<std::ptrdiff_t>(row + x0), cells.begin() + static_cast<std::ptrdiff_t>(row + x1),
                                  d.last.begin() + static_cast<std::ptrdiff_t>(row + x0));
                    }
            }
    // The surface not shown; a new one if someone else still holds it (a snapshot on another thread).
    const int next = 1 - d.shown;
    if (d.surfaces[next].use_count() > 1) {
        d.surfaces[next] = std::make_shared<Surface>(*d.surfaces[d.shown]);
        std::ranges::fill(d.missed, 0);
    }
    Surface& surface = *d.surfaces[next];
    d.rectangles.clear();
    for (std::uint32_t ty = 0; ty < d.grid.height; ++ty) {
        std::uint32_t run = 0;  // tiles of this row changed, side by side: one rectangle
        for (std::uint32_t tx = 0; tx <= d.grid.width; ++tx) {
            const std::size_t k = static_cast<std::size_t>(ty) * d.grid.width + tx;
            const bool now = tx < d.grid.width && d.dirty[k];
            if (tx < d.grid.width && (now || d.missed[k])) {
                d.paint(surface, cells, tx, ty);
                if (now) ++report.tiles;
            }
            if (now) {
                ++run;
                continue;
            }
            if (run) {
                const std::uint32_t x0 = (tx - run) * d.tile.width, y0 = ty * d.tile.height;
                d.rectangles.push_back({static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                                        static_cast<std::int32_t>(std::min(d.shape.width, tx * d.tile.width) - x0),
                                        static_cast<std::int32_t>(std::min(d.shape.height, y0 + d.tile.height) - y0)});
                run = 0;
            }
        }
    }
    if (!d.store->update(d.ref, std::shared_ptr<const Surface>(d.surfaces[next]), d.rectangles))
        return std::unexpected(std::string("the image is no longer in the store"));
    // The surface just let go misses what this present changed.
    d.missed = d.dirty;
    d.shown = next;
    report.rectangles = d.rectangles.size();
    return report;
}

std::expected<PresentReport, std::string> FieldPresenter::present(std::span<const std::uint32_t> cells) {
    Data& d = *data_;
    if (cells.size() != d.area() * d.channels) return std::unexpected(std::format("{} values; the field has {}", cells.size(), d.area() * d.channels));
    std::vector<std::uint8_t> changed(d.dirty.size(), 0);
    if (d.last.size() != cells.size()) {
        d.last.assign(cells.begin(), cells.end());
        std::ranges::fill(changed, 1);
    } else {
        const std::size_t area = d.area();
        for (std::uint32_t channel = 0; channel < d.channels; ++channel)
            for (std::uint32_t y = 0; y < d.shape.height; ++y) {
                const std::size_t row = channel * area + static_cast<std::size_t>(y) * d.shape.width;
                for (std::uint32_t x = 0; x < d.shape.width; ++x)
                    if (cells[row + x] != d.last[row + x]) changed[static_cast<std::size_t>(y / d.tile.height) * d.grid.width + x / d.tile.width] = 1;
            }
    }
    return present(cells, changed);
}

std::expected<void, std::string> FieldPresenter::recolor(Palette palette) { return data_->prepare(palette); }

void FieldPresenter::draw(xrender::DisplayList& list, mlibs::geom::Rect dest, float opacity) const {
    const Data& d = *data_;
    list.draw_image(d.ref, {0, 0, static_cast<float>(d.shape.width), static_cast<float>(d.shape.height)}, dest, xrender::Sampling::nearest, opacity);
}

xrender::ImageRef FieldPresenter::image() const noexcept { return data_->ref; }
Shape FieldPresenter::shape() const noexcept { return data_->shape; }
Shape FieldPresenter::tiles() const noexcept { return data_->grid; }

mlibs::geom::Color FieldPresenter::color(std::span<const std::uint32_t> cells, std::uint32_t x, std::uint32_t y) const noexcept {
    const Data& d = *data_;
    if (x >= d.shape.width || y >= d.shape.height || cells.size() != d.area() * d.channels) return d.palette.fallback;
    const std::size_t index = static_cast<std::size_t>(y) * d.shape.width + x;
    for (const auto& swatch : d.palette.swatches)
        if (std::ranges::all_of(swatch.when, [&](const Test& test) {
                const auto value = cells[test.channel * d.area() + index];
                return value >= test.value.low && value <= test.value.high;
            }))
            return swatch.color;
    return d.palette.fallback;
}

}  // namespace xengine::simulation::cellular
