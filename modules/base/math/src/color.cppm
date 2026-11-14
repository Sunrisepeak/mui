// Shared sRGB readability math. WCAG's relative-luminance definition:
// https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html
// Inputs are finite normalized components; pow uses our deterministic core.
export module mlibs.math.api:color;

import std;
import :det;
import :random;

export namespace mlibs::math::color {

[[nodiscard]] inline double relative_luminance(double r, double g, double b) noexcept {
    const auto linear = [](double c) {
        c = std::clamp(c, 0.0, 1.0);
        return c <= 0.04045 ? c / 12.92 : det::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b);
}

// The arguments are relative luminances, not encoded sRGB channels.
[[nodiscard]] inline constexpr double contrast_ratio(double a, double b) noexcept {
    a = std::clamp(a, 0.0, 1.0); b = std::clamp(b, 0.0, 1.0);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

// A shade: the colour of a state (D-168) — of each item of channel-major numbers (a lattice's cells, a cloud's
// particles, a body's parts). Its base is a row of a table by one channel's value (the key: a material), unevenly light by `grain` so that a body of one material is not one
// flat colour; then each tint in turn moves it toward a colour by how far a channel is along a range — charred,
// wet, hot, alight — and what a tint emits is also the item's light, for whoever draws a glow. Whole numbers
// throughout (xengine.cellular's CONTRACT.md states it): the same pixels on every machine, and on a device.
//
// Colours here are straight RGBA8, red in the lowest byte (0xAABBGGRR).
struct Tint {
    std::uint32_t channel = 0;
    // None of it at `from`, all of it at `to`; `to` below `from`: the less, the more (frost). Equal: no tint.
    std::uint32_t from = 0, to = 0;
    // `to` by the key's row instead (how burned a thing is, against its own fuel); a row whose `to` is `from`
    // is not tinted — which is also how a tint is kept to some rows.
    std::vector<std::uint32_t> to_by_row;
    std::uint32_t color = 0;  // toward this; its alpha: how much of it at most
    bool emits = false;       // light of its own: added to the item's light by the same share
};
struct Shade {
    std::uint32_t key = 0;             // the channel whose value is the row
    std::vector<std::uint32_t> base;   // a colour a row
    std::vector<std::uint8_t> grain;   // a row's unevenness: its colour lighter or darker by up to this, item by item (empty: none)
    std::uint32_t fallback = 0;        // a key beyond the table
    std::vector<Tint> tints;
};
// An item as a renderer stores it — premultiplied RGBA8, red lowest — and the light it gives off (the same
// form: its alpha the greatest of its three; 0 where nothing glows).
struct Shaded {
    std::uint32_t pixel = 0, light = 0;
    bool operator==(const Shaded&) const = default;
};
// What is wrong with a shade for numbers of `channels` channels, or nothing.
[[nodiscard]] std::expected<void, std::string> check(const Shade& shade, std::uint32_t channels);
// The shade of item `index` of `cells` (channel-major, `area` items a channel). A checked shade.
[[nodiscard]] Shaded shade_of(const Shade& shade, std::span<const std::uint32_t> cells, std::size_t area, std::size_t index) noexcept;
// A run of items from `first`: `pixels.size()` of them, and their light where `light` is not empty (the same size).
void shade_run(const Shade& shade, std::span<const std::uint32_t> cells, std::size_t area, std::size_t first, std::span<std::uint32_t> pixels,
               std::span<std::uint32_t> light) noexcept;

}  // namespace mlibs::math::color
