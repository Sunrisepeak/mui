module hanim;

import std;

namespace hanim {

double srgb_to_linear(double c) noexcept {
    if (c <= 0.04045) return c / 12.92;
    return dm::pow((c + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double c) noexcept {
    if (c <= 0.0031308) return 12.92 * c;
    return 1.055 * dm::pow(c, 1.0 / 2.4) - 0.055;
}

// Björn Ottosson's OKLab (2020), from linear sRGB through LMS.
OkLab to_oklab(const Rgba& c) noexcept {
    double r = srgb_to_linear(c.r);
    double g = srgb_to_linear(c.g);
    double b = srgb_to_linear(c.b);
    double l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    double m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    double s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;
    double l_ = dm::cbrt(l), m_ = dm::cbrt(m), s_ = dm::cbrt(s);
    return {0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
            1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
            0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_};
}

Rgba from_oklab(const OkLab& lab, float alpha) noexcept {
    double l_ = lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
    double m_ = lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
    double s_ = lab.l - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
    double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    double r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    double g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    double b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    auto out = [](double v) { return static_cast<float>(dm::clamp(linear_to_srgb(dm::clamp(v, 0.0, 1.0)), 0.0, 1.0)); };
    return {out(r), out(g), out(b), alpha};
}

std::optional<Rgba> Rgba::parse_hex(std::string_view hex) noexcept {
    if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1);
    auto digit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    std::array<int, 4> v{0, 0, 0, 255};
    if (hex.size() == 3 || hex.size() == 4) {
        for (std::size_t i = 0; i < hex.size(); ++i) {
            int d = digit(hex[i]);
            if (d < 0) return std::nullopt;
            v[i] = d * 17;
        }
    } else if (hex.size() == 6 || hex.size() == 8) {
        for (std::size_t i = 0; i < hex.size() / 2; ++i) {
            int hi = digit(hex[2 * i]), lo = digit(hex[2 * i + 1]);
            if (hi < 0 || lo < 0) return std::nullopt;
            v[i] = hi * 16 + lo;
        }
    } else {
        return std::nullopt;
    }
    return bytes(v[0], v[1], v[2], v[3]);
}

std::array<double, 6> Transform2D::matrix() const noexcept {
    // M = T · R · K · S, with K the x-skew.
    double c = dm::cos(rotate), s = dm::sin(rotate);
    double k = skew_x == 0.0 ? 0.0 : dm::sin(skew_x) / dm::cos(skew_x);
    // R·K = [c, c·k − s; s, s·k + c]
    double a = c * sx;
    double b = s * sx;
    double cc = (c * k - s) * sy;
    double d = (s * k + c) * sy;
    return {a, b, cc, d, tx, ty};
}

}  // namespace hanim
