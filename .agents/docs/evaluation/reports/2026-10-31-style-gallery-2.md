Overall: 42/100  
Mean: 63.4/100

Evaluated the running `org.openmui.styles` application through openmui MCP, then inspected the requested source, tests, README, dev story, theme packs and liquid-glass design. Workspace HEAD: `4c7d8ff`, with uncommitted changes; the running binary’s exact revision was not verified. Gallery and theme files have no diff against previous evaluated commit `126cb48`.

Evidence: **used** = live interaction and capture; **measured** = computed results; **viewed** = inspected pixels/source; **inferred** = unverified implementation consequence. No compiled test suites or GPU benchmarks were executed. Performance targets receive no credit as measured results.

**Use log**

All captures were 1440×900. I used `ui_wait(settled:true)` and, after discovering its premature completion, explicitly advanced 60 frames before judging stills.

| Task | What I did | Expected | Actual | Result |
|---|---|---|---|---|
| Inspect and switch packs | `ui_apps`, `ui_snapshot`, `ui_capture`; sidebar clicks and `styles.show` for all five | Distinct packs; working transitions | Five distinct appearances; cross-fades observed, including clock-stepped intermediate frames | Pass |
| Glass controls | Clicked both switches, checkbox, Save/Cancel; dragged Volume; arrow key; replaced name; selected Americas by keyboard | Controls update; Save/Cancel communicate their result | Fields and selection updated; Save/Cancel gave no visible result | Partial |
| Neon controls | Repeated every settings control; scrolled editor | Same behavior and readable feedback | Controls worked; editor scrolled; excessive halos and corner artifacts remained | Partial |
| Retro controls | Clicked **drawn** switch positions; dragged slider; typed name; selected region; clicked buttons | Pixels and hit targets agree | Notifications click missed; visible glass selector also missed | Fail |
| Ink controls | Repeated switches, checkbox, slider drag/key, text, region and buttons | Same usable interface | Controls responded; text and controls were visibly distorted by treatment | Partial |
| Liquid controls | Repeated every settings control; tabbed into editor | Fluid treatment with clear focus | Controls responded; keyboard focus visible; slider joins looked pointed | Partial |
| Workshop edits | Typed orange accent; dragged Corners to zero and glass blur downward; captured | Predictable live edits, recoverable originals | Accent applied; panels retained rounded corners; blur range collapsed | Fail |
| Undo/reset/import | `ui_undo`; searched tree; invoked `styles.reset` and `styles.import` | Recover edits and load a pack | Undo reverted an older Volume keypress; reset/import verbs absent | Fail |
| Invalid colour | Typed `pink`; agent set `invalid` | Visible validation and accurate action result | Invalid edit disappeared without explanation; agent received “done,” but snapshot retained previous colour | Fail |
| Agent-to-pixels | `ui_act` set liquid accent to `#ff0066`; captured | Semantic edit changes picture | Accent, buttons, chart highlight and wallpaper changed | Pass |
| Semantic invariance | Switched all packs without intervening content edits; compared snapshots | Same preview semantics | **28/28 nodes matched** in every pack | Pass |
| Export | `styles.export` dry run, then invocation without an answer | External-effect gate | Confirmation required; tool reported user declined; nothing written | Gate passed; export unverified |
| Light/dark and accessibility variants | Inspected controls, tree and pack definitions | Each pack testable in both appearances and reduced transparency | No workshop controls for these variants; disabled examples absent | Fail coverage |

Defects reproduced through use:

- **Retro hit testing:** after settling retro, click the visible Notifications thumb at approximately `(310,174)`. It hits the Settings region and does nothing. Click the visible glass label at `(150,106)`; it hits the description. The tree still advertises unwarped bounds.
- **Workshop undo:** change Accent, Corners and blur through pointer/text input, then call `ui_undo`. It undoes an earlier preview Volume decrement, leaving style edits intact.
- **Shrinking blur range:** drag glass blur down from 10. Its advertised maximum becomes 1; requesting 10 through `ui_act` returns 1.
- **Misleading Corners:** drag Corners to zero. Input fields and buttons become square; preview panels remain rounded.
- **Silent rejection:** enter an invalid accent. No error appears. An agent’s successful action response can disagree with the subsequent value.
- **False settling:** invoke `styles.show` from retro to glass, call `ui_wait(settled:true)`, then capture immediately. The tool says the condition holds while both frames remain visibly superimposed.
- **Save/Cancel:** change the name and click either button. Neither provides completion feedback; Cancel does not restore the name.
- **Unavailable workflow:** no reset, import, simultaneous comparison or per-pack light/dark controls were found.

