// openmui.core:base — the spec's value types under the runtime's namespace.
//
// The runtime speaks the specification's vocabulary directly: a Value, a Rect
// or a Role in openmui is the spec's, so nothing converts at the boundary
// between the runtime and the bus, the C ABI or a conformance test.
export module openmui.core:base;

import std;
export import openmui.spec;
export import openmui.anim;

export namespace omui {

using spec::Actor;
using spec::Color;
using spec::Effect;
using spec::Envelope;
using spec::IdLiteral;
using spec::Insets;
using spec::Origin;
using spec::PixelRect;
using spec::Point;
using spec::Rect;
using spec::Role;
using spec::Size;
using spec::State;
using spec::StateSet;
using spec::Value;

using anim::seconds;

}  // namespace omui
