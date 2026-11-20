Overall: **62/100**  
Mean: **72.1/100**

Evaluated `db6213f` and current files against the supplied `ece4041` record. **No dimension reaches 90.** Native accessibility has advanced substantially; platform coverage is now the lowest dimension.

Evidence labels: **used** = operated through MCP and checked pixels/tree; **measured** = executed locally; **viewed** = inspected source or supplied results; **inferred/P** = provisional. Competitor comparisons are judgments from official documentation, not matched benchmark runs.

## Use log

All four applications were discovered, captured, and inspected with `ui_apps`, `ui_capture`, and `ui_snapshot`.

| Task | Tool calls, briefly | Expected | Actual | Result |
|---|---|---|---|---|
| Notes: editing and submission | `ui_input`: click `(100,85)`, type, Home/End, Shift+Right, replacement, Tab, Enter | Correct selection, focus, submission | Added “review openmui”; pixels/tree agreed | Pass |
| Notes: multilingual editing | Typed “café 中文 alpha”; selection and Backspace | Preserve Unicode and selection boundaries | Correct resulting text | Pass |
| Notes: word selection | Ctrl+A, type “alpha beta”, double-click `(48,85)`, replace with “tea” | Replace selected word | Visible selection; value became “tea beta” | Pass |
| Tally: pointer and keyboard | Click +1; Tab through Reset to slider; Right; drag slider | Count, goal, progress update coherently | Count 1; goal 56; matching status and pixels | Pass |
| Reversible agent operations | `ui_act(toggle,dry_run)`; two-step star/filter transaction; capture; `ui_undo` | Preview unchanged; grouped restoration | Correct preview, filtering and undo | Pass |
| Supported rollback | Notes checkbox toggle followed by missing node | Restore checkbox | Checkbox remained false; rollback reported accurately | Pass |
| Callback rollback | Tally transaction `[plus.activate, missing.activate]`, including dry run | No surviving increment | Count **1→2**; failure now explicitly names retained increment; restored with −1 | **Fail atomicity** |
| Irreversible operation | `ui_act(delete-all)`; later `ui_invoke(notes.delete_all,dry_run)` | Require person’s answer | Confirmation returned “user declined”; no deletion; preview named answer `"delete"` | Pass refusal; approval untested |
| Canvas | Drag `(550,450)→(620,480)`; wheel; `ui_find("*tally*")`; `fit(all)` | Pan, zoom, discover files, restore view | Camera and framing changed; matching file nodes found | Pass |
| Markdown | Click contents entries; wheel; Tab; semantic activation; captures | Readable contents and useful navigation | Pane retains 240 px; long labels clipped; target section can land at bottom | Partial |
| Motion/settlement | Pause Markdown; activate heading; wait; step 0.2 s; capture; release | Paused motion remains unsettled | `[animating]` appeared; wait timed out; stepping changed pixels; release settled | Pass |

**Defects found through use**

- **U1—Nonatomic transaction:** In Tally, run the two-step transaction above. The increment survives. Reporting is now truthful, but the operation still breaks the advertised all-or-nothing behavior.
- **U2—Clipped contents:** At 980×720, Markdown’s left pane clips long labels at `x=240`. For example, `h1` reports a 553-pixel-wide button beginning at `x=24`, substantially beyond its visible pane.
- **U3—Weak anchor navigation:** Activate contents entry `h10` from the document’s beginning, then settle. Section 10 appears near the bottom while section 6 remains at the top; the requested section’s body is unreadable without further scrolling.
- **U4—Verb discovery:** The tool’s illustrative `notes.new` returns not-found. No MCP discovery tool exposes application verbs.

Native D-Bus creation was denied by the sandbox. I therefore inspected [/tmp/omui-eval-atspi.json](/tmp/omui-eval-atspi.json): **15/15 recorded checks pass**, including native button activation, updated disabled state, slider reading and slider editing. This establishes prior protocol integration, not my own Orca session.

Local lint measured **0 problems**, with document schemas skipped because `jsonschema` is unavailable. `mcpp test -p openmui.accessibility` remained in planning for over two minutes; I stopped it without obtaining a test result.

## Framework and openas feedback

Stable IDs, compact snapshots, effects, dry runs, grouped history and input attribution were useful. Ordinary pointer/keyboard operations and semantic operations produced coherent state. Component-owned scroll motion now participates in settlement.

