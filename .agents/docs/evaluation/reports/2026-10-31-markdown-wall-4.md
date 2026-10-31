Overall: **65/100**  
Mean: **71.1/100**

Evaluated the running `omui-wall .agents/docs` and source at **`ece4041`**. The wall is a more capable prototype than the previous record: summaries, Tidy, search-match reveal, and equivalent agent group movement are substantial improvements. Window identity, text interaction, and persistence completeness still prevent professional daily-tool quality.

Evidence: **used** = operated through MCP and checked pixels/tree; **measured** = executed diagnostics; **viewed** = inspected files or historical results; **inferred** = unverified, provisional. Tests were inspected, not executed. Restart recovery was not exercised.

## Use log

Still-image judgments followed `ui_wait {settled:true}`.

| Task | What I did | Expected | What happened | Result |
|---|---|---|---|---|---|
| Orientation | `ui_capture`, `ui_snapshot`; background drag, wheel, arrows, `2`, `H` | Understand movement and recover overview | Visible hints explain basic navigation; overview and HUD toggle work when canvas has focus | Pass |
| Open/read | Double-clicked README’s overview card | Readable live document | Camera opened a readable CJK document with headings, links and tables | Pass |
| Scroll | Wheel `dy:400` over live page | Scroll page independently | Page scrolled; camera stayed still | Pass |
| Follow link | Clicked implementation-plan link | Open linked document | Correct document became readable | Pass |
| Back/Forward | Opened another document from README at scroll `y:400`; clicked toolbar buttons | Restore document and reading position | Live page and reported `y:400` returned, but visible passage shifted | Fail: visual continuity |
| Search | Clicked Search; typed `确定性`; clicked canvas-design result | Reveal matching passage | Correct passage opened with matching text selected; result labels overflowed panel | Fail: presentation |
| Minimap | Clicked `(1264,832)` | Relocate viewport | Camera moved to corresponding research documents | Pass |
| Window movement | Dragged live title `(543,41)→(640,101)`; Ctrl+Z | Move and restore window | Both worked | Pass |
| Group movement | Dragged printed label, then colored strip; `ui_undo` | Move group and documents | Printed label panned canvas; strip moved everything and undo restored it | Fail: hit area |
| Arrangement | Clicked Tidy; `ui_undo` | Repack wall reversibly | Repacked groups and windows; undo restored arrangement | Pass |
| Zoom tiers | Wheel through overview, summaries and page textures; `zoom_to(.035)` | Useful information throughout | Summaries help; clipping, blank summaries and disappearing identities remain | Fail |
| Text selection | Dragged ordinary live text `(385,148)→(520,148)` | Select text | Canvas panned instead | Fail |
| Tasks | Revealed checklist; clicked checkbox `(392,866)` twice | Toggle and restore | Pixels and checked state changed; original state restored | Pass |
| Streaming | `wall.stream {windows:1,rate:50}`; 120 input frames; `wall.stop`; scrolled down | See appended content | Text appeared below existing document; no visible human controls or automatic reveal | Fail: workflow |
| Agent operation | `ui_find` by title, window `focus`; group `move_by`, undo; local-link `activate` | Equivalent semantic operations | Discovery, reveal, group carriage, undo and local navigation worked without confirmation | Pass |

**Defects reproduced through use**

1. **Printed labels miss their drag targets.** At overview, dragging the visible `evaluation/subjects` name pans the camera. Dragging its thin colored strip moves the group.
2. **Text selection loses to canvas panning.** Open canvas-design and drag its introductory text.
3. **Search results escape their panel.** Search `确定性`; long blue title/path buttons extend outside the centered white panel.
4. **Overview identity remains weak.** At approximately 10%, many cards show indistinguishable `2026-10-…` or `openmui…` prefixes. At 3.5%, document and group names disappear.
5. **Summary coverage is incomplete.** At medium zoom, README and subject cards can contain only a clipped title and path, leaving most of the card blank.
6. **Overlays obscure documents.** Default HUD covers upper-left cards; toolbar obscures upper-right group names and reading-window paths.
7. **Semantic bounds lag pixels.** After checklist `scroll_into_view`, capture showed its checkbox near `y:866` while `ui_find` still reported `y:9585`, including after extra frames. Clicking the visible checkbox refreshed the bounds.
8. **Back restores the number, not precisely the passage.** README reported `y:400` before and after navigation, but the canvas-design table row moved roughly 120 pixels upward.
9. **Window-root snapshots omit live descendants.** `ui_snapshot` rooted at a live window returned only its virtual window node, although its page could still be addressed directly.
10. **Streaming misstates undo support.** Its dry run says “reversible” and “it cannot be undone.” Appended content stays below the current viewport.

