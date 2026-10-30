// The fdlibm algorithms (Sun Microsystems, 1993: "Permission to use, copy,
// modify, and distribute this software is freely granted, provided that this
// notice is preserved"), transcribed to C++ with the words of a double read and
// written through `std::bit_cast` instead of pointer punning.
module openmui.math;

import std;

namespace omui::math::det {

namespace {

constexpr std::int32_t high_word(double x) noexcept {
    return static_cast<std::int32_t>(std::bit_cast<std::uint64_t>(x) >> 32);
}

constexpr std::uint32_t low_word(double x) noexcept {
    return static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(x));
}

constexpr double from_words(std::int32_t hi, std::uint32_t lo) noexcept {
    return std::bit_cast<double>((static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi)) << 32) | lo);
}

constexpr double with_high(double x, std::int32_t hi) noexcept { return from_words(hi, low_word(x)); }
constexpr double with_low(double x, std::uint32_t lo) noexcept { return from_words(high_word(x), lo); }

// ── exp ────────────────────────────────────────────────────────────────────

constexpr double half[2]  = {0.5, -0.5};
constexpr double ln2_hi[2] = {6.93147180369123816490e-01, -6.93147180369123816490e-01};
constexpr double ln2_lo[2] = {1.90821492927058770002e-10, -1.90821492927058770002e-10};
constexpr double inv_ln2  = 1.44269504088896338700e+00;
constexpr double o_threshold = 7.09782712893383973096e+02;
constexpr double u_threshold = -7.45133219101941108420e+02;
constexpr double P1 = 1.66666666666666019037e-01;
constexpr double P2 = -2.77777777770155933842e-03;
constexpr double P3 = 6.61375632143793436117e-05;
constexpr double P4 = -1.65339022054652515390e-06;
constexpr double P5 = 4.13813679705723846039e-08;
constexpr double two_m1000 = 9.33263618503218878990e-302;

// ── trigonometric kernels on [-π/4, π/4] ──────────────────────────────────

constexpr double S1 = -1.66666666666666324348e-01;
constexpr double S2 = 8.33333333332248946124e-03;
constexpr double S3 = -1.98412698298579493134e-04;
constexpr double S4 = 2.75573137070700676789e-06;
constexpr double S5 = -2.50507602534068634195e-08;
constexpr double S6 = 1.58969099521155010221e-10;

