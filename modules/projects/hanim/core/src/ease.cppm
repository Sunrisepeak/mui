// hanim.core:ease — easing curves as values.
//
// An `Ease` is data (a kind and up to four parameters), not a function object:
// it compares, copies into the SoA of `Set`, travels through a view document as
// "out_cubic" or "cubic-bezier(0.2, 0, 0, 1)", and evaluates the same way on
// every platform. Every curve maps [0, 1] to a value that is 0 at 0 and 1 at 1
// (back and elastic overshoot in between).
export module hanim.core:ease;

import std;

export namespace hanim {

enum class EaseKind : std::uint8_t {
    linear,
    in_quad, out_quad, in_out_quad,
    in_cubic, out_cubic, in_out_cubic,
    in_quart, out_quart, in_out_quart,
    in_quint, out_quint, in_out_quint,
    in_sine, out_sine, in_out_sine,
    in_expo, out_expo, in_out_expo,
    in_circ, out_circ, in_out_circ,
    in_back, out_back, in_out_back,
    in_elastic, out_elastic, in_out_elastic,
    in_bounce, out_bounce, in_out_bounce,
    cubic_bezier,  // p = x1, y1, x2, y2 (CSS)
    steps,         // p[0] = count, p[1] = StepPosition
    spring,        // p = stiffness, damping, mass, settle (s): a spring's unit response over the curve
    fall,          // p = restitution, bounces, length (in falls), contact: a dropped body's height, bouncing
};

// Where the jumps of a `steps` curve fall (CSS Easing Functions Level 1).
enum class StepPosition : std::uint8_t { jump_start, jump_end, jump_none, jump_both };

struct Ease {
    EaseKind              kind = EaseKind::linear;
    std::array<double, 4> p{};

    [[nodiscard]] static constexpr Ease bezier(double x1, double y1, double x2, double y2) noexcept {
        return {EaseKind::cubic_bezier, {x1 < 0 ? 0 : (x1 > 1 ? 1 : x1), y1, x2 < 0 ? 0 : (x2 > 1 ? 1 : x2), y2}};
    }

    [[nodiscard]] static constexpr Ease stepped(int count, StepPosition at = StepPosition::jump_end) noexcept {
        return {EaseKind::steps, {static_cast<double>(count < 1 ? 1 : count), static_cast<double>(at), 0, 0}};
    }

    // A spring from rest to 1 (hanim.core's Oscillator), the curve its whole
    // course until it settles (within 1e-4 of 1, slower than 1e-3 a second),
    // where it is exactly 1. settle() is how long that course is at the
    // spring's own pace: a duration of settle() plays it as the spring would.
    [[nodiscard]] static Ease spring(double stiffness, double damping, double mass = 1.0) noexcept;
    [[nodiscard]] double settle() const noexcept { return kind == EaseKind::spring ? p[3] : 0.0; }

    // A body dropped onto the ground (convergence plan §6.8 ⑨ ④): it falls
    // under gravity (t² over the first fall), stays on the ground `contact`
    // first falls times its landing speed (the time it squashes in), bounces
    // `bounces` times, each leaving at `restitution` of the speed it landed
    // with (so rising to restitution² of the height before), and lands for
    // good at 1. Exact: ballistic arcs end to end, no simulation. falls() is
    // the course's length in first falls: a duration of falls() × √(2h/g)
    // plays it at the pace gravity g gives a drop from h.
    [[nodiscard]] static Ease fall(double restitution = 0.5, int bounces = 3, double contact = 0.1) noexcept;
    [[nodiscard]] double falls() const noexcept { return kind == EaseKind::fall ? p[2] : 0.0; }
    // How hard a fall presses into the ground at t: 0 in the air, rising to
    // the landing's speed (1 for the first) half-way through each time on
    // the ground and back; a squash or a thud follows it. 0 for other curves.
    [[nodiscard]] double contact(double t) const noexcept;

    // The eased progress for linear progress t; t outside [0, 1] is clamped.
    [[nodiscard]] double operator()(double t) const noexcept;