**Framework and openas feedback**

Stable paths, typed values, menu options, bounds and action provenance made the interface unusually inspectable. Input responses identified `agent:codex-evaluator;via=service:input`; semantic edits visibly reached rendering. Export’s external-effect gate worked, and the declined action was not retried.

The framework needs effect-aware pointer mapping, settling that includes cross-fades, and action results that distinguish accepted changes from rejected callbacks. History must include text and drag edits consistently. A scroll snapshot also retained `y:420` with `max_y:0` after switching to a shorter editor: semantic state needs clamping.

**Dimension assessment**

Source shorthand: **SG** = [studio.cpp](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp), **GT** = [gallery tests](/home/speak/workspace/mui/showcase/style-gallery/tests/styles_test.cpp), **FX** = `modules/gfx/render/vector/effects/`.

Comparisons use **A** = Apple Liquid Glass, **M** = Material Theme Builder, **T** = tweakcn. Rival comparisons are based on inspected official material, not hands-on rival sessions. For ink/neon/retro, comparisons concern finish and usability, not identical aesthetic offerings.

| Dimension | Score | Evidence and basis | Individual comparisons | To reach 90, highest impact first |
|---|---:|---|---|---|
| `pack_glass` | 70 | **used, viewed:** Initial glass capture has readable controls and real backdrop effects, but broad milky panels and huge discs dominate subtle optics. `FX/liquid_glass.slang:29–64` implements bevel sampling, dispersion and rim. | **A: behind:** less convincing optical hierarchy and interaction response. **M: behind:** less controlled surface hierarchy. **T: behind:** less polished preview composition. | Establish restrained material hierarchy; demonstrate lensing over detailed moving content; tune bright-background readability. |
| `pack_ink` | 67 | **used, viewed:** Warm paper and wandering outlines establish identity, but Settings and Save lettering soften and wobble with their containers. `FX/wobble.slang:25–34`. | **A: behind:** weaker text preservation. **M: behind:** less disciplined text/surface relationships. **T: behind:** lower everyday component clarity. | Apply irregularity to surfaces without resampling text; reduce grain; tune chart colours. |
| `pack_liquid` | 65 | **used, viewed:** Cleanest ordinary interface; fluid identity largely consists of slider bridges. Dragging shows lateral points rather than convincing liquid deformation. | **A: behind:** little interaction-driven material motion. **M: behind:** thinner theme/state coverage. **T: behind:** narrower customization and preview range. | Refine merge geometry and interaction motion; extend identity beyond sliders; keep controls precise. |
| `pack_neon` | 61 | **used, viewed:** Strong identity, but every sidebar action competes with content; Save and selectors show dark corner wedges inside bright rectangular halos. | **A: behind:** weaker emphasis hierarchy. **M: behind:** less restrained state treatment. **T: behind:** less polished component edges. | Fix shape/halo corners; reserve strongest glow for active controls; separate focus from decoration. |
| `pack_retro` | 42 | **used, viewed:** Peripheral text is very dark; CRT displaces pixels away from actual hit targets. `FX/crt.slang:16–28`. | **A: behind:** material appearance compromises input accuracy. **M: behind:** substantially poorer legibility. **T: behind:** preview cannot reliably be operated where drawn. | Correct pointer mapping first; recover luminance; reduce scanline, grille and vignette damage. |
| `layout_composition` | 68 | **used, viewed:** Useful three-column structure, but panels occupy only the upper ~400 px; remaining preview is oversized wallpaper. Settings and dashboard edges miss alignment. SG:138–164,184,233. | **A: behind:** weaker spatial hierarchy. **M: behind:** less useful authoring space. **T: behind:** less informative component coverage. | Align panel grid; reclaim empty space for meaningful states and comparison; make rails adaptive. |
| `typography` | 67 | **used, viewed:** Main hierarchy is understandable. Volume lacks a visible label/value; editor sliders lack values/units; chart lacks quantities and day labels. SG:117–156,203–228. | **A: behind:** weaker small-text treatment. **M: behind:** less complete type/token presentation. **T: behind:** less informative editing controls. | Add persistent labels, numeric entry and units; annotate chart; preserve text sharpness. |
| `contrast_accessibility` | 45 | **used, viewed; inferred fallback:** Liquid focus ring was visible, but retro is difficult to read. No disabled samples or appearance controls. Gallery uses role effects; generic effect painting at `modules/ui/view/src/frame.cpp:533–563` shows no reduced-transparency branch. | **A: behind:** no demonstrated equivalent accessibility adaptation. **M: behind:** no demonstrated accessible light/dark schemes. **T: behind:** insufficient state and appearance inspection. | Fix retro; verify post-effect contrast; implement opaque fallback for role effects; expose light/dark, disabled and focus previews. |
| `effects_quality` | 62 | **used, viewed:** Cross-fades work, but displaced text ghosts during retro transitions; neon corners and ink text suffer. Shader implementations are substantive. GPU cost remains unmeasured. | **A: behind:** motion and optics are less coherent. **M: behind:** effects reduce preview clarity. **T: behind:** visual stability is weaker during customization. | Fix artifacts and settling; refine transition strategy; publish GPU timings and memory costs against budgets. |
| `workshop_usability` | 45 | **used, viewed:** Live editing works, but recovery, validation, numeric control and complete file workflow fail or are missing. SG:203–228 discards setter errors; SG:222 makes ranges depend on current values. | **A: behind:** weaker user control and feedback. **M: behind:** incomplete import/export authoring cycle. **T: behind:** sparse visual editing workflow. | Repair history and ranges; add reset/import and validation; provide numeric entry, comparison and export feedback. |
| `semantic_sameness` | 85 | **measured, used, viewed:** All five preview snapshots matched across 28 nodes, including values, persistent states, action descriptors and child relationships; transient focus/hover and bounds excluded. GT:29–35 checks fewer fields. | **A: behind:** CRT breaks pixel-to-input correspondence. **M: parity in principle:** styling remains separate from content. **T: parity in principle:** theme edits preserve component identity; rival invariance was not measured. | Preserve visual/input correspondence; automate richer comparisons, open-menu states and equivalent actions across all packs. |
| `framework_showcase` | 79 | **used, viewed:** Data packs, role/type effects and agent switching form a convincing demonstration. Scope integration is supported by GT:160–217 but was not exercised here. | **A: behind:** material behavior is less complete. **M: behind:** designer-to-implementation workflow is unfinished. **T: behind:** customization breadth and integration are narrower. | Demonstrate scoped editing live; complete edit/export/load sequence; expose pipeline, cost and accessibility behavior. |
| `code_tests_docs` | 68 | **viewed:** Modular implementation, readable pack data, pixel pinning and scope tests exist. GT:141 claims every-pack interaction but instantiates only ink. README overstates Save/Cancel. | **A: behind:** weaker demonstrated acceptance evidence. **M: behind:** documented workflow exceeds delivery. **T: behind:** prototype gaps remain in core authoring tasks. | Correct claims; add regression coverage for observed failures; execute/publish checks; record a complete signature workflow. |