The principal remaining trust problem is transaction eligibility: unknown rollback capability still permits mutation. Require affirmative rollback support before admission. Expose application verbs and their arguments through discovery.

Accessibility now shares the semantic tree through real native adapters. Its next requirement is rich text navigation and selection, followed by complete tasks with actual screen readers. Accepted confirmation, grants and person-only workflows were not exercised here.

## Dimension scores

**F = Flutter; Q = Qt Quick/QML; C = Compose Multiplatform.** “Ahead” applies only to the named capability. Changes required within each row are ordered by impact.

| Dimension | Score | Evidence | Basis | Individual comparisons | To reach 90 |
|---|---:|---|---|---|---|
| `architecture` | **80** | Used, viewed | Shared projections, staged pipeline and settlement are substantive. [Frame failures](/home/speak/workspace/mui/modules/ui/view/src/frame.cpp:47) are logged, then ordinary output is returned. | **F:** Ahead in explicit agent policy. **Q:** Behind in demonstrated host completeness. **C:** Behind in lifecycle integration. | Propagate failed-frame outcomes; enforce projection invariants; validate plugin failures and lifecycle teardown. |
| `app_api` | **70 P** | Used existing apps; viewed; authoring inferred | [Tally](/home/speak/workspace/mui/examples/03-counter/src/tally.cpp:8) is concise and reactive. Callback undo requires extra machinery; complete navigation, restoration and cancellable async workflows remain unproven. | **F:** Behind complete application recipes. **Q:** Behind integrated QML authoring. **C:** Behind lifecycle-aware effects. | Validate clean onboarding and a complete application; integrate navigation/restoration/cancellation; make callback undo explicit and reliable. |
| `component_model` | **80** | Viewed | Contracts generate [typed builders](/home/speak/workspace/mui/components/base/src/generated/button_builder.cpp:11), actions and language bindings; public services and theme scopes exist. Independent distribution remains unverified. | **F:** Ahead in cross-language contract generation. **Q:** Behind independently consumed extension proof. **C:** Ahead in explicit effect metadata. | Test installed third-party packages; specify ownership/version compatibility; complete theme-token coverage. |
| `component_library` | **65** | Used, viewed | [Base controls](/home/speak/workspace/mui/components/base/README.md:5), virtual lists, overlays, Markdown, canvas, timeline and windows exist. Application breadth and integration polish remain limited. | **F:** Behind Material/Cupertino breadth. **Q:** Behind Controls and model/view depth. **C:** Behind Material/adaptive patterns. | Add navigation containers, tables/trees and date/time controls; complete keyboard, editing and accessibility matrices. |
| `layout_text` | **76** | Used, viewed | Unicode editing worked; [HarfBuzz shaping](/home/speak/workspace/mui/modules/content/text/src/native/shape.cpp:72), bidi, fallback and layout are real. Contents sizing improved, but U2/U3 remain; real IMEs untested. | **F:** Behind editing validation breadth. **Q:** Behind platform IME maturity evidence. **C:** Behind mobile text-input validation. | Fix long-label presentation; test real CJK IMEs, bidi editing, scaling and font fallback across hosts. |
| `accessibility` | **67 P** | Used keyboard; viewed prior measurements; inferred AT experience | [Mapping](/home/speak/workspace/mui/modules/ui/accessibility/src/map.cpp:85) covers roles, values, states, focus and actions. Native adapters exist; AT-SPI report passes. Text runs/selection are incomplete; Windows/macOS screen-reader tasks unproven. | **F:** Behind accessibility checks and delivered coverage. **Q:** Behind native assistive-tool completeness. **C:** Behind integrated accessibility/test semantics. | Implement text runs, caret/selection and relationships; run Orca/NVDA/VoiceOver tasks; verify announcements, contrast and scaling. |
| `agent_native` | **78** | Used, viewed | [`openas::decide`](/home/speak/workspace/mui/modules/ui/core/src/access.cpp:46), effects, refusal, previews and grouped undo work. U1 remains; U4 obstructs discovery. | **F:** Ahead in effect-aware operation policy. **Q:** Ahead in integrated policy; behind Squish’s automation breadth. **C:** Ahead in actor/effect handling; behind dependable completion guarantees. | Reject unknown/nonrollbackable steps; expose verbs; validate accepted answers, grants, barriers and adversarial transactions. |
| `determinism_testing` | **81** | Used, viewed | Clock control and settlement now agree. [Headless-level equivalence](/home/speak/workspace/mui/modules/ui/host/headless/tests/headless_test.cpp:58), pinned pixels and language traces exist. Cross-platform results were not independently rerun. | **F:** Ahead in explicit CPU byte-pin design. **Q:** Ahead in integrated deterministic reference rendering. **C:** Ahead in platform-independent reference-clock design. | Close U1 coverage; publish cross-platform artifacts; map every normative clause to executable CTS and real-host tests. |
| `performance` | **67 P** | Used; viewed numbers; speed inferred | Small examples responded coherently, without measured presentation latency. [Published results](/home/speak/workspace/mui/docs/en/benchmarks/README.md:100): idle p95 0.151 ms/zero allocations; one-label 2.08 ms/~12,544 allocations; all-label 3.79 ms/~48,762. | **F:** Behind representative-device evidence; speed unknown. **Q:** Behind presentation evidence; speed unknown. **C:** Behind measured startup/jank evidence; speed unknown. | Gate active allocations; measure startup/RSS/power and presentation latency; compare matched workloads on integrated/mobile GPUs. |
| `platforms_web` | **62 P** | Viewed; delivery inferred | Desktop, wasm, WebGPU/WebGL2 and DOM mirror implementations exist. [Browser checks](/home/speak/workspace/mui/tools/ci/browser.sh:36) may skip successfully; WebGPU may fall back to offscreen. Native mobile delivery unestablished. | **F:** Behind desktop/mobile/web delivery. **Q:** Behind desktop/mobile/embedded coverage. **C:** Behind stable Android/iOS/desktop delivery. | Require browser provisioning and actual presentation; deliver mobile hosts; run real-device input, IME, lifecycle and accessibility tasks. |
| `tooling_dx` | **70 P** | Used MCP; viewed; onboarding inferred | [Inspector polling](/home/speak/workspace/mui/tools/inspector/src/inspector.cpp:76), CLI, templates, generation, diagnostics and reload exist. Installed developer workflows were not used. | **F:** Behind DevTools/reload workflow. **Q:** Behind Creator/QML profiling. **C:** Behind integrated authoring/lifecycle guidance. | Validate installed SDK workflows; add verb discovery; demonstrate inspector/reload/error recovery end to end. |
| `code_quality` | **73** | Viewed; measured partial lint | Module/implementation separation and native wrappers are disciplined. [Per-component services](/home/speak/workspace/mui/modules/ui/view/src/component.cppm:143) use virtual dispatch; active allocations contradict stated goals. | **F:** Behind demonstrated boundary hardening. **Q:** Behind documented ownership precision. **C:** Behind lifecycle/cancellation integration. | Reconcile rules with actual costs; enforce compiler/allocation policies; add concurrency, teardown and fault-injection coverage. |
| `docs_spec` | **68** | Viewed | [Normative specification](/home/speak/workspace/mui/spec/SPEC.md:17), registry, ABI, schemas and CTS are substantial. English developer guides/reference remain sparse; published guarantees and figures contain discrepancies. | **F:** Behind practical application documentation. **Q:** Behind API/integration reference depth. **C:** Behind lifecycle and application recipes. | Correct guarantees/numbers; publish complete tutorials/reference; establish clause-to-test traceability and independent implementation evidence. |

