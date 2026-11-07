// Shared sRGB readability math. WCAG's relative-luminance definition:
// https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html
// Inputs are finite normalized components; pow uses our deterministic core.
export module mlibs.math.api:color;

import std;
import :det;

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

}  // namespace mlibs::math::color
