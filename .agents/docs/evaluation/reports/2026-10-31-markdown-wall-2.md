Overall: 36/100  
Mean: 59.2/100

Evaluated the running `omui-wall .agents/docs` and the working tree based on `4c7d8ff`, including uncommitted changes. **Relative-document navigation has improved, but the wall remains an incomplete daily reading tool.** Saving, human search, navigation history, text selection, and reliable task interaction are missing or broken.

Evidence labels: **used** = operated through MCP and checked pixels/tree; **measured** = executed benchmark; **viewed** = inspected code, image, or historical results; **inferred** = unverified conclusion, marked provisional. Tests were inspected, not executed.

## Use log

All judged stills followed `ui_wait {settled:true}`. Immediate post-input captures were also examined for inconsistencies.

| Task | What I did | Expected | Actual | Result |
|---|---|---|---|---|
| Understand and navigate | `ui_snapshot`, `ui_capture`; background drag, wheel, arrows, keys `1`, `2`, `h` | Discover navigation and recover overview | Navigation works; `2` restores overview. Instructions omit wheel, keyboard pan, and entering live reading | Partial |
| Open and read | Double-clicked README thumbnail; subsequently zoomed and used semantic `focus` | Open readable, interactive document | Double-click only selected it. Zoom/focus produced readable CJK and tables | Partial |
| Scroll | Wheel inside live README | Scroll document independently | Scroll value and visible content changed correctly | Pass |
| Follow link and return | Clicked visible implementation-plan link at `(450,255)`; tried Alt+Left | Visit target, return to previous reading position | Target opened live. No Back control or working Back shortcut | Fail |
| Search | Ctrl+F, typed `canvas`; then document `find` | Human search and navigable results | No search UI. Agent document search returned block IDs | Fail |
| Minimap | Clicked minimap at `(1355,790)` | Relocate camera | Camera and viewport indicator moved | Pass |
| Arrange and undo | Dragged group strip `(160,533)→(210,558)` and live window title `(630,42)→(690,92)`; Ctrl+Z and `ui_undo` | Move objects, then restore them | Group/windows moved. Neither undo route restored movement | Fail |
| Inspect tiers | Wheel; `zoom_to(0.035)`; focus and overview | Useful identity at overview, summary, reading scales | Reading works; intermediate pages become miniature text; block tier loses document identity | Fail |
| Select text | Dragged across live paragraph `(410,287)→(793,287)` | Highlight text | Canvas panned; no selection appeared | Fail |
| Operate task | Clicked visible checkbox `(391,202)` twice; then explicit semantic `toggle` | Both routes toggle same task | Click hit checkbox but `activate` returned exit 1. Semantic `toggle` worked and was toggled back | Fail |
| Streaming | `wall.stream {windows:1,rate:50}`, 120 input frames, `wall.stop`; inspected HUD/tree | Incremental live updates | HUD counted tokens; appended paragraphs appeared in tree. No human stream controls or automatic reveal of appended text | Partial |
| Agent discovery | Canvas `find` for `组件、绑定`, then `focus_node` | Find document and bring it live into view | Correct key returned; document became readable and interactive | Pass |

**Defects reproduced through use**

- **Thumbnail activation:** double-click README in overview; selection changes, but no reading navigation occurs.
- **Search:** focus canvas, press Ctrl+F, type a query; no field, results, or feedback appears.
- **Text selection:** drag across live paragraph text; the camera moves instead.
- **Checkbox:** focus the component-binding document, scroll to “组件完成标准,” click the first checkbox; it remains unchecked despite a successful hit.
- **Undo:** move a group or title bar, then Ctrl+Z or `ui_undo`; movement remains. The tool reached an earlier external-link history barrier rather than a movement entry.
- **Return navigation:** follow a local document link, then Alt+Left; the previous document/camera/scroll position is not restored.
- **Overview identity:** zoom to 3.5%; individual titles disappear, and narrow groups lose labels.

## Framework and openas feedback