## Framework and openas feedback

Stable document IDs, named windows, structured Markdown roles, actor reporting, and reversible drag history make the framework useful to an evaluator. Shared group carriage now closes an important human/agent discrepancy.

Local link activation also improved: an agent could follow a relative document link without an external-action confirmation. However, the document-level `open_link` action remains uniformly external in its [contract](/home/speak/workspace/mui/components/markdown/contract/markdown.contract.omui.toml:33). Classification should agree across equivalent entry points.

The most serious remaining framework problems are gesture arbitration and consistency between pixels, semantic bounds, and virtual subtrees. Add a frame-consistent inspection boundary, expose the minimap as a named semantic control, and require a real inverse before advertising an operation as reversible.

## Dimension table

**F** = Figma/FigJam; **T** = tldraw; **O** = Obsidian Canvas. Comparisons are judgments against documented capabilities, not fresh hands-on audits of the competitors. Performance rankings remain unestablished without matched workloads.

| Dimension | Score | Evidence and basis | Comparison with each benchmark | To reach 90, ordered by impact |
|---|---:|---|---|---|
| `usefulness` | **74** | **used, viewed.** Reading, search reveal, links and tasks work; selection and dependable reading continuity remain incomplete. [Reading workflow](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:57). | **F—behind:** less complete navigation affordances. **T—behind:** less dependable text/object interaction. **O—behind:** weaker everyday note editing and linking workflow. | Fix selection; preserve the visible reading anchor; handle external edits and save failures; provide document editing/opening controls. |
| `canvas_ux` | **74** | **used, viewed.** Pan, zoom, minimap, dragging and undo work. Labels disagree with hit targets; alignment is an agent verb without visible controls. [Input arbitration](/home/speak/workspace/mui/components/canvas/src/view.cpp:674). | **F—behind:** view controls are less discoverable. **T—behind:** gestures and grouping are less coherent. **O—behind:** fewer accessible arrangement and resize operations. | Match labels to hit areas; resolve live-content gestures; expose zoom, fit, alignment and saved-view controls. |
| `semantic_zoom` | **70** | **used, viewed.** New fixed-size section outlines are readable and useful. Titles still clip; documents without `##`/`###` headings yield empty summaries. [Summary drawing](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:148). | **F—behind:** distant identity is less reliable. **T—behind:** object identification remains inconsistent. **O—behind:** useful note excerpts are less consistently available. | Wrap or intelligently shorten titles; add paragraph/table fallbacks; retain group identity; tune the transition to miniature pages. |
| `window_design` | **65** | **used, viewed.** Restrained chrome and selection outlines are serviceable. Title/path competition, clipping and overlay obstruction remain unresolved. [Window builder](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:31). | **F—behind:** weaker hierarchy. **T—behind:** weaker manipulation affordances. **O—behind:** weaker document identity and card controls. | Establish title/path priority; disclose truncated names; distinguish hover, selection, focus and live state; reserve space for overlays. |
| `markdown_rendering` | **66** | **used, viewed.** CJK, headings, lists, tables and task clicks are readable. Selection fails in the wall; long code is clipped without horizontal scrolling. [Code renderer](/home/speak/workspace/mui/components/markdown/src/parts.cpp:732). | **F—behind:** text interaction is less polished. **T—behind:** rich-text interaction is less complete. **O—behind:** Markdown workflow and content coverage are weaker. | Fix selection; make wide code/tables readable; resolve document-relative assets; add syntax highlighting and mixed-script acceptance cases. |
| `layout_arrangement` | **69** | **used, viewed.** Tidy adds useful masonry packing and reversible reflow. Initial shelving still wastes space; explicit membership and visible alignment controls are missing. [Tidy](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:210). | **F—behind:** fewer arrangement controls. **T—behind:** grouping is less explicit. **O—behind:** resize, grid arrangement and distribution workflows are less complete. | Expose alignment/distribution; support explicit groups and resizing; improve initial packing; preserve all arrangement properties. |
| `performance` | **74** | **used, measured, viewed.** Existing development binary shows cheap panning but substantial allocations and sharpening work. Current-source release and 100k-document evidence are absent. [Harness](/home/speak/workspace/mui/tests/bench/src/scenes.cpp:358). | **F—no speed lead established:** no matched measurement. **T—no speed lead established:** generic culling is insufficient. **O—no speed lead established:** actual note workloads are unmatched. | Measure current release input-to-present with workers and live pages; test 100k documents; gate allocations and total memory. |
| `persistence_navigation` | **71, provisional** | **used, viewed, inferred.** Back restores live focus and scroll value, but visual continuity differs. Saving omits dimensions changed by Tidy; restart consequence is untested. [Persistence](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:399). | **F—behind:** recovery is less demonstrated. **T—behind:** document/session persistence is less complete. **O—behind:** durable canvas and document navigation are weaker. | Persist dimensions and reading anchors; validate restart after Tidy; retain fragments; show save state and errors. |
| `framework_showcase` | **75** | **used, viewed.** Summaries, shared texture/live construction, declarative dragging, streaming and semantic automation are real. Several require agent knowledge or debug UI. [Tier construction](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:132). | **F—behind:** features reach people less naturally. **T—behind:** extensibility produces a less coherent editor. **O—behind:** mechanisms support a less complete note workflow. | Complete human/agent equivalence; expose meaningful streaming controls; demonstrate tiers with real documents and unobstructed views. |
| `code_tests_docs` | **73** | **viewed, used.** Added regressions cover group carriage, Tidy, history and search reveal. Standalone selection coverage misses canvas integration; documentation claims lag implementation. [New tests](/home/speak/workspace/mui/showcase/markdown-wall/tests/wall_test.cpp:398). | **F—behind in demonstrated acceptance:** visible defects remain. **T—behind:** editor integration validation is weaker. **O—behind:** real-document workflow validation is incomplete. | Add integration regressions for reproduced defects; verify current release; correct claims; publish current tier/search/reading signature frames. |

