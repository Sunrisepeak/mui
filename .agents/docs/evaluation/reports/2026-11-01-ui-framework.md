Overall: **62/100**  
Mean: **73.0/100**

Evaluated commit `bc711bc`, current source, and the four running examples against the supplied `db6213f` record. **No dimension reaches 90.** Platform coverage remains the lowest dimension.

Evidence: **used** = operated through MCP and checked pixels/tree; **measured** = executed locally; **viewed** = inspected code, captures, or recorded results; **inferred/P** = provisional. Competitor comparisons concern documented capabilities; no matched performance comparison was run.

## Use log

All four applications were discovered with `ui_apps`, inspected with `ui_snapshot`, and viewed with `ui_capture`. Still-frame conclusions below use settled captures.

| Task | What I did | Expected | Actual | Result |
|---|---|---|---|---|
| Notes: editing and submission | `ui_input`: click `(100,85)`, type, Home/End, Ctrl/Shift selection, replacement, Tab, Enter | Selection, focus, and submission work | Added a note; tree matched pixels | Pass |
| Notes: word selection | Ctrl+A, type `alpha beta`, double-click `(50,85)`, replace with `tea` | Replace one word | Value became `tea beta` | Pass |
| Notes: Unicode editing | Type `café 中文🙂`, Backspace, Home, Shift+Right | Delete one grapheme; select correctly | Emoji removed; accented/CJK text preserved | Pass |
| Tally: pointer and focus | Click +1; Tab through Reset to Goal; Right | Count and goal update | Correct actions, focus, status, and progress | Pass |
| Tally: slider | Drag; Home, End, Left; settle and capture | Clamped values and coherent progress | Goal 99, count 2, status “97 to go” | Pass |
| Agent discovery and preview | `ui_verbs`; `ui_act(toggle,dry_run)` | Discover verbs; preview without mutation | Notes deletion metadata found; toggle preview correct | Pass |
| Grouped history | Notes star/filter transaction; capture; undo; redo; settle | One history step restores both changes | Correct filtering and restoration | Pass |
| Failure rollback | Toggle checkbox, then activate nonexistent node | Restore first step | Checkbox restored; failure identified step 2 | Pass |
| Transaction eligibility | Tally +1 transaction, dry-run then real | Consistent eligibility; no unrollbackable mutation | Preview says “would run”; real call refuses; count unchanged | **Fail preview** |
| Read-only transaction | Notes draft `focus`, dry-run then real | Admit supported read step | Preview accepts; execution refuses unknown rollback | **Fail** |
| Irreversible action | Delete-all dry-run, then `ui_act`; `ui_invoke(notes.delete_all,dry_run)` | Require person’s answer | Tool reported declined; notes preserved; answer `delete` disclosed | Pass refusal; acceptance untested |
| Canvas | Space drag `(550,460)→(650,510)`, wheel, find `*tally*`, semantic `fit(all)` | Pan, zoom, discover, restore | Camera and pixels changed coherently | Pass |
| Markdown | Click contents, wheel, Tab/Enter; activate `h10`; settle | Readable contents; target heading at top | Anchor placement repaired; several contents labels still clip | Partial |
| Motion | Tally pause, input, step 0.1 seconds, capture, release, settle | Controllable motion | Progress changed across controlled frames and settled | Pass |
| Native accessibility | D-Bus probe; inspect saved AT-SPI report | Native protocol operation | Sandbox denied socket creation; saved report records **30/30 passing checks** | Viewed evidence only |

**Defects reproduced through use**

- **U1:** In Tally, a transaction containing `plus.activate` previews successfully but execution refuses it.
- **U2:** A Notes transaction containing only draft `focus` has the same disagreement and rejects a documented read operation.
- **U3:** At 980×720, Markdown contents entries including `h1`, `h4`, and `h11` extend beyond the left pane. Reproduce by opening the supplied document and inspecting the contents.
- **U4:** Space’s `ui_snapshot(root:"main/map",limit:1)` returns one node containing **30,905 child IDs**—approximately **897,296 serialized characters**. Pagination does not bound response size.

The AT-SPI report covers button activation, disabled-state updates, slider reading/writing, and Notes text, caret, selection, word boundaries, EditableText, and submission. It is protocol integration evidence, not my own Orca session. Windows/macOS adapter attachment does not establish successful NVDA/VoiceOver workflows.

