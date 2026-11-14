Overall: 60/100  
Mean: 71.5/100

Evaluated on 2026-10-31 against workspace HEAD `ece4041`. The running application did not expose its build revision.

Evidence: **used** = live input and captures; **measured** = computed results; **viewed** = inspected files; **inferred/P** = provisional. Competitor comparisons use official documentation, not firsthand operation. For non-glass packs, comparisons concern craft and usability rather than matching aesthetics.

**Use log**

| Task | What I did | Expected | Actual | Result |
|---|---|---|---|---|
| Discover | `ui_apps`, `ui_snapshot`, `ui_capture` | Inspect running workshop | Found `org.openmui.styles`, 1440×900 | Pass |
| Switch five packs | Coordinate clicks and `styles.show`; settled captures | Distinct appearances, preserved content | All five worked; values persisted | Pass |
| Inspect motion | `ui_clock(pause/step)`, captures during cross-fade, button press and tab movement | Continuous material response | Intermediate blending, changing button rim and moving lens observed | Pass |
| Settings in every pack | Click both switches and checkbox; drag Volume and use arrows; replace name; select Region using keys | Controls and values agree | All responded, including retro’s warped controls | Pass |
| Save, Cancel, navigation | In every pack: Save, modify, Cancel; click all three tabs | Restore saved values; navigate consistently | Restoration and feedback worked; tab selection worked | Pass |
| Workshop | Change accent, corners, blur, glow, wobble, smoothness and curvature; Undo/Redo/Reset; wheel-scroll | Immediate, recoverable changes | Worked; separate drags and per-pack histories now survive correctly | Pass |
| Agent editing | `ui_act(set_value)` Accent; `styles.set` valid/invalid values; capture | Picture follows accepted values; rejection preserves draft | Colour propagation and rejection recovery worked; effect-range display disagreed | Partial |
| Accessibility/comparison | Toggle Reduce transparency; inspect hover/focus; search light/dark/import controls | Readable fallback and paired previews | Transparent tab fallback, poor hover contrast; requested modes and comparison unavailable | Fail |
| Semantic equivalence | Compare five preview snapshots and expanded Region subtrees | Identical semantic content | Five matching 62-node previews and five-node Region subtrees | Pass |
| Export | Coordinate click; inspect file; semantic dry run | Same authorization through both paths | Click wrote `liquid.style.omui.toml` without an answer; dry run required confirmation | Fail |

Defects reproduced through use:

- **Export authorization disagreement:** click Export at approximately `(110,866)`. The application writes the file and reports success. `ui_act(..., dry_run:true)` says the same external action requires `"yes"`. No answer was supplied.
- **Incomplete transparency fallback:** select glass, scroll the editor to its bottom, enable Reduce transparency. Glass rims disappear, but the bottom tab bar still reveals the differently coloured wallpaper.
- **Glass hover readability:** click Settings in the bottom tab bar and leave the pointer there. Its hover background becomes opaque white beneath pale lavender text. Moving away restores the translucent treatment.
- **Effect value misrepresentation:** invoke `styles.set` with `{"what":"button/thickness","value":60}` in glass. It succeeds, but the workshop label and semantic slider report **48 px**.
- **Incomplete authoring workflow:** no import, colour picker, direct numeric entry, simultaneous comparison, or light/dark inspection for each pack was available.
- **Editing friction:** glass’s long parameter list scrolls its heading and Undo/Redo/Reset completely out of view. Export feedback appears inside the preview’s Settings panel, away from the export control.
- **Readability and composition:** ink visibly softens small text; retro darkens and resamples captions. All full-window previews devote much of their lower half to oversized wallpaper discs.

**Framework and openas feedback**

Stable paths, typed values, action effects, input hit receipts and actor provenance were useful. Retro’s inverse input mapping worked at visible controls. Settling and clock stepping separated transient frames from final appearance.

The external-action boundary is inconsistent. [input.cpp:411](/home/speak/workspace/mui/modules/ui/view/src/input.cpp:411) supplies an external action’s answer automatically; input attribution subsequently identifies the caller as an agent. Restrict automatic answers to actual local-person input and test pointer and keyboard routes alongside semantic invocation.

The preview comparison included IDs, roles, names, values, states and complete action descriptions, excluding bounds and transient hover/focus. Retro’s semantic bounds still describe unwarped geometry; tools should expose or explain that mapping.

**Dimension assessment**

Abbreviations: **A** = Apple Liquid Glass; **M** = Material Theme Builder; **T** = tweakcn.

