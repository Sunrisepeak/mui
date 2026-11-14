Overall: 50/100  
Mean: 68.9/100

Evaluated workspace HEAD `f59bfec` on 2026-10-31. The running binary’s revision was not exposed. No repository files were edited.

Evidence labels: **used** = live MCP operation and captures; **measured** = computed results; **viewed** = inspected source/images; **inferred/P** = provisional. Rival comparisons use primary documentation, not firsthand operation. For ink, neon and retro, comparisons concern craft and usability rather than matching aesthetics.

**Use log**

| Task | What I did | Expected | Actual | Result |
|---|---|---|---|---|
| Discover and inspect | `ui_apps`, `ui_snapshot`, `ui_capture` | Inspect pixels and controls | Found `org.openmui.styles`; readable 1440×900 capture and typed controls | Pass |
| Switch every pack | Coordinate clicks, then `styles.show`; captures after `ui_wait(settled)` | Five distinct, usable appearances | All five appeared and settled | Pass |
| Inspect transition | `ui_clock(pause/step)`, switch glass→neon, capture intermediate and settled frames | Cross-fade between appearances | Intermediate blend and clean final frame observed | Pass |
| Settings in **each** pack | Click both switches and checkbox; drag Volume and press arrow key; replace name; open Region and select with keys | Values and picture agree | All controls responded, including retro’s warped positions | Pass |
| Save and Cancel in each pack | Save, change Notifications, Cancel; capture | Save feedback; restore saved state | Settings restored; “Changes discarded.” displayed | Pass |
| Keyboard operation | Focus name, Tab, Space | Focus moves and switch toggles | Notifications toggled with visible focus outline | Pass |
| Workshop editing | Drag corners, glass blur and neon intensity; use Undo/Redo/Reset; scroll neon editor | Immediate preview and recoverable edits | Worked without the previous disconnect; history boundaries remain wrong | Partial |
| Agent edit and validation | `ui_act` Accent=`#ff0066`; type invalid `pink`; `styles.set` invalid parameter | Picture follows valid values; rejection preserves state | Accent changed pixels; colour error explained; unknown parameter corrupted subsequent editing | Fail |
| Accessibility and comparison | Toggle Reduce transparency; inspect appearance controls | Opaque fallback; light/dark and comparison previews | Glass remained transparent; paired appearances and simultaneous comparison absent | Fail |
| Semantic equivalence | Compare five settled preview snapshots and five expanded Region subtrees | Same content, values and actions | Identical 42-node previews and five-node Region subtrees, excluding bounds | Pass |
| Export/import | Export dry run, then invocation without an answer; inspect available controls | Correct authorization; complete authoring cycle | Export required confirmation and reported declined; nothing written. Import absent | Partial |

Defects found through use:

- **Transparency fallback:** in glass, enable Reduce transparency. The wallpaper becomes sharply visible through panels instead of being obscured.
- **Merged gestures:** in liquid, drag Corners **16→26**, release, then quickly drag **26→8**. Undo returns directly to **16**, skipping **26**.
- **History loss:** edit glass Accent, switch to neon, return to glass. The edited colour remains, but Undo is disabled.
- **Rejected edit poisons the draft:** invoke `styles.set` with `what="role:region/not_a_parameter"`, then a valid Corners edit. Both fail with the unknown-parameter error. Reset recovers.
- **Range disagreement:** `styles.set(corners,64)` succeeds; the tree then reports slider value **64**, maximum **32**.
- **Colour disconnect:** changing liquid Accent to pink leaves violet material around its slider handles.
- **Incomplete workshop:** no import, precise numeric entry, colour picker, simultaneous comparison, or per-pack light/dark inspection.
- **Persistent labels missing:** populated Display name, Accent and Region fields show values without visible field names.

**Framework and openas feedback**

