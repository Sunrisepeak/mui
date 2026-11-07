Overall: **45/100**  
Mean: **69.5/100**

Evaluated `ece4041` and inspected working-tree files against the previous `126cb48` report. **No dimension reaches 90.** Native accessibility remains the overall gate.

Evidence: **used** = operated through MCP and checked pixels/tree; **measured** = locally executed check; **viewed** = inspected source/document; **P** = provisional. Competitor comparisons are documentation-based judgments, not matched benchmark runs.

Local lint reported **0 problems**, but skipped document schemas because `jsonschema` is unavailable. `mcpp test -p openmui.core` stopped before testing because the read-only environment prevented writing module-cache metadata. Neither establishes passing CI.

### Use log

All four applications were discovered with `ui_apps`, inspected with `ui_snapshot`, and viewed with `ui_capture`.

| Task | What I did | Expected | Actual | Result |
|---|---|---|---|---|
| Notes: write and submit | Clicked `(115,85)`; typed Latin/CJK; Shift+Left, replacement text, Tab, Enter | Correct editing and submission | Added “Review café 中!”; pixels and tree agreed | Pass |
| Notes: selection and focus | `double_click`, replacement text, Ctrl+A, Backspace; Tab twice, Space | Word selection, clearing, ordered focus | Selection visible; draft cleared; filter toggled | Pass |
| Tally: controls | Clicked +1; Tab through Reset to slider; Right | Count/progress update; goal increments | Count 1, goal 11, “10 to go”; coherent pixels | Pass |
| Tally: disabled control | Clicked −1 at count 0 | No decrement | Count stayed 0; no action journaled | Pass |
| Agent actions | Notes `ui_act(toggle, dry_run)`, toggle, `ui_undo` | Preview unchanged; reversible action undone | Correct preview and restoration | Pass |
| Supported transaction | Two checkbox toggles; grouped undo; toggle followed by missing node | One history entry; rollback on failure | Checkboxes restored correctly | Pass |
| Callback transaction | Tally +1 followed by missing node; also dry run | Failure without surviving mutation | Count **1→2**, despite “nothing of it stays”; preview contradicted itself | **Fail** |
| Irreversible action | Delete dry runs through `ui_act`/`ui_invoke`; requested activation without supplying an answer | Confirmation before deletion | Tool returned “user declined”; notes remained | Pass refusal; approval untested |
| Canvas | Dragged `(560,420)→(660,470)`; wheel; semantic find; `fit(target:"all")` | Pan, zoom, query, restore framing | Visible camera changes; file semantics matched map | Pass |
| Markdown | Clicked contents sliver; Tab/Enter; wheel; activated contents entry | Readable contents and navigation | Navigation worked, but contents labels were clipped away | **Fail** |
| Motion and settlement | `ui_clock(pause/step/release)`, switch/scroll actions, `ui_wait(settled:true)`, captures | Settlement reflects visible motion | Wait succeeded while subsequent clock steps visibly changed pixels | **Fail** |

**Defects reproduced during use**

- **D1—False atomicity:** Tally transaction `[plus.activate, missing.activate]` leaves the increment behind while reporting rollback. I restored it with −1.
- **D2—Contradictory preview:** The same dry run says “it would run,” while its second step says “it would fail.”
- **D3—False settlement:** Pause Markdown, request a smooth scroll, then wait for settlement. It reports success; stepping 0.08 seconds visibly changes the document. Switch motion likewise lacks `animating` in its snapshot.
- **D4—Clipped contents:** At the supplied 980×720 viewport, Markdown’s contents pane is approximately **29 pixels** wide; its semantic buttons extend hundreds of pixels beyond it.

Real IMEs, assistive technologies, mobile devices, browser deployment, and the accepted-confirmation path were not used.

### Framework and openas feedback

Stable paths, action effects, input actor attribution (`agent:codex-evaluator;via=service:input`), previews, and grouped checkbox undo were useful. Human-style input and semantic actions reached coherent application state in ordinary cases.

The critical weaknesses are **truthfulness of transaction results** and **completeness of motion state**. An agent cannot safely plan from assurances contradicted by pixels or surviving state. Require rollback eligibility before mutation, preserve rollback details through MCP, and include component-owned animation channels in settlement.

