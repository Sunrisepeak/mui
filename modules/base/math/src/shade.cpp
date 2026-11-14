// A shade (D-168): the base colour of an item's row, its grain, the tints in turn, and the light — in whole
// numbers, the arithmetic xengine.cellular's CONTRACT.md states and a device's painter is held to.
module mlibs.math.api;

import std;

namespace mlibs::math::color {
namespace {

struct Channels {
    std::uint32_t red = 0, green = 0, blue = 0, alpha = 0;
};
[[nodiscard]] constexpr Channels unpack(std::uint32_t color) noexcept { return {color & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff, color >> 24}; }
// x·y/255 to the nearest, exactly, for 8-bit values.
[[nodiscard]] constexpr std::uint32_t scaled(std::uint32_t value, std::uint32_t by) noexcept {
    const std::uint32_t product = value * by + 128;
    return (product + (product >> 8)) >> 8;
}
// How far `value` is from `from` to `to`, 0 … 256.
[[nodiscard]] constexpr std::uint32_t share_of(std::uint32_t value, std::uint32_t from, std::uint32_t to) noexcept {
    if (to > from) {
        if (value <= from) return 0;
        if (value >= to) return 256;
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(value - from) << 8) / (to - from));
    }
    if (value >= from) return 0;
    if (value <= to) return 256;
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(from - value) << 8) / (from - to));
}
[[nodiscard]] constexpr std::uint32_t toward(std::uint32_t component, std::uint32_t target, std::uint32_t amount) noexcept {
    return (component * (256 - amount) + target * amount + 128) >> 8;
}

[[nodiscard]] Shaded shade_item(const Shade& shade, std::span<const std::uint32_t> cells, std::size_t area, std::size_t index) noexcept {
    const std::uint32_t row = cells[shade.key * area + index];
    Channels color = unpack(row < shade.base.size() ? shade.base[row] : shade.fallback);
    if (row < shade.grain.size() && shade.grain[row]) {
        // Lighter or darker by a number of the item's place: the same item the same, whatever is in it.
        const int roll = static_cast<int>(mix32(static_cast<std::uint32_t>(index)) & 0xff) - 128;
        const int shift = roll * static_cast<int>(shade.grain[row]) / 128;
        const auto shifted = [shift](std::uint32_t component) { return static_cast<std::uint32_t>(std::clamp(static_cast<int>(component) + shift, 0, 255)); };
        color = {shifted(color.red), shifted(color.green), shifted(color.blue), color.alpha};
    }
    Channels light;
    for (const Tint& tint : shade.tints) {
        const std::uint32_t to = tint.to_by_row.empty() ? tint.to : row < tint.to_by_row.size() ? tint.to_by_row[row] : tint.from;
        if (to == tint.from) continue;
        const Channels target = unpack(tint.color);
        const std::uint32_t amount = share_of(cells[tint.channel * area + index], tint.from, to) * target.alpha / 255;
        if (!amount) continue;
        color = {toward(color.red, target.red, amount), toward(color.green, target.green, amount), toward(color.blue, target.blue, amount),
                 toward(color.alpha, 255, amount)};
        if (tint.emits)
            light = {std::min<std::uint32_t>(255, light.red + ((target.red * amount + 128) >> 8)),
                     std::min<std::uint32_t>(255, light.green + ((target.green * amount + 128) >> 8)),
                     std::min<std::uint32_t>(255, light.blue + ((target.blue * amount + 128) >> 8)), 0};
    }
    Shaded out;
    out.pixel = scaled(color.red, color.alpha) | scaled(color.green, color.alpha) << 8 | scaled(color.blue, color.alpha) << 16 | color.alpha << 24;
    if (light.red | light.green | light.blue) out.light = light.red | light.green << 8 | light.blue << 16 | std::max({light.red, light.green, light.blue}) << 24;
    return out;
}

}  // namespace

std::expected<void, std::string> check(const Shade& shade, std::uint32_t channels) {
    if (shade.key >= channels) return std::unexpected(std::format("the shade's key is channel {}; there are {} channels", shade.key, channels));
    if (shade.base.empty()) return std::unexpected(std::string("a shade has a row at least: the colour of the key's value 0"));
    if (!shade.grain.empty() && shade.grain.size() != shade.base.size())
        return std::unexpected(std::format("grain for {} rows; the shade has {}", shade.grain.size(), shade.base.size()));
    for (std::size_t index = 0; index < shade.tints.size(); ++index) {
        const Tint& tint = shade.tints[index];
        if (tint.channel >= channels) return std::unexpected(std::format("tint {} reads channel {}; there are {} channels", index, tint.channel, channels));
        if (!tint.to_by_row.empty() && tint.to_by_row.size() != shade.base.size())
            return std::unexpected(std::format("tint {} has an end for {} rows; the shade has {}", index, tint.to_by_row.size(), shade.base.size()));
    }
    return {};
}

Shaded shade_of(const Shade& shade, std::span<const std::uint32_t> cells, std::size_t area, std::size_t index) noexcept {
    return shade_item(shade, cells, area, index);
}

void shade_run(const Shade& shade, std::span<const std::uint32_t> cells, std::size_t area, std::size_t first, std::span<std::uint32_t> pixels,
               std::span<std::uint32_t> light) noexcept {
    for (std::size_t offset = 0; offset < pixels.size(); ++offset) {
        const Shaded shaded = shade_item(shade, cells, area, first + offset);
        pixels[offset] = shaded.pixel;
        if (!light.empty()) light[offset] = shaded.light;
    }
}

}  // namespace mlibs::math::color
