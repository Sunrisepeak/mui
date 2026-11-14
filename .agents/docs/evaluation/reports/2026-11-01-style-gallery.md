Overall: 65/100  
Mean: 76.4/100

Evaluated 2026-11-01 against workspace HEAD `996e51c`. The running application did not expose its build revision.

**Evidence:** **used** = live input and captures; **measured** = results computed during this evaluation; **viewed** = inspected files; **P** = provisional. Competitor comparisons are documentation/source-based inferences, not firsthand operation. Pack comparisons concern craft and readability, rather than imitation of unrelated aesthetics.

**Use log**

| Task | What I did | Expected | What happened | Result |
|---|---|---|---|---|
| Discover | `ui_apps`, `ui_snapshot`, `ui_capture` | Locate gallery | Found `org.openmui.styles`, 1440×900; executable name was not an endpoint alias | Pass |
| Switch all packs | Coordinate clicks; `styles.show`; `ui_clock(pause/step/release)`; intermediate and settled captures | Cross-fades preserving content | All five switched; transitions involving retro visibly doubled displaced text | Partial |
| Settings in every pack | Click both switches and checkbox; type names; drag Volume and press arrows; select Region with keys | Controls answer and values match pixels | All responded, including retro’s warped controls | Pass |
| Save, Cancel, navigation | Save in glass/retro/ink/liquid; Cancel in neon; activate tabs with pointer and keyboard | Save/restore feedback and navigation | Saved/discarded feedback worked. At this viewport all destination panels were already visible | Pass; narrow navigation unverified |
| Workshop editing | Change accent, corners, glass blur, neon intensity, CRT curvature, ink wobble and liquid smoothness; capture | Immediate preview | Changes appeared immediately | Pass |
| History | Click Undo/Redo/Reset; drag glass Blur, switch to ink, invoke `ui_undo` twice | Recover pack, then edit | Returned to glass at blur 2.8, then restored 2.0 | Pass |
| Previous regressions | Set thickness 60; inspect slider; reject invalid colour | Accurate value and useful failure | Thickness reported 60; invalid colour preserved draft and explained failure | Pass |
| Agent-to-picture | `ui_act(set_value)` Accent to orange; capture | Tree edit changes picture | Accent propagated into controls, chart treatment and wallpaper | Pass |
| Colour fidelity | Enter `#ff000080`; inspect field | Preserve RGBA representation | Accepted value subsequently displayed as `#ff0000` | Fail |
| Accessibility | Inspect focus/disabled states; toggle Reduce transparency; seek light/dark controls | Readable states and paired appearances | Glass became opaque; paired appearances unavailable | Partial |
| Authoring completion | Seek import, comparison and precise numeric entry | Complete editing workflow | No import, simultaneous comparison, general colour picker or numeric-entry fields | Fail |
| Semantic equivalence | Compare five preview snapshots and expanded Region menus | Same semantics | Matching 62-node previews and five-node select subtrees | Pass |
| Export boundary | Dry run, pointer click, invocation without answer | Require person’s confirmation | Pointer returned exit 2; invocation reported declined, nothing done | Boundary passed; file delivery unverified |

Defects encountered through use:

- **Alpha disappears from the editor:** glass → enter `#ff000080` in Accent → move focus. The field/tree reads `#ff0000`, concealing the accepted transparency.
- **Transition readability:** clock-step glass → retro or retro → ink by 0.15 seconds. Settings headings, labels and field text appear twice at displaced positions.
- **Small-text degradation:** inspect ink’s settings/chart captions and retro’s sidebar/editor captions at full size. Ink softens glyphs; retro adds resampling, grille and dimming.
- **Unsafe colour combinations:** liquid → choose orange. White selected-pack text remains on orange; the nominal colour pair measures **2.80:1**.
- **Incomplete inspection workflow:** no controls expose both appearances, import an exported pack, or compare packs simultaneously.
- **Long parameter lists:** glass and neon require substantial scrolling through repeated parameter names; their history headers now remain visible.

