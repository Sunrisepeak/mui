Overall: **63/100**  
Mean: **73.2/100**

Evaluated checkout `7a6873e` and the four live applications against the supplied `bc711bc` record. **No dimension reaches 90.** Platform delivery remains the lowest dimension.

Evidence: **used** = operated through MCP and checked pixels/tree; **measured** = executed locally; **viewed** = inspected source or recorded results; **inferred/P** = provisional. Competitors were researched through official documentation; no matched performance comparison was run.

## Use log

All four applications were discovered with `ui_apps`, inspected with `ui_snapshot`, and viewed with `ui_capture`. Still-frame judgments used `ui_wait(settled:true)`.

| Task | What I did | Expected | Actual | Result |
|---|---|---|---|---|
| Notes editing | Click `(100,85)`; type `Review 世界`; Left, Shift+Left, replacement | Correct selection and Unicode replacement | Draft became `Review UI界`; pixels matched tree | Pass |
| Notes submission and focus | Tab, Enter, Tab, Space | Submit, advance focus, filter | Third note added; starred filter showed Call Ada | Pass |
| Word/grapheme editing | Type `alpha beta`; double-click `(50,85)`; replace; append accented/CJK/emoji text; Backspace | Replace one word; delete one emoji | `tea beta café 中文` | Pass |
| Tally input | Click +1 twice; drag Goal `(90,194)→(180,194)`; Right; Tab | Update count, slider, status and focus | Count 2, goal 60, “58 to go”; focus wrapped to −1 | Pass |
| Agent preview | `ui_verbs`; toggle and deletion dry runs | Discover effects without mutation | Correct reversible/irreversible outcomes and required answer | Pass |
| Grouped history | Notes filter/draft transaction; undo; inspect | Restore both changes together | Both restored | Pass |
| Failure rollback | Toggle filter, then activate missing node | Restore first step | Explicit step-2 failure; filter restored | Pass |
| Transaction admission | Preview/run Tally +1; preview/run Notes focus | Consistent eligibility | Both reject unknown-inverse callback; both accept read step | Pass |
| Irreversible request | `ui_invoke(notes.delete_all)` without an answer | Require person’s confirmation | Tool reported declined; notes preserved | Pass refusal; acceptance untested |
| Space canvas | Drag, wheel, `ui_find("*tally*")`, semantic `fit(all)` | Pan, zoom, discover, restore | Camera and pixels changed coherently; fit restored overview | Pass |
| Markdown navigation | Click contents; wheel; Tab/Enter; semantic `h12.activate` | Wrapped contents and correct anchors | Contents readable; anchors worked | Pass |
| Markdown wide table | Open “Side by side”; horizontal wheel | Reach every column | Rightmost columns inaccessible; `max_x:0` | **Fail** |
| Snapshot limits | Space root `main/map`, `limit:1`; repeat with `children_limit:5` | Manageable response | Default: 911,332 serialized characters; explicit child limit worked | Partial |
| Motion | Tally pause; click +1; step 0.1 s; capture; release; settle | Inspect intermediate progress | Progress advanced across controlled frames | Pass |
| History after typing | After transaction undo, type unrelated draft; `ui_undo(redo:true)` | Safe handling of intervening editing | Old transaction remained redoable and overwrote new draft | **Fail** |
| Native accessibility | Read supplied `/tmp/omui-eval-atspi.json` | Native protocol evidence | **30/30 recorded checks passed** | Viewed only |

**Defects reproduced through use**

- **U1 — inaccessible table:** In md-view, activate `main/body/toc/h12`. The table extends beyond the right edge; horizontal wheel input does nothing.
- **U2 — excessive default snapshot:** Request Space’s `main/map` with `limit:1`. Its 31,389 child IDs remain included unless `children_limit` is supplied.
- **U3 — stale redo after typing:** Undo the filter/draft transaction, edit the draft through `ui_input`, then redo the transaction. The newly typed draft is replaced without invalidating or warning about the stale redo.

The AT-SPI script builds applications and creates temporary files/private buses, so I used the supplied report under this read-only sandbox. It covers buttons, disabled states, slider values, editable text, caret, selection, word boundaries and submission—not an Orca session. Windows/macOS attachment is not evidence of successful NVDA/VoiceOver workflows.