| Dimension | Score | Evidence and basis | Individual comparisons | To reach 90, in priority order |
|---|---:|---|---|---|
| `pack_glass` | 76 | **used, viewed:** navigation/control glass has stronger hierarchy and responsive rims. Content surfaces remain slightly translucent; hover treatment damages the material. [Pack:15](/home/speak/workspace/mui/modules/ui/theme/styles/styles/glass.style.omui.toml:15). | A: behind in optical integration. M: behind in surface consistency. T: behind in preview coverage. | Fix hover/fallback; demonstrate glass over detailed moving content; validate adaptive foreground contrast. |
| `pack_ink` | 67 | **used, viewed:** convincing paper and irregular outlines, but grain and whole-layer distortion soften labels and numbers. [wobble.slang:25](/home/speak/workspace/mui/modules/gfx/render/vector/effects/wobble.slang:25). | A: behind in text preservation. M: behind in caption clarity. T: behind in editing comfort. | Preserve glyphs outside distortion; reduce grain; tune paper and graphic colours together. |
| `pack_liquid` | 70 | **used, viewed:** accent now recolours the material correctly. Fluid identity remains concentrated in pointed slider bridges. [Pack:20](/home/speak/workspace/mui/modules/ui/theme/styles/styles/liquid.style.omui.toml:20). | A: behind in fluid response. M: behind in colour-role completeness. T: behind in customization breadth. | Refine joins through dragging; develop coherent fluid states beyond sliders; improve chart contrast. |
| `pack_neon` | 69 | **used, viewed:** readable content and coherent token-bound glow; almost every editor rail and inactive navigation control competes for attention. [Pack:23](/home/speak/workspace/mui/modules/ui/theme/styles/styles/neon.style.omui.toml:23). | A: behind in emphasis hierarchy. M: behind in state restraint. T: behind in authoring clarity. | Reserve strongest glow for meaningful states; distinguish focus; reduce idle decoration. |
| `pack_retro` | 65 | **used, viewed:** clear terminal identity and working warped input. Grille, vignette and resampling reduce sustained readability. [crt.slang:22](/home/speak/workspace/mui/modules/gfx/render/vector/effects/crt.slang:22). | A: behind in clarity preservation. M: behind in small-text legibility. T: behind in sustained editing comfort. | Add a readable CRT variant; protect captions; constrain dimming and grille strength. |
| `layout_composition` | 70 | **used, viewed:** understandable three-column workspace, but panel tops differ and wallpaper dominates unused space. Editor width is fixed. [studio.cpp:367](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:367). | A: behind in spatial hierarchy. M: behind in workspace efficiency. T: behind in useful preview density. | Align panel starts; add comparison/state previews below; keep history controls visible. |
| `typography` | 78 | **used, viewed:** persistent labels, chart quantities and parameter units are substantial improvements. “Ior” and repeated parameter names still need explanation; effects degrade captions. [studio.cpp:404](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:404). | A: behind in material-safe type. M: behind in type authoring. T: behind in editing clarity. | Preserve text sharpness; explain optical terminology; expose typography controls. |
| `contrast_accessibility` | 60 | **used, measured, viewed:** chart samples are **3.58:1 glass**, **2.38:1 liquid**, **4.63:1 neon**. Labels and focus exist, but glass hover/fallback fail and paired appearances are absent. [tabs.cpp:171](/home/speak/workspace/mui/components/base/src/tabs.cpp:171). | A: behind in automatic adaptation. M: behind in paired schemes. T: behind in appearance inspection. | Fix opaque fallback and hover contrast; audit post-effect text/graphics/focus; provide both appearances. |
| `effects_quality` | 70 P | **used, viewed; cost inferred:** cross-fades, press response and sliding glass lens work. Shaders implement substantive optics, but ink/retro compromise text. Hardware timing remains unmeasured. [liquid_glass.slang:38](/home/speak/workspace/mui/modules/gfx/render/vector/effects/liquid_glass.slang:38). | A: behind in integrated motion/optics. M: behind in information preservation. T: behind in editing stability. | Protect text; test interrupted transitions; publish GPU timing and memory measurements. |
| `workshop_usability` | 71 | **used, viewed:** history, validation and colour propagation improved. Accepted effect values can be displayed incorrectly; authoring and export remain incomplete. [studio.cpp:480](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:480). | A: behind in feedback consistency. M: behind in authoring completion. T: behind in editing breadth. | Correct authorization and value display; add precise entry, picker, import, comparison and destination feedback. |
| `semantic_sameness` | 88 | **used, measured, viewed:** all five 62-node previews and expanded menus matched. Automated comparison still omits values and action effects. [styles_test.cpp:29](/home/speak/workspace/mui/showcase/style-gallery/tests/styles_test.cpp:29). | A: parity in control identity. M: parity in content preservation. T: parity in theme/content separation. | Automate fuller comparisons across editing, error, disabled and menu states; address transformed bounds. |
| `framework_showcase` | 72 P | **used, viewed; scopes inferred:** data packs, role/type effects and agent-to-picture edits are convincing. Scope isolation has source tests, but export breaks input-path equivalence. [library.cpp:324](/home/speak/workspace/mui/modules/ui/theme/src/library.cpp:324). | A: behind in material integration. M: behind in designer-to-code completion. T: behind in integration workflow. | Restore authorization equivalence; demonstrate scoped editing and export/reload; expose measured costs. |
| `code_tests_docs` | 74 | **used, viewed:** transactional edits and gesture-aware history are clearer. Tests cover semantics, motion, export and scopes, but miss reproduced failures. [studio.cpp:68](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:68). | A: behind in acceptance evidence. M: behind in workflow verification. T: behind in editor regression coverage. | Add input authorization, fallback and range regressions; strengthen semantic assertions; publish fresh test/benchmark results. |

