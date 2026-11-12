# hanim.timeline

The timeline every editor shares (D-86): a video editor's, a sequencer's, an animation editor's, `gongfu.studio`'s. An independent component: the standard library, [hanim.core](../core) (its eases and keyed tracks) and [mlibs.json](../../../base/json).

```cpp
import hanim.timeline;
namespace tl = hanim::timeline;

auto t = tl::read_timeline(json, "promo.timeline.omui.json");   // every problem at its JSON pointer, line and column
std::vector<tl::Playing> now;
tl::playing(*t, t->rate.frame_time(90), now);              // which clips, where in their sources, at what level
auto undo = tl::apply(*t, *tl::split(*t, 0, 0, mlibs::time::flicks_per_second));   // a change gives back its inverse
tl::apply(*t, *undo);                                            // and the timeline is exactly as it was
```

- **Exact time.** Times are flicks, 1/705,600,000 of a second. Every common frame rate (24 to 120, and the NTSC 1000/1001 rates) and sample rate (44.1, 48, 96 kHz) divides a second into a whole number of them, so a frame or a sample is never a rounding and two machines agree on every edit.
- **What a timeline holds.** Tracks of a kind (video, audio, animation, events) hold clips in order, never overlapping. A clip is a piece of a source placed `at` for a `length`, starting `in` into its source at a `speed`, with a `level` (opacity or gain), fades with an ease, and properties keyed over its own time with hanim's curves. Markers name moments.
- **What plays when.** `playing` answers, for a moment, each clip that covers it: where in its source and at what level. It refills one list, so a player calling it every frame reuses it. `Clip::value` gives a keyed property at a moment.
- **Edits that undo exactly.** Every change is made of three steps (insert, remove, replace a clip). `apply` makes a change, each step keeping its track in order and without overlap, and answers the change that undoes it. A change that cannot be made is not made at all. The usual edits are built from the steps: insert, remove, ripple remove, move (to a track of the same kind), trim (the source staying under the same time) and split (the second half going on where the first stopped). They fit the runtime's reversible actions: an edit's inverse is its undo.
- **The file.** `*.timeline.omui.ts` (`defineTimeline({ name, rate, size, markers, tracks })`) is written for people and agents; the library reads the JSON it evaluates to (D-139). Times are text: `90f` (frames), `1.5s`, `250ms`, or `123fl` (flicks) for a time that is neither. Every problem is reported at its line and column with the nearest right word, and what `write_timeline` writes (JSON) reads back as itself.

Tests (`mcpp test -p hanim.timeline`): exact frame and sample times; times as text both ways; reading, and what plays at moments (fades, half speed, eased keys); located problems; each edit, its undo and its redo restoring the file's very text, and refused edits leaving it as it was; a minute of frames hashed, the same on GCC, LLVM and wasm.

## Optional adapters

| Package | Purpose |
|---|---|
| [audio](audio) | Frozen keys/clip fades on an audio sample clock; read-only visual queries and the mixer's prepared gain source. Neither parent imports the adapter. |

Without a GUI: [tool](tool), the `hanim-timeline` command — a timeline checked, read at a moment, edited and an edit taken back, with its operations' catalog (`timeline.ops.omui.ts`) and skill.
