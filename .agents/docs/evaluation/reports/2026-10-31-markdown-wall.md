Overall: 30/100  
Mean: 58.8/100

Evaluated commit `126cb48`. **The wall demonstrates credible framework mechanisms, but the daily reading workflow is incomplete. Persistence is the lowest-scoring dimension.**

Evidence labels: **Viewed** means inspected code, files or supplied pixels; **Inferred / provisional** means behavior inferred without execution. Benchmark numbers below are **viewed historical measurements**, not measurements reproduced for this evaluation. Tests were inspected, not run. Paths are repository-relative.

Both attached images show the same overview. Neither establishes a reading view, search interface, overview block tier, selection state or transition quality.

### Dimension table

In comparisons, **F** means Figma/FigJam, **T** means tldraw, and **O** means Obsidian Canvas. Comparisons concern the specified dimension; no matched competitor performance benchmark was supplied.

| Dimension | Score | Evidence type and basis | Individual comparisons | Changes needed for 90, highest impact first |
|---|---:|---|---|---|
| `usefulness` | **48** | **Viewed + inferred / provisional.** Folder loading is implemented (`showcase/markdown-wall/src/documents.cpp:155`). The application view provides canvas and HUD, without human search controls (`src/app.cpp:229`). Its Markdown construction supplies source without document-link navigation (`src/scene.cpp:45`). Saving and external-change reconciliation are absent from the inspected application. | **F: Behind:** fewer discoverable ways to organize and revisit work. **T: Behind:** the arrangement workflow lacks durable state and established editing controls. **O: Behind:** opening Markdown is useful, but connected reading and continuing work across sessions are incomplete. | 1. Save and restore the workspace. 2. Implement title/path/content search with keyboard results. 3. Resolve document links and external file changes. |
| `canvas_ux` | **59** | **Viewed + inferred / provisional.** Cursor-centered wheel zoom and trackpad pan exist (`components/canvas/src/view.cpp:605`). Selection, drag, snap, minimap and declarative behaviors are enabled (`src/app.cpp:234`). Double-click emits activation (`components/canvas/src/view.cpp:669`), but the wall subscribes only to `hud.toggle` (`src/app.cpp:245`). Motion feel was not observed. | **F: Behind:** navigation controls and selection feedback are less discoverable. **T: Behind:** interaction completion, undo and visible editing affordances are weaker. **O: Behind:** moving from an overview card into focused reading lacks a complete application-level action. | 1. Wire activation to focus/live reading. 2. Add visible zoom, fit-selection and navigation controls. 3. Add undo/redo and user-created named views; verify touch and keyboard workflows. |
| `semantic_zoom` | **55** | **Viewed.** The actual chain is block → page texture → live page (`src/scene.cpp:107`). The block contains no document title (`:110–115`); the texture retains full-page detail rather than a readable summary. Both images show tiny page text. Group labels do maintain screen-space readability (`:92–102`). | **F: Behind:** document identity becomes difficult to recognize at overview scale. **T: Behind:** changing rendering cost does not adequately preserve useful information at each scale. **O: Behind:** the intermediate presentation provides weak support for scanning note content. | 1. Add readable overview document labels. 2. Introduce a genuine title/summary tier. 3. Capture threshold crossings and verify stable geometry, anchoring and focus. |
| `window_design` | **61** | **Viewed + inferred / provisional.** Consistent rounded cards and restrained folder colors are visible. Chrome uses a 30-world-unit bar with title, spacer and path (`src/scene.cpp:19`, `:40–44`). At the supplied zoom, titles and paths are extremely small. Selected/focused states are not shown. | **F: Behind:** hierarchy and active-object feedback are less resolved. **T: Behind:** manipulation affordances and selection clarity lack equivalent evidence. **O: Behind:** card identity and reading focus are weaker. | 1. Keep document identity readable across tiers. 2. Specify title/path width priorities, truncation and full-path disclosure. 3. Finish and capture hover, selection, focus and drag states. |
| `markdown_rendering` | **64** | **Viewed + inferred / provisional.** Images demonstrate headings, lists, links, code, tables, tasks and CJK. Semantic-role, anchor, binding and streaming tests exist (`components/markdown/tests/markdown_view_test.cpp:64`, `:115`, `:141`, `:162`). No supplied reading-scale frame establishes typography quality. The wall omits `base_url` (`src/scene.cpp:45`), although relative image resolution uses it (`components/markdown/src/parts.cpp:110`). | **F: Behind:** the demonstrated reading presentation lacks comparable typographic polish, although Markdown parsing is a different specialization. **T: Behind:** readable text presentation and control sizing are insufficiently demonstrated. **O: Behind:** note reading, relative assets and integrated navigation are materially less complete. | 1. Establish a comfortable reading view. 2. Pass each document’s asset base directory. 3. Validate long code, wide tables, nested lists, images and mixed CJK/Latin typography with visual fixtures. |
| `layout_arrangement` | **57** | **Viewed.** Both images contain a large central gap and unused lower-right area while document contents remain miniature. Workspace placement uses square-root row counts (`src/scene.cpp:162`); card height estimates count source bytes and clamp to a minimum (`src/documents.cpp:141–152`). Non-overlap tests exist (`tests/wall_test.cpp:213`). | **F: Behind:** grouping is orderly but composition and arrangement controls are rudimentary. **T: Behind:** users lack a comparable set of arrangement operations. **O: Behind:** automatic folder grouping is helpful, but density and continued manual organization are weaker. | 1. Pack workspace bounds for viewport aspect ratio. 2. Reduce inter-group gaps. 3. Measure preview content and offer compact/reflow commands that preserve deliberate placements. |
| `performance` | **74** | **Viewed historical measurements; current behavior provisional.** Committed desktop results report wall pan p95 **0.0125 ms**, wall zoom CPU p95 **0.1153 ms**, GPU sweep p99 **7.1056 ms**, wall sharpen p95 **53.3073 ms**, and canvas 100k pan p95 **0.0143 ms** (`docs/benchmarks/2026-10-28-desktop.bench.omui.json:1`). Worker completion is excluded from timed wall frames (`tests/bench/src/scenes.cpp:358–362`). Wall counters report **live: 0**. Wall pan averages **96.2 allocations/iteration**. | **F: Behind in demonstrated readiness:** narrow desktop results do not establish equivalent sustained interaction reliability. **T: Behind in demonstrated readiness:** promising culling results do not prove superior end-to-end performance. **O: Behind in demonstrated readiness:** there is no matched document workload establishing parity or leadership. | 1. Benchmark uninterrupted input-to-present with live documents and concurrent texture jobs. 2. Add integrated-GPU, memory and cold-start results. 3. Enforce allocation budgets and test an actual 100k-document wall, beyond the generic canvas scene. |
| `persistence_navigation` | **30** | **Viewed + inferred / provisional.** Startup rebuilds the scene (`src/app.cpp:210–214`); teardown drops it (`:29–32`). The application exposes streaming verbs but no save/load/history workflow. Markdown emits link activation (`components/markdown/src/document.cpp:47–52`), which the wall does not handle (`src/scene.cpp:45`). Two predefined camera views exist (`src/scene.cpp:200`), without user bookmark persistence. | **F: Behind:** arrangement continuity and revisiting work are incomplete. **T: Behind:** document and camera/session persistence are missing. **O: Behind:** saved canvases and connected document navigation are fundamental capabilities the wall lacks. | 1. Persist arrangement, camera and document state atomically. 2. Resolve relative links and anchors into wall navigation. 3. Add Back/Forward, saved views and migration/error recovery tests. |
| `framework_showcase` | **75** | **Viewed + inferred / provisional.** Texture and live rendering share the same window builder (`src/scene.cpp:117–127`). Semantic window/group roles are explicit (`:104`, `:129`). Streaming throttles texture invalidation (`src/app.cpp:104–123`); tour tests compare three languages (`tests/wall_test.cpp:339`). Streaming produces synthetic words (`src/app.cpp:15–24`). Images demonstrate none of these transitions. | **F: Behind:** the supplied presentation communicates less of its capabilities through ordinary work. **T: Behind:** mechanisms are credible, but the useful interactive example is less complete. **O: Behind:** framework features are less naturally connected to everyday document work. | 1. Demonstrate link → focus → task edit → overview. 2. Connect meaningful incoming document updates. 3. Capture pixels, accessibility and agent results for that same workflow. |
| `code_tests_docs` | **65** | **Viewed.** Good module separation, contracts, deterministic fixtures, cross-language checks and pinned pixels (`tests/wall_test.cpp:339`, `:357`). Direct filesystem/stream I/O violates repository platform rules (`src/documents.cpp:155–175`). Streaming copies growing source (`src/app.cpp:118`). Rate validation accepts positive infinity (`:220`). Dev story’s approximately 1 ms sharpening statement conflicts with the wall’s recorded 53.3 ms p95. | **F: Behind in available evidence:** release-quality workflow validation and presentation are incomplete. **T: Behind:** documentation and acceptance coverage leave substantial application behavior unverified. **O: Behind:** persistence, navigation and real-document failure cases lack corresponding tests. | 1. Add workflow acceptance tests and fix repository-rule violations. 2. Correct benchmark claims and validate inputs. 3. Publish reproducible signature captures covering every tier and actual reading. |

