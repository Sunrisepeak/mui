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

// A path as UTF-8 text, for messages, keys and identities. Never through
// std::filesystem::path::string(): on Windows it converts through the
// process's ANSI code page and throws on a name the code page cannot spell
// (a Chinese folder on an English system), while on Linux and macOS the same
// call cannot fail — so the defect is invisible where tests usually run.
// lint rule `paths`; mcpp's .agents/skills/mcpp-contributing, 路径窄化不变式.
[[nodiscard]] inline std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// The spec's wire geometry and the shared geometry have the same fields.
[[nodiscard]] constexpr spec::Rect to_spec(const Rect& r) noexcept { return {r.x, r.y, r.width, r.height}; }
[[nodiscard]] constexpr Rect from_spec(const spec::Rect& r) noexcept { return {r.x, r.y, r.width, r.height}; }
[[nodiscard]] constexpr spec::Point to_spec(Point p) noexcept { return {p.x, p.y}; }
[[nodiscard]] constexpr Point from_spec(spec::Point p) noexcept { return {p.x, p.y}; }

}  // namespace omui
