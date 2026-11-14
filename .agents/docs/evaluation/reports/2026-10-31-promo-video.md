Overall: 52/100  
Mean: 63.7/100

Evaluated at `126cb48`. The film has a credible technical premise and substantial machinery, but its essential demonstrations remain too small and editorially diffuse for the named launch-film standard.

**Evidence limits:** I inspected the supplied frames, repository guidance, design document, README, dev story, scripts, timelines, implementation, and tests. I ran limited checks using the existing executable; I did not rebuild, watch a continuous export, or hear the soundtrack. Motion, pacing, and sound scores are therefore **provisional**.

I consulted [Apple’s introduction](https://www.apple.com/newsroom/2023/06/introducing-apple-vision-pro/), [Linear’s releases](https://linear.app/changelog), and [Stripe’s Sessions archive](https://stripe.com/sessions/2025). These establish reference context, not a completed audiovisual comparison. The individual benchmark judgments below are **provisional inferences**, with no credit awarded for unverified superiority. Their production tools and internal tests are unavailable.

Paths below are relative to `showcase/promo-video/`. Frame filenames refer to the attachments.

**Dimension assessment**

| Dimension | Score | Evidence type and basis | Individual benchmark comparison | Changes needed for 90, highest impact first |
|---|---:|---|---|---|
| `story_script` | 64 | **Inspected:** `script/film.md`, `promo.subtitles.omui.toml`, `short.timeline.omui.toml`. The pixel hook, language equivalence, and irreversible action form useful demonstrations. However, accessibility and tests never become recognizable outputs of the same tree. The short omits the agent chapter and explicit C++ positioning. | **Apple: behind** in making the product’s benefit immediately understandable. **Linear: behind** in connecting demonstrations to a recognizable workflow. **Stripe: behind** in building one escalating argument. | Establish one task and one semantic node across pixels, accessibility, agent JSON, and tests. State the practical benefit early. Give the short a complete product proposition. |
| `shot_design` | 55 | **Inspected:** `promo-frames__0900.png`, left: a large code container with tiny contents and extensive unused space. `__1820.png`, center: preview, editor, sidebar, and decorative circles compete. `__2900.png`: sparse game field. `src/shots/styles.cpp:35–73` fits the entire workshop into a smaller frame. | **Apple: behind** in focal isolation. **Linear: behind** in selecting and enlarging the meaningful interface detail. **Stripe: behind** in deliberate use of scale and negative space. | Compose dedicated film views of the live scenes. Enlarge the relevant interaction. Remove workshop controls from the principal style comparison. Frame a stronger game event tightly. |
| `motion_design` | 69 provisional | **Inspected / inferred:** `src/shots/code.cpp:250` implements scatter/gather; `promo.timeline.omui.toml:175–191` keys camera travel and style switches; `src/player.cpp:58–82` establishes consistent moment evaluation. These prove implementation intent, not perceptual polish. | **Apple: provisionally behind** in motivated camera movement and settling. **Linear: provisionally behind** in attention handoffs between interface elements. **Stripe: provisionally behind** in layered choreography. | Review continuous exports at normal speed. Design outgoing and incoming focal points for each cut. Replace generic scattering where a semantic transformation would explain more. Preserve stable reading intervals. |
| `typography_titles` | 52 | **Inspected / calculated:** the wordmark in `__0400.png` works. Code in `__0900.png` and protocol in `__2470.png` do not survive reduction. `src/overlay.cpp:12` specifies 50/29 logical-pixel subtitles: at 480-wide landscape output, nominal sizes are **12.5/7.25 px**. `src/shots/origin.cpp:95` gives the explanatory tagline only 19 px, or 4.75 px at that width. | **Apple: behind** in immediate title hierarchy. **Linear: behind** in readable product explanation. **Stripe: behind** in typography carrying the argument independently of interface detail. | Set a 480-wide readability floor. Show selected, enlarged code and JSON lines. Make the product definition prominent. Reveal subtitles as readable phrases and measure their fully readable duration. |
| `identity_colour` | 70 | **Inspected:** `blueprint.style.omui.toml:11–33` and `src/look.cppm:15–26` define a coherent navy/cyan palette with functional status colours. `__0400.png` is the strongest identity frame. `anim-0091.png` is muddy; `__1820.png` mixes cyan outlines, purple glow, and large coloured circles. The site uses its own dark theme (`site/src/app.cpp:1021`); shared brand-token equivalence is not demonstrated. | **Apple: behind** in material and contrast control. **Linear: behind** in restraint. **Stripe: behind** in the range and integration of the visual system. | Establish shared site/film brand tokens. Reduce competing borders, grid, and glow. Art-direct each style’s contrast while retaining a recognizable subject. |
| `pacing_edit` | 61 provisional | **Inspected / measured from data:** picture durations are 8/8/12/8/10/4/1/4/5 seconds. The wall receives 20% of the film. The short devotes six seconds to the wall and switches styles every 0.5 seconds. The main ending completes command typing at approximately 57.6 s and starts removing it around 58 s (`promo.timeline.omui.toml:242–248`; `src/shots/end.cpp:131`). | **Apple: provisionally behind** in comprehension-led timing. **Linear: provisionally behind** in economical feature delivery. **Stripe: provisionally behind** in tension, release, and a decisive ending. | Reallocate time from overview travel to the central proof. Alternate close detail with clean statements. Give the completed command and destination a sustained hold. Edit the short independently. |
| `sound_music` | 60 provisional | **Measured:** the main check found 17 marked onsets within 0.36 frames; portrait short found three within 0.04 frames. **Inspected:** `src/sound.cpp:114–136` schedules cues, renders the mix, then adds reversed audio after the limiter. `dev-story.md:60` says the score has not been heard by a person. No listening or delivery-loudness evidence earns credit. | **Apple: provisionally behind** in verified emotional and sonic finish. **Linear: provisionally behind** in demonstrated cue clarity. **Stripe: provisionally behind** in demonstrated musical arc and impact. These are evidence-limited judgments, not listening findings. | Audition the complete film on headphones and phone speakers. Measure final integrated loudness and true peak after rewind processing. Refine silence, cue hierarchy, and musical resolution against picture. |
| `concept_payoff` | 76 | **Inspected:** `__3030.png` visibly reverses the film, including captions and timeline. `src/player.cpp:168–181` retrieves or regenerates source frames. `src/shots/time.cpp:199–219` compares independently generated simulation checksums. `tests/promo_test.cpp:163–183` checks replay equality. The underlying proof is stronger than its visual communication. | **Apple: provisionally behind** in making the payoff effortless to understand. **Linear: provisionally behind** in presenting proof at a useful viewing scale. **Stripe: provisionally behind** in staging a memorable climax. No demonstrated lead over any comparator. | Establish one unmistakable collision or transformation. Reverse it visibly, then repeat it exactly. Make the visual match the primary proof and hashes secondary. |
| `tool_quality` | 64 | **Measured:** existing binary’s main check passed 22 sampled frames, 3 seek comparisons, and 3 repeated-frame comparisons in 10.30 s. **Inspected:** frame digests and chunk rendering exist (`src/cache.cpp:29–69`, `src/main.cpp:278–310`). README reports 10 s edited rendering against a 3 s target. Code and wall input overrides are bypassed. Preview remains absent. | **Apple: unranked**, because its production toolchain is unavailable. **Linear: unranked** for the same reason. **Stripe: unranked** for the same reason. None supports a parity claim; the score reflects openmui’s observable delivery gaps. | Fix input routing and test edited output end to end. Deliver preview/reload/write-back. Benchmark cold, warm, and edited exports reproducibly. Meet latency and memory budgets. Validate CLI inputs strictly. |
| `code_tests_docs` | 66 | **Inspected:** tests cover malformed cuts, pinned frames, seeking, replay, aspect ratios, and digest invalidation (`tests/promo_test.cpp:57–272`). However, cue timing contains a tautological assertion (`src/checks.cpp:123`); typography inspection is restricted to selected `main/type` subtrees (`:215–216`). README and dev story disagree about whether incremental caching exists. | **Apple: unranked** on unavailable source and tests. **Linear: unranked** on unavailable source and tests. **Stripe: unranked** on unavailable source and tests. Public reproducibility is valuable, but does not establish superiority over unseen implementations. | Replace vacuous checks. Test actual edited pixels and cached export equivalence. Extend typography coverage to painted and nested scene text. Reconcile documentation and attach reproducible measurement artifacts. |

**Benchmark summary**

The relevant strengths to pursue are Apple’s benefit-led product clarity, Linear’s focused interface explanation, and Stripe’s typographic and audiovisual escalation. These remain provisional reference judgments without side-by-side playback.

openmui’s strongest evidenced asset is that the film’s claim is executable: scenes, replay, semantic comparisons, and frame dependencies are implemented. That is a distinctive premise, not evidence of cinematic leadership. The largest visible deficits are small-screen readability, focal hierarchy, and explaining why the capabilities belong together. No dimension has evidence supporting parity with the strongest benchmark.

**Change from the previous record**

`git diff 2a370fa HEAD -- showcase/promo-video` is empty. Consequently, this report identifies reassessment and new evidence, not film improvements.

| Current dimension | Previous counterpart → current | Explanation |
|---|---|---|
| `story_script` | 64 → 64 | Same feature-tour structure and incomplete short proposition. |
| `shot_design` | 55 → 55 | Same supplied compositions and scaling choices. |
| `motion_design` | 69 → 69 | Still provisional; continuity concerns from former `transitions` score 65 are incorporated here. |
| `typography_titles` | 52 → 52 | Calculated viewing-size text confirms the previous concern. |
| `identity_colour` | 70 → 70 | Coherent base identity, uneven style treatments. |
| `pacing_edit` | 61 → 61 | Same timing and compressed ending. |
| `sound_music` | 60 → 60 | Onsets now measured locally; listening quality remains unverified. |
| `concept_payoff` | 76 → 76 | Substantial implementation, insufficiently prominent visual proof. |
| `tool_quality` | 68 → 64 | Additional checker limitations and unresolved input routing reduce confidence. |
| `code_tests_docs` | Former code 65 and documentation 74 → 66 | New combined dimension; meaningful tests are offset by weak assertions, incomplete coverage, and stale documentation. |

Overall remains 52. Mean changes from 64.9 to 63.7, but the reduction from twelve dimensions to ten prevents a clean like-for-like mean comparison.

**Highest-priority changes**

1. Rebuild the narrative around one task and one tree serving all four outputs.
2. Recompose every proof for 480-wide viewing; enlarge essential text and actions.
3. Give the 15-second cut an explicit product definition and benefit.
4. Replace the full workshop presentation with a focused live style comparison.
5. Stage a recognizable event for rewind and exact replay.
6. Simplify the ending and hold the completed command with an accessible destination.
7. Listen, remix, and measure the final soundtrack after all processing.
8. Route editable inputs through `ShotContext::files`; verify changed pixels and cache reuse together.
9. Strengthen acceptance checks and publish reproducible render/edit benchmarks.

**Specific defects and limitations**

- `promo-frames__0060.png`, center-right: provenance text is incomplete at the sampled instant; lower caption is also mid-reveal. This weakens the hook’s immediate meaning.
- `__0400.png`, beneath wordmark: the product-definition line is extremely small and partly revealed.
- `__0900.png`, left: code occupies only the upper portion of an oversized container. Right-bottom: language chips and hashes are too small.
- `__1500.png`, upper-right: “≤ 1 ms of CPU a frame · tests/bench” can be read as a rendering claim without clearly distinguishing simulation, submission, and raster cost.
- `__1820.png` and `__1860.png`, right: the editing panel runs into the subtitle band. Large circles compete with the settings preview.
- `anim-0091.png`, throughout: paper tint, noise, and low-contrast controls weaken the ink chapter.
- `__2470.png`, right: protocol lines are clipped at the panel edge. Middle and right columns require reading several simultaneous information streams.
- `__2900.png`, center: substantial empty game area makes the small ball and intended event difficult to locate.
- `__3420.png`, center: the command is still incomplete at 57 seconds; the remaining sequence must accommodate command completion, editor, closing card, and fade.
- `src/shots/code.cpp:39–47` and `src/shots/infinite.cpp:44–49`: default `Files` instances bypass the supplied edit directory. **Inferred consequence:** cache invalidation can occur without corresponding changed scene content.
- `src/checks.cpp:123`: sample position is compared with itself, so cue-placement verification cannot fail.
- `src/checks.cpp:195–216`: moving text is excluded; only selected type subtrees are collected. Passing does not establish complete rendered-text readability.
- `src/main.cpp:79–100`: numeric parsing does not consistently require full consumption or report conversion errors.
- `src/film.cpp:43–45` and `src/cache.cpp:74`: direct stream-based file access bypasses the repository’s required `mcpplibs.os` route.
- `src/sound.cpp:121–136`: reversed audio is added after limiter measurement and hard-clamped; reported limiter statistics do not describe the final signal.
- `dev-story.md:61–62`: still describes incremental caching and chunk encoding as missing, contradicting the README and implementation.
- **Measured edge case:** portrait short check at scale 0.125 reported frame 899 as entirely black and failed; the default 0.25-scale check passed. Investigate fade/quantization and define whether a terminal black frame is permissible.
- Existing-binary checks establish sampled behavior only; they do not verify build provenance, full-film export performance, or warm-cache timing.

```json
{"overall":52,"mean":63.7,"dimensions":{"story_script":64,"shot_design":55,"motion_design":69,"typography_titles":52,"identity_colour":70,"pacing_edit":61,"sound_music":60,"concept_payoff":76,"tool_quality":64,"code_tests_docs":66}}
```