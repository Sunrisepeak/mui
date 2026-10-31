Overall: 57/100  
Mean: 66.9/100

Evaluated the running `omui-wall .agents/docs` and working tree at `d4f7012`, including uncommitted changes. **The reading workflow has improved substantially, but arrangement and readability across zoom levels still prevent daily-tool quality.**

Evidence: **used** = operated through MCP and checked pixels/tree; **measured** = executed benchmarks; **viewed** = inspected code, images or historical results; **inferred** = unverified, provisional. Tests were inspected, not executed.

## Use log

Judged stills followed `ui_wait {settled:true}`. Additional input frames were sometimes necessary to synchronize semantic bounds.

| Task | What I did | Expected | Actual | Result |
|---|---|---|---|---|---|
| Find my way | `ui_capture`, `ui_snapshot`; background drag, wheel, keys `2`, `H` | Understand navigation and recover overview | Visible hints explain the basics; overview and HUD toggle work | Pass |
| Open and read | Double-clicked a document body | Readable live document | Camera fitted the document; headings, CJK, lists and tables were readable | Pass |
| Scroll | `ui_input wheel` inside a live page | Scroll independently | Page content and scroll value changed | Pass |
| Follow and return | Clicked implementation-plan link; clicked Back and Forward | Navigate documents and resume reading | Camera navigation works; reading position and live focus are lost | Partial |
| Search | Clicked Search, typed `确定性`, clicked first result | Find text and inspect its occurrence | Correct document opened; results overflow panel; occurrence is not revealed | Partial |
| Minimap | Clicked `(1371,796)` | Relocate viewport | Camera and viewport indicator moved | Pass |
| Move group | Dragged visible label, then actual strip | Move group with documents | Label drag panned canvas; strip drag moved group and documents | Partial |
| Move and undo | Dragged window strip; `ui_undo`; repeated with group | Restore completed moves | Both movements were restored | Pass |
| Inspect tiers | Wheel, overview, `zoom_to(0.035)`, document focus | Useful overview, summary and reading | Labels help at overview; intermediate pages are microscopic; distant identities disappear | Fail |
| Select text | Dragged live paragraph `(389,150)→(571,150)` | Highlight text | Canvas panned; no selection | Fail |
| Operate task | Revealed component checklist; clicked first checkbox twice | Toggle through real input | Pixels and checked state changed; original state restored | Pass |
| Stream | `wall.stream {windows:1,rate:50}`, 120 frames, `wall.stop`; scrolled to end | See incremental content | Appended paragraphs appeared; no human controls or automatic reveal | Partial |
| Agent discovery | Canvas `find` for `总体架构`, then `focus_node` | Discover and reveal document | Correct document became live | Pass |
| Agent group movement | Group `move_to`, snapshot/capture, undo | Same group movement as dragging | Only backdrop moved; documents stayed behind | Fail |
| Agent link authorization | Local link `activate`, dry run then request without an answer | Accurate effect classification | Internal navigation requested external-action confirmation; request was declined, nothing executed | Fail |

**Defects reproduced through use**

- **Overview labels:** drag `evaluation/subjects` at `(167,515)`; canvas pans. Dragging its strip at `(167,533)` works. Window labels similarly extend below their approximately four-pixel drag strips.
- **Search:** enter `确定性`; long result buttons extend beyond both sides of the panel. Opening a body-text result starts at the document top without highlighting the match.
- **Reading selection:** drag ordinary paragraph text; camera moves.
- **Return navigation:** scroll README to `y=500`, open implementation plan, then Back. README returns as a texture without its live page subtree. Reopening it gives `y=0`.
- **Agent groups:** move `evaluation/subjects` from world `x=0` to `x=300`; backdrop moves about 39 screen pixels while its first document remains at screen `x=32`. Undo restores the backdrop.
- **Tier identity:** at 13.1%, titles are hard-clipped into similar prefixes; around 24%, page text becomes tiny thumbnail detail; at 3.5%, document and group names disappear.
- **Overlay obstruction:** default HUD covers upper-left documents; toolbar covers upper-right group labels and window paths.
- **Semantic synchronization:** after checklist `scroll_into_view` and a settled wait, pixels showed the checkbox near `y=857` while `ui_find` still reported `y=9585`; extra input frames corrected it.
- **Streaming:** appended content remains below the viewport. Dry run calls streaming reversible while explicitly saying it cannot be undone.
- **Minimap semantics:** the visible, interactive minimap has no independently named semantic control.

