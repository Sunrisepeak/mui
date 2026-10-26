// openmui.core:base — the vocabulary of the runtime.
//
// Values, roles, effects and the rest of the semantic vocabulary are the
// spec's, so nothing converts between the runtime and the bus, the C ABI or a
// conformance test. Geometry is openmui.geom's, shared with layout, raster
// and render; the semantic tree's bounds convert at that one boundary.
export module openmui.core:base;

import std;
export import openmui.spec;
export import openmui.anim;
export import openmui.geom;
export import openmui.memory;

export namespace omui {

using spec::Actor;
using spec::Effect;
using spec::Envelope;
using spec::IdLiteral;
using spec::Origin;
using spec::Role;
using spec::State;
using spec::StateSet;
using spec::Value;

using geom::Affine;
using geom::Color;
using geom::CornerRadii;
using geom::Insets;
using geom::PixelRect;
using geom::Point;
using geom::Rect;
using geom::Size;

using anim::seconds;

// The spec's wire geometry and the shared geometry have the same fields.
[[nodiscard]] constexpr spec::Rect to_spec(const Rect& r) noexcept { return {r.x, r.y, r.width, r.height}; }
[[nodiscard]] constexpr Rect from_spec(const spec::Rect& r) noexcept { return {r.x, r.y, r.width, r.height}; }
[[nodiscard]] constexpr spec::Point to_spec(Point p) noexcept { return {p.x, p.y}; }
[[nodiscard]] constexpr Point from_spec(spec::Point p) noexcept { return {p.x, p.y}; }

}  // namespace omui