Stable IDs, named window/group roles, document structure, agent discovery, and focus navigation are useful. Input reports identify both the hit node and actor, including `agent:codex-evaluator;via=service:input`. Dry runs expose effects and undo limitations.

The checkbox exposes a fundamental mismatch: semantic `toggle` works while pointer-generated `activate` fails. Its implementation accepts only `toggle` ([parts.cpp:624](/home/speak/workspace/mui/components/markdown/src/parts.cpp:624)). Existing task tests exercise semantic actions rather than clicks.

Local document links are classified **external**, creating a history barrier even when navigation stays inside the wall. Effect classification should distinguish internal navigation from opening an external address. `wall.stream` also reports “reversible” while its dry run says it cannot be undone.

The minimap has no independently named, actionable semantic control. Immediate post-wheel captures sometimes lagged tree/input geometry; subsequent frames converged. Add checks that captures, bounds, and hit testing represent the same completed frame.

## Dimension table

References below:

- **A:** [application and link handling](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:64)
- **S:** [window construction, tiers, arrangement](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:31)
- **D:** [document loading](/home/speak/workspace/mui/showcase/markdown-wall/src/documents.cpp:155)
- **C:** [canvas input](/home/speak/workspace/mui/components/canvas/src/view.cpp:603)
- **M:** [Markdown implementation](/home/speak/workspace/mui/components/markdown/src/document.cpp:111)

**F** = Figma/FigJam; **T** = tldraw; **O** = Obsidian Canvas. Comparisons concern suitability for this subject, not equivalent native Markdown features.

| Dimension | Score | Evidence and basis | Comparison with each benchmark | To reach 90, in impact order |
|---|---:|---|---|---|
| `usefulness` | **52** | **used, viewed.** Folder import and reading work; local links now navigate. Human search, save, text selection, and reliable task clicks prevent daily use. A:64; D:155. | **F—behind:** weaker discoverable navigation and organization. **T—behind:** incomplete editing/recovery loop. **O—behind:** substantially weaker continuing note workflow. | Persist work; implement human search; complete reading, selection, task editing, and file-update handling. |
| `canvas_ux` | **60** | **used, viewed.** Pan, cursor zoom, minimap, title drag, group drag and selection work. Double-click activation is unwired; completed moves lack undo. C:674; A:273. | **F—behind:** no visible zoom/fit-selection controls. **T—behind:** manipulation and history are less complete. **O—behind:** entering and revisiting cards takes more effort. | Wire activation; record undo transactions; add visible navigation, keyboard object operations, and editable named views. |
| `semantic_zoom` | **52** | **used, viewed.** At 3.5%, anonymous blocks; at 13.5%, unreadable page thumbnails; live reading is legible. No meaningful summary tier. S:108. | **F—behind:** object identification is weaker. **T—behind:** distant objects retain too little useful identity. **O—behind:** overview supports note recognition poorly. | Add readable document labels; introduce title/summary previews; verify threshold transitions and identity stability. |
| `window_design` | **64** | **used, viewed.** Restrained chrome, folder colors, readable live titles, and clear selected outlines. Long paths clip; overview chrome vanishes; focus hierarchy is limited. S:41. | **F—behind:** less resolved hierarchy. **T—behind:** fewer manipulation affordances. **O—behind:** weaker card identity and reading controls. | Define title/path priorities; provide truncation disclosure; distinguish hover, selection, focus, and live state. |
| `markdown_rendering` | **62** | **used, viewed.** CJK, headings, lists, links and tables render legibly live. Checkbox clicks fail; text selection pans; long code is clipped. Relative asset base is omitted. S:46; M:111. | **F—behind:** less reliable text interaction. **T—behind:** text handling and control behavior are incomplete. **O—behind:** reading, assets, selection and task operation are materially weaker. | Fix pointer activation and selection; support overflowing code; resolve assets; validate wide tables and mixed scripts. |
| `layout_arrangement` | **57** | **used, viewed.** Folder grouping is orderly, but square-root shelving leaves substantial empty space and forces small overview documents. Heights estimate source bytes. S:145; D:141. | **F—behind:** fewer arrangement controls. **T—behind:** weaker alignment/distribution workflow. **O—behind:** less flexible continued organization. | Pack groups for viewport proportions; add alignment/distribution and reflow; preserve deliberate placements. |
| `performance` | **74** | **used, measured, viewed historical results.** Efficient generic culling; document-wall allocations remain substantial. Current diagnostics and historical release results are separated below. Actual 100k-document wall and sustained mixed-live performance remain unproven. | **F—behind in demonstrated readiness:** no matched end-to-end evidence. **T—behind in demonstrated readiness:** culling speed alone establishes no lead. **O—behind in demonstrated readiness:** no matched note workload. | Measure release input-to-present with concurrent workers/live pages; test real 100k documents; enforce allocations and memory; add integrated-GPU results. |
| `persistence_navigation` | **36, provisional** | **used, viewed, inferred.** Local links now work; Back failed. Model rebuild/drop and application interfaces contain no durable arrangement workflow. Restart survival was not tested. A:29,64,250. | **F—behind:** arrangement continuity missing. **T—behind:** document/session persistence missing. **O—behind:** saved canvases and connected navigation remain far stronger. | Atomic save/restore; Back/Forward with scroll positions; cross-document anchors, saved views, recovery/migration tests. |
| `framework_showcase` | **70** | **used, viewed.** Shared texture/live builder, semantic discovery, declarative group drag and streaming are real. Ordinary users encounter broken task/selection paths and cannot discover several capabilities. S:118; A:144. | **F—behind:** capabilities are less accessible through ordinary work. **T—behind:** example workflow is less complete. **O—behind:** mechanisms connect less naturally to useful note work. | Complete one human/agent reading workflow; expose streaming meaningfully; publish synchronized pixels/tree/input demonstrations. |
| `code_tests_docs` | **65** | **viewed, used.** Contracts, module separation, deterministic fixtures and three-language tours are strengths. Tests miss click failures; direct filesystem I/O and private RNG violate repository rules; performance prose remains misleading. | **F—behind in available evidence:** workflow validation incomplete. **T—behind:** acceptance coverage and reproducible examples weaker. **O—behind:** persistence and real-document error handling absent. | Add real-input workflow tests; correct benchmark claims; follow platform/math rules; capture every tier, reading, search and live interaction. |