Verb discovery also needs improvement: MCP’s illustrative `notes.new` returned not-found; the available tools lack `ui_describe`, and protocol `describe` lists operations without application verbs ([protocol.cpp:336](/home/speak/workspace/mui/modules/ui/agent/src/protocol.cpp:336)).

### Dimension scores

**F = Flutter; Q = Qt Quick/QML; C = Compose Multiplatform.** “Ahead” is restricted to the named capability.

| Dimension | Score | Evidence and basis | Individual comparisons | To reach 90, highest impact first |
|---|---:|---|---|---|
| `architecture` | **78** | **Used, viewed.** Shared runtime and stage plugins are substantive. Semantic projection misses component motion; frame pipeline errors are logged while normal output is returned ([frame.cpp:47](/home/speak/workspace/mui/modules/ui/view/src/frame.cpp:47)). | **F:** Ahead in explicit agent effects; behind in demonstrated failure handling. **Q:** Ahead in unified agent policy; behind in host completeness. **C:** Behind in lifecycle and semantic synchronization evidence. | Propagate failed-frame outcomes; make settlement complete; test projection invariants and plugin failures. |
| `app_api` | **70 P** | **Used, viewed; authoring untested.** [Tally’s screen](/home/speak/workspace/mui/examples/03-counter/src/tally.cpp:8) is concise and reactive. Ordinary callbacks need additional undo plumbing; complete navigation/restoration/async workflows remain unproven. Authoring/onboarding evidence cap applies. | **F:** Behind complete application workflows. **Q:** Behind integrated QML authoring. **C:** Behind navigation and lifecycle integration. | Build a complete application with navigation, persistence, cancellation and errors; make reversible callback authoring reliable; validate clean onboarding. |
| `component_model` | **80** | **Viewed.** Typed [generated builders](/home/speak/workspace/mui/components/base/src/generated/button_builder.cpp:11), CRTP dispatch, public runtime services, and scoped themes form a credible model. External package installation remains unverified. | **F:** Ahead in shared contract generation; behind extension proof. **Q:** Behind independently consumable component tooling. **C:** Ahead in explicit action metadata; behind authoring ecosystem depth. | Validate independently installed components; document ownership/lifecycle; enforce compatibility and theme-token coverage. |
| `component_library` | **65** | **Used, viewed.** Inputs, overlays, virtual lists, Markdown, canvas, timeline and windows exist. The [base inventory](/home/speak/workspace/mui/components/base/README.md:5) remains much narrower than complete application catalogs; D4 exposes integration polish gaps. | **F:** Behind Material/Cupertino breadth. **Q:** Behind Controls and model/view depth. **C:** Behind Material/adaptive application patterns. | Add navigation containers, tables/trees and date/time controls; complete keyboard, editing and accessibility acceptance matrices. |
| `layout_text` | **73** | **Used, viewed.** Multilingual typing and selection worked. [HarfBuzz shaping](/home/speak/workspace/mui/modules/content/text/src/native/shape.cpp:72), SheenBidi, fallback and editing are substantive. The supplied Markdown screen fails basic width allocation. | **F:** Behind end-to-end editing validation. **Q:** Behind platform IME/text maturity. **C:** Behind mobile editing and adaptive-layout evidence. | Fix contents sizing/wrapping; run real CJK IMEs and mixed-direction editing; test scaling, fallback and long labels. |
| `accessibility` | **45 P** | **Used keyboard; viewed adapters.** Focus/Tab worked. [ARIA generation](/home/speak/workspace/mui/modules/ui/host/web/src/mirror.cpp:57) exists; no native UIA, NSAccessibility or AT-SPI adapter or real AT acceptance evidence was found. | **F:** Behind delivered accessibility/testing support. **Q:** Behind native platform adapters. **C:** Behind implemented assistive-technology integration. | Deliver native adapters; complete NVDA/VoiceOver/Orca tasks; verify announcements, text navigation, scaling and contrast. |
| `agent_native` | **74** | **Used, viewed.** [Policy calls `openas::decide`](/home/speak/workspace/mui/modules/ui/core/src/access.cpp:46); effects, confirmation refusal and basic undo worked. D1–D3 undermine transactional and observation guarantees. | **F:** Ahead in built-in actor/effect policy; behind trustworthy completion evidence. **Q:** Ahead in framework policy integration; behind Squish’s validation breadth. **C:** Ahead in effect-aware agents; behind dependable automation outcomes. | Reject nonrollbackable transaction steps before mutation; fix preview/error aggregation; expose verbs and adversarial acceptance results. |
| `determinism_testing` | **78** | **Used, viewed.** Injected time and [cross-level semantic tests](/home/speak/workspace/mui/modules/ui/host/headless/tests/headless_test.cpp:58) are valuable; pinned pixels and replay exist. Settlement and rollback defects escape current coverage; cross-platform results were not reproduced. | **F:** Ahead in explicit CPU byte-pin design; behind integration reliability evidence. **Q:** Ahead in integrated reference rendering; behind established test coverage. **C:** Ahead in reference-clock control; behind device validation. | Add adversarial transaction and settlement tests; publish cross-platform artifacts; map normative clauses to executable CTS. |
| `performance` | **67 P** | **Used, viewed numbers.** Small examples responded coherently, without measured presentation latency. Published [results](/home/speak/workspace/mui/docs/en/benchmarks/README.md:100) report one-label p95 **2.08 ms/12,544 allocations**, all-label **3.79 ms/48,762 allocations**; these were not reproduced. | **F:** Behind representative-device evidence; speed ranking unknown. **Q:** Behind deployment/presentation evidence; speed ranking unknown. **C:** Behind startup/jank evidence; speed ranking unknown. | Gate active-frame allocations; measure matched workloads, startup/RSS/power and presentation latency; publish integrated/mobile GPU results. |
| `platforms_web` | **62 P** | **Viewed.** Desktop and wasm/WebGPU/WebGL implementations exist. [Browser checks](/home/speak/workspace/mui/tools/ci/browser.sh:36) can skip successfully; WebGPU can fall back to offscreen. Native mobile delivery was not established. | **F:** Behind demonstrated desktop/mobile/web delivery. **Q:** Behind desktop/mobile/embedded integration. **C:** Behind demonstrated Android/iOS deployment. | Provision mandatory browsers; require actual presentation separately from offscreen checks; deliver mobile hosts and real-device tests. |
| `tooling_dx` | **70 P** | **Used MCP; viewed developer tools.** Inspector, CLI, templates, diagnostics and reload implementations exist. Clean authoring/install workflows were not used; MCP discovery and truthful diagnostics need work. Onboarding cap applies. | **F:** Behind DevTools/reload workflow. **Q:** Behind Creator/QML profiling. **C:** Behind IDE preview and inspection integration. | Validate installed-SDK workflows; fix D1/D2 diagnostics; add verb discovery and reproducible installation guidance. |
| `code_quality` | **73** | **Viewed; measured partial lint.** Module boundaries and native isolation are disciplined. [Style comparison](/home/speak/workspace/mui/modules/content/layout/src/engine.cpp:15) now avoids temporary vectors; watcher storage is reused. Virtual stage dispatch and active allocations still conflict with stated rules. | **F:** Behind hardening evidence. **Q:** Behind failure/lifetime validation evidence. **C:** Behind concurrency/lifecycle validation evidence. | Resolve policy/implementation discrepancies; enforce allocation and compiler policies; expand lifecycle/concurrency testing. |
| `docs_spec` | **68** | **Viewed.** Normative specification, registry, generated ABI/stubs and language CTS are real. English onboarding/reference coverage is sparse; transaction claims contradict use; [competitor claims](/home/speak/workspace/mui/docs/en/features.md:13) understate existing semantics-based testing. | **F:** Behind practical developer documentation. **Q:** Behind API/integration reference coverage. **C:** Behind application recipes and lifecycle guidance. | Correct claims first; publish complete guides/reference; establish clause-to-test traceability and independent implementation evidence. |

