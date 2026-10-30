// hanim:math — the elementary functions animation uses, the same bits on every
// platform: openmui.math's det (D-87; they began here). hanim::dm is that
// namespace, so animation code reads from one place and the functions exist once.
export module hanim:math;

export import openmui.math;

export namespace hanim {
namespace dm = ::omui::math::det;
}  // namespace hanim
