// hanim.core:math — the elementary functions animation uses, the same bits on every
// platform: mlibs.math's det (D-87; they began here). hanim::dm is that
// namespace, so animation code reads from one place and the functions exist once.
export module hanim.core:math;

export import mlibs.math;

export namespace hanim {
namespace dm = ::mlibs::math::det;
}  // namespace hanim
