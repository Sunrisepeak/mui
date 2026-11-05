# Reaction lab: integer rules, one shared workspace

The Inspector's cellular workspace edits and inspects real projects using
`openmui.simulation.cellular` and the shared `openmui.simulation::Timeline`.
Its model, view and CLI live in `src/cellular/` under the existing manifest.
The website embeds this workspace at `#/explore/reactions`. It does not
implement another simulator, clock, history or renderer.

The workspace executes the CPU rule program. The separately available RHI
cellular kernel is not silently substituted into this UI. GPU-resident
rendering, sparse worlds, streamed halo exchange, physical conservation and
Noita-scale workloads remain separate work.
This workspace currently ships with the Inspector; an internal feature that
removes its source and dependency cost has not yet been introduced. The
standalone simulation libraries keep their own smaller dependency boundaries.

## Use without a window

```sh
mcpp run omui-inspector -p openmui.inspector -- --cellular-batch tools/inspector/examples/spark.batch.omui.json --out /tmp/spark
mcpp run omui-inspector -p openmui.inspector -- --cellular --headless --serve
mcpp run omui-inspector -p openmui.inspector -- --cellular
```

The first command creates numbered PNGs, a timed lossless WebP, a current
`spark.cellular.omui.json` project and `spark.report.omui.json`. The output
prefix's parent must exist. Each file is checked; a later failure returns
`ok:false`, a nonzero exit status and paths already written. This is not a
multi-file atomic transaction. Input is bounded to 2 MiB, 64 commands,
120 sampled intervals and 128 MiB of retained pixels. `samples.step` is
1..4 simulation ticks per image; report times are exact rational multiples of
`step / project.rate`. WebP timestamps round those cumulative times to the
format's millisecond precision, without accumulating per-frame rounding drift.
Include every tick when inspecting discrete reaction boundaries.

The second command exposes the normal Agent endpoint with real offscreen
capture; no window or display server is required. SDK policy still controls
its development/release access. The batch command explicitly binds its own
trusted local development session and uses the existing Agent protocol.
It never changes the process-global fallback policy.

## Operations and minimal SOP

Names start with `cellular.` and use the normal openas effect, actor, session,
journal and result envelope. Inputs below are operation args:

| Operation | Input | Result / behavior |
|---|---|---|
| `describe` | none | Format version, limits, operations and workflow |
| `snapshot` | none | Revision, simulation tick, replay frame/range, brushes, state hash |
| `project` | none | Reloadable current field, rules, palette and brushes |
| `config` | none | Editable configuration without the large cell array |
| `inspect` | integer `x`, `y` | All channel values of a cell |
| `load` | `project` | Validate before replacement; new replay history |
| `configure` | `config` | Merge allowed configuration over current field, validate; new history |
| `paint` | integer `x`, `y`, `radius` (0..8), brush ID | Record an input without consuming a simulation tick |
| `advance` | integer `count` (1..120) | Fixed steps through the shared rule program |
| `seek` | integer `frame` | Rebuild a retained frame; no outside effects |
| `reset` | none | Restore this loaded/configured project's initial field |
| `play` | boolean `enabled` | Start live playback, or pause |

Mutations accept optional integer `revision`. Stale revisions, invalid
coordinates, floats where integers are required, unknown keys, limits and
invalid rules are refused before publication. Reads do not advance time.
A saved project captures current cells and tick, not the replay log. Process
reports retain input, initial sampled project, receipts, times and real frames.
`state_hash` covers dimensions, channel count, tick and integer cells; it is
not a hash of rules, palette or brush metadata. Use the revision and exported
project to identify edits that leave the field unchanged.

SOP: discover → read snapshot/revision → load or edit configuration → paint
through named brushes → advance and inspect/capture intermediate states →
seek to examine a boundary → export `project` → load in a fresh session and
compare subsequent evolution. Capture uses the host's existing `capture`
operation or headless renderer, with region, viewport and DPI provenance.
Hashes support regression; inspect the actual frames/replay too. Offline
samples do not certify real-time pacing, input latency or aesthetics.

`load`, `configure`, `paint` and `reset` register owned before/after states
with runtime actor history. Undo/redo restores exact state and starts a new
local replay history. Transport uses bounded simulation checkpoints (240
retained inputs, snapshot every 30), not one UI inverse per simulation tick.
Seeking earlier and advancing creates a branch.

Live playback retains the **bound invocation**, including session, and
resubmits each bounded advance through `Runtime::invoke`. Revocation and
restrictions are rechecked; refusal stops playback and reports why.
The trusted C++ `Runtime::current_invocation()` accessor is not JSON input or
a grant mechanism. The site pauses playback on leaving the workspace or
losing foreground focus. The initial view is paused; Play is explicit.
Embedded C++ callers keep the borrowed runtime alive while the attached
session is in use. A session without a host supports finite edits and replay;
live playback requires a shared session attached to a runtime.

## Project version 1

Data files use `*.cellular.omui.json`:

- `version:1`, nonempty `name`, `size:[width,height]` (each 1..128).
- `channels`: 1..4 unique stable identifiers; at most 16,384 field values.
- `cells`: exactly channels × height × width uint32 integers, channel-major.
- `rate`: 1..60 ticks/s; `tick`: 0..1,000,000,000.
- `boundary`: edge is `constant`, `clamp` or `wrap`, plus uint32 `value`.
- `rules`: at most 32, each with `when` (AND, up to 16 conditions) and
  `write` (unique channels). Self tests: `{channel,value:[low,high]}`.
  Neighbor tests add `offsets:[[dx,dy],…]` and `count:[low,high]`. Offset and
  rule validation delegates to the library's prepared program. Writes have
  `channel`, `op` (`set`, `add` or `subtract`) and uint32 `value`; arithmetic saturates.
- `palette`: 1..32 first-match entries with `when` (self tests) and
  `color:"#RRGGBB"`; the last entry has an empty `when` as fallback.
- `brushes`: 1..16 entries with `id`, `label`, `color` and `values`
  (one uint32 per channel).

Rules read one immutable old field. The first matching rule writes,
unspecified channels copy through, and an empty write is a barrier.
The path/spark/wall project is an editable recipe, not engine material types.
The palette is data, not a style-name switch in a component.

## Validation

`mcpp test --locked -p openmui.inspector` exercises malformed input and stale
revisions, every intermediate state and reverse seek, project reload/evolution,
real pointer versus Agent painting, actor undo/redo, revoked-session playback,
restricted reads/writes, narrow Chinese and high-DPI offscreen pixels, and the
actual batch exporter. `site/tests/reactions_test.cpp` covers embedding,
route changes and pause. `tools/ci/site-reactions.mjs` checks real browser
pointer/keyboard input, 25 canvas samples, reverse replay, navigation and a
Chinese phone layout, and runs in the existing site browser CI job. Concrete
run results belong to the checkpoint report.
