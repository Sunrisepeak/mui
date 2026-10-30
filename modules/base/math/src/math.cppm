// openmui.math — the repository's mathematics, one implementation each (D-87).
//
//     import openmui.math;
//     const double y = omui::math::det::sin(x);        // the same bits everywhere
//     omui::math::SplitMix64 rng{seed};
//     const double u = omui::math::unit24(rng.next());  // [0, 1)
//
// Determinism is stated by the namespace: det:: is bit-exact on every
// platform (fdlibm with the five correctly rounded operations); the random
// functions are integer arithmetic, exact by construction.
export module openmui.math;

export import :det;
export import :random;
