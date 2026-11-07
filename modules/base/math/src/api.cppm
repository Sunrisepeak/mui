// mlibs.math.api — the repository's mathematics, one implementation each (D-87).
//
//     import mlibs.math;
//     const double y = mlibs::math::det::sin(x);        // the same bits everywhere
//     mlibs::math::SplitMix64 rng{seed};
//     const double u = mlibs::math::unit24(rng.next());  // [0, 1)
//
// Determinism is stated by the namespace: det:: is bit-exact on every
// platform (fdlibm with the five correctly rounded operations); the random
// functions are integer arithmetic, exact by construction.
export module mlibs.math.api;

export import :det;
export import :random;
export import :noise;
export import :color;