### Performance evidence

Executed existing benchmark binary with `--filter wall/ --min-time 200` and separately `--filter canvas/ --min-time 200`. Compile commands identify **`-O0 -g`**, so these are development diagnostics, not release acceptance results.

| Metric | Current development p95 | Committed historical release |
|---|---:|---:|
| Wall zoom CPU | 8.077 ms | 0.115 ms |
| Wall pan | 0.109 ms | 0.0125 ms |
| Workspace drag | 1.602 ms | 0.231 ms |
| Selection drag | 2.445 ms | 0.355 ms |
| Stream 20 | 2.803 ms | 0.593 ms |
| Wall sharpen | 100.802 ms | 53.307 ms |
| Generic canvas 100k pan | 0.112 ms | 0.0143 ms |

Current wall pan averaged **112.9 allocations/iteration**; selection drag **2004.9**. Current wall workloads reported **live: 0**. GPU testing skipped because no video device was available. Historical RTX 4080 sweep reports **7.106 ms p99**.

The harness waits for workers outside timed frames ([scenes.cpp:358](/home/speak/workspace/mui/tests/bench/src/scenes.cpp:358)). These numbers do not establish uninterrupted 120 Hz interaction. Historical measurements are in [the desktop report](/home/speak/workspace/mui/docs/benchmarks/2026-10-28-desktop.bench.omui.json:1).

## Benchmark summary