**Framework and openas feedback**

Stable IDs, typed values, actor provenance and hit receipts made real input auditable. Retro’s inverse pointer mapping worked. Clock stepping, settling and semantic snapshots were particularly useful. Global undo across pack changes now works.

Export correctly retained its external-effect boundary; I supplied no confirmation answer. However, the pointer receipt’s exit 2 provides little actionable explanation in the interface. Add visible confirmation-status feedback and structured failure details.

Expose transformed geometry to agents: retro’s snapshot bounds remain unwarped. Also expose the selected pack as semantic state, and group effect parameters semantically so four “Thickness” sliders have discoverable context.

**Dimension assessment**

A = Apple Liquid Glass; M = Material Theme Builder; T = tweakcn. Remedies are ordered by impact.

| Dimension | Score | Evidence and basis | Comparison with each benchmark | To reach 90 |
|---|---:|---|---|---|
| `pack_glass` | **83** | **used, viewed:** convincing bevel/rim treatment, pressed swelling and moving tab lens. Detailed-backdrop adaptation remains weakly demonstrated. [Shader:45](/home/speak/workspace/mui/modules/gfx/render/vector/effects/liquid_glass.slang:45). | A: behind in contextual adaptation. M: behind in coordinated surface states. T: behind in preview coverage. | Demonstrate moving text/media backdrops; adapt foreground contrast; refine material hierarchy. |
| `pack_ink` | **67** | **used, viewed:** warm paper and irregular outlines establish identity, but whole-panel warping also softens text. [wobble.slang:28](/home/speak/workspace/mui/modules/gfx/render/vector/effects/wobble.slang:28). | A: behind in preserving information clarity. M: behind in typographic consistency. T: behind in sustained editing comfort. | Separate border distortion from glyph rendering; reduce grain over text; refine chart treatment. |
| `pack_liquid` | **72** | **used, measured:** clean rounded surfaces; fluid identity remains concentrated in pointed slider bridges. Default chart sample improved to 3.08:1. [Pack:21](/home/speak/workspace/mui/modules/ui/theme/styles/styles/liquid.style.omui.toml:21). | A: behind in fluid interaction breadth. M: behind in colour-role completeness. T: behind in customization breadth. | Refine joins across slider positions; coordinate fluid states throughout controls; strengthen identity beyond sliders. |
| `pack_neon` | **74** | **used, viewed:** readable cyan/magenta system; stronger interaction states now distinguish controls better. Numerous glowing editor rails still compete. [Pack:27](/home/speak/workspace/mui/modules/ui/theme/styles/styles/neon.style.omui.toml:27). | A: behind in emphasis hierarchy. M: behind in restrained state differentiation. T: behind in authoring clarity. | Quiet idle rails/buttons further; distinguish keyboard focus from hover; reduce decorative competition. |
| `pack_retro` | **65** | **used, viewed:** coherent phosphor identity and working warped input. Captions remain dimmed and resampled. [crt.slang:22](/home/speak/workspace/mui/modules/gfx/render/vector/effects/crt.slang:22). | A: behind in clarity preservation. M: behind in small-text readability. T: behind in editing comfort. | Provide a readable preset; protect glyph sharpness; constrain grille/vignette and distortion. |
| `layout_composition` | **74** | **used, viewed:** understandable columns and fixed editor header. Settings starts 16 px below dashboard; wallpaper occupies much of the lower preview. [studio.cpp:410](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:410). | A: behind in spatial hierarchy. M: behind in task organization. T: behind in useful preview density. | Align panel starts; use spare space for comparison/state specimens; improve parameter navigation. |
| `typography` | **80** | **used, viewed:** clear hierarchy, persistent labels, values and units; “Refraction (index)” improves terminology. Effects still damage captions; “Adaptive · 100%” obscures boolean behavior. [studio.cpp:435](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:435). | A: behind in material-safe typography. M: behind in type-system authoring. T: behind in font/spacing controls. | Preserve glyphs; use typed parameter controls; add typography editing and contextual help. |
| `contrast_accessibility` | **73** | **used, measured:** all sampled default chart bars exceed 3:1; opaque glass fallback works. Edited accents can create poor text contrast; paired appearances remain unavailable. [studio.cpp:338](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:338). | A: behind in automatic adaptation. M: behind in coordinated appearances. T: behind in inspection breadth. | Audit rendered text/focus/error states; adapt foregrounds after colour edits; add appearance and reduced-motion inspection. |
| `effects_quality` | **78 P** | **used, viewed:** substantive optics, observed press/lens motion and working cross-fades. Retro transitions double text. Current hardware cost was not measured. [Shader:63](/home/speak/workspace/mui/modules/gfx/render/vector/effects/liquid_glass.slang:63). | A: behind in integrated optics/motion. M: behind in information preservation. T: behind in stable editing previews. | Improve displaced-geometry transitions; test interruptions; publish current integrated-GPU timing and memory. |
| `workshop_usability` | **77** | **used, viewed:** immediate edits, swatches, fixed header, accurate thickness and repaired global undo. Alpha display and missing workflows remain. [studio.cpp:473](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:473). | A: behind in feedback consistency. M: behind in authoring completion. T: behind in editing breadth. | Preserve RGBA; add precise numeric entry/picker/import; add comparison and explicit export destination/status. |
| `semantic_sameness` | **88** | **used, measured, viewed:** five previews matched IDs, hierarchy, roles, names, values, actions and stable states, excluding bounds and transient states. Automated helper still omits values/action metadata. [Test:29](/home/speak/workspace/mui/showcase/style-gallery/tests/styles_test.cpp:29). | A: parity in retained identity within this scene. M: parity in content/theme separation. T: parity in content/theme separation. | Automate fuller comparisons across edits, errors, disabled states and menus; document transformed bounds. |
| `framework_showcase` | **82 P** | **used, viewed:** data packs, role/type effects, agent edits, authorization and repaired history demonstrate the architecture. Embedded scopes were inspected in source/tests only. [library.cpp:324](/home/speak/workspace/mui/modules/ui/theme/src/library.cpp:324). | A: behind in platform integration. M: behind in implementation handoff. T: behind in integration/preview breadth. | Demonstrate scoped editing and export/reload live; expose costs; improve agent context and geometry. |
| `code_tests_docs` | **80** | **viewed, used:** meaningful regressions now cover range display, history and validation. Export round-trip, scope tests and pixel pins exist. No fresh suite execution claimed. [Tests:233](/home/speak/workspace/mui/showcase/style-gallery/tests/styles_test.cpp:233). | A: behind in material guidance. M: behind in workflow documentation. T: behind in editor coverage. | Add discovered regressions; strengthen semantic assertions; publish current test/benchmark evidence and better signature media. |

