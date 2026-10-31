Overall: 45/100  
Mean: 64.8/100

Evaluated commit `126cb48`, using the supplied frames and inspected source. **No tests or benchmarks were executed** in this read-only evaluation. Existing assertions and budgets receive credit as implementation evidence, not passing measurements.

Evidence: **V** = viewed image or code; **I** = inferred behavior, provisional. Paths below use `SG` = `showcase/style-gallery`, `FX` = `modules/gfx/render/vector/effects`, and `PACK` = `modules/ui/theme/styles/styles`.

Benchmark shorthand: **A** = Apple Liquid Glass; **M** = Material Theme Builder; **T** = tweakcn. For specialty packs, comparison concerns visual craft and usable controls; M and T do not provide equivalent CRT, ink or neon materials. Their absence earns openmui no automatic advantage.

| Dimension | Score | Evidence and basis | Comparison with each benchmark | Changes needed for 90, highest impact first |
|---|---:|---|---|---|
| `pack_glass` | 70 | **V:** `styles-glass__glass.png`, Settings and dashboard: convincing frost, rounded edges and backdrop transmission, but broad milky surfaces flatten depth. `FX/liquid_glass.slang:33–64` implements refraction, dispersion, adaptive tint and directional rim lighting. | **A: behind:** lacks its integrated material hierarchy and refinement. **M: behind:** foreground/background separation is less dependable. **T: behind:** control and panel hierarchy is less crisp. | Establish readable material variants; refine bevel/rim balance; demonstrate refraction over detailed moving content rather than oversized discs. |
| `pack_ink` | 67 | **V:** `styles-ink__ink.png`, whole frame: recognizable paper and wandering outlines, but grain covers text and controls uniformly. `FX/wobble.slang:26–34` displaces the entire rendered layer, including lettering. | **A: behind:** surface treatment compromises content clarity. **M: behind:** small text and control boundaries are less clean. **T: behind:** decorative texture competes with hierarchy. | Preserve sharp text; concentrate irregularity on outlines; reduce grain and edge darkening; develop a deliberate ink-specific type treatment. |
| `pack_liquid` | 65 | **V:** `styles-liquid__liquid.png`, sliders: visible droplet joins, otherwise largely a conventional light theme. `PACK/liquid.style.omui.toml:19–20` applies metaball only to sliders. | **A: behind:** fluidity is localized and lacks demonstrated coordinated motion. **M: behind:** the overall system is less developed. **T: behind:** preview breadth and finished control styling are weaker. | Develop a coherent fluid interaction language; show joins separating during movement; coordinate slider, toggle and button states. |
| `pack_neon` | 61 | **V:** `styles-neon__neon.png`, sidebar and action buttons: excessive cyan rectangles, abrupt dark corner wedges and competing magenta panel halos. `FX/neon.slang:20–29` uses a simple bounded quadratic glow. | **A: behind:** illumination hierarchy is less restrained. **M: behind:** too many controls demand equal attention. **T: behind:** selected, ordinary and secondary actions are less differentiated. | Reduce ordinary-control glow; remove corner artifacts; reserve strongest illumination for selection/focus; tune falloff and saturation. |
| `pack_retro` | 45 | **V:** `styles-retro__retro.png`, entire frame: very dark text, strong grille interference and vignette, especially at sidebar/editor edges. `FX/crt.slang:23–28` multiplies mask, scanline and vignette attenuation. | **A: behind:** material spectacle overwhelms legibility. **M: behind:** labels and boundaries are harder to read. **T: behind:** usable preview clarity deteriorates substantially. | Restore foreground luminance; greatly reduce mask/scanline attenuation; soften curvature and vignette; preserve readable peripheral controls. |
| `layout_composition` | 68 | **V:** all full-size frames: orderly upper cards, but most lower preview space contains decorative circles. `SG/src/studio.cpp:138–164,184,233` fixes sidebar/editor widths at 220/300 and Settings at 380; dashboard minimum is 380. Wrapping exists inside the preview. | **A: behind:** adaptive density is less resolved. **M: behind:** editing space is less purposefully allocated. **T: behind:** preview and controls use available space less effectively. | Add collapsible/resizable rails and compact layouts; align Settings with stat-card tops; replace excessive wallpaper space with useful preview cases. |
| `typography` | 69 | **V:** clear heading/value hierarchy in glass/liquid, but no visible values or units beside editor sliders. Retro damages small text. `SG/src/studio.cpp:208–228` renders raw parameter identifiers; `:141–156` supplies no chart axis/day labels. | **A: behind:** material-aware text clarity is weaker. **M: behind:** labels and numerical information are less systematic. **T: behind:** the editor exposes implementation vocabulary rather than polished controls. | Show numeric values and units; use human-readable labels and help; protect small text from effects; label chart data and preview controls visibly. |
| `contrast_accessibility` | 50 | **V:** retro is visibly difficult to read; no supplied focus or disabled-state frames. Reduced-motion fades are handled in `modules/ui/view/src/frame.cpp:475`. **I:** gallery glass uses role effects, while reduced-transparency handling found in `components/liquid-glass/src/glass.cpp:65–79` belongs to a different component path; gallery fallback remains unproven. | **A: behind:** Apple explicitly integrates reduced transparency, increased contrast and reduced motion. **M: behind:** readable color-role discipline is less dependable here. **T: behind:** retro and effect-heavy controls undermine basic preview usability. | Fix post-effect contrast; verify opaque fallback for role-targeted glass; test keyboard focus, disabled states and actual screen-reader output across all packs. |
| `effects_quality` | 64 | **V:** substantive Slang implementations, deterministic reference-oriented arithmetic and effect tests exist. `tests/bench/src/scenes.cpp:519–542` benchmarks all five packs; budgets specify 8.33 ms p95. No measured results reviewed. **I, provisional:** transition smoothness, hardware cost and animated artifacts remain unverified. | **A: behind:** integrated optical/motion quality is not demonstrated. **M: behind:** added rendering complexity lacks demonstrated user-value/performance balance. **T: behind:** effects introduce visible artifacts into otherwise simple controls. | Supply hardware timings and memory/pass counts; repair visible artifacts; test interrupted transitions and parameter extremes; capture motion at multiple scales. |
| `workshop_usability` | 52 | **V:** `SG/src/studio.cpp:190–234` provides live accent, corner and numeric-effect editing, but no visible values, undo/reset, import, color picker or inline validation. Errors are discarded at `:203,209,228`. Export writes a file at `:64–73`. | **A: behind:** preference controls are less understandable and protected. **M: behind:** theme-authoring workflow is far narrower. **T: behind:** customization and reusable output are less discoverable and recoverable. | Add values/direct entry, undo/redo/reset and error feedback; add import and export confirmation; expose complete theme roles; separate editor chrome from experimental styling. |
| `semantic_sameness` | 82 | **V:** one preview builder, `SG/src/studio.cpp:122–164`; tests compare IDs, roles, names, action names and state bits across five packs, `SG/tests/styles_test.cpp:29–36,60–79`. **I, provisional:** assertions were not run; values, action schemas, relationships and equivalent interaction sequences are not fully compared. | **A: behind:** complete accessibility behavior is not established. **M: ahead in inspected evidence:** explicit five-style semantic snapshot comparison is a stronger declared contract. **T: ahead in inspected evidence:** cross-style agent-action identity is explicitly tested. | Run full semantic diffs and identical action sequences under every pack; include values, relationships and action schemas; verify visual hit locations after distortion. |
| `framework_showcase` | 80 | **V:** data selectors target types/roles; agent verbs exist at `SG/src/studio.cpp:261–275`; embedding tests verify scoped pixels, surrounding theme retention and popup theme, `SG/tests/styles_test.cpp:160–217`. | **A: behind:** system-wide integration and interaction refinement are much broader. **M: ahead in inspected scope:** demonstrates runtime shader effects plus agent switching. **T: ahead in inspected scope:** explicitly combines scoped effects, semantic actions and pack switching. | Turn these mechanisms into a convincing end-to-end authoring task; show exported-pack consumption in a separate application; add observable transition/effect diagnostics. |
| `code_tests_docs` | 70 | **V:** meaningful snapshot, export, agent and scope tests exist. However, “under every pack” interaction test runs only ink (`SG/tests/styles_test.cpp:141–154`). README claims working Cancel, but Cancel has no handler (`SG/src/studio.cpp:135`); Save only increments a counter (`:137`). | **A: behind:** documentation and acceptance evidence do not support equivalent confidence. **M: behind:** documented authoring purpose exceeds delivered workflow. **T: behind:** implementation and documentation still expose prototype limitations. | Correct unsupported claims; implement real Save/Cancel; expand interaction and failure tests; publish benchmark results and a reproducible edit/export/import signature sequence. |