Stable paths, typed values, action effects and input receipts made the application easy to inspect. Coordinate input identified actual hit targets and provenance, including `agent:codex-evaluator;via=service:input`. Retro’s inverse input mapping worked at the visible controls. Clock stepping and settling were useful for separating motion from final appearance.

Export was correctly classified as external. No confirmation answer was fabricated, and no export was completed. Reversible edits need stronger guarantees: failed validation currently changes hidden draft state. Add transactional application, gesture identifiers for history, and one authoritative parameter range.

Retro’s snapshot bounds describe pre-effect geometry while its pixels are warped. Tools should expose the visual geometry or explicitly identify the coordinate mapping.

**Dimension assessment**

Source shorthand:

- **SG:** [showcase/style-gallery/src/studio.cpp](/home/speak/workspace/mui/showcase/style-gallery/src/studio.cpp)
- **GT:** [showcase/style-gallery/tests/styles_test.cpp](/home/speak/workspace/mui/showcase/style-gallery/tests/styles_test.cpp)
- **FX:** `modules/gfx/render/vector/effects/`
- **A:** Apple Liquid Glass; **M:** Material Theme Builder; **T:** tweakcn.

| Dimension | Score | Evidence and basis | Individual comparisons | To reach 90, highest impact first |
|---|---:|---|---|---|
| `pack_glass` | 70 | **used, viewed:** coherent frost and luminous edges, but broad milky cards and giant discs dominate. FX/liquid_glass.slang:29–67 implements meaningful refraction, dispersion and rim lighting. | **A behind:** weaker material hierarchy. **M behind:** less controlled surface relationships. **T behind:** less informative preview composition. | Establish surface hierarchy; demonstrate optics over detailed content; validate readability across backgrounds. |
| `pack_ink` | 67 | **used, viewed:** irregular outlines convey ink, but grain and resampling soften lettering throughout Settings and the editor. FX/wobble.slang:25–34 distorts the whole layer. | **A behind:** poorer text preservation. **M behind:** weaker small-text clarity. **T behind:** less comfortable editing surface. | Separate text from distortion; reduce grain; replace inherited pale graphics. |
| `pack_liquid` | 65 | **used, viewed:** rounded and clean, but pointed handle bridges carry most of its identity. Pink Accent leaves violet bridges; liquid.style.omui.toml:20 fixes effect RGB independently. | **A behind:** limited fluid behavior. **M behind:** less coherent colour propagation. **T behind:** narrower visual customization. | Bind material colours to tokens; refine joins during dragging; demonstrate richer fluid interaction. |
| `pack_neon` | 68 | **used, viewed:** readable content and working controls; strong cyan glow surrounds inactive navigation and virtually every editor rail. SG:389–415 exposes repeated RGB controls. | **A behind:** weaker emphasis hierarchy. **M behind:** less restrained state treatment. **T behind:** decoration competes with authoring. | Reserve intense glow for meaningful states; differentiate focus; simplify colour editing. |
| `pack_retro` | 65 | **used, viewed:** visible-position clicks work; terminal identity is clear. Grille, dimming and resampling still degrade captions and editor text. FX/crt.slang:20–28. | **A behind:** clarity suffers for appearance. **M behind:** weaker small-text readability. **T behind:** less comfortable sustained authoring. | Provide a readable CRT variant; preserve glyph sharpness; constrain vignette and grille. |
| `layout_composition` | 68 | **used, viewed:** useful three-column structure, but Settings starts below the dashboard and roughly half the preview is wallpaper. SG:275–308,425. | **A behind:** weaker spatial hierarchy. **M behind:** inefficient workspace allocation. **T behind:** insufficient preview/state coverage. | Align panel starts; use lower space for comparison and states; adapt fixed rails. |
| `typography` | 74 | **used, viewed:** sound basic hierarchy; percentages, weekday labels and parameter units help. Persistent field labels and visible chart quantities are missing. SG:248–300,373–414. | **A behind:** effects impair captions. **M behind:** no typography authoring. **T behind:** incomplete editing labels. | Add persistent labels and chart values; expose type controls; keep text outside degrading effects. |
| `contrast_accessibility` | 50 | **used, measured, viewed:** chart contrasts measured **1.66:1 glass, 1.23:1 liquid, 1.68:1 neon**. Reduced transparency visibly fails its opaque fallback requirement. [frame.cpp:540](/home/speak/workspace/mui/modules/ui/view/src/frame.cpp:540). | **A behind:** incomplete accessibility adaptation. **M behind:** no paired accessible schemes. **T behind:** inadequate appearance inspection. | Implement opaque surfaces; raise meaningful graphic contrast; audit post-effect text, focus and both appearances. |
| `effects_quality` | 68 P | **used, viewed; cost inferred:** cross-fade works and settles; shaders produce substantive effects. Ink/retro degrade text. GPU cost and transition memory were not measured. | **A behind:** weaker integration of optics and motion. **M behind:** treatments compromise information clarity. **T behind:** less visually stable editing presentation. | Preserve text; refine liquid joins; measure hardware GPU timing, memory and interrupted transitions. |
| `workshop_usability` | 63 | **used, viewed:** live editing and recovery now work, but rejected edits poison the draft, gestures merge, switching loses history, and ranges disagree. SG:55–69,84–105,383. | **A behind:** weaker feedback consistency. **M behind:** incomplete authoring cycle. **T behind:** substantially narrower editing workflow. | Fix transactional rejection and history first; unify ranges; add precise entry, picker, import and comparison. |
| `semantic_sameness` | 88 | **used, measured, viewed:** all five 42-node preview snapshots matched, including values and action descriptions; expanded Region subtrees also matched. GT:29–36 omits values/effects from its automated comparison. | **A parity in principle:** styling preserves control identity. **M parity in architecture:** content survives theme changes. **T parity in architecture:** themes remain separate from components. Product-wide equivalence is unproven. | Put these fuller comparisons into tests; include relationships, editing states and transformed geometry. |
| `framework_showcase` | 78 P | **used, viewed; scope inferred:** data packs, role/type effects, agent switching and shared semantics are convincing. Scope isolation has source tests, but was not operated here; export/load remained unverified. SG:459–501; GT:198–255. | **A behind:** narrower material demonstration. **M behind:** weaker designer-to-code completion. **T behind:** fewer customization/integration workflows. | Demonstrate scoped editing and export/load; expose accessibility adaptation and measured costs. |
| `code_tests_docs` | 72 | **viewed, used:** delayed parameter application fixes the previous iterator hazard, and editing stayed connected. Tests cover packs/history/scopes, but miss reproduced failures. SG:388–418; GT:171–195. | **A behind:** weaker acceptance evidence. **M behind:** incomplete workflow verification. **T behind:** editor robustness remains unfinished. | Add input regressions for discovered defects; complete the suite; correct documentation guarantees and publish measurements. |