Contrast measurements used mean RGB from 3×3 patches centred at chart-local `(25,100)` inside Monday’s bar and `(55,100)` in the adjacent gap, converted through sRGB relative luminance. These samples are not a complete accessibility audit.

`mcpp test -p openmui.showcase.style_gallery --profile release` remained in build planning and was interrupted. No fresh suite pass is claimed. GPU benchmark scenes and budgets exist; their results were not measured here.

**Benchmark summary**

Apple’s strongest qualities are integrated material behavior and automatic transparency, contrast and motion adaptations. [Apple’s material presentation](https://developer.apple.com/videos/play/wwdc2025/219/).

Material Theme Builder leads in coordinated light/dark schemes, colour harmonization, type customization and implementation export/import. [Official documentation](https://github.com/material-foundation/material-theme-builder).

tweakcn leads in approachable visual customization and starting presets. [Official project](https://github.com/jnsahaj/tweakcn).

openmui’s clearest distinction is inspectable agreement between semantic content, agent actions and pixels. No superiority over all three benchmarks is established. The largest gaps are accessibility, dependable authoring completion and material polish. Figma Variables/Tokens Studio interoperability was not demonstrated.

**Change from the previous record**

| Dimension | Previous → current | Reason |
|---|---:|---|
| `pack_glass` | 70 → 76 | Better material placement and responsive controls; hover/fallback remain wrong. |
| `pack_ink` | 67 → 67 | Grain and text distortion remain. |
| `pack_liquid` | 65 → 70 | Accent now propagates into the material. |
| `pack_neon` | 68 → 69 | Cleaner parameter presentation; excessive idle glow remains. |
| `pack_retro` | 65 → 65 | Input works; readability remains weak. |
| `layout_composition` | 68 → 70 | More informative panels; space allocation remains inefficient. |
| `typography` | 74 → 78 | Persistent field labels and visible chart quantities added. |
| `contrast_accessibility` | 50 → 60 | Chart contrast improved; fallback and hover still fail. |
| `effects_quality` | 68 → 70 | Additional material responses observed; cost unverified. |
| `workshop_usability` | 63 → 71 | Rejected-edit recovery, separate gestures, retained histories and corner bounds fixed. |
| `semantic_sameness` | 88 → 88 | Fuller current preview verified; automated omissions remain. |
| `framework_showcase` | 78 → 72 | Newly exercised Export input path bypasses confirmation. |
| `code_tests_docs` | 72 → 74 | Implementation improves, but important regressions lack coverage. |

Overall rises **50→60**; mean rises **68.9→71.5**.

**Highest-priority changes**

1. Prevent agent input from supplying external-action answers automatically.
2. Make reduced-transparency component paint genuinely opaque.
3. Correct glass tab hover alpha and foreground contrast.
4. Share effect ranges and actual values across API, editor and semantic tree.
5. Add precise numeric entry, colour picking and import.
6. Add paired appearances, comparison and comprehensive contrast checks.
7. Keep history controls visible; clarify export destination and feedback.
8. Preserve text sharpness in ink and retro; restrain neon emphasis.
9. Automate reproduced failures and publish hardware effect/transition measurements.

**Detailed defects**

Additional file-specific findings:

- **Fallback misses custom component paint:** runtime solidification handles node backgrounds at [frame.cpp:605](/home/speak/workspace/mui/modules/ui/view/src/frame.cpp:605), while Tabs paints translucent tokens itself at [tabs.cpp:106](/home/speak/workspace/mui/components/base/src/tabs.cpp:106).
- **Misleading navigation target:** Settings navigation searches `/panels/settings`, while the actual path contains `/panels/column:0/settings`. Narrow-layout failure is **inferred**, not exercised. [studio.cpp:349](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:349).
- **Validation classification:** invalid parameters produce an `internal:` tool error, although these are ordinary rejected inputs.
- **Restricted parameter coverage:** only numeric keys already present become controls; token-bound colours and other registered parameters cannot be authored fully. [studio.cpp:470](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:470).
- **Platform-rule exception:** export directly calls `std::filesystem` directory operations. [studio.cpp:203](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp:203).
- **Documentation drift:** README claims neon exposes twenty parameters; the current editor exposes eight effect sliders. [README.md:17](/home/speak/workspace/mui/showcase/style-gallery/README.md:17).
- **Unverified cost claim:** “one cross-fade texture” lacks measured backing; runtime retains a display list. [dev-story.md:32](/home/speak/workspace/mui/showcase/style-gallery/dev-story.md:32).
- **Signature image:** `media/five-packs.png`, 576×360, makes right-column editor labels too small for comfortable inspection.

```json
{"overall":60,"mean":71.5,"dimensions":{"pack_glass":76,"pack_ink":67,"pack_liquid":70,"pack_neon":69,"pack_retro":65,"layout_composition":70,"typography":78,"contrast_accessibility":60,"effects_quality":70,"workshop_usability":71,"semantic_sameness":88,"framework_showcase":72,"code_tests_docs":74}}
```