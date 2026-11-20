Overall: 45/100  
Mean: 72.0/100

Reviewed HEAD `126cb48` **and the current working-tree changes**, including the named architecture, implementation plan, framework layers, bindings, tools, benchmarks and workflows. This is an evidence audit, not certification of passing CI.

**Evidence:** **Viewed** = inspected source, documents or supplied frames; **Measured** = executed locally; **Inferred/P** = provisional judgment requiring runtime validation. Published benchmark numbers are **Viewed**, not independently reproduced. Local lint reported zero problems but skipped document-schema validation because `jsonschema` is unavailable. I did not rerun the workspace suite.

In comparisons below, **F = Flutter; Q = Qt 6 Quick/QML; C = Compose Multiplatform**. “Ahead” applies only to the stated capability.

| Dimension | Score | Evidence and basis | Comparison with each reference | Changes needed to reach 90, highest impact first |
|---|---:|---|---|---|
| `architecture` | **81** | **Viewed.** Runtime identity, actions, stages and semantic projection are coherent. [Tree refresh](modules/ui/semantic/src/tree.cpp:40) derives semantic nodes from the richer runtime tree; the serialized semantic tree is not itself the rendering model. [Frame execution](modules/ui/view/src/frame.cpp:47) logs pipeline failure and continues returning output. | **F:** Ahead in explicit action/effect integration; behind in demonstrated failure handling. **Q:** Ahead in a unified agent contract; behind in host integration maturity. **C:** Strong shared semantics, but insufficient evidence of comparable lifecycle robustness. | Define invariants between projections; propagate frame failure explicitly; demonstrate replacement and conformance for every advertised plugin category. |
| `app_api` | **76** | **Viewed + Inferred/P.** The [counter screen](examples/03-counter/src/tally.cpp:8) is concise and uses explicit IDs, reactive properties and two-way binding. [Tasks](modules/ui/core/src/task.cppm:19) provide cancellation and UI dispatch. Complete navigation, restoration and asynchronous screen-lifecycle workflows were not demonstrated. Script writes are asynchronous mirrors ([binding model](lang/README.md:5)). | **F:** Behind its complete application workflows. **Q:** Behind QML’s integrated application/component tooling. **C:** Behind navigation and lifecycle-aware state workflows. | Ship a complete mult screen application; specify ownership, cancellation and thread affinity; document reload identity and asynchronous error delivery. |
| `component_model` | **80** | **Viewed.** [Contracts](spec/registry/components/button.contract.omui.toml:1), [generated builders](components/base/src/generated/button_builder.cpp:11) and [generator dispatch](tools/gen/src/emit_cpp.cpp:3) form a real typed system. [Button behavior](components/base/src/button.cpp:21) uses public runtime/theme services. Independent distribution remains fragile. | **F:** Ahead in generating several language interfaces from one contract; behind in extension ecosystem. **Q:** Comparable declarative intent, behind in reusable package consumption. **C:** More explicit machine-readable actions; behind in authoring ergonomics and ecosystem proof. | Test third-party packages outside this checkout; publish component-author guidance and lifecycle rules; verify API compatibility across SDK upgrades. |
| `component_library` | **65** | **Viewed + Inferred/P.** [Base inventory](components/base/README.md:5) includes useful inputs, overlays and virtualized lists. Canvas, Markdown, timeline and window components have implementations. No comparable shipped breadth of tables/trees, date/time controls, navigation containers and adaptive application patterns was established. | **F:** Behind Material/Cupertino breadth. **Q:** Behind Quick Controls and model/view depth. **C:** Behind Material and adaptive application components. | Complete common application controls first; add keyboard/IME/AT acceptance matrices; document customization and interaction states for every control. |
| `layout_text` | **77** | **Viewed + Inferred/P.** Flex/grid caching is substantial ([layout](modules/content/layout/README.md:3)). [Shaping](modules/content/text/src/native/shape.cpp:72) uses HarfBuzz, bidi levels and language/features; editing supports grapheme selection and composition. Real multilingual IME behavior across operating systems remains unverified here. | **F:** Behind end-to-end editing/platform validation. **Q:** Behind mature text and input-method integration. **C:** Behind mobile editing and adaptive-layout validation. | Run real Japanese/Chinese/Korean IMEs and mixed-direction editing tests; test font fallback and scaling on devices; publish layout/text limitations. |
| `accessibility` | **45** | **Viewed + Inferred/P.** Semantic roles, focus and the [ARIA mirror](modules/ui/host/web/src/mirror.cpp:57) are meaningful foundations. No native UIA, NSAccessibility or AT-SPI adapter was found in the inspected hosts. Browser tests import a [fake DOM](modules/ui/host/web/tests/js/wasm.test.mjs:17); simulated activation does not establish screen-reader usability. | **F:** Behind native assistive-technology delivery. **Q:** Behind platform accessibility adapters. **C:** Behind implemented platform bridges, notwithstanding Compose’s own platform limitations. | Implement native adapters; test NVDA, VoiceOver and Orca with real users/tasks; verify focus, announcements, text navigation, scaling and contrast. |
| `agent_native` | **84** | **Viewed + Inferred/P.** [Access control](modules/ui/core/src/access.cpp:46) actually calls `openas::decide`; [action dispatch](modules/ui/view/src/act.cpp:220) refuses remote person-only actions. MCP, dry runs and transactions are implemented. However, [rollback](modules/ui/view/src/history.cpp:279) can retain steps without inverses, and confirmation provenance needs a precise trust-boundary statement. | **F:** Ahead in built-in effect policy, MCP and undo integration. **Q:** Ahead in these native framework contracts; behind Squish-class validation breadth. **C:** Ahead in agent policy and transactions; less proven operationally than its semantics testing stack. | Enforce rollback eligibility before side effects; document/authenticate confirmation boundaries; publish adversarial tests and repeated real-agent task results. |
| `determinism_testing` | **82** | **Viewed.** [Headless tests](modules/ui/host/headless/tests/headless_test.cpp:58) compare semantic identity across levels; [runtime tests](modules/ui/view/tests/view_test.cpp:1050) pin output hashes. Replay and injected time have implementations. Workflow definitions support a strong intended matrix, but passing runs were not independently inspected. | **F:** Ahead in explicit cross-platform CPU byte pins; behind in demonstrated production testing breadth. **Q:** Ahead in integrated replay/pixel-reference design; behind in established test tooling coverage. **C:** Ahead in deterministic reference rendering; behind in device-level integration evidence. | Publish CI artifacts and replay failures; require real-browser execution; map specification clauses to portable CTS and broaden failure/concurrency testing. |
| `performance` | **67** | **Viewed + Inferred/P.** [Published results](docs/en/benchmarks/README.md:100) report one-label p95 **2.08 ms / 12,544.3 allocations**, all-label **3.79 ms / 48,761.8 allocations**. Idle records zero allocations. The [machine/method](docs/en/benchmarks/README.md:9) uses an i9/RTX 4080 and offscreen GPU rendering. Startup, memory, power and comparable competitor workloads are missing. | **F:** Relative speed unproven; behind in representative-device evidence. **Q:** Relative speed unproven; behind in deployment profiling depth. **C:** Relative speed unproven; behind in startup/jank measurement workflows. | Remove active-frame allocations; benchmark identical applications on shared hardware; add startup/RSS/presentation latency and integrated/mobile GPU results; enforce complete budgets and regressions. |
| `platforms_web` | **62** | **Viewed + Inferred/P.** Desktop, wasm, WebGPU/WebGL and browser input code exist. [Browser checks](tools/ci/browser.sh:36) may succeed without Chromium; [WebGPU checking](tools/ci/browser.sh:68) can fall back to offscreen rendering. Native mobile applications and real Safari/Firefox/AT coverage were not established. | **F:** Behind supported mobile/desktop/web deployment coverage. **Q:** Behind desktop/mobile/embedded integration. **C:** Behind demonstrated Android/iOS application delivery. | Make provisioned browser tests mandatory; add Firefox/Safari and real presentation checks; deliver and test Android/iOS hosts before claiming mobile support. |
| `tooling_dx` | **74** | **Viewed + Inferred/P.** CLI, generator, Inspector, templates and MCP are substantive. [Inspector](tools/inspector/README.md:8) exposes tree, properties, timing, pipeline and time controls. [Contract generation](mcpp/contracts/src/openmui.build.contracts.cpp:13) assumes a sibling `spec/registry` layout. Installed-package workflows and diagnostic quality remain insufficiently proven. | **F:** Behind DevTools and integrated reload workflow. **Q:** Behind Creator/QML profiling and distribution tooling. **C:** Behind IDE previews, inspection and application tooling. | Validate clean installed-SDK consumption; complete diagnostics and profiling workflows; document toolchain constraints and provide reproducible installation. |
| `code_quality` | **71** | **Viewed; Measured lint, partial.** Module interfaces/implementations and native dependency isolation are disciplined. [Layout equality](modules/content/layout/src/engine.cpp:24) constructs temporary vectors; [bounds processing](modules/ui/view/src/stages.cpp:94) copies watchers. Stage dispatch uses virtual calls ([stage wrapper](modules/ui/view/src/stages.cpp:23)), requiring reconciliation with the stated hot-path rule. | **F:** Clear boundaries, behind in verified lifecycle/failure coverage. **Q:** Leaner policy surface, behind in hardening evidence. **C:** Explicit low-level control, behind in lifecycle/concurrency validation. | Eliminate active-path allocation; clarify intentional virtual dispatch exceptions; enforce compiler policy and audit ownership, concurrency and error boundaries. |
| `docs_spec` | **72** | **Viewed.** [Normative draft](spec/SPEC.md:17), registry, generated ABI/stubs and conformance scenarios are valuable. Public English documentation remains much narrower than the planned onboarding/reference set. Architecture examples retain superseded names and dependencies. Portable clause coverage is asserted but not demonstrated by a complete traceability index. | **F:** Ahead in an implementation-independent normative ambition; behind in practical documentation. **Q:** Behind API reference and integration guidance. **C:** Behind application recipes and lifecycle guidance. | Publish onboarding and complete API/component references; reconcile obsolete design examples; provide clause-to-CTS traceability and validate a second implementation. |