**Benchmark summary**

Apple’s strength is coordinated optics, motion and automatic accessibility adaptation. Material Theme Builder supplies dynamic colour, accessible light/dark schemes, a type scale and import/export. tweakcn emphasizes visual customization and theme presets. These are documented in [Apple’s material presentation](https://developer.apple.com/videos/play/wwdc2025/219/), [Material Theme Builder’s README](https://github.com/material-foundation/material-theme-builder), and [tweakcn’s README](https://github.com/jnsahaj/tweakcn).

openmui’s strongest demonstrated distinction is the directly inspectable relationship between semantic identity, agent actions and five rendered appearances. No overall lead over these products is established. The largest deficits are retro input accuracy/readability and workshop recovery. Figma Variables and Tokens Studio remain useful references for structured token authoring; interoperability was not demonstrated.

**Change from the last record**

These are evidence-driven reassessments, not claimed implementation regressions.

| Dimension | Previous → current | Reason |
|---|---:|---|
| `pack_glass` | 70 → 70 | Live use confirms existing visual assessment. |
| `pack_ink` | 67 → 67 | Identity and text degradation both confirmed. |
| `pack_liquid` | 65 → 65 | Dragging confirms limited fluid treatment. |
| `pack_neon` | 61 → 61 | Glow hierarchy and corner defects persist. |
| `pack_retro` | 45 → 42 | Previously suspected pointer mismatch reproduced. |
| `layout_composition` | 68 → 68 | Space allocation remains unresolved. |
| `typography` | 69 → 67 | Missing values and labels impede actual editing. |
| `contrast_accessibility` | 50 → 45 | Live retro use exposes combined readability/input failure. |
| `effects_quality` | 64 → 62 | Premature settling and transition ghosting reproduced. |
| `workshop_usability` | 52 → 45 | Undo skipping edits and shrinking ranges reproduced. |
| `semantic_sameness` | 82 → 85 | Richer live five-pack comparison now measured. |
| `framework_showcase` | 80 → 79 | Strong semantic demonstration offset by input/history failures. |
| `code_tests_docs` | 70 → 68 | Existing tests miss newly reproduced workflow defects. |

Overall falls **45 → 42**; mean **64.8 → 63.4**.

**Highest-priority changes**

1. Make distorted rendering and pointer coordinates agree.
2. Restore retro legibility before further aesthetic refinement.
3. Record style text/drag edits in undo history; add reset and redo.
4. Keep parameter ranges stable and expose numeric values/units.
5. Surface validation errors and return truthful agent results.
6. Implement meaningful Save/Cancel with dirty-state feedback.
7. Add light/dark, reduced-transparency, disabled and focus coverage.
8. Fix neon corners, ink text resampling and premature settling.
9. Complete import/export/comparison, then publish regression and GPU benchmark results.

**Detailed defects**

In addition to the reproduced failures above:

- **All packs, editor:** raw names such as `tint_a`, `color_b` and `role:region` lack designer-facing explanations.
- **All packs, dashboard:** goal bars omit targets; chart exposes a figure name but no accessible day/value data.
- **Ink sidebar:** selected background remains purple while accent controls are red; chart colours likewise retain inherited relationships.
- **Liquid sliders:** effect colour remains partly independent of the edited accent, producing visible mixed-colour joins.
- **Preview panels:** hard-coded radius 16 bypasses the Corners token, SG:110–111.
- **Export implementation:** fixed pack filenames can overwrite existing files; direct filesystem/stream use violates the repository’s platform-I/O rule, SG:64–73.
- **Draft isolation, inferred:** edits replace entries in the process-wide theme library, SG:28–38; interference between studio instances is unverified.
- **README:** line 17 says neon has three effects/fifteen parameters; the pack declares four effects/twenty numeric parameters.
- **README picture:** the inspected 576×360 first frame makes editor text too small for useful inspection.
- **Dev story:** line 38 describes numbers in fields, whereas editing currently uses sliders.
- **Performance evidence:** `tests/bench/budgets.bench.omui.json:41–50` specifies 8.33 ms targets; this evaluation establishes no hardware pass.

```json
{"overall":42,"mean":63.4,"dimensions":{"pack_glass":70,"pack_ink":67,"pack_liquid":65,"pack_neon":61,"pack_retro":42,"layout_composition":68,"typography":67,"contrast_accessibility":45,"effects_quality":62,"workshop_usability":45,"semantic_sameness":85,"framework_showcase":79,"code_tests_docs":68}}
```