double kernel_sin(double x, double y, bool tail) noexcept {
    std::int32_t ix = high_word(x) & 0x7fffffff;
    if (ix < 0x3e400000 && static_cast<int>(x) == 0) return x;  // |x| < 2^-27
    double z = x * x;
    double v = z * x;
    double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (!tail) return x + v * (S1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

constexpr double C1 = 4.16666666666666019037e-02;
constexpr double C2 = -1.38888888888741095749e-03;
constexpr double C3 = 2.48015872894767294178e-05;
constexpr double C4 = -2.75573143513906633035e-07;
constexpr double C5 = 2.08757232129817482790e-09;
constexpr double C6 = -1.13596475577881948265e-11;

double kernel_cos(double x, double y) noexcept {
    std::int32_t ix = high_word(x) & 0x7fffffff;
    if (ix < 0x3e400000 && static_cast<int>(x) == 0) return 1.0;
    double z = x * x;
    double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3fd33333) return 1.0 - (0.5 * z - (z * r - x * y));  // |x| < 0.3
    double qx = ix > 0x3fe90000 ? 0.28125 : from_words(ix - 0x00200000, 0);
    double hz = 0.5 * z - qx;
    double a  = 1.0 - qx;
    return a - (hz - (z * r - x * y));
}

// x − n·π/2 as y0 + y1, returning n. Cody–Waite with π/2 in three parts, which
// is exact enough for |x| < 2^19·π/2; a larger argument is first reduced by an
// exact `fmod` with 2π (fmod is an exact operation, so the reduction is the
// same everywhere, if less accurate than Payne–Hanek — animation never asks).
constexpr double inv_pio2 = 6.36619772367581382433e-01;
constexpr double pio2_1   = 1.57079632673412561417e+00;
constexpr double pio2_1t  = 6.07710050650619224932e-11;
constexpr double pio2_2   = 6.07710050630396597660e-11;
constexpr double pio2_2t  = 2.02226624879595063154e-21;
constexpr double pio2_3   = 2.02226624871116645580e-21;
constexpr double pio2_3t  = 8.47842766036889956997e-32;

int rem_pio2(double x, double& y0, double& y1) noexcept {
    std::int32_t hx = high_word(x);
    std::int32_t ix = hx & 0x7fffffff;
    if (ix > 0x413921fb) {
        x  = std::fmod(x, two_pi);
        hx = high_word(x);
        ix = hx & 0x7fffffff;
    }
    double t  = abs(x);
    int n     = static_cast<int>(t * inv_pio2 + 0.5);
    double fn = static_cast<double>(n);
    double r  = t - fn * pio2_1;
    double w  = fn * pio2_1t;
    std::int32_t j = ix >> 20;
    y0 = r - w;
    std::int32_t i = j - ((high_word(y0) >> 20) & 0x7ff);
    if (i > 16) {  // 2nd iteration: 118 bits of π/2
        t  = r;
        w  = fn * pio2_2;
        r  = t - w;
        w  = fn * pio2_2t - ((t - r) - w);
        y0 = r - w;
        i  = j - ((high_word(y0) >> 20) & 0x7ff);
        if (i > 49) {  // 3rd iteration: 151 bits
            t  = r;
            w  = fn * pio2_3;
            r  = t - w;
            w  = fn * pio2_3t - ((t - r) - w);
            y0 = r - w;
        }
    }
    y1 = (r - y0) - w;
    if (hx < 0) {
        y0 = -y0;
        y1 = -y1;
        return -n;
    }
    return n;
}

// ── log ────────────────────────────────────────────────────────────────────

constexpr double two54 = 1.80143985094819840000e+16;
constexpr double Lg1 = 6.666666666666735130e-01;
constexpr double Lg2 = 3.999999999940941908e-01;
constexpr double Lg3 = 2.857142874366239149e-01;
constexpr double Lg4 = 2.222219843214978396e-01;
constexpr double Lg5 = 1.818357216161805012e-01;
constexpr double Lg6 = 1.531383769920937332e-01;
constexpr double Lg7 = 1.479819860511658591e-01;

// ── atan ───────────────────────────────────────────────────────────────────

constexpr double atan_hi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01,
                              9.82793723247329054082e-01, 1.57079632679489655800e+00};
constexpr double atan_lo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17,
                              1.39033110312309984516e-17, 6.12323399573676603587e-17};
constexpr double aT[] = {3.33333333333329318027e-01,  -1.99999999998764832476e-01,
                         1.42857142725034663711e-01,  -1.11111104054623557880e-01,
                         9.09088713343650656196e-02,  -7.69187620504482999495e-02,
                         6.66107313738753120669e-02,  -5.83357013379057348645e-02,
                         4.97687799461593236017e-02,  -3.65315727442169155270e-02,
                         1.62858201153657823623e-02};
constexpr double pi_lo = 1.2246467991473531772e-16;

}  // namespace

double exp(double x) noexcept {
    std::uint32_t hx = static_cast<std::uint32_t>(high_word(x));
    int xsb = static_cast<int>((hx >> 31) & 1);
    hx &= 0x7fffffff;

    if (hx >= 0x40862e42) {  // |x| ≥ 709.78…
        if (hx >= 0x7ff00000) {
            if (((hx & 0xfffff) | low_word(x)) != 0) return x + x;  // NaN
            return xsb == 0 ? x : 0.0;                              // exp(±∞) = ∞ / 0
        }
        if (x > o_threshold) return std::numeric_limits<double>::infinity();
        if (x < u_threshold) return 0.0;
    }

    double hi = 0, lo = 0;
    int k = 0;
    if (hx > 0x3fd62e42) {  // |x| > 0.5 ln2
        if (hx < 0x3ff0a2b2) {  // and < 1.5 ln2
            hi = x - ln2_hi[xsb];
            lo = ln2_lo[xsb];
            k  = 1 - xsb - xsb;
        } else {
            k = static_cast<int>(inv_ln2 * x + half[xsb]);
            double t = k;
            hi = x - t * ln2_hi[0];
            lo = t * ln2_lo[0];
        }
        x = hi - lo;
    } else if (hx < 0x3e300000) {  // |x| < 2^-28
        return 1.0 + x;
    }

    double t = x * x;
    double c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    if (k == 0) return 1.0 - ((x * c) / (c - 2.0) - x);
    double y = 1.0 - ((lo - (x * c) / (2.0 - c)) - hi);
    if (k >= -1021) return with_high(y, high_word(y) + (k << 20));
    return with_high(y, high_word(y) + ((k + 1000) << 20)) * two_m1000;
}