### Performance evidence

Fresh diagnostics used the existing `d006c12d0c81900d` binary, whose compilation database records **`-O0 -g`**. Binary/source correspondence is unverified; these are **not release acceptance results**. Each used `--min-time 200`.

| Workload | Iterations | p95 ms | Mean allocations/iteration |
|---|---:|---:|---:|
| Wall pan, 1000 | 1,871 | 0.109 | 114.2 |
| Selection drag, 1000 | 178 | 2.421 | 2,031.0 |
| Stream, 20 | 890 | 2.859 | 1,732.1 |
| Wall sharpen | 12 | 214.383 | 974,015.2 |
| Generic canvas pan, 100k | 1,411 | 0.168 | 21.9 |

Wall frame diagnostics reported **`live=0`**. The harness waits for workers outside timed frames. Generic 100k nodes do not establish 100k Markdown-window performance.

The broad `wall/` run crashed during Vulkan/Mesa initialization after an anonymous-memory allocation error in this restricted environment; no GPU score follows from that failure.

The [historical release artifact](/home/speak/workspace/mui/docs/benchmarks/2026-10-28-desktop.bench.omui.json:1) records wall sharpening p95 **53.307 ms** and RTX 4080 sweep p99 **7.106 ms**. Neither establishes current-source integrated-GPU acceptance.

## Benchmark summary