### Benchmark summary

Figma/FigJam’s strongest relevant contribution is spatial organization and navigation; its official material describes canvas navigation and sections. tldraw provides explicit document/session persistence and zoom-related performance mechanisms. Obsidian Canvas most directly matches the subject’s purpose: Markdown cards, links, groups and document reading. See [FigJam’s tool overview](https://www.figma.com/resource-library/figjam-tools-for-the-classroom/), [tldraw persistence](https://tldraw.dev/docs/persistence), [tldraw performance](https://tldraw.dev/sdk-features/performance), and [Obsidian Canvas documentation](https://obsidian.md/help/plugins/canvas).

openmui has a promising combination of semantic virtualization, shared texture/live construction and cross-language automation. **No inspected evidence establishes a dimension-level lead over all three competitors.** The greatest deficits are durable work, connected navigation, human search and information hierarchy during zoom.

### Change relative to the previous record

The rubric changed, so these are nearest mappings, not strictly comparable trend measurements. The earlier report already identified most current defects.

| Current dimension | Previous counterpart | Change and reason |
|---|---|---|
| `usefulness` | usefulness_as_a_real_tool: 48 | **48 → 48:** missing daily-workflow capabilities remain. |
| `canvas_ux` | canvas_navigation_and_zoom_ux: 59 | **59 → 59:** mechanisms exist; application activation remains unwired. |
| `semantic_zoom` | Previously inside navigation/zoom | **New separate score: 55:** no readable summary tier; no numeric delta justified. |
| `window_design` | window_design: 61 | **61 → 61:** same miniature chrome and missing state evidence. |
| `markdown_rendering` | markdown_rendering_quality: 64 | **64 → 64:** broad structure support, insufficient reading-scale evidence. |
| `layout_arrangement` | layout_of_the_wall: 57 | **57 → 57:** same fixed shelving and excessive empty space. |
| `performance` | performance_evidence: 74 | **74 → 74:** historical results remain useful but narrowly scoped. |
| `persistence_navigation` | Previously inside usefulness | **New separate score: 30:** isolates a missing capability that the old rubric averaged into usefulness. |
| `framework_showcase` | framework_features_shown_naturally: 77; behaviours_and_live_windows: 68 | **75:** combines mechanism strengths with weak natural-workflow evidence; not evidence of regression. |
| `code_tests_docs` | code_quality_and_tests: 66; readme_dev_story_signature_shot: 58 | **65:** combined assessment; good test structure does not resolve documentation and capture gaps. |

The overall decline from **48 to 30** principally reflects persistence becoming an explicit gate. The mean changes from **63.2 to 58.8** under a different dimension set; it does not establish implementation regression.

### Highest-priority changes

1. Save and restore arrangements, camera, scroll positions and task edits; test restart recovery.
2. Implement relative-document links, anchors and Back/Forward.
3. Add visible title/path/content search with keyboard navigation and result highlighting.
4. Wire double-click and keyboard activation to focused live reading.
5. Introduce readable document labels and summaries between overview blocks and full pages.
6. Replace fixed-gap shelving with compact, viewport-aware workspace packing.
7. Complete title/path sizing, focus, selection, hover and drag affordances.
8. Measure continuous mixed live/texture workloads; enforce memory and allocation limits.
9. Correct performance documentation and produce deterministic tier, reading, search and streaming captures.

### Detail defects

- **Both images, entire frame:** no visible folder identity, search, zoom value or navigation controls.
- **Both images, center around y=380–570:** excessive empty horizontal band.
- **Both images, lower right:** substantial unused space despite unreadably small document content.
- **All card title bars:** document identity is much weaker than workspace identity; paths are particularly faint and small.
- **Handbook and Using it cards:** large unused lower regions.
- **Installing card:** tiny Copy control and cramped table text.
- **Meeting card:** CJK body text is too small to assess comfortable reading.
- **Todo card:** checkbox targets are approximately nine screen pixels in this overview; no reading-scale interaction evidence accompanies them.
- **Bottom-right minimap:** saturated violet viewport border competes with the otherwise muted palette; its container has stronger elevation than the documents.
- **Both supplied filenames:** identical compositions add no tier or interaction coverage.
- **`src/scene.cpp:45`:** missing asset base directory and document-link callback.
- **`src/scene.cpp:202`:** overview preset assumes 1920×1080 geometry, while the application defaults to 1440×900 (`src/app.cpp:209`).
- **`src/documents.cpp:159–173`:** traversal errors can end collection and unreadable files are silently skipped.
- **`src/app.cpp:35–38`:** document lookup scans the vector for each key.
- **`src/app.cpp:118`:** each streaming update constructs a complete growing source string.
- **`src/app.cpp:220`:** positive-infinite rates pass validation.
- **`dev-story.md`, §4:** approximately 1 ms sharpening conflates the generic canvas fixture with the substantially slower document-wall result.

```json
{"overall":30,"mean":58.8,"dimensions":{"usefulness":48,"canvas_ux":59,"semantic_zoom":55,"window_design":61,"markdown_rendering":64,"layout_arrangement":57,"performance":74,"persistence_navigation":30,"framework_showcase":75,"code_tests_docs":65}}
```