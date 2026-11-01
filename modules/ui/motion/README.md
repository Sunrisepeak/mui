# openmui.motion

Universal motion ([D-93](../../../.agents/docs/2026-11-01-openmui-universal-motion-design.md)): what a node drew, as matter a motion moves; and morphs between two drawings of it.

- **Matter** (`Matter::extract`): a range of a display list (a node's) taken apart into pieces — each drawable command, or a group drawn whole (an effect's layer inside it, a nested root's range) — with what it is drawn under (the transform from the range's start, its clips, the plain layers' opacity) and whose it is (its node's keys, given by the caller; its place among the node's pieces of its class). A style's effect layer around everything is kept as the wrapper.
- **Morph**: two matters matched — by node and place, then by meaning (a node whose key changed but whose role and name did not), then glyph by glyph inside matched text runs (their longest common subsequence) — and drawn between them as a pure function of time: matched pieces travel (transform decomposed, rectangles, radii, OKLab colours, paths of the same verbs, glyph positions and sizes), what only one has fades. Before its start, the first drawing; settled, the second's pixels exactly. Curves are tweens or springs (an interrupted morph's velocity carried on); pieces can start together, in document order or by distance from the middle.

It knows display lists, not views: the view runtime ([view](../view)) gives node markers their keys and decides when a drawing changed and which motion its slot holds.
