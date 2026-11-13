module xengine.cellular.decor;
import std;

namespace xengine::cellular::decor {
namespace matter = xengine::matter;

std::expected<Decor, std::string> Decor::make(const matter::Program& program, Shape shape, const Options& options) {
    const auto fill = program.material(options.fill);
    if (!fill) return std::unexpected(std::format("no material `{}` beyond the layer's edges", options.fill));
    if (!shape.width || !shape.height || !options.tile.width || !options.tile.height) return std::unexpected(std::string("a layer of no cells, or tiles of none"));
    auto lattice = cellular::lower(program, {.seed = options.seed});
    if (!lattice) return std::unexpected(std::format("the program does not lower: {}", lattice.error().text()));
    Decor decor;
    decor.program_ = &program;
    decor.options_ = options;
    decor.shape_ = shape;
    decor.lattice_ = std::move(*lattice);
    decor.channels_ = static_cast<std::uint32_t>(program.channels().size());
    decor.outside_ = program.fresh(1, *fill);
    const std::size_t area = static_cast<std::size_t>(shape.width) * shape.height;
    decor.owned_.assign(area, 0);
    const Shape grid = cellular::tiles(shape, options.tile);
    decor.activity_.assign(static_cast<std::size_t>(grid.width) * grid.height, 0);
    decor.dirty_.assign(decor.activity_.size(), 0);
    return decor;
}

bool Decor::owns(std::uint32_t x, std::uint32_t y) const noexcept {
    return x < shape_.width && y < shape_.height && owned_[static_cast<std::size_t>(y) * shape_.width + x] != 0;
}

std::uint32_t Decor::put(Tile region, std::uint32_t material) {
    const std::size_t area = static_cast<std::size_t>(shape_.width) * shape_.height;
    if (cells_.size() != area * channels_ || material >= program_->materials().size() || program_->phase(material) != matter::Phase::solid) return 0;
    std::uint32_t count = 0;
    for (std::uint32_t y = region.y; y < std::min(region.y + region.height, shape_.height); ++y)
        for (std::uint32_t x = region.x; x < std::min(region.x + region.width, shape_.width); ++x) {
            const std::size_t cell = static_cast<std::size_t>(y) * shape_.width + x;
            const std::uint32_t there = cells_[cell];
            if (there != matter::none && (there >= program_->materials().size() || program_->phase(there) != matter::Phase::gas)) continue;
            program_->place(cells_, area, cell, material);
            owned_[cell] = 1;
            ++count;
        }
    if (count) lattice_.wake(shape_, options_.tile, region, activity_);
    return count;
}

std::uint32_t Decor::place(Tile region, std::uint32_t material) {
    const std::uint32_t count = put(region, material);
    if (count) placed_.push_back({region, material});
    return count;
}

void Decor::step(std::span<const std::uint32_t> authoritative) {
    const std::size_t area = static_cast<std::size_t>(shape_.width) * shape_.height;
    if (authoritative.size() != area * channels_) return;
    last_ = {};
    if (cells_.empty()) {
        // The first: the layer is the authoritative field.
        cells_.assign(authoritative.begin(), authoritative.end());
        next_.assign(cells_.size(), 0);
        seen_ = cells_;
        last_.followed = area;
        std::ranges::fill(activity_, std::uint8_t{0});
    } else {
        const Shape grid = cellular::tiles(shape_, options_.tile);
        std::ranges::fill(dirty_, std::uint8_t{0});
        for (std::uint32_t y = 0; y < shape_.height; ++y) {
            const std::size_t tile_row = static_cast<std::size_t>(y / options_.tile.height) * grid.width;
            for (std::uint32_t x = 0; x < shape_.width; ++x) {
                const std::size_t cell = static_cast<std::size_t>(y) * shape_.width + x;
                bool follow = false;
                if (owned_[cell]) {
                    // Scenery is the layer's while it is solid and the world has put nothing of its own there.
                    const std::uint32_t mine = cells_[cell], theirs = authoritative[cell];
                    const bool solid = mine != matter::none && mine < program_->materials().size() && program_->phase(mine) == matter::Phase::solid;
                    const bool room = theirs == matter::none || (theirs < program_->materials().size() && program_->phase(theirs) == matter::Phase::gas);
                    if (solid && room) {
                        ++last_.owned;
                        continue;
                    }
                    owned_[cell] = 0;
                    ++last_.released;
                    follow = !room;  // what the world put there; what scenery burned to goes on by the laws
                }
                for (std::uint32_t channel = 0; channel < channels_ && !follow; ++channel) follow = authoritative[channel * area + cell] != seen_[channel * area + cell];
                if (!follow) continue;
                for (std::uint32_t channel = 0; channel < channels_; ++channel) cells_[channel * area + cell] = authoritative[channel * area + cell];
                dirty_[tile_row + x / options_.tile.width] = 1;
                ++last_.followed;
            }
        }
        std::ranges::copy(authoritative, seen_.begin());
        if (options_.sparse)
            for (std::uint32_t tile_y = 0; tile_y < grid.height; ++tile_y)
                for (std::uint32_t tile_x = 0; tile_x < grid.width; ++tile_x)
                    if (dirty_[static_cast<std::size_t>(tile_y) * grid.width + tile_x])
                        lattice_.wake(shape_, options_.tile, {tile_x * options_.tile.width, tile_y * options_.tile.height, options_.tile.width, options_.tile.height}, activity_);
    }
    Execution execution{.tile = options_.tile, .boundary = options_.open ? open_to(outside_) : nothing_beyond, .tick = tick_++};
    if (options_.sparse) execution.activity = activity_;
    (void)lattice_.step(shape_, cells_, next_, execution);
    cells_.swap(next_);
}

void Decor::restart(std::span<const std::uint32_t> authoritative, int frames) {
    cells_.clear();
    std::ranges::fill(owned_, std::uint8_t{0});
    tick_ = 0;
    step(authoritative);
    for (const Placed& placed : placed_) (void)put(placed.region, placed.material);
    for (int frame = 0; frame < frames; ++frame) step(authoritative);
}

Statistics Decor::last(std::span<const std::uint32_t> count) const noexcept {
    Statistics out = last_;
    const std::size_t area = static_cast<std::size_t>(shape_.width) * shape_.height;
    if (count.size() == area * channels_ && cells_.size() == count.size())
        for (std::size_t cell = 0; cell < area; ++cell) out.differing += cells_[cell] != count[cell];
    return out;
}

}  // namespace xengine::cellular::decor