### Benchmark summary

Flutter’s strongest advantages here are its [component catalog](https://docs.flutter.dev/ui/widgets/material), [accessibility checks](https://docs.flutter.dev/ui/accessibility/accessibility-testing), and integrated application workflow.

Qt’s strengths are [Quick Controls](https://doc.qt.io/qt-6/qtquickcontrols-guidelines.html), [native accessibility](https://doc.qt.io/qt-6/accessible.html), and [Squish automation](https://www.qt.io/quality-assurance/squish/platform-qt-gui-test-automation).

Compose’s strengths are shared [accessibility/test semantics](https://developer.android.com/develop/ui/compose/testing/semantics), [navigation integration](https://developer.android.com/guide/navigation/testing/compose), and broader [platform delivery](https://kotlinlang.org/docs/multiplatform/supported-platforms.html).

openmui’s clearest differentiated strengths are effect-aware agent operations and deterministic reference rendering. Its largest deficits are native accessibility, application completeness, platform proof, and reliable transactional/settlement guarantees. No comparative speed advantage is established.

### Change from the previous record

| Dimension | Previous → current | Reason |
|---|---:|---|
| `architecture` | 81→78 | Motion/semantic synchronization failure reproduced. |
| `app_api` | 76→70 | Authoring evidence cap; callback undo burden. |
| `component_model` | 80→80 | Strong contracts; distribution proof still missing. |
| `component_library` | 65→65 | Useful scope, unchanged breadth deficit. |
| `layout_text` | 77→73 | Real screen exposes severe clipping. |
| `accessibility` | 45→45 | Native delivery still unestablished. |
| `agent_native` | 84→74 | Rollback concern becomes a reproduced false guarantee. |
| `determinism_testing` | 82→78 | False settlement and missing adversarial coverage. |
| `performance` | 67→67 | No new independently reproduced benchmark evidence. |
| `platforms_web` | 62→62 | Browser/mobile evidence gaps persist. |
| `tooling_dx` | 74→70 | Onboarding cap and misleading transaction diagnostics. |
| `code_quality` | 71→73 | Allocation fixes and stricter budget invocation. |
| `docs_spec` | 72→68 | Published guarantees contradicted by use. |

Overall stays **45**; mean falls **72.0→69.5**. These are evidence-driven reassessments, not all code regressions.

### Highest-priority changes

1. Implement native accessibility adapters and real AT acceptance tasks.
2. Enforce transaction rollback eligibility before mutation.
3. Report retained changes and rollback failures truthfully through MCP.
4. Make settlement include every component-owned animation channel.
5. Propagate pipeline failures to hosts and callers.
6. Fix Markdown contents sizing; add long-label viewport regressions.
7. Gate active-frame allocations and publish representative-device benchmarks.
8. Require real-browser presentation and establish mobile delivery.
9. Validate external SDK/component installation and complete developer guides.

### Specific defects and gaps

- D1: [history.cpp:313](/home/speak/workspace/mui/modules/ui/view/src/history.cpp:313) retains inverse-less steps; [server.cpp:396](/home/speak/workspace/mui/tools/mcp/src/server.cpp:396) nevertheless appends “nothing of it stays.”
- D2: [tree.cpp:249](/home/speak/workspace/mui/modules/ui/semantic/src/tree.cpp:249) aggregates only outcomes present in step data; a missing-node error can leave overall preview as `run`.
- D3: [settlement checking](/home/speak/workspace/mui/modules/ui/semantic/src/tree.cpp:293) reads semantic flags; [component motion](/home/speak/workspace/mui/components/base/src/parts.cpp:46) requests frames without publishing animation state.
- D4: [viewer.cpp:148](/home/speak/workspace/mui/examples/08-markdown-viewer/src/viewer.cpp:148) requests width 240, but the running pane shrinks to approximately 29; labels remain oversized.
- [button.cpp:15](/home/speak/workspace/mui/components/base/src/button.cpp:15): local size constants remain despite the README’s universal theme-token claim.
- [contracts plugin:13](/home/speak/workspace/mui/mcpp/contracts/src/openmui.build.contracts.cpp:13): default registry discovery assumes monorepo-relative placement.
- [SDL README:15](/home/speak/workspace/mui/modules/ui/host/sdl3/README.md:15): obsolete “GPU replaces it at M4” wording conflicts with implemented GPU documentation.
- Nightly configuration supplies ASan/UBSan, but does not establish the planned fuzzing, TSan or coverage gates.
- CI’s `--all-budgets` improvement is real; it does not supply missing active-frame allocation budgets or matched competitor measurements.

```json
{"overall":45,"mean":69.5,"dimensions":{"architecture":78,"app_api":70,"component_model":80,"component_library":65,"layout_text":73,"accessibility":45,"agent_native":74,"determinism_testing":78,"performance":67,"platforms_web":62,"tooling_dx":70,"code_quality":73,"docs_spec":68}}
```