## Benchmark summary

Flutter’s strongest advantages are its [component catalog](https://docs.flutter.dev/ui/widgets/material), [application guidance](https://docs.flutter.dev/app-architecture/guide), and [DevTools](https://docs.flutter.dev/tools/devtools/performance).

Qt’s strengths are [Quick Controls](https://doc.qt.io/qt-6/qtquickcontrols-index.html), [native accessibility](https://doc.qt.io/qt-6/accessible.html), [QML profiling](https://doc.qt.io/qtcreator/creator-qml-performance-monitor.html), and [Squish automation](https://www.qt.io/quality-assurance/squish/platform-qt-gui-test-automation).

Compose’s strengths are shared [accessibility/test semantics](https://developer.android.com/develop/ui/compose/testing/semantics), [lifecycle-aware effects](https://developer.android.com/develop/ui/compose/side-effects), and [platform delivery](https://kotlinlang.org/docs/multiplatform/supported-platforms.html). Shared semantics are therefore not uniquely openmui’s.

For performance, Flutter and Qt document approximately 16 ms frame deadlines, not directly comparable framework throughput. Compose publishes **0.21% jank** for a specified Pixel 3a workload. openmui’s offscreen desktop numbers cannot establish superiority over these different measurements. [Flutter](https://docs.flutter.dev/tools/devtools/performance), [Qt](https://doc.qt.io/qt-6/qtquick-performance.html), [Compose measurements](https://developer.android.com/develop/ui/compose/performance/herobenchmark).

openmui’s clearest advantages remain explicit actor/effect policy and deterministic reference rendering. Its largest deficits are platform proof, complete application workflows, assistive text support and component breadth.

## Change from the last record

| Dimension | Previous → current | Reason |
|---|---:|---|
| `architecture` | 78→80 | Settlement repaired; native projection implemented. |
| `app_api` | 70→70 | Authoring untested; callback burden persists. |
| `component_model` | 80→80 | No new distribution evidence. |
| `component_library` | 65→65 | Breadth unchanged. |
| `layout_text` | 73→76 | Pane width repaired; clipping remains. |
| `accessibility` | 45→67 | Native adapters plus successful AT-SPI protocol evidence. |
| `agent_native` | 74→78 | Truthful failures and previews; atomicity still incomplete. |
| `determinism_testing` | 78→81 | Settlement repair reproduced and regression test inspected. |
| `performance` | 67→67 | No new comparable measurements. |
| `platforms_web` | 62→62 | Delivery gaps unchanged. |
| `tooling_dx` | 70→70 | Developer workflow cap remains. |
| `code_quality` | 73→73 | New adapter is disciplined; hardening gaps remain. |
| `docs_spec` | 68→68 | Inaccurate guarantees and sparse guides persist. |

Overall **45→62**; mean **69.5→72.1**.

## Highest-priority changes

1. Require proven rollback capability before transaction mutation.
2. Complete accessibility text navigation/selection and real screen-reader tasks.
3. Require browser presentation tests; establish native mobile delivery.
4. Propagate pipeline failures to callers and hosts.
5. Gate active-frame allocations and measure representative devices.
6. Validate a complete application with navigation, persistence and cancellation.
7. Expand application components and their acceptance matrices.
8. Expose verbs and validate installed SDK/component workflows.
9. Correct documentation guarantees and benchmark figures.

## Specific defects and gaps

- U1: [transaction eligibility](/home/speak/workspace/mui/modules/ui/semantic/src/tree.cpp:274) rejects explicit `undoable:false`, but admits unknown capability.
- U2: [contents buttons](/home/speak/workspace/mui/examples/08-markdown-viewer/src/viewer.cpp:156) retain intrinsic widths beyond the pane.
- U3: [anchor navigation](/home/speak/workspace/mui/components/markdown/src/document.cpp:184) uses minimal `scroll_into_view`, leaving the requested section at the bottom.
- U4: [protocol describe](/home/speak/workspace/mui/modules/ui/agent/src/protocol.cpp:336) lists operations, not application verbs.
- [Native request decoding](/home/speak/workspace/mui/modules/ui/accessibility/src/native/accesskit/adapter.cpp:253) lacks text-selection payload handling.
- [Button constants](/home/speak/workspace/mui/components/base/src/button.cpp:15) contradict the README’s universal theme-token claim.
- [Features](/home/speak/workspace/mui/docs/en/features.md:47) cite 36 budgets rather than **48**, glass 0.82 ms rather than the report’s **1.61 ms**, and wall p99 5.7 ms rather than **7.11 ms**.
- [Contract registry discovery](/home/speak/workspace/mui/mcpp/contracts/src/openmui.build.contracts.cpp:13) assumes monorepo-relative placement.
- SDL host README still says GPU rendering “replaces it at M4,” despite documenting the implemented GPU path.
- Nightly config establishes ASan/UBSan, not the planned TSan, parser-fuzzing or coverage gates.

```json
{"overall":62,"mean":72.1,"dimensions":{"architecture":80,"app_api":70,"component_model":80,"component_library":65,"layout_text":76,"accessibility":67,"agent_native":78,"determinism_testing":81,"performance":67,"platforms_web":62,"tooling_dx":70,"code_quality":73,"docs_spec":68}}
```