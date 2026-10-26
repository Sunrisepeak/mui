module openmui.anim;

import std;

namespace omui::anim {

SpringParams SpringParams::response(double response_seconds, double bounce) noexcept {
    double r = response_seconds > 1e-6 ? response_seconds : 1e-6;
    double w0 = dm::two_pi / r;
    double b = dm::clamp(bounce, -0.999, 0.999);
    double zeta = b >= 0.0 ? 1.0 - b : 1.0 / (1.0 + b);
    return {w0 * w0, 2.0 * zeta * w0, 1.0};
}

double SpringParams::natural_frequency() const noexcept {
    return mass > 0 && stiffness > 0 ? dm::sqrt(stiffness / mass) : 0.0;
}

double SpringParams::damping_ratio() const noexcept {
    double km = stiffness * mass;
    return km > 0 ? damping / (2.0 * dm::sqrt(km)) : 0.0;
}

namespace {

// The first t ≥ 0 at which `done(t)` holds, for a predicate that stays true
// once true. Doubling to bracket, then a fixed bisection: the answer depends on
// the inputs only.
template <class F>
double first_true(F done, double scale) {
    if (done(0.0)) return 0.0;
    double hi = scale > 0 ? scale : 1.0;
    int guard = 0;
    while (!done(hi)) {
        hi *= 2.0;
        if (++guard > 200) return std::numeric_limits<double>::infinity();
    }
    double lo = 0.0;
    for (int i = 0; i < 64; ++i) {
        double mid = 0.5 * (lo + hi);
        if (done(mid)) hi = mid;
        else lo = mid;
    }
    return hi;
}

double settle_from_envelope(double amplitude, double limit, double rate) {
    // amplitude · e^(−rate·t) ≤ limit
    if (amplitude <= limit) return 0.0;
    if (rate <= 0.0) return std::numeric_limits<double>::infinity();
    return dm::log(amplitude / limit) / rate;
}

}  // namespace

Oscillator::Oscillator(double x0, double v0, double target, SpringParams p, Rest rest) noexcept
    : x0_(x0), v0_(v0), target_(target) {
    double A = x0 - target;
    double w0 = p.natural_frequency();
    if ((dm::abs(A) < rest.delta && dm::abs(v0) < rest.speed) || w0 <= 0.0) {
        regime_ = Regime::rest;
        settle_ = 0.0;
        return;
    }
    double zeta = p.damping_ratio();
    if (zeta < 1.0 - 1e-9) {
        regime_ = Regime::under;
        k_ = zeta * w0;
        w_ = w0 * dm::sqrt(1.0 - zeta * zeta);
        a_ = A;
        b_ = (v0 + k_ * A) / w_;
        double amp = dm::sqrt(a_ * a_ + b_ * b_);
        settle_ = std::max(settle_from_envelope(amp, rest.delta, k_),
                           settle_from_envelope(amp * w0, rest.speed, k_));
    } else if (zeta <= 1.0 + 1e-9) {
        regime_ = Regime::critical;
        k_ = w0;
        a_ = A;
        b_ = v0 + w0 * A;
        double pa = dm::abs(a_), pb = dm::abs(b_), k = k_;
        settle_ = first_true(
            [&](double t) {
                double e = dm::exp(-k * t);
                return e * (pa + pb * t) <= rest.delta && e * (pb + k * pa + k * pb * t) <= rest.speed;
            },
            1.0 / k);
    } else {
        regime_ = Regime::over;
        double s = dm::sqrt(zeta * zeta - 1.0);
        k_ = -w0 * (zeta - s);  // r1, the slow root
        w_ = -w0 * (zeta + s);  // r2
        a_ = (v0 - w_ * A) / (k_ - w_);
        b_ = A - a_;
        double amp = dm::abs(a_) + dm::abs(b_);
        double vamp = dm::abs(a_ * k_) + dm::abs(b_ * w_);
        settle_ = std::max(settle_from_envelope(amp, rest.delta, -k_),
                           settle_from_envelope(vamp, rest.speed, -k_));
    }
}

double Oscillator::position(double t) const noexcept {
    if (t >= settle_) return target_;
    if (t <= 0.0) return x0_;
    switch (regime_) {
        case Regime::rest: return target_;
        case Regime::under: {
            double e = dm::exp(-k_ * t);
            return target_ + e * (a_ * dm::cos(w_ * t) + b_ * dm::sin(w_ * t));
        }
        case Regime::critical: return target_ + dm::exp(-k_ * t) * (a_ + b_ * t);
        case Regime::over: return target_ + a_ * dm::exp(k_ * t) + b_ * dm::exp(w_ * t);
    }
    return target_;
}

double Oscillator::velocity(double t) const noexcept {
    if (t >= settle_) return 0.0;
    if (t < 0.0) return 0.0;
    switch (regime_) {
        case Regime::rest: return 0.0;
        case Regime::under: {
            double e = dm::exp(-k_ * t);
            double c = dm::cos(w_ * t), s = dm::sin(w_ * t);
            return e * ((b_ * w_ - k_ * a_) * c - (a_ * w_ + k_ * b_) * s);
        }
        case Regime::critical: return dm::exp(-k_ * t) * (b_ - k_ * (a_ + b_ * t));
        case Regime::over: return a_ * k_ * dm::exp(k_ * t) + b_ * w_ * dm::exp(w_ * t);
    }
    return 0.0;
}

Glide::Glide(double x0, double v0, DecayParams p) noexcept : x0_(x0), v0_(v0) {
    if (!(p.rate > 0.0 && p.rate < 1.0) || v0 == 0.0) {
        tau_ = 0.0;
        settle_ = 0.0;
        return;
    }
    tau_ = -1.0 / (1000.0 * dm::log(p.rate));
    double speed = dm::abs(v0);
    settle_ = std::max(settle_from_envelope(speed, p.rest.speed, 1.0 / tau_),
                       settle_from_envelope(speed * tau_, p.rest.delta, 1.0 / tau_));
}

double Glide::position(double t) const noexcept {
    if (t <= 0.0) return x0_;
    if (t >= settle_) return destination();
    return x0_ + v0_ * tau_ * (1.0 - dm::exp(-t / tau_));
}

double Glide::velocity(double t) const noexcept {
    if (t < 0.0 || t >= settle_) return 0.0;
    return v0_ * dm::exp(-t / tau_);
}

double interpolate(double x, std::span<const double> input, std::span<const double> output,
                   InterpolateOptions options) noexcept {
    std::size_t n = std::min(input.size(), output.size());
    if (n == 0) return x;
    if (n == 1) return output[0];
    auto segment = [&](std::size_t i, double u, bool eased) {
        return dm::mix(output[i], output[i + 1], eased ? options.ease(u) : u);
    };
    auto unit = [&](std::size_t i) {
        double span = input[i + 1] - input[i];
        return span > 0.0 ? (x - input[i]) / span : 1.0;
    };
    if (x < input[0]) {
        switch (options.left) {
            case Extrapolate::clamp:    return output[0];
            case Extrapolate::identity: return x;
            case Extrapolate::extend:   return segment(0, unit(0), false);
        }
    }
    if (x > input[n - 1]) {
        switch (options.right) {
            case Extrapolate::clamp:    return output[n - 1];
            case Extrapolate::identity: return x;
            case Extrapolate::extend:   return segment(n - 2, unit(n - 2), false);
        }
    }
    std::size_t i = static_cast<std::size_t>(std::ranges::upper_bound(input.first(n), x) - input.begin());
    if (i >= n) return output[n - 1];
    if (i == 0) i = 1;
    return segment(i - 1, unit(i - 1), true);
}

}  // namespace omui::anim
