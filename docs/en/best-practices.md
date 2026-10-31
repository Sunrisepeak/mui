# Best practices

What building the showcases taught, as rules a program on openmui can follow. Each rule names the showcase it came from; their dev stories (`showcase/*/dev-story.md`) tell how. This is a living document: a showcase's self-review that finds something general adds it here.

## 1. Put each piece at its layer

openmui is layered (D-82): independent components (`modules/`), the UI framework, the game engine services (`mgame`), specialised frameworks (`gongfu`), and applications. A capability goes to the lowest layer where it is general:

- Recolouring a range of drawn commands is a renderer capability (`DisplayList::recolor`), even if one fighter's time stop is what needed it.
- Compiling TypeScript from an editor's text is the script binding's (`compile_typescript_source`), even if one workshop asked for it.
- A theme change is the host's: a component says what it wants (an event), and the application applies it.

Never bend the architecture for one application's convenience. When something is awkward to do, fix the layer it belongs to.

## 2. Choose the language by what the code does

| The code | Write it in | Why |
|---|---|---|
| Simulation, rendering, mixing, layout: anything run every frame | C++ (modules) | no allocation, no virtual calls on the hot path, exact arithmetic |
| Content tuned again and again: fighters, sounds, themes, levels | data files (`*.<kind>.omui.toml` and the like) with located diagnostics | a reviewer, an agent and a tool can all read and write them; the build checks what is derived from them |
| Logic tuned live: rules, scene scripts, tours, game modes | TypeScript, Lua or Python, hot-reloaded | a reload under 200 ms; no rebuild |

Scripts never run inside a frame. A script produces data at load or reload: rules, a view document, a verb's answer. That data reaches a per-frame system at a step's boundary (widget breakout, level 6).

## 3. Make simulations deterministic, then everything else is easy

- State is plain data: copied to save, hashed to compare (`static_assert(std::is_trivially_copyable_v<State>)`).
- A fixed step: `step(state, input) → state`. Nothing outside the state matters: no clock, no allocation.
- Numbers: fixed point (gongfu), or doubles with exactly rounded operations only. That means + − × ÷ and √, no contraction (`-ffp-contract=off`), no fast-math, no libm transcendentals, and constants written correctly rounded. The lint's `determinism` rule keeps listed packages to this.
- Everything that changes play is in the state, including a camera, a gate's window and rules reloaded from a script (through the step's input). Rewind, replay, rollback netcode and pinned tests then come for free (fighting, widget breakout).
- Pin whole-run hashes in tests on every platform.

## 4. Derive presentation; never store it

Draw a frame as a pure function of (state, frame): particles seeded by their event, cinematics as timelines keyed to a frame, secondary motion from the state's history. Rollback then saves no rendering state, and a replay renders identically (fighting).

## 5. Contract first, one set of actions

Write the component's contract (`*.contract.omui.toml`) first; the bindings are generated. A person, an agent and a test act through the same actions, and the semantic node states what an agent needs to play or use it. Ids are explicit and never depend on displayed text (fighting, widget breakout).

An action of two or more arguments takes them as one generated struct, by name (`const GuardToArgs& args`, then `args.x`), so they cannot be taken in the wrong order. Positional `Value` parameters used to compile in any order; a breakout handler took `seen` for `x`.

## 6. Assets are data the build checks

Embed data files at build time (`openmui::build::embed`). Let the build refuse what is stale: gongfu's baked hurtboxes record the hash of what they came from. Diagnostics name the file, the line and a fix (fighting).

## 7. Validate scripts at the boundary

What a script answers is checked where it enters: known fields, the right types, finite whole numbers where whole numbers are meant, within bounds. On failure keep the previous values and say why (widget breakout's workshop).

## 8. Tests draw exact pixels; keep pins meaningful

The headless host renders the same bytes on every platform, so a test can pin a frame. Keep a pinned frame free of text derived from unrelated state: a checksum line breaks the pin whenever the state's layout changes. Pin a region instead. The README pictures are drawn by tests too (`tools/ci/readme-media.sh`).

## 9. Performance is measured and budgeted

Every performance claim has a benchmark in `tests/bench` and a budget in `budgets.bench.omui.json`. Plain data and no allocation per frame make code fast and deterministic at once. Measure before optimising.

## 10. Ship a picture

Every showcase's README shows its signature shot, drawn headless by its tests: a still, or an animated PNG under 1 MB. Animated PNG stores only what changed between frames, so it suits interface loops; for full-frame motion use video.

## 11. A scene meant to be seekable: what its time says, and its moments

A scene that must render any frame on its own (a video's shot, a test's still, a replay) sets what is continuous from the time before each frame — a camera from keys, a reveal, a value — and makes what changes in a step at *moments*, each on a frame: before a moment the frame before it is drawn, after it its own frame, whichever way the frame asked for is reached. Then drawing a frame directly is mounting the scene and replaying its moments, and frames can be drawn in parallel. Pin motion to its moment rather than to when it was set (a glyph motion's transition `delay`); draw frames complete (`complete_frames`: nothing that would stream in over later frames); and check where text is with `drawn_bounds`, through the visual transforms. The film proves each with its tests: a frame drawn directly is the frame played to (promo video).