## Framework and openas feedback

Named windows, stable path-derived IDs, structured Markdown, offscreen canvas discovery, real-input actor reporting and reversible drag history are useful framework capabilities.

The group discrepancy violates the promise that human and agent operations express the same semantics. Group movement should be one shared operation, including membership and undo.

Internal document links retain an **external** effect. Human input follows them, but agent navigation requests confirmation and creates an external history boundary. Classify resolved local navigation separately from opening an external address.

Add frame-consistency guarantees for captures, semantic bounds and settled waits; expose minimap navigation semantically; require an actual inverse for actions advertised as reversible. Gesture arbitration must let live text selection claim its drag.

## Dimension table

**F** = Figma/FigJam; **T** = tldraw; **O** = Obsidian Canvas. Comparisons concern documented capabilities and observed suitability; proprietary implementations were not audited.

| Dimension | Score | Evidence and basis | Comparison with each benchmark | To reach 90, highest impact first |
|---|---:|---|---|---|
| `usefulness` | **70** | **used, viewed.** Folder reading, links, search and tasks work. Selection, reading continuity and file-update handling remain incomplete. [Application](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:105). | **F—behind:** navigation controls are less complete. **T—behind:** everyday manipulation is less consistent. **O—behind:** sustained note-reading and editing workflow is weaker. | Preserve reading state; enable selection/copy; reveal search matches; handle external file changes and save failures. |
| `canvas_ux` | **72** | **used, viewed.** Pan, zoom, minimap, double-click, dragging and undo work. Visible labels disagree with hit areas; editable named views and arrangement controls are absent. [Input/history](/home/speak/workspace/mui/components/canvas/src/view.cpp:285). | **F—behind:** fewer discoverable view controls. **T—behind:** weaker gesture and object-operation consistency. **O—behind:** fewer selection, resizing and organization affordances. | Fix label hit areas; unify group operations; add visible fit/zoom/selection controls and saved views. |
| `semantic_zoom` | **60** | **used, viewed.** Fixed-size overview titles improve identity, but are truncated; texture tier offers miniature pages rather than summaries. [Tier construction](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:110). | **F—behind:** distant structure is harder to identify. **T—behind:** scanning objects is less effective. **O—behind:** useful note excerpts are less accessible. | Introduce title/excerpt summaries; retain group identity; tune thresholds through real-document acceptance captures. |
| `window_design` | **65** | **used, viewed.** Restrained chrome and selection outlines work. Long titles and paths compete, clip, and disappear beneath overlays; elevation and live/focus distinctions are weak. [Chrome](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:31). | **F—behind:** hierarchy is less resolved. **T—behind:** manipulation affordances are weaker. **O—behind:** document identity and card controls are less complete. | Establish title/path priority; add truncation disclosure; distinguish hover, selection, focus and live state; relocate overlays. |
| `markdown_rendering` | **66** | **used, viewed.** Readable CJK, hierarchy, lists, tables and working task clicks. Selection fails; code clips horizontally; asset context is omitted. [Code rendering](/home/speak/workspace/mui/components/markdown/src/parts.cpp:650). | **F—behind:** basic text interaction is less polished. **T—behind:** live text manipulation is less complete. **O—behind:** Markdown reading and editing coverage is materially weaker. | Selection first; horizontal code reading and highlighting; document-relative assets; wide-table and mixed-script validation. |
| `layout_arrangement` | **57** | **used, viewed.** Orderly folder shelves avoid initial overlap but waste space and produce many indistinguishable cards. No alignment/distribution/reflow UI. [Arrangement](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:148). | **F—behind:** arrangement tools are substantially weaker. **T—behind:** continued spatial editing is less flexible. **O—behind:** grouping, resizing and distribution are less capable. | Add alignment/distribution and reflow; pack groups for viewport proportions; preserve deliberate placements and explicit membership. |
| `performance` | **74** | **used, measured, viewed.** Efficient generic culling; document-wall costs and allocations are substantial. Actual 100k-document and mixed-live workloads remain unproven. [Harness](/home/speak/workspace/mui/tests/bench/src/scenes.cpp:358). | **F—behind in demonstrated readiness:** no matched end-to-end workload. **T—behind in demonstrated readiness:** culling figures establish no speed lead. **O—behind in demonstrated readiness:** real-note scale is insufficiently validated. | Measure release input-to-present with concurrent workers/live pages; test 100k actual documents; enforce allocation and total-memory budgets. |
| `persistence_navigation` | **65, provisional** | **used, viewed, inferred.** Camera/arrangement file exists and updates; atomic writing and restore are implemented. Restart recovery was not exercised. Back loses reading state; cross-document fragments are discarded. [Persistence/navigation](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:136). | **F—behind:** continuity and recovery are weaker. **T—behind:** session persistence is less complete. **O—behind:** document navigation and durable canvas workflow remain stronger. | Save document focus/scroll/history; resolve fragments; verify restart, malformed state and recovery; surface save status. |
| `framework_showcase` | **71** | **used, viewed.** Shared texture/live builder, declarative dragging, semantic discovery and streaming are real. Agent groups diverge; streaming lacks an ordinary user workflow. [Promotion](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:130). | **F—behind:** capabilities are less naturally discoverable. **T—behind:** extensibility reaches users less consistently. **O—behind:** mechanisms serve a less complete note workflow. | Complete equivalent human/agent tasks; expose streaming with meaningful content and controls; demonstrate tiers without debugging overlays. |
| `code_tests_docs` | **69** | **viewed, used.** New workflow, persistence and click tests are valuable; contracts and language tours are strengths. Coverage misses observed failures; prose and signature image lag implementation. [Workflow tests](/home/speak/workspace/mui/showcase/markdown-wall/tests/wall_test.cpp:277). | **F—behind in available validation:** polish is insufficiently demonstrated. **T—behind:** editor acceptance coverage is weaker. **O—behind:** document-workflow coverage remains incomplete. | Add regressions for observed defects; validate real folders; correct claims and repository-rule violations; publish current tier/search/reading captures. |