double log(double x) noexcept {
    std::int32_t hx = high_word(x);
    std::uint32_t lx = low_word(x);
    int k = 0;
    if (hx < 0x00100000) {  // x < 2^-1022
        if (((hx & 0x7fffffff) | static_cast<std::int32_t>(lx)) == 0)
            return -std::numeric_limits<double>::infinity();
        if (hx < 0) return std::numeric_limits<double>::quiet_NaN();
        k -= 54;
        x *= two54;  // subnormal: scale up
        hx = high_word(x);
    }
    if (hx >= 0x7ff00000) return x + x;
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    std::int32_t i = (hx + 0x95f64) & 0x100000;
    x = with_high(x, hx | (i ^ 0x3ff00000));  // normalise x or x/2
    k += (i >> 20);
    double f = x - 1.0;
    double dk = static_cast<double>(k);
    if ((0x000fffff & (2 + hx)) < 3) {  // |f| < 2^-20
        if (f == 0.0) return k == 0 ? 0.0 : dk * ln2_hi[0] + dk * ln2_lo[0];
        double R = f * f * (0.5 - 0.33333333333333333 * f);
        if (k == 0) return f - R;
        return dk * ln2_hi[0] - ((R - dk * ln2_lo[0]) - f);
    }
    double s = f / (2.0 + f);
    double z = s * s;
    i = hx - 0x6147a;
    double w = z * z;
    std::int32_t j = 0x6b851 - hx;
    double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    i |= j;
    double R = t2 + t1;
    if (i > 0) {
        double hfsq = 0.5 * f * f;
        if (k == 0) return f - (hfsq - s * (hfsq + R));
        return dk * ln2_hi[0] - ((hfsq - (s * (hfsq + R) + dk * ln2_lo[0])) - f);
    }
    if (k == 0) return f - s * (f - R);
    return dk * ln2_hi[0] - ((s * (f - R) - dk * ln2_lo[0]) - f);
}

