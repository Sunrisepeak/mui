// Colour science for the contrast checker: WCAG 2.2's relative luminance and
// contrast ratio, the colour-vision deficiencies of Machado, Oliveira and
// Fernandes (2009) at full severity, and OKLab (Ottosson 2020, hanim.core's)
// for the nearest passing colour.
module colors;

import std;
import hanim.core;

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

double linear(double c) { return hanim::srgb_to_linear(c); }
double gamma(double c) { return hanim::linear_to_srgb(c); }

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

using Lab = hanim::OkLab;

Lab oklab(Rgb c) { return hanim::linear_to_oklab({linear(c.r), linear(c.g), linear(c.b)}); }

Rgb srgb(Lab c) {
    const auto l = hanim::oklab_to_linear(c);
    return {gamma(std::clamp(l.r, 0.0, 1.0)), gamma(std::clamp(l.g, 0.0, 1.0)), gamma(std::clamp(l.b, 0.0, 1.0))};
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