**Benchmark summary**

Flutter’s strongest demonstrated advantages are its complete application workflow, component catalogs, accessibility delivery and [DevTools profiling](https://docs.flutter.dev/tools/devtools/performance). Qt’s are [Quick Controls breadth](https://doc.qt.io/qt-6/qtquickcontrols-index.html), desktop integration, [platform accessibility](https://doc.qt.io/qt-6/accessible.html) and QML tooling. Compose’s are declarative application integration, [semantics-based testing](https://developer.android.com/develop/ui/compose/testing/semantics) and [Macrobenchmark workflows](https://developer.android.com/topic/performance/benchmarking/macrobenchmark-overview).

openmui’s clearest leads are explicit agent action/effect contracts, framework-level undo integration, and deterministic CPU reference rendering. Its largest deficits are usable native accessibility, platform delivery, ordinary application completeness and independent operational evidence.

The competitors’ published frame targets are not equivalent workload measurements: [Flutter](https://docs.flutter.dev/perf/best-practices) and [Qt](https://doc.qt.io/qt-6/qtquick-performance.html) discuss frame budgets; Compose documents device benchmarking. None justifies claiming openmui is faster from its current report.

**Changes relative to the previous record**

The rubric changed from 12 broad dimensions to 13 framework-specific dimensions. These are reassessments, not measured regressions or improvements; HEAD is unchanged, and the working tree contains edits.

| Current dimension | Previous counterpart → current | Reason |
|---|---|---|
| `architecture` | 81 → 81 | Same coherent model and unresolved frame-failure behavior. |
| `app_api` | Combined APIs 78 → 76 | Application lifecycle/navigation judged separately. |
| `component_model` | Combined APIs 78 → 80 | Contract generation deserves distinct credit. |
| `component_library` | No direct predecessor → 65 | Breadth now explicitly evaluated. |
| `layout_text` | No direct predecessor → 77 | Strong implementation; device editing evidence incomplete. |
| `accessibility` | No direct predecessor → 45 | Missing native delivery is now an independent gate. |
| `agent_native` | Distinctive features 82 → 84 | Focused credit for real policy/action integration. |
| `determinism_testing` | Testing/CI 75 → 82 | Narrower dimension rewards deterministic infrastructure. |
| `performance` | Performance evidence 70 → 67 | Stronger weighting of allocations and comparative evidence. |
| `platforms_web` | Cross-platform/Web 65 → 62 | Mobile and actual browser delivery explicitly weighted. |
| `tooling_dx` | 75 → 74 | Installed-consumer evidence remains weak. |
| `code_quality` | 72 → 71 | Active-path policy violations remain visible. |
| `docs_spec` | Documentation/specification 72 → 72 | No established completion of the missing guides/reference. |

The previous completeness, package-structure and game-engine dimensions have no direct successors. Overall **60 → 45** chiefly reflects exposing accessibility as its own gate. Means **73.4 → 72.0** are not directly comparable.

**Highest-priority changes**

1. Deliver native accessibility adapters and real assistive-technology acceptance tests.
2. Guarantee transaction rollback eligibility before actions mutate state.
3. Propagate pipeline failures through explicit frame/host outcomes.
4. Remove active-frame allocations and gate representative update workloads.
5. Require provisioned browsers and verify actual presentation.
6. Build a complete application demonstrating navigation, restoration, async cancellation and errors.
7. Expand common controls with consistent keyboard, IME and accessibility behavior.
8. Test SDK, generator and third-party component installation outside the monorepo.
9. Publish matched competitor benchmarks, device results and traceable CI/CTS evidence.
10. Complete developer guides and reconcile documentation with implementation.

**Specific defects and omissions**

- [frame.cpp:47](modules/ui/view/src/frame.cpp:47): pipeline errors are logged without a failed-frame result.
- [history.cpp:291](modules/ui/view/src/history.cpp:291): rollback explicitly leaves inverse-less steps done; qualify the advertised “all taken back” guarantee.
- [server.cpp:267](tools/mcp/src/server.cpp:267): non-elicitation confirmation relies on the caller truthfully forwarding consent; document that trust assumption.
- [engine.cpp:24](modules/content/layout/src/engine.cpp:24): temporary vectors for single-track comparison.
- [stages.cpp:94](modules/ui/view/src/stages.cpp:94): watcher-vector copy during bounds processing.
- [button.cpp:15](components/base/src/button.cpp:15): dimensions remain local constants, despite the README’s claim that every size comes from theme tokens.
- [browser.sh:36](tools/ci/browser.sh:36): missing-browser checks can exit successfully.
- [harness.cpp:514](tests/bench/src/harness.cpp:514): missing/skipped budgets require strict mode to fail; CI does not request `--all-budgets`.
- [sdl3 README:15](modules/ui/host/sdl3/README.md:15): obsolete “GPU replaces it at M4” wording contradicts the implemented GPU path below.
- `11-lens-tree.png`, main document: role labels and nested rectangles overlap text; heading/paragraph content is heavily dimmed.
- `10-lens-test.png`, main pane: repeated long structural paths dominate assertions; truncated names reduce diagnostic usefulness.
- Both supplied frames, footer: timeline, hashes and status compete for space. A displayed matching hash is not independent CI evidence.

```json
{"overall":45,"mean":72.0,"dimensions":{"architecture":81,"app_api":76,"component_model":80,"component_library":65,"layout_text":77,"accessibility":45,"agent_native":84,"determinism_testing":82,"performance":67,"platforms_web":62,"tooling_dx":74,"code_quality":71,"docs_spec":72}}
```