// Colour science for the contrast checker: WCAG 2.2's relative luminance and
// contrast ratio, the colour-vision deficiencies of Machado, Oliveira and
// Fernandes (2009) at full severity, and OKLab (Ottosson 2020) for the
// nearest passing colour.
module colors;

import std;

namespace colors::contrast {

namespace {

struct Rgb {
    double r = 0, g = 0, b = 0;  // sRGB, 0 to 1
};

std::expected<Rgb, std::string> parse(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    const bool shape = s.size() > 1 && s.front() == '#' && (s.size() == 4 || s.size() == 7);
    if (!shape || std::ranges::any_of(s.substr(1), [&](char c) { return digit(c) < 0; }))
        return std::unexpected(std::format("`{}` is not a colour: write #rgb or #rrggbb", s));
    auto channel = [&](std::size_t i) {
        return s.size() == 4 ? digit(s[1 + i]) * 17 : digit(s[1 + 2 * i]) * 16 + digit(s[2 + 2 * i]);
    };
    return Rgb{channel(0) / 255.0, channel(1) / 255.0, channel(2) / 255.0};
}

int byte(double c) { return static_cast<int>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0)); }
std::string hex(Rgb c) { return std::format("#{:02x}{:02x}{:02x}", byte(c.r), byte(c.g), byte(c.b)); }
Rgb quantised(Rgb c) { return {byte(c.r) / 255.0, byte(c.g) / 255.0, byte(c.b) / 255.0}; }

double linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double gamma(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }

double luminance(Rgb c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); }

double ratio(Rgb a, Rgb b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// Two decimals, never rounded up past a threshold: 4.499 reads 4.49:1.
std::string ratio_text(double r) { return std::format("{:.2f}:1", std::floor(r * 100.0) / 100.0); }

// ── colour-vision deficiencies ──

using Matrix = std::array<std::array<double, 3>, 3>;

Rgb apply(const Matrix& m, Rgb c) {
    const double r = linear(c.r), g = linear(c.g), b = linear(c.b);
    auto row = [&](int i) { return std::clamp(m[i][0] * r + m[i][1] * g + m[i][2] * b, 0.0, 1.0); };
    return {gamma(row(0)), gamma(row(1)), gamma(row(2))};
}

struct Deficiency {
    std::string_view kind, label;
    Matrix           m;
};

constexpr std::array<Deficiency, 3> dichromacies{{
    {"protanopia", "No red cones", {{{0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}}}},
    {"deuteranopia", "No green cones", {{{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}}}},
    {"tritanopia", "No blue cones", {{{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}}}},
}};

Rgb grey(Rgb c) {
    const double y = gamma(luminance(c));
    return {y, y, y};
}

// ── OKLab ──

struct Lab {
    double l = 0, a = 0, b = 0;
};

Lab oklab(Rgb c) {
    const double r = linear(c.r), g = linear(c.g), b = linear(c.b);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

Rgb srgb(Lab c) {
    const double l = c.l + 0.3963377774 * c.a + 0.2158037573 * c.b;
    const double m = c.l - 0.1055613458 * c.a - 0.0638541728 * c.b;
    const double s = c.l - 0.0894841775 * c.a - 1.2914855480 * c.b;
    const double L = l * l * l, M = m * m * m, S = s * s * s;
    const double r = 4.0767416621 * L - 3.3077115913 * M + 0.2309699292 * S;
    const double g = -1.2684380046 * L + 2.6097574011 * M - 0.3413193965 * S;
    const double b = -0.0041960863 * L - 0.7034186147 * M + 1.7076147010 * S;
    return {gamma(std::clamp(r, 0.0, 1.0)), gamma(std::clamp(g, 0.0, 1.0)), gamma(std::clamp(b, 0.0, 1.0))};
}

// The lightness between `from` and `to` (one of them meeting the target)
// nearest `from` that meets it, as a colour; nothing when `to` does not.
std::optional<Rgb> search(Lab base, Rgb bg, double target, double to) {
    auto meets = [&](double l) { return ratio(quantised(srgb({l, base.a, base.b})), bg) >= target; };
    if (!meets(to)) return std::nullopt;
    double near = base.l, far = to;  // near: does not meet; far: meets
    for (int i = 0; i < 48; ++i) {
        const double mid = (near + far) / 2;
        (meets(mid) ? far : near) = mid;
    }
    return quantised(srgb({far, base.a, base.b}));
}

}  // namespace

std::expected<Report, std::string> check(const std::string& foreground, const std::string& background) {
    auto fg = parse(foreground);
    if (!fg) return std::unexpected(fg.error());
    auto bg = parse(background);
    if (!bg) return std::unexpected(bg.error());
    const double r = ratio(*fg, *bg);
    return Report{.foreground = hex(*fg), .background = hex(*bg), .ratio = r, .text = ratio_text(r), .aa = r >= 4.5, .aa_large = r >= 3.0,
                  .aaa = r >= 7.0, .aaa_large = r >= 4.5, .ui = r >= 3.0};
}

std::expected<std::vector<View>, std::string> simulate(const std::string& foreground, const std::string& background) {
    auto fg = parse(foreground);
    if (!fg) return std::unexpected(fg.error());
    auto bg = parse(background);
    if (!bg) return std::unexpected(bg.error());
    std::vector<View> out;
    auto view = [&](std::string_view kind, std::string_view label, Rgb f, Rgb b) {
        f = quantised(f);
        b = quantised(b);
        out.push_back({std::string(kind), std::string(label), hex(f), hex(b), ratio_text(ratio(f, b))});
    };
    for (const Deficiency& d : dichromacies) view(d.kind, d.label, apply(d.m, *fg), apply(d.m, *bg));
    view("achromatopsia", "No colour at all", grey(*fg), grey(*bg));
    return out;
}

std::expected<std::optional<std::string>, std::string> suggest(const std::string& foreground, const std::string& background, double target) {
    auto fg = parse(foreground);
    if (!fg) return std::unexpected(fg.error());
    auto bg = parse(background);
    if (!bg) return std::unexpected(bg.error());
    if (!(target >= 1.0 && target <= 21.0)) return std::unexpected(std::format("a target ratio is 1 to 21, not {}", target));
    if (ratio(*fg, *bg) >= target) return std::optional<std::string>{hex(*fg)};
    const Lab base = oklab(*fg);
    const auto darker = search(base, *bg, target, 0.0);
    const auto lighter = search(base, *bg, target, 1.0);
    if (!darker && !lighter) return std::optional<std::string>{};
    auto distance = [&](const std::optional<Rgb>& c) { return c ? std::abs(oklab(*c).l - base.l) : 2.0; };
    return std::optional<std::string>{hex(distance(darker) <= distance(lighter) ? *darker : *lighter)};
}

}  // namespace colors::contrast
