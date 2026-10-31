module hanim;

import std;

namespace hanim {

namespace {

constexpr double pow2(double x) noexcept { return x * x; }

// 2^x through the deterministic exp.
double exp2(double x) noexcept { return dm::exp(x * dm::ln2); }

double bounce_out(double t) noexcept {
    constexpr double n1 = 7.5625;
    constexpr double d1 = 2.75;
    if (t < 1.0 / d1) return n1 * t * t;
    if (t < 2.0 / d1) {
        t -= 1.5 / d1;
        return n1 * t * t + 0.75;
    }
    if (t < 2.5 / d1) {
        t -= 2.25 / d1;
        return n1 * t * t + 0.9375;
    }
    t -= 2.625 / d1;
    return n1 * t * t + 0.984375;
}

// CSS cubic-bezier: x(s) and y(s) are the curve's coordinates at parameter s;
// the eased value at progress x is y(s) where x(s) = x. The solve uses a fixed
// number of Newton steps, then a fixed bisection if Newton did not land, so the
// answer depends on the inputs only.
double bezier_coord(double s, double p1, double p2) noexcept {
    // Bernstein form with P0 = 0 and P3 = 1.
    double u = 1.0 - s;
    return 3.0 * u * u * s * p1 + 3.0 * u * s * s * p2 + s * s * s;
}

double bezier_slope(double s, double p1, double p2) noexcept {
    double u = 1.0 - s;
    return 3.0 * u * u * p1 + 6.0 * u * s * (p2 - p1) + 3.0 * s * s * (1.0 - p2);
}

double solve_bezier(double x, const std::array<double, 4>& p) noexcept {
    double x1 = p[0], y1 = p[1], x2 = p[2], y2 = p[3];
    if (x1 == y1 && x2 == y2) return x;  // the curve is the diagonal
    double s = x;
    bool solved = false;
    for (int i = 0; i < 8; ++i) {
        double err = bezier_coord(s, x1, x2) - x;
        if (dm::abs(err) < 1e-12) {
            solved = true;
            break;
        }
        double d = bezier_slope(s, x1, x2);
        if (dm::abs(d) < 1e-9) break;
        s -= err / d;
        if (s < 0.0 || s > 1.0) break;
    }
    if (!solved) {
        double lo = 0.0, hi = 1.0;
        s = x;
        for (int i = 0; i < 52; ++i) {
            double v = bezier_coord(s, x1, x2);
            if (v < x) lo = s;
            else hi = s;
            s = 0.5 * (lo + hi);
        }
    }
    return bezier_coord(s, y1, y2);
}

double solve_steps(double t, const std::array<double, 4>& p) noexcept {
    int n = static_cast<int>(p[0]);
    auto at = static_cast<StepPosition>(static_cast<int>(p[1]));
    double step = std::floor(t * n);
    int jumps = n;
    switch (at) {
        case StepPosition::jump_start: step += 1; break;
        case StepPosition::jump_end:   break;
        case StepPosition::jump_none:  jumps = n - 1; break;
        case StepPosition::jump_both:  step += 1; jumps = n + 1; break;
    }
    if (t >= 1.0 && at != StepPosition::jump_start && at != StepPosition::jump_both) step = n;
    if (jumps < 1) return t < 1.0 ? 0.0 : 1.0;
    double v = step / jumps;
    return dm::clamp(v, 0.0, 1.0);
}

}  // namespace

double Ease::operator()(double t) const noexcept {
    if (!(t > 0.0)) t = 0.0;  // also maps NaN to 0
    if (t > 1.0) t = 1.0;
    // Every curve but `steps` passes through (0, 0) and (1, 1) exactly; the
    // formulas below can miss by an ulp or return −0.
    if (kind != EaseKind::steps && (t == 0.0 || t == 1.0)) return t;
    constexpr double c1 = 1.70158;
    constexpr double c2 = c1 * 1.525;
    constexpr double c3 = c1 + 1.0;
    constexpr double c4 = dm::two_pi / 3.0;
    constexpr double c5 = dm::two_pi / 4.5;
    switch (kind) {
        case EaseKind::linear: return t;

        case EaseKind::in_quad:     return t * t;
        case EaseKind::out_quad:    return 1.0 - (1.0 - t) * (1.0 - t);
        case EaseKind::in_out_quad: return t < 0.5 ? 2.0 * t * t : 1.0 - pow2(-2.0 * t + 2.0) / 2.0;

        case EaseKind::in_cubic:     return t * t * t;
        case EaseKind::out_cubic:    { double u = 1.0 - t; return 1.0 - u * u * u; }
        case EaseKind::in_out_cubic: {
            if (t < 0.5) return 4.0 * t * t * t;
            double u = -2.0 * t + 2.0;
            return 1.0 - u * u * u / 2.0;
        }

        case EaseKind::in_quart:     return t * t * t * t;
        case EaseKind::out_quart:    { double u = 1.0 - t; return 1.0 - u * u * u * u; }
        case EaseKind::in_out_quart: {
            if (t < 0.5) return 8.0 * t * t * t * t;
            double u = -2.0 * t + 2.0;
            return 1.0 - u * u * u * u / 2.0;
        }

        case EaseKind::in_quint:     return t * t * t * t * t;
        case EaseKind::out_quint:    { double u = 1.0 - t; return 1.0 - u * u * u * u * u; }
        case EaseKind::in_out_quint: {
            if (t < 0.5) return 16.0 * t * t * t * t * t;
            double u = -2.0 * t + 2.0;
            return 1.0 - u * u * u * u * u / 2.0;
        }

        case EaseKind::in_sine:     return 1.0 - dm::cos(t * dm::half_pi);
        case EaseKind::out_sine:    return dm::sin(t * dm::half_pi);
        case EaseKind::in_out_sine: return -(dm::cos(dm::pi * t) - 1.0) / 2.0;

        case EaseKind::in_expo:  return t == 0.0 ? 0.0 : exp2(10.0 * t - 10.0);
        case EaseKind::out_expo: return t == 1.0 ? 1.0 : 1.0 - exp2(-10.0 * t);
        case EaseKind::in_out_expo:
            if (t == 0.0 || t == 1.0) return t;
            return t < 0.5 ? exp2(20.0 * t - 10.0) / 2.0 : (2.0 - exp2(-20.0 * t + 10.0)) / 2.0;

        case EaseKind::in_circ:     return 1.0 - dm::sqrt(1.0 - t * t);
        case EaseKind::out_circ:    return dm::sqrt(1.0 - pow2(t - 1.0));
        case EaseKind::in_out_circ:
            return t < 0.5 ? (1.0 - dm::sqrt(1.0 - pow2(2.0 * t))) / 2.0
                           : (dm::sqrt(1.0 - pow2(-2.0 * t + 2.0)) + 1.0) / 2.0;

        case EaseKind::in_back:  return c3 * t * t * t - c1 * t * t;
        case EaseKind::out_back: {
            double u = t - 1.0;
            return 1.0 + c3 * u * u * u + c1 * u * u;
        }
        case EaseKind::in_out_back:
            return t < 0.5 ? (pow2(2.0 * t) * ((c2 + 1.0) * 2.0 * t - c2)) / 2.0
                           : (pow2(2.0 * t - 2.0) * ((c2 + 1.0) * (t * 2.0 - 2.0) + c2) + 2.0) / 2.0;

        case EaseKind::in_elastic:
            if (t == 0.0 || t == 1.0) return t;
            return -exp2(10.0 * t - 10.0) * dm::sin((t * 10.0 - 10.75) * c4);
        case EaseKind::out_elastic:
            if (t == 0.0 || t == 1.0) return t;
            return exp2(-10.0 * t) * dm::sin((t * 10.0 - 0.75) * c4) + 1.0;
        case EaseKind::in_out_elastic:
            if (t == 0.0 || t == 1.0) return t;
            return t < 0.5 ? -(exp2(20.0 * t - 10.0) * dm::sin((20.0 * t - 11.125) * c5)) / 2.0
                           : (exp2(-20.0 * t + 10.0) * dm::sin((20.0 * t - 11.125) * c5)) / 2.0 + 1.0;

        case EaseKind::in_bounce:     return 1.0 - bounce_out(1.0 - t);
        case EaseKind::out_bounce:    return bounce_out(t);
        case EaseKind::in_out_bounce:
            return t < 0.5 ? (1.0 - bounce_out(1.0 - 2.0 * t)) / 2.0 : (1.0 + bounce_out(2.0 * t - 1.0)) / 2.0;

        case EaseKind::cubic_bezier:
            if (t == 0.0 || t == 1.0) return t;
            return solve_bezier(t, p);
        case EaseKind::steps: return solve_steps(t, p);
    }
    return t;
}

double Ease::slope(double t) const noexcept {
    constexpr double h = 1.0 / 4096.0;  // a power of two: t ± h is exact for t on the 2^-12 grid
    double a = t - h < 0.0 ? 0.0 : t - h;
    double b = t + h > 1.0 ? 1.0 : t + h;
    if (b <= a) return 0.0;
    return ((*this)(b) - (*this)(a)) / (b - a);
}

namespace {

constexpr std::array<std::pair<EaseKind, std::string_view>, 31> names{{
    {EaseKind::linear, "linear"},
    {EaseKind::in_quad, "in_quad"},       {EaseKind::out_quad, "out_quad"},       {EaseKind::in_out_quad, "in_out_quad"},
    {EaseKind::in_cubic, "in_cubic"},     {EaseKind::out_cubic, "out_cubic"},     {EaseKind::in_out_cubic, "in_out_cubic"},
    {EaseKind::in_quart, "in_quart"},     {EaseKind::out_quart, "out_quart"},     {EaseKind::in_out_quart, "in_out_quart"},
    {EaseKind::in_quint, "in_quint"},     {EaseKind::out_quint, "out_quint"},     {EaseKind::in_out_quint, "in_out_quint"},
    {EaseKind::in_sine, "in_sine"},       {EaseKind::out_sine, "out_sine"},       {EaseKind::in_out_sine, "in_out_sine"},
    {EaseKind::in_expo, "in_expo"},       {EaseKind::out_expo, "out_expo"},       {EaseKind::in_out_expo, "in_out_expo"},
    {EaseKind::in_circ, "in_circ"},       {EaseKind::out_circ, "out_circ"},       {EaseKind::in_out_circ, "in_out_circ"},
    {EaseKind::in_back, "in_back"},       {EaseKind::out_back, "out_back"},       {EaseKind::in_out_back, "in_out_back"},
    {EaseKind::in_elastic, "in_elastic"}, {EaseKind::out_elastic, "out_elastic"}, {EaseKind::in_out_elastic, "in_out_elastic"},
    {EaseKind::in_bounce, "in_bounce"},   {EaseKind::out_bounce, "out_bounce"},   {EaseKind::in_out_bounce, "in_out_bounce"},
}};

constexpr std::array<std::string_view, 4> step_names{"jump-start", "jump-end", "jump-none", "jump-both"};

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

std::optional<double> number(std::string_view s) {
    s = trim(s);
    double v = 0;
    auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end != s.data() + s.size()) return std::nullopt;
    return v;
}

// The arguments of "name(a, b, …)", split at commas.
std::optional<std::vector<std::string_view>> arguments(std::string_view text, std::string_view name) {
    if (!text.starts_with(name)) return std::nullopt;
    text.remove_prefix(name.size());
    text = trim(text);
    if (text.size() < 2 || text.front() != '(' || text.back() != ')') return std::nullopt;
    text = text.substr(1, text.size() - 2);
    std::vector<std::string_view> out;
    while (true) {
        auto comma = text.find(',');
        out.push_back(trim(text.substr(0, comma)));
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    return out;
}

}  // namespace

std::string to_string(const Ease& e) {
    if (e.kind == EaseKind::cubic_bezier)
        return std::format("cubic-bezier({}, {}, {}, {})", e.p[0], e.p[1], e.p[2], e.p[3]);
    if (e.kind == EaseKind::steps)
        return std::format("steps({}, {})", static_cast<int>(e.p[0]), step_names[static_cast<int>(e.p[1]) & 3]);
    for (auto& [k, n] : names)
        if (k == e.kind) return std::string(n);
    return "linear";
}

std::optional<Ease> parse_ease(std::string_view text) {
    text = trim(text);
    for (auto& [k, n] : names)
        if (n == text) return Ease{k};
    if (text == "ease") return ease::css_ease;
    if (text == "ease-in") return ease::css_ease_in;
    if (text == "ease-out") return ease::css_ease_out;
    if (text == "ease-in-out") return ease::css_ease_in_out;
    if (text == "step-start") return Ease::stepped(1, StepPosition::jump_start);
    if (text == "step-end") return Ease::stepped(1, StepPosition::jump_end);
    if (text == "standard") return ease::standard;
    if (text == "emphasized") return ease::emphasized;
    if (auto args = arguments(text, "cubic-bezier"); args && args->size() == 4) {
        std::array<double, 4> v{};
        for (int i = 0; i < 4; ++i) {
            auto n = number((*args)[i]);
            if (!n) return std::nullopt;
            v[i] = *n;
        }
        if (v[0] < 0 || v[0] > 1 || v[2] < 0 || v[2] > 1) return std::nullopt;
        return Ease::bezier(v[0], v[1], v[2], v[3]);
    }
    if (auto args = arguments(text, "steps"); args && (args->size() == 1 || args->size() == 2)) {
        auto n = number((*args)[0]);
        if (!n || *n < 1 || *n != std::floor(*n)) return std::nullopt;
        auto at = StepPosition::jump_end;
        if (args->size() == 2) {
            auto s = (*args)[1];
            if (s == "start") s = "jump-start";
            if (s == "end") s = "jump-end";
            auto it = std::ranges::find(step_names, s);
            if (it == step_names.end()) return std::nullopt;
            at = static_cast<StepPosition>(it - step_names.begin());
        }
        return Ease::stepped(static_cast<int>(*n), at);
    }
    return std::nullopt;
}

}  // namespace hanim