Figma/FigJam is strongest here in discoverable spatial navigation and section organization; its documented interface includes explicit zoom, fit, and selection navigation. [Figma navigation](https://help.figma.com/hc/en-us/articles/360041065034-Adjust-your-zoom-and-view-options).

tldraw is strongest in the completeness of its extensible editor infrastructure, including document/session persistence and rendering optimizations. [Persistence](https://tldraw.dev/docs/persistence), [performance](https://tldraw.dev/sdk-features/performance).

Obsidian Canvas is the closest functional benchmark: saved Markdown-centered canvases, connections, groups, editing, resizing and arrangement. [Canvas documentation](https://obsidian.md/help/plugins/canvas).

openmui’s semantic automation and shared texture/live construction are distinctive strengths. This evaluation establishes **no dimension-level lead** over the three benchmarks. Durable work and complete human workflows remain the largest gaps.

## Change from the last record

| Dimension | Previous → current | Reason |
|---|---:|---|
| `usefulness` | 48 → **52** | Local links now work; daily-workflow gaps remain. |
| `canvas_ux` | 59 → **60** | Navigation and dragging verified; undo and activation incomplete. |
| `semantic_zoom` | 55 → **52** | Actual block/thumbnail use confirms poor identity and scanning. |
| `window_design` | 61 → **64** | Live reading and selection states now directly observed. |
| `markdown_rendering` | 64 → **62** | Real clicks reveal broken tasks; selection is absent. |
| `layout_arrangement` | 57 → **57** | Same shelving and density limitations. |
| `performance` | 74 → **74** | New diagnostics add evidence, not a comparable release improvement. |
| `persistence_navigation` | 30 → **36** | Relative navigation implemented; durability/history absent. |
| `framework_showcase` | 75 → **70** | Human/agent divergence reduces practical demonstration quality. |
| `code_tests_docs` | 65 → **65** | Link tests improved; major acceptance/documentation gaps persist. |

Overall rises **30 → 36**; mean **58.8 → 59.2**.

## Highest-priority changes

1. Persist arrangements, camera, task edits, and reading positions.
2. Fix checkbox `activate` and add coordinate-based click tests.
3. Implement text selection without canvas gesture interception.
4. Add human title/path/content search with keyboard results.
5. Implement Back/Forward and cross-document anchor navigation.
6. Make completed window/group moves undoable.
7. Wire double-click/keyboard activation to live reading.
8. Add readable overview labels and genuine summary previews.
9. Validate release mixed-live performance; correct claims and expand signature captures.

## Detail defects

- [Task implementation:624](/home/speak/workspace/mui/components/markdown/src/parts.cpp:624): rejects advertised pointer activation.
- [Task tests:151](/home/speak/workspace/mui/components/markdown/tests/markdown_view_test.cpp:151): semantic toggle coverage misses that failure.
- [Canvas commit:285](/home/speak/workspace/mui/components/canvas/src/view.cpp:285): emits movement events without supplying the observed undo workflow.
- [Window builder:46](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:46): omits asset base directory.
- [Link handling:70](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:70): discards cross-document fragments; missing targets fail silently.
- [Code painting:685](/home/speak/workspace/mui/components/markdown/src/parts.cpp:685): clips long lines without horizontal reading controls.
- [Document loading:159](/home/speak/workspace/mui/showcase/markdown-wall/src/documents.cpp:159): traversal errors stop collection; unreadable files are skipped silently.
- [Streaming:158](/home/speak/workspace/mui/showcase/markdown-wall/src/app.cpp:158): copies growing source; rate validation permits positive infinity.
- [Preset:204](/home/speak/workspace/mui/showcase/markdown-wall/src/scene.cpp:204): overview assumes 1920×1080 despite a 1440×900 default.
- [Dev story:30](/home/speak/workspace/mui/showcase/markdown-wall/dev-story.md:30): approximately 1 ms sharpening conflates generic canvas and document-wall workloads.
- **README `media/wall.png`:** large central gap, tiny controls, unused card interiors; shows neither scale transitions nor live interaction.
- **Running overview:** HUD obscures documents; narrow group labels disappear; minimap lacks semantic controls.

```json
{"overall":36,"mean":59.2,"dimensions":{"usefulness":52,"canvas_ux":60,"semantic_zoom":52,"window_design":64,"markdown_rendering":62,"layout_arrangement":57,"performance":74,"persistence_navigation":36,"framework_showcase":70,"code_tests_docs":65}}
```