Local lint **failed with one path-narrowing violation**. Document-schema checks were skipped because `jsonschema` is unavailable. Builds, benchmarks and the CI matrix were not rerun.

## Framework and openas feedback

Stable IDs, typed actions, actor attribution, refusal, transaction rollback and clock control were useful. The previous transaction preview/read-step disagreements are repaired.

Typing exposed a remaining split: text changed without an action-journal entry, and global redo survived intervening edits. Raw input needs coherent policy, audit and history behavior.

There is also an **inferred policy gap**: [input dispatch](/home/speak/workspace/mui/modules/ui/view/src/input.cpp:103) calls component handlers directly, while [text insertion](/home/speak/workspace/mui/components/base/src/text_box.cpp:371) mutates before reaching the semantic action checks. I did not verify this under a restrictive cap or person-only field. Add adversarial tests before claiming input-path policy equivalence.

Accepted confirmation and grant workflows remain untested here; I supplied no answer on the person’s behalf.

## Dimension scores

**F = Flutter; Q = Qt Quick/QML; C = Compose Multiplatform.** Comparisons concern the stated capability, not blanket superiority. Changes within each row are ordered by impact.

| Dimension | Score | Evidence and basis | Individual comparison | To reach 90 |
|---|---:|---|---|---|
| `architecture` | **81** | **Used, viewed.** Shared projections and [frame stages](/home/speak/workspace/mui/modules/ui/view/src/stages.cpp:58) are substantive. [Frame failures now propagate](/home/speak/workspace/mui/modules/ui/view/src/frame.cpp:47), but desktop handling remains incomplete. | **F:** Behind application-lifecycle proof. **Q:** Behind host recovery completeness. **C:** Behind lifecycle/effect integration. | Define host recovery; test failures at every stage; enforce projection consistency under mutation and faults. |
| `app_api` | **70 P** | **Used applications; viewed; authoring inferred.** [Tally](/home/speak/workspace/mui/examples/03-counter/src/tally.cpp:8) is concise. Signals, regions and tasks exist; navigation, restoration and scoped cancellation lack a demonstrated complete application. | **F:** Behind complete application recipes. **Q:** Behind integrated authoring workflow. **C:** Behind composition-scoped async work. | Validate clean onboarding and a substantial application; provide navigation/restoration; integrate cancellation and history recipes. |
| `component_model` | **80** | **Viewed.** [Generated builders](/home/speak/workspace/mui/components/base/src/generated/button_builder.cpp:11), contracts and public services are real. Python/Lua alternatives are now typed. Registry discovery still assumes repository layout. | **F:** Ahead in explicit cross-language contracts. **Q:** Behind independently distributed extension proof. **C:** Ahead in effect metadata; behind lifecycle ergonomics. | Prove installed third-party components; define compatibility/ownership; validate complete theming and generated diagnostics. |
| `component_library` | **65** | **Used, viewed.** [Shipped controls](/home/speak/workspace/mui/components/base/README.md:5), virtual lists, overlays, Markdown, canvas, timeline and windows exist. Application breadth remains limited. | **F:** Behind Material breadth. **Q:** Behind Controls and model/view depth. **C:** Behind Material/adaptive patterns. | Add navigation, tables/trees and date/time controls; complete behavior, keyboard and accessibility matrices. |
| `layout_text` | **79** | **Used, viewed.** Unicode editing worked; [HarfBuzz shaping](/home/speak/workspace/mui/modules/content/text/src/native/shape.cpp:72), bidi and fallback are implemented. Contents wrapping improved; U1 blocks document content. Real IMEs untested. | **F:** Behind editing-validation breadth. **Q:** Behind platform IME maturity evidence. **C:** Behind mobile input validation. | Repair table overflow; test real CJK IMEs and bidi editing; validate scaling, fallback and rich selection across hosts. |
| `accessibility` | **70 P** | **Used keyboard; viewed native results; AT experience inferred.** [Mapping and text runs](/home/speak/workspace/mui/modules/ui/accessibility/src/map.cpp:215) are substantial. Saved AT-SPI checks pass; long text runs are now split. | **F:** Behind delivered accessibility validation. **Q:** Behind native assistive-workflow coverage. **C:** Behind integrated accessibility practice. | Run Orca/NVDA/VoiceOver tasks; cover documents, announcements, relationships, scaling and contrast. |
| `agent_native` | **82 P** | **Used, viewed; input-policy concern inferred.** [openas decisions](/home/speak/workspace/mui/modules/ui/core/src/access.cpp:46), refusal and rollback work. Transaction admission is repaired; U2/U3 and raw-handler policy questions remain. | **F:** Ahead in integrated actor/effect policy. **Q:** Ahead in built-in policy; behind Squish automation breadth. **C:** Ahead in explicit grants/effects; behind dependable completion evidence. | Unify input authorization/history; default-bound snapshots; exercise accepted answers, grants, barriers and adversarial sequences. |
| `determinism_testing` | **81** | **Used, viewed.** Clock stepping works; [headless equivalence tests](/home/speak/workspace/mui/modules/ui/host/headless/tests/headless_test.cpp:58), replay digests, pixel pins and language traces exist. Cross-platform runs not independently repeated. | **F:** Ahead in explicit cross-platform CPU pixel target. **Q:** Ahead in integrated deterministic reference design. **C:** Ahead in injected-clock/reference-rendering design. | Publish reproducible matrix artifacts; add mixed-input history regressions; establish clause-to-CTS coverage and real-host equivalence. |
| `performance` | **67 P** | **Used; viewed recorded measurements.** [Report](/home/speak/workspace/mui/docs/en/benchmarks/README.md:100): idle p95 0.151 ms/zero allocations; one-label 2.08 ms/~12,544 allocations; all-label 3.79 ms/~48,762. GPU results exclude presentation. | **F:** Behind profiling/device evidence; speed unknown. **Q:** Behind presentation evidence; speed unknown. **C:** Behind startup/jank evidence; speed unknown. | Gate active allocations; fail failed GPU measurements; measure startup, RSS, power and presentation on matched representative devices. |
| `platforms_web` | **63 P** | **Viewed; delivery inferred.** Desktop/wasm/WebGPU/WebGL2 implementations exist. [CI now requires a browser](/home/speak/workspace/mui/.github/workflows/ci.yml:81); WebGPU presentation may still fall back offscreen. Mobile delivery unestablished. | **F:** Behind desktop/mobile/web delivery. **Q:** Behind desktop/mobile/embedded coverage. **C:** Behind mobile/desktop delivery. | Deliver mobile hosts; require presentation evidence; validate lifecycle, touch, IME and accessibility on devices. |
| `tooling_dx` | **70 P** | **Used MCP; viewed; onboarding inferred.** CLI, [Inspector](/home/speak/workspace/mui/tools/inspector/src/inspector.cpp:76), generation, reload and templates exist. Installed authoring/debugging workflows were not exercised. | **F:** Behind DevTools/reload workflow. **Q:** Behind Creator/QML profiling. **C:** Behind integrated authoring guidance. | Validate installed SDK workflows; demonstrate reload/error recovery; improve build diagnostics and large-tree inspection. |
| `code_quality` | **74** | **Viewed, measured lint.** Module/native boundaries are disciplined. [Services](/home/speak/workspace/mui/modules/ui/view/src/component.cppm:143) uses per-component virtual calls; active allocations conflict with broad project rules. Lint currently fails. | **F:** Behind demonstrated correctness hardening. **Q:** Behind extension/ownership maturity. **C:** Behind async lifetime guarantees. | Fix lint; reconcile performance rules with implementation; enforce compiler policies; add concurrency/fuzz/teardown validation. |
| `docs_spec` | **70** | **Viewed.** [Normative draft](/home/speak/workspace/mui/spec/SPEC.md:17), registry, ABI, schemas and CTS are substantial. Practical guides/reference remain sparse; comparative claims exceed demonstrated coverage. | **F:** Behind tutorial completeness. **Q:** Behind API/integration reference depth. **C:** Behind lifecycle/application recipes. | Publish complete guides/reference; qualify comparisons; prove clause coverage and independent consumption. |

