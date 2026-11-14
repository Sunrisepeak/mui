Overall: 52/100

The film has a distinctive technical premise and substantial implementation, but currently reads as an engineering walkthrough. Its visual hierarchy, readability and editorial focus fall well below the named launch-film bar. Motion and sound scores are provisional: I inspected the supplied frames, timelines and code, without watching a continuous export, listening to the mix or rerunning tests.

Paths below are relative to `showcase/promo-video/`.

| Dimension | Score | Evidence | Changes to reach 90+ |
|---|---:|---|---|
| Story and script | 64 | `script/film.md`; `promo.subtitles.omui.toml`; `promo-frames__0900.png`, `__2470.png`. The pixel hook and consequences example are useful ideas. The film lists capabilities without clearly demonstrating pixels, accessibility, agent JSON and tests as outputs of **one tree**. | 1. Build the story around one recognizable task and tree across those outputs. 2. State the practical benefit before the feature tour. 3. Give the short cut its own understandable product proposition; it currently omits the agent and language proof. |
| Shot design and composition | 55 | `__0900.png`: oversized code container, tiny contents. `__1820.png`: workshop chrome competes with the preview. `__2900.png`: sparse game field. `src/shots/styles.cpp:35` scales the entire studio into the shot. | 1. Compose dedicated film views of the live scenes, with one focal subject per shot. 2. Enlarge the meaningful interaction and suppress peripheral editors. 3. Choose a denser, stronger game rally and frame its action. |
| Motion design and easing | 69 provisional | `promo.timeline.omui.toml`: logarithmic magnification, cubic/sine camera keys, timed gathering and sweep. `src/shots/code.cpp:250`: scatter/gather language changes. These establish intent, not polished perceived movement. | 1. Review continuous exports for acceleration, settling and attention handoffs. 2. Replace repeated typewriter/scatter treatments with motion that explains the relationship between objects. 3. Give important text a stable, fully revealed reading interval. |
| Typography and titles / subtitles | 52 | `__0060.png`: incomplete provenance and subtitle. `__0900.png`: code approximately 8 px at the supplied size. `__2470.png`: protocol approximately 7 px. `src/overlay.cpp:14` uses 50/29 logical px subtitles, while chapter labels use 15. | 1. Establish a viewing-size typography floor; essential text must work at 480 px video width. 2. Reduce code and JSON to selected, enlarged lines. 3. Reveal short subtitles as complete phrases. 4. Separate subtitle space from scene content. |
| Colour and visual identity | 70 | `__0400.png`: coherent cyan/navy blueprint identity and strong wordmark. `anim-0091.png`: muddy ink treatment. `__1820.png`: bright cyan, purple glow and saturated decorative circles compete. | 1. Define foreground, secondary and atmospheric contrast levels across every pack. 2. Reduce grid, glow and border competition. 3. Art-direct the ink and neon shots individually while preserving the shared composition. |
| Pacing and editing | 61 provisional | Timeline allocates 12 s to the wall, 10 s to agent, 1 s each to later languages and styles. The short switches styles every 0.5 s. Closing command completes around 57.6 s; closing transition begins around 58 s. | 1. Reallocate time according to comprehension, especially the ending. 2. Shorten repetitive overview travel and spend that time on the central semantic proof. 3. Alternate detail, action and clean statements instead of maintaining near-constant interface density. |
| Transitions | 65 provisional | Origin’s tree-to-wordmark transition has a conceptual connection. Other picture clips mostly abut directly in `promo.timeline.omui.toml`; code morphs and style cross-fades provide local transitions. | 1. Design continuity between chapters using the same node, frame or action. 2. Give each cut a clear outgoing and incoming focal point. 3. Reserve elaborate transformation for major narrative turns. |
| Sound design and music | 60 provisional | `music/promo.music.omui.toml`: chapter arrangement, motifs, buses and ducking. `sounds/promo.sound.omui.toml`: seeded synthesized cues. `src/sound.cpp`: cue placement and reverse playback. `dev-story.md:60` explicitly says no person has heard the score. | 1. Conduct a listening and picture-sync review. 2. Measure integrated loudness, true peak and cue intelligibility; audition phone speakers and headphones. 3. Shape silence, impact and musical resolution around the story. Data sophistication earns no credit for an unverified listening experience. |
| “Frames are functions of time / film rewinds itself” | 76 | `__3030.png` visibly reverses the recorded frame with tape distortion. `src/player.cpp` replays source frames; `src/shots/time.cpp:199–219` compares independently simulated checksums. Tests cover seek and rewind. | 1. Make the first play contain an unmistakable event worth recognizing again. 2. Show its reversal and exact recurrence prominently. 3. Replace small checksum dependence with a readable visual comparison, then reveal the technical proof. |
| Tool quality: render, check, incremental cache | 68 | `src/main.cpp`: multiple outputs, workers, JSON reports and cached chunk rendering. `src/cache.cpp` implements dependency digests. However, edited code and wall pages use default `Files` instances (`src/shots/code.cpp:40`, `infinite.cpp:44`), bypassing the supplied directory. README reports 10 s edited renders against a 3 s target; preview remains missing. | 1. Route all shot inputs through `ShotContext::files`; test actual edited output. 2. Deliver preview, seek and reload controls using the same reports. 3. Meet edit latency and memory budgets with reproducible measurements. 4. Validate numeric CLI arguments strictly and return structured errors consistently. |
| Code quality and tests | 65 | Good interface/implementation separation and meaningful pinned-frame, seek, sound and cache tests (`tests/promo_test.cpp`). Problems: `checks.cpp:123` compares a sample index with itself; typography collection excludes moving/faded text and only visits `main/type` (`:196–216`). `styles.cpp:136` prints “unchanged ✓” without comparing a baseline. Direct file streams bypass the repository’s OS rule. `code.cpp:310` implements local random generation. | 1. Replace self-confirming checks with independent observations and negative tests. 2. Measure all visible text, including painter labels, scene UI and animation. 3. Make style verification conditional on real equality. 4. Use `mcpplibs.os` and shared math/random facilities. 5. Test cancellation/failure propagation through frame-store waits. |
| README / dev-story / signature shot | 74 | README has useful commands, limitations and reproducible frame references. `tests/promo_test.cpp:110` pins signature frames and `:210` generates the loop. But README describes an implemented cache while `dev-story.md:61–62` calls it missing. The loop makes already small UI even smaller. | 1. Reconcile documentation with current implementation and measurements. 2. Choose one immediately legible signature shot instead of relying on a miniature chapter survey. 3. Supply playable exports with audio, portrait evidence and dated validation reports. 4. Distinguish engineering acceptance from design approval. |