**Benchmark summary**

Apple is strongest in integrated material behavior and accessibility adaptation, including [reduced transparency, increased contrast and reduced motion](https://developer.apple.com/videos/play/wwdc2025/219/). Material Theme Builder’s strength is [dynamic-color theme creation](https://github.com/material-foundation/material-theme-builder). tweakcn’s strength is [visual customization of shadcn/ui components](https://github.com/jnsahaj/tweakcn).

openmui’s clearest differentiator is its explicit connection between style data, runtime effects, theme scopes and semantic agent actions. The inspected tests make that proposition concrete. This is a narrower advantage than overall product leadership: the largest deficits are retro readability, safe editing, useful preview coverage and demonstrated rendering performance. No dimension reaches 90.

**Change from the previous record**

The previous rubric had 15 dimensions; this subject has 13. Direct mappings are used where possible; merged dimensions are identified. These are reassessments, not proof that the implementation changed.

| Current dimension | Previous → current | Reason |
|---|---:|---|
| `pack_glass` | 70 → 70 | Real material implementation; milky, flattened composition persists. |
| `pack_ink` | 66 → 67 | Distinct outlines and paper treatment earn modest credit; text degradation remains. |
| `pack_liquid` | 65 → 65 | Fluid treatment remains limited to sliders. |
| `pack_neon` | 61 → 61 | Strong identity, excessive glow and corner artifacts. |
| `pack_retro` | 45 → 45 | Readability remains the gating failure. |
| `layout_composition` | 68 → 68 | Useful upper grid, unresolved space allocation. |
| `typography` | 69 → 69 | Clear main hierarchy; editor values/units remain absent. |
| `contrast_accessibility` | 52 → 50 | Gallery-specific fallback and focus evidence are insufficient; retro contrast remains poor. |
| `effects_quality` | 60 → 64 | Concrete shader code, benchmark scenes and budgets merit credit; no performance pass claimed. |
| `workshop_usability` | 55 / 60 → 52 | Combines prior workshop/export dimensions; missing recovery, import and feedback weigh heavily. |
| `semantic_sameness` | 85 → 82 | Comparison covers only part of semantic state; execution and all-pack behavior remain unverified. |
| `framework_showcase` | 81 → 80 | Strong scope/agent mechanisms, weaker complete user task. |
| `code_tests_docs` | 73 / 69 → 70 | Combines prior code/docs dimensions; useful tests offset inaccurate claims and missing coverage. |

Overall remains 45. Mean changes from 65.3 to 64.8, but the changed dimension set prevents a strict like-for-like average comparison.

**Highest-priority changes**

1. Rebalance CRT luminance, mask and vignette until all peripheral labels remain readable.
2. Verify reduced-transparency behavior for gallery role effects; add focus, disabled-state and post-effect contrast coverage.
3. Add numeric values, units, direct entry and descriptive parameter labels.
4. Implement undo/redo, reset, import, validation feedback and visible export success/failure.
5. Implement meaningful settings Save/Cancel and dirty-state feedback.
6. Remove neon corner artifacts and reduce glow on ordinary sidebar actions.
7. Preserve sharp text in ink; refine glass depth and material hierarchy.
8. Make the rails adaptive and add useful preview cases instead of excess wallpaper.
9. Execute semantic/action tests across every pack and verify distorted-frame pointer alignment.
10. Publish hardware performance results and a signature sequence showing edit → export → load elsewhere.

**Specific defects**

- **All full-size frames, Settings slider:** no visible “Volume” label or current value, despite its semantic label (`SG/src/studio.cpp:129`).
- **All full-size frames, editor:** Accent has no persistent visible label; sliders expose neither values nor units (`:203–228`).
- **All full-size frames, chart:** days, quantities and goal targets are absent. Progress bars expose only “Of the goal” (`:117–119`).
- **All full-size frames, upper preview:** Settings begins below the stat-card top edge; its bottom also misses the chart’s bottom alignment.
- **Glass, Settings background:** the enormous pale disc dominates the material rather than revealing fine optical detail.
- **Ink, Settings title and Save:** lettering appears warped/softened along with the enclosing layer.
- **Liquid, slider handles:** joins form pointed lateral bulges; the static frame does not establish convincing fluid motion.
- **Neon, sidebar buttons and Save/Cancel:** bright rectangular halos contain dark triangular corner wedges.
- **Retro, editor and Export:** peripheral darkening makes already small text especially difficult to read.
- **README frame:** at 576 × 360, descriptions and effect labels are too small to inspect; the supplied still shows only glass.
- **Corner editing:** “Corners” changes `medium`, while preview panels hard-code radius 16 (`SG/src/studio.cpp:110–111,205–209`).
- **Parameter ranges:** editor maxima are heuristically capped using defaults rather than exposing the full declared range (`:219–227`).
- **Draft safety:** edits mutate the process-wide library (`:28–38`), creating potential interference between studio instances; isolation is untested.
- **Export:** fixed filenames overwrite through ordinary stream opening; no application-level destination or completion UI is implemented (`:64–73`).
- **Repository rule violation:** export uses `std::filesystem` and `std::ofstream` directly instead of `mcpplibs.os` (`:64–71`).
- **Potential pointer mismatch, provisional:** CRT warps rendered positions (`FX/crt.slang:16–22`), while hit testing uses node-local rectangles (`modules/ui/view/src/frame.cpp:688–699`); no CRT inverse mapping is demonstrated.
- **Documentation drift:** README line 17 says neon has three effects; its pack declares four. Dev story line 38 describes numeric fields, while the current editor uses sliders.

```json
{"overall":45,"mean":64.8,"dimensions":{"pack_glass":70,"pack_ink":67,"pack_liquid":65,"pack_neon":61,"pack_retro":45,"layout_composition":68,"typography":69,"contrast_accessibility":50,"effects_quality":64,"workshop_usability":52,"semantic_sameness":82,"framework_showcase":80,"code_tests_docs":70}}
```