double sin(double x) noexcept {
    std::int32_t ix = high_word(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return kernel_sin(x, 0.0, false);  // |x| ≤ π/4
    if (ix >= 0x7ff00000) return x - x;                       // ∞ or NaN
    double y0 = 0, y1 = 0;
    switch (rem_pio2(x, y0, y1) & 3) {
        case 0:  return kernel_sin(y0, y1, true);
        case 1:  return kernel_cos(y0, y1);
        case 2:  return -kernel_sin(y0, y1, true);
        default: return -kernel_cos(y0, y1);
    }
}

double cos(double x) noexcept {
    std::int32_t ix = high_word(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return kernel_cos(x, 0.0);
    if (ix >= 0x7ff00000) return x - x;
    double y0 = 0, y1 = 0;
    switch (rem_pio2(x, y0, y1) & 3) {
        case 0:  return kernel_cos(y0, y1);
        case 1:  return -kernel_sin(y0, y1, true);
        case 2:  return -kernel_cos(y0, y1);
        default: return kernel_sin(y0, y1, true);
    }
}

double atan(double x) noexcept {
    std::int32_t hx = high_word(x);
    std::int32_t ix = hx & 0x7fffffff;
    if (ix >= 0x44100000) {  // |x| ≥ 2^66
        if (ix > 0x7ff00000 || (ix == 0x7ff00000 && low_word(x) != 0)) return x + x;
        return hx > 0 ? atan_hi[3] + atan_lo[3] : -atan_hi[3] - atan_lo[3];
    }
    int id = -1;
    if (ix < 0x3fdc0000) {  // |x| < 0.4375
        if (ix < 0x3e200000) return x;  // |x| < 2^-29
    } else {
        x = abs(x);
        if (ix < 0x3ff30000) {  // |x| < 1.1875
            if (ix < 0x3fe60000) { id = 0; x = (2.0 * x - 1.0) / (2.0 + x); }  // 7/16 ≤ |x| < 11/16
            else                 { id = 1; x = (x - 1.0) / (x + 1.0); }        // 11/16 ≤ |x| < 19/16
        } else {
            if (ix < 0x40038000) { id = 2; x = (x - 1.5) / (1.0 + 1.5 * x); }  // |x| < 2.4375
            else                 { id = 3; x = -1.0 / x; }                     // 2.4375 ≤ |x| < 2^66
        }
    }
    double z  = x * x;
    double w  = z * z;
    double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) return x - x * (s1 + s2);
    z = atan_hi[id] - ((x * (s1 + s2) - atan_lo[id]) - x);
    return hx < 0 ? -z : z;
}

double atan2(double y, double x) noexcept {
    if (x != x || y != y) return x + y;
    if (x == 1.0) return atan(y);
    int m = (std::signbit(y) ? 1 : 0) | (std::signbit(x) ? 2 : 0);
    if (y == 0.0) {
        switch (m) {
            case 0:
            case 1:  return y;     // atan(±0, +anything) = ±0
            case 2:  return pi;    // atan(+0, -anything) = π
            default: return -pi;   // atan(-0, -anything) = −π
        }
    }
    if (x == 0.0) return y < 0 ? -half_pi : half_pi;
    constexpr double inf = std::numeric_limits<double>::infinity();
    if (x == inf || x == -inf) {
        if (y == inf || y == -inf) {
            switch (m) {
                case 0:  return 0.25 * pi;
                case 1:  return -0.25 * pi;
                case 2:  return 0.75 * pi;
                default: return -0.75 * pi;
            }
        }
        switch (m) {
            case 0:  return 0.0;
            case 1:  return -0.0;
            case 2:  return pi;
            default: return -pi;
        }
    }
    if (y == inf || y == -inf) return y < 0 ? -half_pi : half_pi;

    int k = ((high_word(y) & 0x7ff00000) - (high_word(x) & 0x7ff00000)) >> 20;
    double z = 0;
    if (k > 60) z = half_pi + 0.5 * pi_lo;            // |y/x| > 2^60
    else if (m >= 2 && k < -60) z = 0.0;              // |y|/x < -2^60
    else z = atan(abs(y / x));
    switch (m) {
        case 0:  return z;
        case 1:  return -z;
        case 2:  return pi - (z - pi_lo);
        default: return (z - pi_lo) - pi;
    }
}

double cbrt(double x) noexcept {
    constexpr std::int32_t B1 = 715094163;  // (682 − 0.03306235651)·2^20
    constexpr std::int32_t B2 = 696219795;  // (664 − 0.03306235651)·2^20
    constexpr double C = 5.42857142857142815906e-01;
    constexpr double D = -7.05306122448979611050e-01;
    constexpr double E = 1.41428571428571436819e+00;
    constexpr double F = 1.60714285714285720630e+00;
    constexpr double G = 3.57142857142857150787e-01;

    std::int32_t hx = high_word(x);
    std::int32_t sign = hx & static_cast<std::int32_t>(0x80000000u);
    hx ^= sign;
    if (hx >= 0x7ff00000) return x + x;
    if ((hx | static_cast<std::int32_t>(low_word(x))) == 0) return x;
    x = with_high(x, hx);  // |x|

    double t = 0.0;
    if (hx < 0x00100000) {  // subnormal
        t = from_words(0x43500000, 0);  // 2^54
        t *= x;
        t = with_high(t, high_word(t) / 3 + B2);
    } else {
        t = from_words(hx / 3 + B1, 0);
    }
    double r = t * t / x;
    double s = C + r * t;
    t *= G + F / (s + E + D / s);
    t = from_words(high_word(t) + 1, 0);  // chop to 20 bits, rounded away from zero
    s = t * t;
    r = x / s;
    double w = t + t;
    r = (r - t) / (w + r);
    t = t + t * r;
    return with_high(t, high_word(t) | sign);
}

double pow(double x, double y) noexcept {
    if (y == 0.0) return 1.0;
    if (x == 1.0) return 1.0;
    if (x != x || y != y) return x + y;
    if (x < 0.0) return std::numeric_limits<double>::quiet_NaN();
    if (x == 0.0) return y > 0 ? 0.0 : std::numeric_limits<double>::infinity();
    if (y == 1.0) return x;
    if (y == 2.0) return x * x;
    if (y == 0.5) return std::sqrt(x);
    return exp(y * log(x));
}

}  // namespace omui::math::det