## Top 7 fixes

1. **Rebuild the narrative around one semantic node:** show its pixels, accessibility representation, agent action and test assertion with the same persistent identifier.
2. **Create film-specific compositions:** enlarge the counter, settings preview and permission decision; remove peripheral editor chrome from those moments.
3. **Set a small-screen readability gate:** inspect every essential message at 480 px width and replace dense code/JSON with enlarged excerpts.
4. **Repair the ending:** hold the complete command long enough to read, then show one clean brand card with an actionable destination.
5. **Stage a recognizable rewind payoff:** select a memorable collision sequence, reverse it, replay it and visibly demonstrate the match.
6. **Fix verification credibility:** remove the tautological timing check, compare style hashes honestly, cover animated/painter text, and verify `--dir` edits affect rendered scenes.
7. **Review the complete audiovisual export:** tune timing and mix by viewing/listening, record findings, and update both documentation files with the same current results.

## Detail defects

Partial words below are observable animation states, not necessarily permanent clipping; they still undermine these selected presentation frames.

- `anim-0000.png`: centre label is only “button”; provenance is not yet understandable. Bottom subtitle has barely appeared.
- `promo-frames__0060.png`: provenance starts inside the cyan square and extends past its right edge, weakening its relationship to the pixel.
- `promo-frames__0060.png`: English subtitle reads only “This pixel knows”; Chinese is also incomplete and unevenly revealed.
- `promo-frames__0400.png`: specification tagline is tiny, dim and only partially revealed beneath the rule.
- `promo-frames__0400.png`: measurement lines and background grid add competing detail around an otherwise strong title.
- `promo-frames__0900.png`: Lua code, line numbers and language chips are too small for comfortable video reading.
- `promo-frames__0900.png`: code occupies the upper portion of a large mostly empty panel.
- `promo-frames__0900.png`: subtitle card overlaps the code panel’s lower-right boundary.
- `promo-frames__0900.png`: hash rows beneath the counter resemble debugging output and lack useful reading hierarchy.
- `anim-0045.png`: document labels collapse into tiny strokes; the count is the only readily readable information.
- `promo-frames__1500.png`: subtitle obscures lower document groups.
- `promo-frames__1500.png`: minimap sits across the timeline region, creating a bottom-right overlay collision.
- `promo-frames__1500.png`: tier, zoom and CPU-budget annotations are extremely small and low contrast.
- `anim-0068.png`: workshop controls and verification line become unreadable at README animation size.
- `anim-0091.png`: ink paper, outlines and controls have weak tonal separation; upper-left style labels compete.
- `promo-frames__1820.png`, `__1860.png`: right-hand slider labels and left-hand style descriptions are microscopic.
- `promo-frames__1820.png`, `__1860.png`: large circles dominate the lower preview without explaining the interface; the subtitle overlaps that region.
- `promo-frames__1820.png`: purple glow softens panel edges while thin cyan controls remain harsh, producing inconsistent edge emphasis.
- `promo-frames__2470.png`: protocol records terminate in ellipses or clipped fragments, including the decision’s error text.
- `promo-frames__2470.png`: tree and protocol panels have substantial empty lower regions while their text remains tiny.
- `promo-frames__2470.png`: confirmation card overlaps the notes panel’s lower boundary; its technical explanation is much smaller than its heading.
- `promo-frames__2900.png`: the game’s large empty centre overwhelms the small ball, paddle and controls.
- `promo-frames__2900.png`: checksum is too small and peripheral to substantiate the main claim visually.
- `promo-frames__3030.png`: RGB separation degrades subtitles and controls along with the action, impairing recognition during rewind.
- `anim-0159.png`, `promo-frames__3420.png`: ending shows an incomplete command in a largely empty frame; the call to action has not landed.
- Across chapter frames: chapter names, timecodes and hairlines are too small at the supplied viewing sizes to justify their persistent visual noise.