Local lint reported **0 problems**, but document schemas were skipped because `jsonschema` was unavailable. Selected mcpp tests remained in planning and were stopped without a test result.

## Framework and openas feedback

Stable IDs, typed action descriptions, actor attribution (`agent:codex-evaluator;via=service:input`), refusal, previews, grouped history, and semantic/pixel inspection were useful. Person-style input and semantic actions generally produced coherent state.

Rejecting callbacks without known inverses repairs the previous surviving-mutation defect. Eligibility now needs one shared implementation for preview and execution, with explicit treatment of read operations. Callback authors also need a straightforward inverse-registration recipe.

Verb discovery is now available and worked. Large semantic containers need child pagination or bounded summaries. Accepted confirmation, person-only actions, and grants were not exercised here and receive no use credit.

## Dimension scores

**F = Flutter; Q = Qt Quick/QML; C = Compose Multiplatform.** Narrow advantages below do not imply superiority across the entire dimension. Changes within each row are ordered by impact.

| Dimension | Score | Evidence and basis | Individual comparison | To reach 90 |
|---|---:|---|---|---|
| `architecture` | **80** | **Used, viewed.** Shared projections and [stage plugins](/home/speak/workspace/mui/modules/ui/view/src/stages.cpp:58) are substantive. [Pipeline failure](/home/speak/workspace/mui/modules/ui/view/src/frame.cpp:47) is logged without a failed-frame result. | **F:** Ahead in explicit action policy. **Q:** Behind demonstrated host/lifecycle completeness. **C:** Behind lifecycle integration evidence. | Propagate frame failures; test recovery and teardown; enforce projection invariants under faults. |
| `app_api` | **70 P** | **Used existing apps; viewed; authoring inferred.** [Tally’s screen](/home/speak/workspace/mui/examples/03-counter/src/tally.cpp:8) is concise. Signals and regions exist; complete navigation, restoration, and cancellable application workflows remain unproven. | **F:** Behind complete application recipes. **Q:** Behind integrated authoring workflow. **C:** Behind lifecycle-aware effects. | Validate clean onboarding and a substantial application; integrate restoration/cancellation; document callback inverses. |
| `component_model` | **80** | **Viewed.** [Generated builders](/home/speak/workspace/mui/components/base/src/generated/button_builder.cpp:11), contracts, public services, and theme scopes exist. [Registry discovery](/home/speak/workspace/mui/mcpp/contracts/src/openmui.build.contracts.cpp:13) assumes repository-relative placement. | **F:** Ahead in cross-language contract generation. **Q:** Behind independently consumed extension proof. **C:** Ahead in explicit effect contracts. | Prove independently installed components; specify ownership/version compatibility; complete token coverage. |
| `component_library` | **65** | **Used, viewed.** [Controls](/home/speak/workspace/mui/components/base/README.md:5), virtual lists, overlays, canvas, Markdown, timeline, and windows exist. Breadth remains limited. | **F:** Behind Material component breadth. **Q:** Behind Controls and model/view depth. **C:** Behind Material/adaptive patterns. | Add navigation containers, tables/trees, date/time controls; complete keyboard and accessibility acceptance matrices. |
| `layout_text` | **78** | **Used, viewed.** Unicode editing worked; [HarfBuzz shaping](/home/speak/workspace/mui/modules/content/text/src/native/shape.cpp:72), bidi, fallback, and layout are real. Anchor navigation improved; U3 persists. Real IMEs remain untested. | **F:** Behind editing validation breadth. **Q:** Behind demonstrated platform IME maturity. **C:** Behind mobile text-input validation. | Fix initial wrapping; exercise real CJK IMEs and bidi editing; validate scaling, fallback, and rich-text selection across hosts. |
| `accessibility` | **70 P** | **Used keyboard; viewed native results; AT experience inferred.** [Text runs](/home/speak/workspace/mui/modules/ui/accessibility/src/map.cpp:215) and [selection decoding](/home/speak/workspace/mui/modules/ui/accessibility/src/native/accesskit/adapter.cpp:281) now exist. Saved AT-SPI checks pass. | **F:** Behind delivered accessibility validation. **Q:** Behind native assistive-workflow coverage. **C:** Behind integrated accessibility practice. | Run Orca/NVDA/VoiceOver tasks; cover rich documents, relationships, announcements, scaling, and contrast. |
| `agent_native` | **82** | **Used, viewed.** [openas decisions](/home/speak/workspace/mui/modules/ui/core/src/access.cpp:46), discovery, refusal, undo, and supported rollback work. U1/U2/U4 remain. | **F:** Ahead in effect-aware operation policy. **Q:** Ahead in integrated policy; behind Squish’s automation breadth. **C:** Ahead in actor/effect handling; behind dependable completion guarantees. | Unify transaction eligibility; bound snapshots; exercise accepted answers, grants, barriers, and adversarial sequences. |
| `determinism_testing` | **81** | **Used, viewed; cross-platform guarantees unverified locally.** Clock control works; [headless equivalence tests](/home/speak/workspace/mui/modules/ui/host/headless/tests/headless_test.cpp:58), session digests, pinned pixels, and language traces exist. | **F:** Ahead in explicit cross-platform CPU pixel target. **Q:** Ahead in integrated deterministic reference design. **C:** Ahead in injected-clock/reference-rendering design. | Publish reproducible matrix artifacts; close transaction regressions; establish clause-to-CTS traceability and real-host coverage. |
| `performance` | **67 P** | **Used; viewed published measurements.** [Report](/home/speak/workspace/mui/docs/en/benchmarks/README.md:100): idle p95 0.151 ms/zero allocations; one-label 2.08 ms/~12,544 allocations; all-label 3.79 ms/~48,762. Presentation latency was not measured. | **F:** Behind representative-device evidence; relative speed unknown. **Q:** Behind presentation evidence; speed unknown. **C:** Behind startup/jank evidence; speed unknown. | Gate active allocations; measure startup/RSS/power and presentation; compare matched workloads on representative devices. |
| `platforms_web` | **62 P** | **Viewed; delivery inferred.** Desktop, wasm, WebGPU/WebGL2, and DOM mirror implementations exist. [Browser checks](/home/speak/workspace/mui/tools/ci/browser.sh:36) can skip; WebGPU presentation can fall back offscreen. Mobile delivery is unestablished. | **F:** Behind desktop/mobile/web delivery. **Q:** Behind desktop/mobile/embedded coverage. **C:** Behind documented mobile/desktop delivery. | Require browser provisioning and presentation; deliver mobile hosts; validate device input, lifecycle, IME, and accessibility. |
| `tooling_dx` | **70 P** | **Used MCP; viewed; onboarding inferred.** CLI, inspector, templates, generation, diagnostics, and reload exist. [Inspector polling](/home/speak/workspace/mui/tools/inspector/src/inspector.cpp:76) avoids overlapping rounds. Installed workflows were not used. | **F:** Behind DevTools/reload workflow. **Q:** Behind Creator/QML profiling. **C:** Behind integrated authoring guidance. | Validate installed SDK workflows; demonstrate inspector/reload/error recovery; improve build-planning diagnostics. |
| `code_quality` | **74** | **Viewed, measured partial lint.** Module boundaries and native wrappers are disciplined. [Per-component Services](/home/speak/workspace/mui/modules/ui/view/src/component.cppm:143) uses virtual dispatch; published active allocations contradict broad zero-allocation claims. | **F:** Behind demonstrated boundary hardening. **Q:** Behind ownership and extension maturity. **C:** Behind lifecycle/cancellation guarantees. | Reconcile rules with actual costs; enforce allocation/compiler policies; add concurrency, parser-fuzzing, and teardown validation. |
| `docs_spec` | **70** | **Viewed.** [Normative draft](/home/speak/workspace/mui/spec/SPEC.md:17), registry, schemas, ABI, and CTS are substantial. Benchmark claims were corrected; planned tutorials and complete application/API guides remain absent. | **F:** Behind practical application documentation. **Q:** Behind API/integration reference depth. **C:** Behind lifecycle and application recipes. | Publish complete tutorials/reference; qualify comparative claims; prove clause coverage and independent consumption. |