## Benchmark summary

Flutter’s strongest demonstrated advantages are its [Material catalog](https://docs.flutter.dev/ui/widgets/material), [DevTools](https://docs.flutter.dev/tools/devtools/performance) and [accessibility testing guidance](https://docs.flutter.dev/ui/accessibility/accessibility-testing).

Qt’s are [Quick Controls](https://doc.qt.io/qt-6/qtquickcontrols-index.html), native integration and [Squish’s cross-device automation](https://www.qt.io/quality-assurance/squish/platform-qt-gui-test-automation).

Compose’s are [shared accessibility/test semantics](https://developer.android.com/develop/ui/compose/testing/semantics), [lifecycle-aware effects](https://developer.android.com/develop/ui/compose/side-effects) and documented [platform delivery](https://kotlinlang.org/docs/multiplatform/supported-platforms.html).

openmui leads narrowly in integrated effect-aware agent operation and deterministic reference design. Its largest deficits are platform delivery, component breadth and production workflow validation. Shared accessibility/test semantics alone are not unique.

Performance superiority is unestablished: Qt’s [approximately 16 ms frame guidance](https://doc.qt.io/qt-6/qtquick-performance.html) is a deadline; Compose’s [hero benchmarks](https://developer.android.com/develop/ui/compose/performance/herobenchmark) measure startup and scrolling. Neither directly compares with these offscreen desktop measurements.

## Change from the last record

| Dimension | Previous → current | Reason |
|---|---:|---|
| `architecture` | 80→81 | Frame failure now returned; host recovery remains incomplete. |
| `app_api` | 70→70 | Complete authoring/lifecycle workflow still untested. |
| `component_model` | 80→80 | Better generated alternatives; distribution gap remains. |
| `component_library` | 65→65 | No material breadth increase established. |
| `layout_text` | 78→79 | Contents wrapping repaired; inaccessible table discovered. |
| `accessibility` | 70→70 | Long-run mapping improved; no new assistive-user evidence. |
| `agent_native` | 82→82 | Transaction fixes offset by stale redo and input-path concerns. |
| `determinism_testing` | 81→81 | No stronger execution evidence obtained. |
| `performance` | 67→67 | Same measurement limitations. |
| `platforms_web` | 62→63 | Browser checks now mandatory in CI. |
| `tooling_dx` | 70→70 | Installed workflows remain untested. |
| `code_quality` | 74→74 | Local improvements; allocation/concurrency gaps and lint failure remain. |
| `docs_spec` | 70→70 | Core documentation gaps unchanged. |

Overall **62→63**; mean **73.0→73.2**.

## Highest-priority changes

1. Unify raw-input authorization, auditing and semantic history.
2. Invalidate or reconcile redo after intervening text edits.
3. Deliver mobile hosts and mandatory browser presentation evidence.
4. Validate complete Orca/NVDA/VoiceOver workflows.
5. Make wide Markdown tables horizontally accessible.
6. Bound snapshot children by default.
7. Gate active allocations and reject failed GPU benchmark iterations.
8. Validate application restoration, cancellation and installed SDK workflows.
9. Publish complete developer guides and clause coverage.

## Specific defects and gaps

- U1: [Markdown table style](/home/speak/workspace/mui/components/markdown/src/parts.cpp:544) clips horizontal overflow and caps width without providing scrolling.
- U2: [Snapshot child truncation](/home/speak/workspace/mui/modules/ui/semantic/src/tree.cpp:146) is opt-in; node pagination alone leaves enormous responses.
- U3: [Text editing](/home/speak/workspace/mui/components/base/src/text_box.cpp:229) updates state outside global action history; stale transaction redo overwrote subsequent typing.
- **Inferred:** direct component event mutation needs restrictive-cap/person-only tests; semantic checks alone do not establish input equivalence.
- [Desktop frame handling](/home/speak/workspace/mui/modules/ui/host/sdl3/src/desktop.cpp:450) does not inspect the new failure field.
- [GPU benchmark drawing](/home/speak/workspace/mui/tests/bench/src/scenes.cpp:139) prints rendering/wait failures but continues recording timings.
- [Registry discovery](/home/speak/workspace/mui/mcpp/contracts/src/openmui.build.contracts.cpp:13) depends on monorepo-relative placement.
- [Roster filename conversion](/home/speak/workspace/mui/showcase/fighting/src/roster.cpp:120) fails the Windows path-narrowing lint rule.
- Nightly configuration covers ASan/UBSan, without the planned TSan, parser-fuzzing and coverage gates.

```json
{"overall":63,"mean":73.2,"dimensions":{"architecture":81,"app_api":70,"component_model":80,"component_library":65,"layout_text":79,"accessibility":70,"agent_native":82,"determinism_testing":81,"performance":67,"platforms_web":63,"tooling_dx":70,"code_quality":74,"docs_spec":70}}
```