### Performance evidence

No fresh build was obtained: `mcpp run -p openmui.bench` remained in planning for three minutes before interruption. These are **existing development-binary diagnostics**, with recorded compilation flags `-O0 -g`, not release acceptance or proof of the reviewed tree’s exact performance.

| Workload | Development p95, ms | Historical release p95, ms |
|---|---:|---:|
| Wall zoom sweep | 1.922 | 0.115 |
| Wall pan, 1000 | 0.583 | 0.0125 |
| Workspace drag, 100 | 3.018 | 0.231 |
| Selection drag, 1000 | 5.169 | 0.355 |
| Stream, 20 | 11.298 | 0.593 |
| Wall sharpen | 314.431 | 53.307 |
| Generic canvas pan, 100k | 0.110 | 0.0143 |

Zoom used `--min-time 2000` and 1,839 iterations; other diagnostics used 200 ms. Mean allocations/iteration included **137.5** for wall pan and **2,035.4** for selection drag. Wall frame workloads reported **live=0**. GPU measurement skipped because no video device was available.

The harness waits for workers outside timed frames. Generic 100k nodes do not establish 100k Markdown-window performance. The [historical release artifact](/home/speak/workspace/mui/docs/benchmarks/2026-10-28-desktop.bench.omui.json:1) reports RTX 4080 sweep p99 **7.106 ms**; it does not establish the integrated-GPU target.

