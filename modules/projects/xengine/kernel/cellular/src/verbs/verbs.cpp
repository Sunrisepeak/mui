module xengine.cellular.verbs;

import std;
import xengine.matter;

namespace xengine::cellular::verbs {

namespace {

// The square of `radius` about a cell, within the world; of no cells when it lies outside.
Tile square(const rigid::MatterWorld& world, Cell centre, std::int64_t radius) {
    const std::int64_t left = std::max<std::int64_t>(0, centre.x - radius), top = std::max<std::int64_t>(0, centre.y - radius);
    const std::int64_t right = std::min<std::int64_t>(world.shape().width - 1, centre.x + radius), bottom = std::min<std::int64_t>(world.shape().height - 1, centre.y + radius);
    if (right < left || bottom < top) return {};
    return {static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top), static_cast<std::uint32_t>(right - left + 1), static_cast<std::uint32_t>(bottom - top + 1)};
}
std::uint32_t stored(const matter::Program& program, std::uint32_t channel, double value) {
    return static_cast<std::uint32_t>(value * static_cast<double>(std::uint64_t{1} << program.channels()[channel].fraction));
}

}  // namespace

void heat(rigid::MatterWorld& world, Cell centre, std::int64_t radius, double kelvin) {
    const auto temperature = world.program().channel("temperature");
    const Tile region = square(world, centre, radius);
    if (!temperature || !region.width) return;
    world.set(*temperature, region, stored(world.program(), *temperature, kelvin));
}

void explode(rigid::MatterWorld& world, Cell centre, const Explosion& explosion) {
    const auto pressure = world.program().channel("pressure");
    if (pressure && explosion.pressure > 0) {
        // A disc, a row at a time.
        for (std::int64_t down = -explosion.radius; down <= explosion.radius; ++down) {
            const auto half = static_cast<std::int64_t>(std::floor(std::sqrt(static_cast<double>(explosion.radius * explosion.radius - down * down))));
            const std::int64_t row = centre.y + down, left = std::max<std::int64_t>(0, centre.x - half), right = std::min<std::int64_t>(world.shape().width - 1, centre.x + half);
            if (row < 0 || row >= world.shape().height || right < left) continue;
            world.set(*pressure, {static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(right - left + 1), 1}, explosion.pressure);
        }
    }
    if (explosion.kelvin > 0) heat(world, centre, explosion.heat_radius, explosion.kelvin);
}

std::uint32_t pour(rigid::MatterWorld& world, Cell centre, std::uint32_t material, const Pour& pour) {
    const matter::Program& program = world.program();
    if (material >= program.materials().size()) return 0;
    const Shape shape = world.shape();
    std::uint32_t filled = 0;
    for (std::int64_t y = centre.y - pour.radius; y <= centre.y + pour.radius; ++y)
        for (std::int64_t x = centre.x - pour.radius; x <= centre.x + pour.radius; ++x) {
            if (x < 0 || y < 0 || x >= shape.width || y >= shape.height) continue;
            if (pour.reach > 0 && std::abs(x - centre.x) + std::abs(y - centre.y) > pour.reach) continue;
            const std::uint32_t there = world.cells()[static_cast<std::size_t>(y) * shape.width + static_cast<std::size_t>(x)];
            if (there >= program.materials().size() || program.phase(there) != matter::Phase::gas) continue;
            world.place({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 1, 1}, material);
            ++filled;
        }
    return filled;
}

std::uint32_t vent(rigid::MatterWorld& world, const Vent& vent) {
    if (vent.every == 0 || world.frames() % vent.every != 0) return 0;
    std::uint32_t filled = 0;
    if (vent.material) filled = pour(world, vent.at, *vent.material, vent.pour);
    if (vent.kelvin > 0) heat(world, vent.at, vent.heat_radius, vent.kelvin);
    return filled;
}

}  // namespace xengine::cellular::verbs
