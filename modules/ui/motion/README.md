# openmui.motion

Universal motion ([D-93](../../../.agents/docs/2026-11-01-openmui-universal-motion-design.md)): what a node drew, as matter a motion moves; and morphs between two drawings of it.

- **Matter** (`Matter::extract`): a range of a display list (a node's) taken apart into pieces — each drawable command, or a group drawn whole (an effect's layer inside it, a nested root's range) — with what it is drawn under (the transform from the range's start, its clips, the plain layers' opacity) and whose it is (its node's keys, given by the caller; its place among the node's pieces of its class). A style's effect layer around everything is kept as the wrapper.
- **Morph**: two matters matched — by node and place, then by meaning (a node whose key changed but whose role and name did not), then glyph by glyph inside matched text runs (their longest common subsequence) — and drawn between them as a pure function of time: matched pieces travel (transform decomposed, rectangles, radii, OKLab colours, paths of the same verbs, glyph positions and sizes), what only one has fades. Before its start, the first drawing; settled, the second's pixels exactly. Curves are tweens or springs (an interrupted morph's velocity carried on); pieces can start together, in document order or by distance from the middle.

It knows display lists, not views: the view runtime ([view](../view)) gives node markers their keys and decides when a drawing changed and which motion its slot holds.

Prepared path morphs capture their geometry through the public
`hanim::import_path` adapter and shared `mlibs.geom` path validation. The local
path-to-contour parser is removed. This module selects the Hanim object feature
without its player or renderer features; it continues to use the existing core
shape plan for time sampling. Shared data capture does not yet provide the full
configurable UI-to-HObject animation policy or migrate all UI motion to Engine.

The C++ `motion::Options::vertex_path` consumes the public `hanim::PointPath`
operator for matched filled/stroked paths. It works on prepared contour
correspondence and compatible paths (including legacy open paths); no UI-local
trajectory implementation is added. Text, rectangles, layout and their hit
policies are unchanged. This option is not yet exposed through MotionSpec,
script props or the site's interactive gallery.

The default stays linear. Invalid controls or unrepresentable float geometry
are reported through `path_morphs()` and the existing unsupported-path fallback.
Headless CPU tests check changed middle pixels, endpoint equality, actual-pose
retarget, seek repeatability and warm allocation-free geometry recording.