Chart measurements used mean RGB from 3×3 captures at window `(699,360)` inside Monday’s bar and `(729,360)` in the adjacent gap, converted through sRGB relative luminance: glass **3.55**, ink **3.05**, liquid **3.08**, neon **4.62**, retro **3.99**. These samples do not constitute a full accessibility audit.

The [historical benchmark artifact](/home/speak/workspace/mui/docs/benchmarks/2026-10-28-desktop.bench.omui.json) reports RTX 4080 style-frame p95 values of **0.36–1.38 ms**. This is **viewed historical evidence**, not a current measurement or proof of the integrated-GPU target. No fresh mcpp suite ran in this read-only evaluation.

**Benchmark summary**

Apple is strongest in jointly designed optics, motion and backdrop-dependent legibility. openmui now demonstrates responsive material behavior, but has much less contextual adaptation. [Apple presentation](https://developer.apple.com/videos/play/wwdc2025/219/).

Material Theme Builder is strongest in coordinated colour systems, light/dark consistency, typography and import/export. openmui’s workshop edits a much narrower subset. [Official documentation](https://github.com/material-foundation/material-theme-builder).

tweakcn offers broader component/application previews and richer colour, typography, spacing and shadow authoring. Those are substantial workflow gaps here. [Controls](https://raw.githubusercontent.com/jnsahaj/tweakcn/main/components/editor/theme-control-panel.tsx), [previews](https://raw.githubusercontent.com/jnsahaj/tweakcn/main/components/editor/theme-preview-panel.tsx).

openmui’s distinctive strength is auditable agreement between input, semantics and pixels. Superiority over the three benchmarks is not established. Figma Variables/Tokens Studio interoperability was not demonstrated.

**Change from the last record**

| Dimension | Previous → current | Reason |
|---|---:|---|
| `pack_glass` | 80 → 83 | Observed press response and travelling lens. |
| `pack_ink` | 67 → 67 | Glyph degradation persists. |
| `pack_liquid` | 70 → 72 | Chart contrast improved. |
| `pack_neon` | 69 → 74 | Better interaction-state emphasis. |
| `pack_retro` | 65 → 65 | Readability remains limiting. |
| `layout_composition` | 70 → 74 | Header/history stay visible. |
| `typography` | 78 → 80 | Better parameter wording and units. |
| `contrast_accessibility` | 66 → 73 | Default chart samples corrected. |
| `effects_quality` | 74 → 78 | Responsive material motion demonstrated. |
| `workshop_usability` | 71 → 77 | Value-range and cross-pack undo defects fixed. |
| `semantic_sameness` | 88 → 88 | Strong live equivalence; automated omissions remain. |
| `framework_showcase` | 78 → 82 | History integration now works. |
| `code_tests_docs` | 76 → 80 | Relevant regressions and documentation improved. |

Overall remains **65**; mean rises **73.2 → 76.4**.

**Highest-priority changes**

1. Protect ink/retro text from material distortion and dimming.
2. Add rendered contrast checks for edited colours and every control state.
3. Add paired light/dark and reduced-motion inspection.
4. Preserve alpha in colour display and editing.
5. Add numeric entry, a colour picker and import.
6. Add simultaneous comparison and broader state specimens.
7. Improve transitions involving warped geometry.
8. Group parameters by effect/selector with semantic context.
9. Publish current tests, integrated-GPU costs and full-resolution signature media.

**Detailed defects**

- [studio.cpp:392](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:392): Settings navigation searches a suffix lacking `column:0`; narrow-layout failure is **inferred**, not exercised.
- [studio.cpp:473](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:473): colour display discards alpha; reproduced through typing and `ui_act`.
- [studio.cpp:374](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:374): independently spaced day labels do not share bar-column centres.
- [studio.cpp:425](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:425): current pack is visually primary without selected/checked semantic state.
- [studio.cpp:543](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:543): repeated slider names lack selector context in their semantic names.
- [studio.cpp:225](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:225): direct filesystem directory operations bypass the repository’s required platform abstraction.
- [styles_test.cpp:57](/home/speak/workspace/mui/modules/ui/theme/styles/tests/styles_test.cpp:57): mixing theme-name hashes into appearance checks can conceal identical rendering.
- `modules/ui/theme/styles/README.md` remains missing.
- [README picture](/home/speak/workspace/mui/showcase/style-gallery/media/five-packs.png): five 576×360 frames; workshop labels are too small for inspection.
- [dev-story.md:32](/home/speak/workspace/mui/showcase/style-gallery/dev-story.md:32): “one cross-fade texture” lacks memory evidence; runtime retains the preceding display list.

```json
{"overall":65,"mean":76.4,"dimensions":{"pack_glass":83,"pack_ink":67,"pack_liquid":72,"pack_neon":74,"pack_retro":65,"layout_composition":74,"typography":80,"contrast_accessibility":73,"effects_quality":78,"workshop_usability":77,"semantic_sameness":88,"framework_showcase":82,"code_tests_docs":80}}
```