- **Figma/FigJam:** strongest relevant benchmark for discoverable zoom/view navigation. Its explicit percentage and fit/selection commands expose controls the wall largely hides behind keys or agent actions. [Figma navigation](https://help.figma.com/hc/en-us/articles/360041065034-Adjust-your-zoom-and-view-options).
- **tldraw:** strongest editor-infrastructure comparison: configurable [camera behavior](https://tldraw.dev/sdk-features/camera), [rich-text interaction](https://tldraw.dev/sdk-features/rich-text), and separated document/session [persistence](https://tldraw.dev/docs/persistence).
- **Obsidian Canvas:** closest daily-use reference: editable Markdown cards, connections, groups, resizing, alignment, distribution and open-format persistence. [Canvas documentation](https://obsidian.md/help/plugins/canvas).

openmui’s distinctive strengths are inspectable semantic documents and shared texture/live rendering. No overall dimension-level lead over these three is established. The largest deficits remain interaction reliability, card identity, and durable spatial organization.

## Change from the last record

| Dimension | Previous → current | Reason |
|---|---:|---|
| `usefulness` | 70 → **74** | Search now reveals matches; reading workflow advances. |
| `canvas_ux` | 72 → **74** | Tidy and equivalent agent group movement help; gesture defects persist. |
| `semantic_zoom` | 60 → **70** | Readable outlines replace some miniature-page scanning. |
| `window_design` | 65 → **65** | Core clipping and hierarchy problems remain. |
| `markdown_rendering` | 66 → **66** | Selection and wide-code limitations remain. |
| `layout_arrangement` | 57 → **69** | Reversible Tidy provides meaningful packing. |
| `performance` | 74 → **74** | Diagnostics establish no comparable release improvement. |
| `persistence_navigation` | 65 → **71** | Live focus/scroll restoration added; completeness remains provisional. |
| `framework_showcase` | 71 → **75** | Summaries and shared group operations strengthen the demonstration. |
| `code_tests_docs` | 69 → **73** | Relevant regression coverage and current README image improved. |

Overall **57 → 65**; mean **66.9 → 71.1**.

## Highest-priority changes

1. Make text selection win over canvas panning inside live documents.
2. Fix title wrapping, truncation disclosure, and overview identity.
3. Persist Tidy’s dimensions and verify restart recovery.
4. Restore reading by content anchor, with pixel-level acceptance.
5. Make visible group labels actual drag targets.
6. Bound search results and synchronize semantic bounds with pixels.
7. Expose alignment, resizing, saved views and minimap semantics.
8. Validate current release performance with mixed live workloads, allocations and 100k documents.
9. Correct documentation and add integration regressions plus real-folder signature captures.

## Detail defects

Additional **viewed** findings:

- [scene.cpp:46](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:46): Markdown receives no `base_url`; relative images lack document-directory context.
- [app.cpp:470](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:470): cross-document fragments are discarded; missing targets fail silently.
- [app.cpp:432](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:432): persistence stores positions only, although Tidy resizes groups.
- [app.cpp:440](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:440): save errors update `said`, which the application view does not display.
- [documents.cpp:157](/home/speak/workspace/mui/showcase/markdown-wall/src/documents.cpp:157): direct filesystem/stream operations bypass the required OS abstraction; unreadable files are silently skipped.
- [documents.cpp:13](/home/speak/workspace/mui/showcase/markdown-wall/src/documents.cpp:13): duplicates randomness outside `openmui.math`.
- [app.cpp:694](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:694): stream-rate validation accepts positive infinity.
- [scene.cpp:259](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:259): named overview assumes 1920×1080 despite a 1440×900 default.
- README describes a **160 px** texture threshold; [implementation](/home/speak/workspace/mui/showcase/markdown-wall/src/wall.cppm:56) uses **320 px**; [dev story](/home/speak/workspace/mui/showcase/markdown-wall/dev-story.md:23) still says **40 px**.
- [Dev story:30](/home/speak/workspace/mui/showcase/markdown-wall/dev-story.md:30): approximately 1 ms sharpening conflates generic canvas content with Markdown pages.
- **`showcase/markdown-wall/media/wall.png`:** toolbar is current, but five curated fixtures and the large central gap do not demonstrate real-folder scale, summary tiers, or search.

```json
{"overall":65,"mean":71.1,"dimensions":{"usefulness":74,"canvas_ux":74,"semantic_zoom":70,"window_design":65,"markdown_rendering":66,"layout_arrangement":69,"performance":74,"persistence_navigation":71,"framework_showcase":75,"code_tests_docs":73}}
```