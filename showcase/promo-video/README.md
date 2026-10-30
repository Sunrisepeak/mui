# S5 — openmui's film

*This pixel knows what it is.* Sixty seconds at 60 frames a second, and a fifteen-second cut (16:9 and 9:16), made with openmui: every shot is a live openmui scene drawn on the headless host, the film itself is data, and it checks itself against its acceptance ([promo-video design](../../.agents/docs/2026-10-26-openmui-promo-video-design.md) §5.1).

![A beat of every chapter: the pixel, the wordmark, the counter in four languages, the wall, the styles, the agent, the rewind, the command (drawn headless)](media/film.webp)

How it was made, and what making it taught: [dev story](dev-story.md).

```sh
P="mcpp run --release -p openmui.showcase.promo_video --"
$P render promo --out promo.webm                       # VP9 and Opus, 1080p60
$P render promo --out promo.webm --scale 2             # 4K60
$P render short --out short.webm                       # the 15 s cut, 16:9
$P render short --out short-916.webm --size 1080x1920  # … and 9:16
$P render promo --out promo.frames.txt                 # every frame's pixel hash, no encoding
$P still promo --at 6.5 --out wordmark.png             # one frame
$P sound promo --out promo.wav                         # the sound track
$P check promo                                         # the acceptance, as JSON
$P render promo --dir showcase/promo-video --out x.webm  # an edited copy of the data
$P render promo --out promo.webm --cache .omui-cache --realtime --speed 8   # again after an edit: only what it changed
```

`--workers N` draws N frames at once (default: a quarter of the cores, at most 8); `--threads N --realtime --speed 7 --bitrate 10000` trades the WebM's bytes being the same everywhere for a faster encode.

**Rendering again after an edit** (`--cache DIR`, D-54's frame and chunk caches). Every frame has a digest of what it depends on (`:cache`, `Digests`):
- the program's own bytes (the shots' code, the fonts and art it carries) and the player's options;
- the files the shots read (all but the cuts and the lines, which are taken frame by frame);
- the clip it shows (its whole definition, as the timeline file writes it) and where in it, a played-back frame's own digest;
- the lines up at it, and what the overlay draws from the whole cut.

The WebM is put together from 2-second chunks (`--chunk N` frames), each begun with a key frame and encoded on its own, kept in `DIR` by their frames' digests (`openmui.video.webm`'s `encode_webm_chunked`). A chunk whose frames' inputs did not change is copied and never drawn; the rest are drawn and encoded `--workers` at a time. The file is the same bytes whichever chunks were found.

## The film

| Time | Chapter | Scene (`src/shots`) | What it shows |
|---|---|---|---|
| 0:00 | Hook | `origin`: the style workshop's settings panel (S2) in the blueprint style, on a host of its own | A cyan pixel lights in the dark, and the label beside it is read from the renderer's object ids and the semantic tree: `button#save · role=button · effect=reversible` |
| 0:04 | Title | `origin` | The camera pulls back through the panel's real pixels; its semantic tree grows beside it, the path to the button lit; the tree's lines fly to the wordmark's outline; a band of light (a `wipe` inside a `wipe`) crosses it |
| 0:08 | Code | `code`: the tally counter in TypeScript, C++, Python and Lua (`code/`) | `tally.ts` typed (glyph motion over a code block), the counter appearing a node at a time as its lines arrive; then C++, Python, Lua on the beat — each **run** in its own runtime (QuickJS-ng, compiled C++, PocketPy, Lua) and its tree and pixel hashes shown: the same four times |
| 0:16 | Infinite | `infinite`: the Markdown wall (S1), a thousand documents, the film's own script among them | One shot from a live page out to the whole wall and in to another page; the corner counts what each frame draws, by tier |
| 0:28 | Styles | `styles`: the style workshop (S2) | Glass, neon, retro, ink, liquid and back to the blueprint, on the beat; the preview's semantic tree hashed each frame: unchanged |
| 0:36 | Agent | `agent`: a notes panel | An agent stars a note (reversible), undoes it, asks to delete every note — stopped with `needs-answer` until a person answers; a headline says what each step means; the tree as it reads it, the protocol's records, the journal |
| 0:46 | Time | `time`: the haunted settings breakout (S4), played by a bot through the field's actions | Mid-rally; then **the film rewinds itself** (a `film` clip of its own frames at −5×, the tape's colours drifting apart), into the chapter before, and the game plays again: the same frames, and a MATCH stamp setting its checksum beside the first play's — computed by a second run of the game on a host of its own |
| 0:55 | End | `end` | Back into the pixel, which becomes a cursor: `mcpp new hello --template openmui`; this cut's own timeline in `openmui.ui.timeline`'s editor, its playhead on the frame; then a clean card: the wordmark and the closing line |

The data, every file carried in the program (`openmui.build.embed`) and read from `--dir` when edited:

- `promo.timeline.omui.toml`, `short.timeline.omui.toml` — the cuts (`openmui.timeline`): a picture track of `shot:<name>` and `film` clips whose keyed properties are each scene's camera and choreography; the lines' track (`line:<id>`); the music's clips; the sound cues; `chapter:` and `hit:` markers.
- `promo.subtitles.omui.toml` — every line in Chinese and English.
- `music/promo.music.omui.toml` — the score (`openmui.audio`), 120 beats a minute, a section a chapter; `sounds/promo.sound.omui.toml` — the picture's sounds.
- `blueprint.style.omui.toml` — the film's look, a style pack; `script/*.md` — the storyboard, a page a chapter, shown on the wall; `code/tally.*` — the four versions of the counter.

