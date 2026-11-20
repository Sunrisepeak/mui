// mlibs.geom:measure — exact measures of a closed contour of cubic Béziers:
// its signed area and centroid, from polynomial integrals (no flattening),
// in double precision whatever the coordinates' type. Animation (shape
// morphs pairing outlines and holes) and simulation (bodies' mass and
// centre) share these (convergence plan §6.9).
export module mlibs.geom.api:measure;

import std;

export namespace mlibs::geom {

// Any cubic with start, control1, control2 and end points of x and y.
template <class Curve>
concept CubicCurve = requires(const Curve& curve) {
    { curve.start.x } -> std::convertible_to<double>;
    { curve.start.y } -> std::convertible_to<double>;
    { curve.control1.x } -> std::convertible_to<double>;
    { curve.control2.y } -> std::convertible_to<double>;
    { curve.end.x } -> std::convertible_to<double>;
};

struct ContourMeasure {
    double area =
        0;  // signed: positive counter-clockwise in a y-up sense (clockwise on a y-down screen)
    double centroid_x = 0,
           centroid_y = 0;  // the area's centre; the start point when the area is zero
};

namespace detail {
// The power-basis coefficients of one coordinate of a cubic, from `origin`
// and divided by `scale`.
inline std::array<double, 4> power_basis(double start, double control1, double control2, double end,
                                         double origin, double scale) noexcept {
    start = (start - origin) / scale;
    control1 = (control1 - origin) / scale;
    control2 = (control2 - origin) / scale;
    end = (end - origin) / scale;
    return {start, 3 * (control1 - start), 3 * (start - 2 * control1 + control2),
            end - start + 3 * (control1 - control2)};
}
// The contour's start and the larger side of its control bounds.
template <CubicCurve Curve>
std::array<double, 3> frame(std::span<const Curve> curves) noexcept {
    const double origin_x = curves.front().start.x, origin_y = curves.front().start.y;
    double low_x = origin_x, high_x = origin_x, low_y = origin_y, high_y = origin_y;
    for (const auto& curve : curves)
        for (const auto& point : {curve.start, curve.control1, curve.control2, curve.end}) {
            low_x = std::min<double>(low_x, point.x), high_x = std::max<double>(high_x, point.x);
            low_y = std::min<double>(low_y, point.y), high_y = std::max<double>(high_y, point.y);
        }
    return {origin_x, origin_y, std::max(high_x - low_x, high_y - low_y)};
}
}  // namespace detail

// The signed area divided by the square of the larger side of the control
// bounds: its orientation and relative size, free of underflow for tiny
// contours. The exact integral of (x·y' − y·x') / 2 from the start point.
template <CubicCurve Curve>
[[nodiscard]] double normalized_signed_area(std::span<const Curve> curves) noexcept {
    if (curves.empty())
        return 0;
    const auto [origin_x, origin_y, scale] = detail::frame(curves);
    if (scale == 0)
        return 0;
    double result = 0;
    for (const auto& curve : curves) {
        const auto x = detail::power_basis(curve.start.x, curve.control1.x, curve.control2.x,
                                           curve.end.x, origin_x, scale);
        const auto y = detail::power_basis(curve.start.y, curve.control1.y, curve.control2.y,
                                           curve.end.y, origin_y, scale);
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 1; j < 4; ++j)
                result +=
                    (x[i] * static_cast<double>(j) * y[j] - y[i] * static_cast<double>(j) * x[j]) /
                    static_cast<double>(i + j);
    }
    return result * 0.5;
}

// The signed area and centroid. The centroid's integrals, ½∮x²dy and −½∮y²dx,
// are exact polynomial integrals as the area's is.
template <CubicCurve Curve>
[[nodiscard]] ContourMeasure measure(std::span<const Curve> curves) noexcept {
    if (curves.empty())
        return {};
    const auto [origin_x, origin_y, scale] = detail::frame(curves);
    ContourMeasure result{0, origin_x, origin_y};
    if (scale == 0)
        return result;
    double area = 0, moment_x = 0, moment_y = 0;
    for (const auto& curve : curves) {
        const auto x = detail::power_basis(curve.start.x, curve.control1.x, curve.control2.x,
                                           curve.end.x, origin_x, scale);
        const auto y = detail::power_basis(curve.start.y, curve.control1.y, curve.control2.y,
                                           curve.end.y, origin_y, scale);
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 1; j < 4; ++j) {
                const double degree = static_cast<double>(j);
                area += (x[i] * degree * y[j] - y[i] * degree * x[j]) / static_cast<double>(i + j);
                // x²·y' and y²·x': the products of three power-basis terms.
                for (unsigned k = 0; k < 4; ++k) {
                    moment_x += x[i] * x[k] * degree * y[j] / static_cast<double>(i + k + j);
                    moment_y += y[i] * y[k] * degree * x[j] / static_cast<double>(i + k + j);
                }
            }
    }
    area *= 0.5;
    result.area = area * scale * scale;
    if (area != 0) {
        result.centroid_x = origin_x + moment_x * 0.5 / area * scale;
        result.centroid_y = origin_y - moment_y * 0.5 / area * scale;
    }
    return result;
}

}  // namespace mlibs::geom
