// openmui.anim — a general animation engine: every animation a pure function
// of time.
//
// Standard library only. It knows nothing of nodes, views or rendering; the
// view layer of openmui maps its values onto properties, and a game, a video
// composition or another UI framework can use it the same way.
//
//   :math      elementary functions with identical bits on every platform
//   :time      clocks (interactive, virtual, render) and frame rates
//   :ease      easing curves as data
//   :value     what can be animated (lanes), colours in OKLab, transforms
//   :motion    tween, keyframes, spring, decay, interruptible motion, interpolate
//   :path      motion along a curve by arc length
//   :timeline  clips placed in time, nested, with markers
//   :set       the batch of a running UI, evaluated in one pass
export module openmui.anim;

export import :math;
export import :time;
export import :ease;
export import :value;
export import :motion;
export import :path;
export import :timeline;
export import :set;

export namespace omui::anim {

inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;
inline constexpr int version_patch = 0;

}  // namespace omui::anim