## How it works

- **A frame is a function of time.** A shot is mounted at its time 0; before each frame it sets what its time says (a camera from its clip's keys, a reveal, a hash), and what changes in a step — a style switched, an agent's action, a game's step — happens at its *moments*, each on a frame. The player keeps one rule both ways: before a moment, the frame before it is drawn; after it, its own frame. So a frame drawn directly (a scene mounted afresh, its moments replayed) is the frame playing up to it gives — the tests hold every chapter to it, and `render` draws frame *w*, *w + n*, *w + 2n* … on each of *n* workers without any of them needing another's frames.
- **Frames drawn complete.** `HeadlessOptions::complete_frames` (`Services::complete_frames`, added for this film): a canvas makes every texture the frame shows at its level before drawing it, and its nodes go live without waiting for the camera to rest — a frame is what it would be at rest, whatever came before.
- **Motion pinned to its moment.** A glyph motion's transition `delay` holds it until then (added for this film): a line set when the overlay mounts rises at its clip's time.
- **The overlay** (`:overlay`) is a view of its own on a transparent host, on the cut's own time: the lines inside the title-safe area, the chapter, the cut's timeline along the bottom with its playhead and time code, and over a `film` clip the tape's look (`chromatic`, `displace`) and its speed.
- **The rewind is data.** A `film` clip plays the cut's own frames at its speed (here −5): those frames are kept (as PNG, shared between workers) as they are drawn, and its sound is the track at those times, backwards, with the tape's whine over it.
- **The sound** (`:sound`): the score rendered once and placed by its clips (from its `in`, through its fades) into `openmui.audio`'s mixer, every cue — the sfx track's and those each shot makes of what it does (keys typed, a style struck, an agent's touch, a brick) — on its frame's first sample, the music ducked under them, one limiter.

## Acceptance (§5.1)

`omui-promo check` and `tests/promo_test.cpp` measure each; the tests run what CI affords (every 30th frame and each clip's ends at a quarter scale, 24 frames drawn directly, ten pinned frames at half size, one at 1080p, one at 4K).

| Item | Standard | Measured |
|---|---|---|
| Duration | 60 ± 5 s; short 15 ± 1 s | 60.000 s (3600 frames); 15.000 s (900) |
| Spec | 1080p60 and 4K60; short 16:9 and 9:16 | 1920 × 1080 at 60; 3840 × 2160 at scale 2; the short laid out for 1080 × 1920 |
| Determinism | the same bytes twice (CPU) | the whole film's 3600 frame hashes the same with 1, 8 and 16 workers, and between debug and release builds; frames and the sound pinned |
| Seek | a frame drawn directly = played to | the tests' 24 frames across every chapter, after moments and through the rewind; `check` draws more |
| Typography per frame | no overlap, inside the safe area, no blank frame | 0 problems in the sampled frames of all three (16:9 and the short at 16:9 and 9:16) |
| Reading time | each line up long enough | ≥ 1 s, ≤ 9 Chinese / 17 English characters a second; the tightest line has 1.45 s to spare |
| Beats | cuts and hits within a frame | 0.00 frames off by the score's beat; onsets heard in the track within 0.36 frames |
| Lines | Chinese and English | all of them |
| Rendering | 60 s 1080p60: ≤ 15 s (GPU), ≤ 60 s (CPU) | CPU only, a shared 32-core machine: the frames alone in 36 s (8 workers, 2.9 GB) or 85 s (one, 1.2 GB); in chunks, 8 at once, VP9 real-time at speed 8, 61 s (one thread an encoder: the same bytes everywhere); the 15 s cut at libvpx's good quality 72 s in chunks (307 s in one stream); 4K frames 212 s; GPU not yet (below) |
| Rendering after an edit | one shot changed: ≤ 3 s; unchanged chunks 100 % reused | the film after one line of the wall's chapter edited: 3 of 30 chunks drawn again, 10 s (real-time VP9; the wall's frames are what takes it); nothing changed: 2 s, every chunk found; the line's 324 frames of the short exactly the frames whose digest changed |

Not yet: rendering on the GPU and with hardware encoding (the video pipeline draws on the CPU reference renderer: what an edit's 3 s needs), the cache's second layer (static subtrees' rasters kept between frames) — its first, third and fourth are above —, the preview with hot reload and edits written back (§12.6), the comparison with the benchmarks and the outside viewers (§5.2) — and the benchmarks' shot-by-shot breakdown (§2.3) needs their files.

## Tests

`tests/promo_test.cpp`: both cuts read, and mistakes in them are found (an unknown shot, an unknown line, a rewind into the future); the data checks above for both cuts, the sound pinned; a frame of each chapter pinned, one at 1080p, one at 4K; frames drawn directly the frames played to; the rewind plays back the film's own frames and the replay is the first play's very frames; the typography of sampled frames at 16:9 and 9:16; the README's loop; what a frame depends on — the same inputs the same digests, another program every frame another, one line edited exactly the frames it is up on.

## Make your own

Copy this package: replace the cuts' clips and keys, the lines, the score; write a shot for each scene of your own (`Shot`: a view, what its time says, its moments, its cues). `omui-promo check` holds yours to the same acceptance.