    // Valid serializable parameters for prepared consumers. This does not
    // require monotone output: back/elastic and Bezier y values may overshoot.
    [[nodiscard]] bool valid() const noexcept;

    // d(eased)/dt at t, by a fixed central difference (deterministic); used to
    // carry velocity when a tween is interrupted by a spring.
    [[nodiscard]] double slope(double t) const noexcept;

    // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it.
    friend constexpr bool operator==(const Ease& a, const Ease& b) noexcept { return a.kind == b.kind && a.p == b.p; }
};

namespace ease {
inline constexpr Ease linear{EaseKind::linear};
inline constexpr Ease in_quad{EaseKind::in_quad};
inline constexpr Ease out_quad{EaseKind::out_quad};
inline constexpr Ease in_out_quad{EaseKind::in_out_quad};
inline constexpr Ease in_cubic{EaseKind::in_cubic};
inline constexpr Ease out_cubic{EaseKind::out_cubic};
inline constexpr Ease in_out_cubic{EaseKind::in_out_cubic};
inline constexpr Ease in_quart{EaseKind::in_quart};
inline constexpr Ease out_quart{EaseKind::out_quart};
inline constexpr Ease in_out_quart{EaseKind::in_out_quart};
inline constexpr Ease in_quint{EaseKind::in_quint};
inline constexpr Ease out_quint{EaseKind::out_quint};
inline constexpr Ease in_out_quint{EaseKind::in_out_quint};
inline constexpr Ease in_sine{EaseKind::in_sine};
inline constexpr Ease out_sine{EaseKind::out_sine};
inline constexpr Ease in_out_sine{EaseKind::in_out_sine};
inline constexpr Ease in_expo{EaseKind::in_expo};
inline constexpr Ease out_expo{EaseKind::out_expo};
inline constexpr Ease in_out_expo{EaseKind::in_out_expo};
inline constexpr Ease in_circ{EaseKind::in_circ};
inline constexpr Ease out_circ{EaseKind::out_circ};
inline constexpr Ease in_out_circ{EaseKind::in_out_circ};
inline constexpr Ease in_back{EaseKind::in_back};
inline constexpr Ease out_back{EaseKind::out_back};
inline constexpr Ease in_out_back{EaseKind::in_out_back};
inline constexpr Ease in_elastic{EaseKind::in_elastic};
inline constexpr Ease out_elastic{EaseKind::out_elastic};
inline constexpr Ease in_out_elastic{EaseKind::in_out_elastic};
inline constexpr Ease in_bounce{EaseKind::in_bounce};
inline constexpr Ease out_bounce{EaseKind::out_bounce};
inline constexpr Ease in_out_bounce{EaseKind::in_out_bounce};

// The CSS keywords.
inline constexpr Ease css_ease        = Ease::bezier(0.25, 0.1, 0.25, 1.0);
inline constexpr Ease css_ease_in     = Ease::bezier(0.42, 0.0, 1.0, 1.0);
inline constexpr Ease css_ease_out    = Ease::bezier(0.0, 0.0, 0.58, 1.0);
inline constexpr Ease css_ease_in_out = Ease::bezier(0.42, 0.0, 0.58, 1.0);

// Material 3's emphasized and standard curves, the defaults of the openmui
// motion tokens.
inline constexpr Ease standard   = Ease::bezier(0.2, 0.0, 0.0, 1.0);
inline constexpr Ease emphasized = Ease::bezier(0.3, 0.0, 0.0, 1.0);
}  // namespace ease

// The canonical text of an ease: "out_cubic", "cubic-bezier(0.2, 0, 0, 1)",
// "steps(4, jump-end)", "spring(170, 26, 1)". `parse_ease` reads the same forms plus the CSS keywords
// ("ease", "ease-in" …); it answers nullopt for anything else.
[[nodiscard]] std::string to_string(const Ease& e);
[[nodiscard]] std::optional<Ease> parse_ease(std::string_view text);

}  // namespace hanim