## Benchmark summary

Flutter’s clearest strengths are its [Material component catalog](https://docs.flutter.dev/ui/widgets/material) and [DevTools frame analysis](https://docs.flutter.dev/tools/devtools/performance). Qt’s are [Quick Controls](https://doc.qt.io/qt-6/qtquickcontrols-index.html), [native accessibility integration](https://doc.qt.io/qt-6/accessible.html), and [Squish automation breadth](https://www.qt.io/quality-assurance/squish/platform-qt-gui-test-automation). Compose combines [accessibility/test semantics](https://developer.android.com/develop/ui/compose/testing/semantics), [lifecycle-aware effects](https://developer.android.com/develop/ui/compose/side-effects), and documented [platform delivery](https://kotlinlang.org/docs/multiplatform/supported-platforms.html).

openmui’s strongest differentiated capabilities are explicit actor/effect policy and deterministic CPU reference rendering. Shared accessibility/test semantics alone are not unique.

Performance superiority is unestablished. Qt’s [approximately 16 ms frame guidance](https://doc.qt.io/qt-6/qtquick-performance.html) is a deadline, while Compose’s [hero benchmarks](https://developer.android.com/develop/ui/compose/performance/herobenchmark) measure application startup and scrolling. Neither is directly comparable to openmui’s offscreen desktop measurements.

## Change from the last record

| Dimension | Previous → current | Reason |
|---|---:|---|
| `architecture` | 80→80 | Failure propagation gap remains. |
| `app_api` | 70→70 | Complete authoring workflow untested. |
| `component_model` | 80→80 | Independent distribution remains unproven. |
| `component_library` | 65→65 | No material breadth increase established. |
| `layout_text` | 76→78 | Anchor placement repaired; partial wrapping improvement. |
| `accessibility` | 67→70 | Text/caret/selection implementation and expanded AT-SPI evidence. |
| `agent_native` | 78→82 | Unknown-inverse callbacks refused; verbs discoverable. Preview/read-step defects remain. |
| `determinism_testing` | 81→81 | No stronger cross-platform execution evidence obtained. |
| `performance` | 67→67 | No new matched measurements. |
| `platforms_web` | 62→62 | Delivery/provisioning gaps remain. |
| `tooling_dx` | 70→70 | Discovery improved; onboarding remains untested. |
| `code_quality` | 73→74 | Safer transaction admission and stronger adapter implementation. |
| `docs_spec` | 68→70 | Benchmark discrepancies corrected; guide gaps remain. |

Overall **62→62**; mean **72.1→73.0**.

## Highest-priority changes

1. Deliver mandatory browser presentation tests and mobile host evidence.
2. Complete real screen-reader workflows on all desktop platforms.
3. Make transaction previews and execution share eligibility rules.
4. Propagate failed-frame outcomes and validate recovery.
5. Bound semantic snapshots, including child lists.
6. Gate active-frame allocations and measure representative devices.
7. Validate one complete application with restoration and cancellation.
8. Expand application components and installed-package workflows.
9. Publish practical tutorials and correct remaining guarantees.

## Specific defects and gaps

- U1/U2: [Transaction preview](/home/speak/workspace/mui/modules/ui/semantic/src/tree.cpp:245) rejects explicit false; [execution admission](/home/speak/workspace/mui/modules/ui/semantic/src/tree.cpp:274) also rejects missing capability, including read actions.
- U3: [Contents sizing](/home/speak/workspace/mui/examples/08-markdown-viewer/src/viewer.cpp:156) still produces clipped initial labels despite `max_width`.
- U4: [Snapshot copying](/home/speak/workspace/mui/modules/ui/semantic/src/tree.cpp:136) retains complete child arrays outside pagination.
- [Frame failure](/home/speak/workspace/mui/modules/ui/view/src/frame.cpp:47) is logged without propagating failure.
- [Button sizing constants](/home/speak/workspace/mui/components/base/src/button.cpp:15) contradict the README’s universal theme-token claim.
- [Python/Lua type generation](/home/speak/workspace/mui/tools/gen/src/emit_lang.cpp:38) falls back to `Any`/`any` for alternatives handled precisely by TypeScript.
- [Accessible word starts](/home/speak/workspace/mui/components/base/src/text_box.cpp:463) stop after 256 graphemes per line; long-line word navigation needs validation.
- [Registry lookup](/home/speak/workspace/mui/mcpp/contracts/src/openmui.build.contracts.cpp:13) depends on monorepo layout.
- Browser absence can pass successfully; WebGPU presentation failure can pass through offscreen coverage.
- Nightly configuration provides ASan/UBSan, without the planned TSan, parser-fuzzing, and coverage gates.

```json
{"overall":62,"mean":73.0,"dimensions":{"architecture":80,"app_api":70,"component_model":80,"component_library":65,"layout_text":78,"accessibility":70,"agent_native":82,"determinism_testing":81,"performance":67,"platforms_web":62,"tooling_dx":70,"code_quality":74,"docs_spec":70}}
```