## Benchmark summary

Figma/FigJam’s relevant strength is discoverable spatial navigation, including explicit zoom percentage and fit/selection commands. [Figma navigation](https://help.figma.com/hc/en-us/articles/360041065034-Adjust-your-zoom-and-view-options).

tldraw’s strength is a coherent editor infrastructure with persisted document/session state, assets and migrations. [tldraw persistence](https://tldraw.dev/docs/persistence).

Obsidian Canvas is the closest functional reference: Markdown cards, editing, connections, groups and extensive arrangement controls. [Obsidian Canvas](https://obsidian.md/help/plugins/canvas).

openmui demonstrates distinctive semantic automation and shared texture/live rendering. **No dimension-level lead over all three is established.** The largest deficits are arrangement, useful semantic summaries and uninterrupted reading continuity.

## Change from the last record

| Dimension | Previous → current | Reason |
|---|---:|---|
| `usefulness` | 52 → **70** | Human search, opening, tasks and saved layout state now exist. |
| `canvas_ux` | 60 → **72** | Double-click reading and drag undo now work. |
| `semantic_zoom` | 52 → **60** | Overview titles help; summaries remain absent. |
| `window_design` | 64 → **65** | Better overview identity; clipping persists. |
| `markdown_rendering` | 62 → **66** | Task clicks fixed; selection remains broken. |
| `layout_arrangement` | 57 → **57** | Same shelving and organization limitations. |
| `performance` | 74 → **74** | Additional diagnostics establish no comparable release improvement. |
| `persistence_navigation` | 36 → **65** | Atomic layout persistence and camera history added; incomplete reading restoration. |
| `framework_showcase` | 70 → **71** | More complete workflows, offset by agent/group divergence. |
| `code_tests_docs` | 65 → **69** | Meaningful new tests; documentation and coverage gaps remain. |

Overall **36 → 57**; mean **59.2 → 66.9**.

## Highest-priority changes

1. Add alignment, distribution and viewport-aware reflow.
2. Build readable summary tiers and persistent group labels.
3. Enable text selection without panning.
4. Restore document focus and scroll through Back/Forward.
5. Make agent group movement identical to pointer dragging.
6. Surface save failures and handle external-file conflicts.
7. Bound search results and navigate to highlighted matches.
8. Validate release mixed-live performance, 100k documents and allocations.
9. Add regression tests and current signature captures for those workflows.

## Detail defects

Additional **viewed** findings:

- [Window builder:46](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:46): omits `base_url`; relative images lack document-directory context.
- [Link resolution:209](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:209): strips fragments; missing targets fail silently.
- [Save status:179](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:179): failures update `said`, but the view never displays it.
- [Loader:157](/home/speak/workspace/mui/showcase/markdown-wall/src/documents.cpp:157): traversal errors terminate collection; unreadable files are silently skipped. Direct filesystem/stream operations bypass the mandated OS layer.
- [Synthetic RNG:13](/home/speak/workspace/mui/showcase/markdown-wall/src/documents.cpp:13): duplicates randomness outside `openmui.math`.
- [Streaming:293](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:293): copies growing source; rate validation accepts positive infinity.
- [Preset:216](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:216): overview assumes 1920×1080 despite a 1440×900 default.
- [Dev story:23](/home/speak/workspace/mui/showcase/markdown-wall/dev-story.md:23): still says 40-pixel threshold; implementation uses 160. Line 30’s approximately 1 ms sharpening describes generic content, not the document wall.
- **`showcase/markdown-wall/media/wall.png`:** five curated fixture documents, a large central gap, and no current search/history toolbar; insufficient signature evidence for the actual document collection.

```json
{"overall":57,"mean":66.9,"dimensions":{"usefulness":70,"canvas_ux":72,"semantic_zoom":60,"window_design":65,"markdown_rendering":66,"layout_arrangement":57,"performance":74,"persistence_navigation":65,"framework_showcase":71,"code_tests_docs":69}}
```