Contrast measurements use live chart captures: mean RGB over 3×3 patches centred at chart-local `(25,100)` inside Monday’s bar and `(55,100)` in the adjacent gap, converted through sRGB relative luminance. These samples are not a complete accessibility audit.

The `mcpp test -p openmui.showcase.style_gallery` attempt was interrupted after approximately one minute in planning. No fresh suite pass or GPU performance result is claimed.

**Benchmark summary**

Apple’s strongest documented qualities are material hierarchy and automatic accessibility adaptation. Its guidance places glass in a distinct functional layer and cautions against overuse. openmui’s large content cards lack that restraint. [Apple materials guidance](https://developer.apple.com/design/human-interface-guidelines/materials).

Material Theme Builder leads in coordinated light/dark colour generation, type customization, colour selection and import/export. [Official documentation](https://github.com/material-foundation/material-theme-builder).

tweakcn leads in the breadth of visual theme customization and polished starting presets. [Official project](https://github.com/jnsahaj/tweakcn).

openmui’s strongest distinction is directly inspectable agreement between semantics, agent actions and pixels. No overall lead over these benchmarks is established. The largest deficits are accessibility and completing a dependable authoring cycle. Figma Variables and Tokens Studio interoperability was not demonstrated.

**Change from the last record**

| Dimension | Previous → current | Reason |
|---|---:|---|
| `pack_glass` | 70 → 70 | Appearance remains essentially unchanged. |
| `pack_ink` | 67 → 67 | Live use confirms grain/text weaknesses. |
| `pack_liquid` | 65 → 65 | Limited liquid identity and colour disconnect remain. |
| `pack_neon` | 68 → 68 | Glow hierarchy remains excessive. |
| `pack_retro` | 62 → 65 | Visible-coordinate input now verified successfully. |
| `layout_composition` | 68 → 68 | Space allocation remains weak. |
| `typography` | 74 → 74 | Existing gains confirmed; labels still incomplete. |
| `contrast_accessibility` | 50 → 50 | Low contrast and broken fallback confirmed live. |
| `effects_quality` | 62 → 68 | Working transitions and stable effect editing now observed. |
| `workshop_usability` | 55 → 63 | Editing survives; concrete history/validation defects remain. |
| `semantic_sameness` | 80 → 88 | Fuller live equivalence measured across all packs and menus. |
| `framework_showcase` | 70 → 78 | Agent switching and value-to-picture behavior demonstrated. |
| `code_tests_docs` | 67 → 72 | Iterator fix inspected and live stability improved. |

Overall remains **50**; mean rises **66.0→68.9**. Much of the increase reflects stronger evidence after the previous interrupted session.

**Highest-priority changes**

1. Make draft validation/application transactional; rejected edits must preserve draft and history.
2. Implement genuinely opaque reduced-transparency surfaces.
3. Correct chart contrast and audit post-effect readability in every pack.
4. Use gesture boundaries for Undo; retain history per pack.
5. Share parameter ranges between API, tree and editor.
6. Add numeric entry, colour picking, import and simultaneous comparison.
7. Bind effect colours to theme tokens.
8. Preserve text sharpness in ink and retro.
9. Automate the reproduced failures and publish hardware effect/transition benchmarks.

**Detailed defects**

In addition to the reproducible use defects above:

- **Unlabeled regions:** Settings and dashboard cards expose role `region` without names; SG:226–235.
- **Opaque-fallback comment contradicts pixels:** frame.cpp:538–541 calls the remaining panel opaque, while glass’s surface remains 10%-alpha.
- **Implementation vocabulary:** editor shows “Tint a”, “Color b/g/r” and “on every region”; SG:333–345,392–414.
- **Restricted parameter coverage:** only numeric keys already present in each pack become controls; other registered parameters cannot be added through the editor; SG:400–415.
- **Chart information:** day quantities are available in the figure’s semantic name but absent visually; SG:278–300.
- **Export destination:** fixed pack filename and default working directory offer no visible destination selection; SG:175–184.
- **Platform abstraction:** direct `std::filesystem` directory operations remain in export; SG:177–178.
- **Documentation drift:** dev-story.md:38 describes numbers in fields, while the interface uses sliders.
- **Unsubstantiated cost description:** dev-story.md:32 says one cross-fade texture; [runtime.cpp:358](/home/speak/workspace/mui/modules/ui/view/src/runtime.cpp:358) preserves a display list. Actual rendering cost needs measurement.
- **README image:** `media/five-packs.png` is 576×360; editor labels are too small for comfortable inspection.

```json
{"overall":50,"mean":68.9,"dimensions":{"pack_glass":70,"pack_ink":67,"pack_liquid":65,"pack_neon":68,"pack_retro":65,"layout_composition":68,"typography":74,"contrast_accessibility":50,"effects_quality":68,"workshop_usability":63,"semantic_sameness":88,"framework_showcase":78,"code_tests_docs":72}}
```