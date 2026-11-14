Overall: 60/100

This is a substantial framework implementation with unusually coherent semantics, deterministic rendering, and agent integration. It has not yet demonstrated production completeness beside Flutter, Qt, SwiftUI, Compose, Slint, or egui; mgame remains substantially narrower than a general engine such as Bevy. **The overall score is the lowest dimension, not the average.**

I reviewed the current working tree, including existing uncommitted changes. This is a source-and-evidence audit, not a fresh certification of CI results. Local lint reported zero problems, but skipped document-schema validation because `jsonschema` was unavailable. I did not rerun the hour-long workspace suite.

| Dimension | Score | Evidence | Changes to reach 90+ |
|---|---:|---|---|
| **1. Completeness against plan** | **68** | M0–M3 have substantial implementations: generation, templates, signals, text, animation, documents, bindings, replay. M4 has real GPU backends, canvas, effects, and reports. M5/M6 remain partial against [§19](/home/speak/workspace/mui/.agents/docs/2026-10-26-openmui-implementation-plan.md:938): native accessibility missing; video preview/GPU export and comparative benchmarks unfinished; complete guides and four weeks of stable gates unproven. Breakout now has five rooms while acceptance still specifies six. | Maintain an acceptance ledger linking every requirement to executable evidence or signed review. Resolve changed requirements explicitly. Finish global gates before declaring milestones complete. |
| **2. Architecture** | **81** | The runtime genuinely shares identity, actions, interaction, painting, and semantic projection. [Tree::refresh](/home/speak/workspace/mui/modules/ui/semantic/src/tree.cpp:47) derives semantic nodes from richer runtime nodes: the wire semantic tree itself does not drive rendering. Layer separation, typed stage resources, CPU reference, RHI backends, and hosts are credible. [Render graph](/home/speak/workspace/mui/modules/gfx/render/src/graph.cpp:31) validates ordering and culls passes, but is a deliberately simple ordered graph. [Frame execution](/home/speak/workspace/mui/modules/ui/view/src/frame.cpp:47) logs pipeline failure and continues producing output. | Specify invariants between runtime, semantic projection, layout, and pixels. Propagate stage failure coherently. Demonstrate replacement and CTS for every promised plugin kind; distinguish static substitution from a supported dynamic plugin ABI. |
| **3. Directory/package structure** | **83** | Role-based `base/gfx/content/media/ui`, nested RHI backends, canvas/kit, and separate spec are clear. The manifest lists **112 members**. Small packages often represent legitimate independent boundaries; package count alone is not a defect. `view` is approximately 8,600 source lines; `gongfu.present2d` approximately 5,000 and contains reusable presentation machinery. Some leaf packages lack READMEs despite lint passing. | Preserve the broad structure. Extract reusable rigging from fighting presentation; strengthen leaf documentation checks. Reduce exported interface coupling before multiplying runtime packages. |
| **4. Code quality** | **72** | `.cppm` interfaces plus `.cpp` implementations, native library isolation, deterministic math, generational storage, and located diagnostics are strong. Concrete allocation violations: [layout equality](/home/speak/workspace/mui/modules/content/layout/src/engine.cpp:24) constructs temporary vectors; [bounds processing](/home/speak/workspace/mui/modules/ui/view/src/stages.cpp:94) copies watchers. [World::add/remove](/home/speak/workspace/mui/mgame/world/src/world.cppm:93) bypass generation validation. Exception lint is lexical; it does not establish that throwing standard-library operations cannot escape core paths. | Remove active-frame allocations, document and enforce handle preconditions, audit failure paths and concurrency, and verify exception/RTTI policy through effective compiler configuration and boundary tests. |
| **5. Application/component APIs** | **78** | Signals, designated-initializer builders, keyed regions, contracts, generated bindings, and explicit IDs form a usable model. The rating example demonstrates third-party authorship; official components use public services. However, asynchronous script mirrors, positional signal identity during reload, and explicit registration introduce consequential lifecycle rules. Process-global access/registries also complicate multiple independent applications. | Publish lifecycle, ownership, thread-affinity, reload-identity, and cancellation contracts. Provide a complete application tutorial and component-author guide, with failure examples. Scope application policy appropriately. |
| **6. Distinctive features** | **82** | `openas::decide` is actually called in [access control](/home/speak/workspace/mui/modules/ui/core/src/access.cpp:48); person-only refusal, grants, undo, transactions, MCP, virtualized semantics, and replay have implementation evidence. Analytic animation and CPU pixel determinism are substantive. The site explicitly says time travel rewinds drawing while the tree remains current. Contracts and animation alone do not establish superiority over the named competitors. | Complete native accessibility to fulfill the central thesis. Document presentation rewind versus state replay precisely. Validate the combined workflow with independent applications and external users; replace categorical competitor checkmarks with sourced, qualified comparisons. |
| **7. Performance/evidence** | **70** | Real harness, allocation counting, CPU raster/damage, GPU comparisons, and **48 budgets** deserve credit. But only seven allocation budgets exist; UI’s zero-allocation budget covers idle. The public report itself records allocations during active canvas/wall frames. GPU evidence is an offscreen RTX 4080 run, not the specified integrated-GPU target. [Budget checking](/home/speak/workspace/mui/tests/bench/src/harness.cpp:514) permits absent/skipped cases unless strict mode is requested; CI does not request it. Absolute limits do not implement §19’s ≤5% p99 regression gate. | Require benchmark completeness, baseline comparison, active-frame allocation budgets, representative integrated/mobile hardware, browser frame measurements, startup, memory, and presentation latency. Label CPU encoding versus GPU execution accurately. |
| **8. Testing/CI** | **75** | Strong deterministic hashes, Unicode/CommonMark fixtures, language traces, real-process agent tests, GPU CTS, and nightly ASan/UBSan. [CI](/home/speak/workspace/mui/.github/workflows/ci.yml) repeats the entire suite across six native configurations. No coverage gate, TSan job, or dedicated coverage-guided parser-fuzzing campaign is demonstrated. Browser checks can skip successfully. Timing assertions on shared runners invite noise. | Introduce test levels and duration reporting; preserve cross-platform golden coverage. Add coverage, TSan, persistent fuzz corpora, mandatory browser jobs, and explicit skip accounting. See speed plan below. |
| **9. Tooling/DX** | **75** | Build actions declare inputs/outputs; CLI, MCP, and Inspector share useful runtime data. Templates and installed Python-wheel tests are valuable. However, [contract generation](/home/speak/workspace/mui/mcpp/contracts/src/openmui.build.contracts.cpp:13) finds the registry through a sibling repository layout, weakening independent package distribution. Reflection is present but explicitly blocked in the full toolchain graph. Inspector lacks the complete promised analysis surface. | Test installed SDK/build-plugin consumption outside this checkout. Ship reproducible distributions and migration/versioning guidance. Finish Inspector profiling views and make unsupported reflection status unmistakable. |
| **10. Cross-platform/Web** | **65** | Desktop matrix, ARM Linux, wasm tests, actual WebGPU/WebGL implementations, IME textarea, DOM semantics mirror, and static site fallback are real. Native screen-reader adapters are absent. [browser.sh](/home/speak/workspace/mui/tools/ci/browser.sh:35) succeeds without Chromium; WebGPU presentation can fall back to offscreen verification. Node and fake-DOM tests cannot establish Safari/Firefox, assistive-technology, or mobile-browser behavior. | Mandatory provisioned browsers; browser/OS/AT compatibility matrix; native accessibility; real hardware presentation and input testing. Treat mobile/embedded support as unproven until demonstrated. |
| **11. mgame/gongfu as an engine** | **60** | Gongfu is the strongest engine subsystem: fixed-point simulation, copyable state, rollback, frame-data measurement, bots, and presentation separation. mgame supplies useful fixed-capacity columns, timing, input, and swept collisions. It does not demonstrate a general scene/asset workflow, scalable scheduling, broad physics, general 3D rendering, or engine editor. [Net inputs](/home/speak/workspace/mui/mgame/net/src/net.cppm:102) use raw `memcpy`; little-endian message headers do not make arbitrary input structs portable. Options also lack validation before modulo/ring operations. | Choose an explicit engine scope. Harden serialization, option validation, stale handles, and hostile-message handling. Build a second substantially different game using reusable services before claiming general-engine maturity. |
| **12. Documentation/specification** | **72** | Registry-generated ABI/schema/stubs, normative spec, detailed decisions, member documentation, and dev stories are substantial. Public English docs contain features and practices, not the planned complete onboarding/reference set. Architecture prose retains superseded layouts/dependencies; hanim’s README still says standard-library-only after the math extraction. Portable conformance is concentrated in S scenarios; other evidence lives largely in implementation tests. | Reconcile documents with decisions and manifests. Publish task-oriented guides, generated API reference, compatibility policy, and a clause-to-CTS index. Validate the standard through a second independent implementation. |

## Directory tree: recommended changes

Keep the established layers and package names. Do not merge math, hash, memory, simulation, and input merely to reduce member count; fix build orchestration instead.

```text
modules/
  base/
    rig/                    # NEW: reusable skeletons, skinning, pose evaluation
  gfx/render/
    vector/                 # retain current 2D renderer
    mesh/                   # add when a distinct general mesh renderer exists
  ui/
    view/src/
      ...                   # first improve internal ownership/interfaces
    host/
      accessibility/        # NEW: shared semantic adapter, platform native units

mgame/gongfu/
  present2d/                # retain fighting camera, impact/cinematic composition
                            # move reusable rig machinery to base/rig

tests/
  ...                       # separate fast, integration, visual, stress manifests
                            # avoid cloning production source ownership

docs/en/
  getting-started/
  guides/
  component-authors/
  reference/
```

Extract rigging along D-88’s boundary; do not move fighting-specific choreography into the foundation. Keep theme packs and backend packages separately selectable.

## Test speed: diagnosis and plan

The supplied **60–70-minute** runtime is credible, but I cannot assign measured percentages without timing logs.

Three distinct costs are visible:

- **Rendering work:** [site frame tests](/home/speak/workspace/mui/site/tests/frames_test.cpp:37) run transitions for up to 240 frames, repeatedly rasterizing a 2560×1600 surface on one thread. Promo tests render hundreds of sampled/seek frames, include 1080p/4K pins, synthesize soundtracks, and encode README animation.
- **Simulation work:** fighting tests run 100 rounds for every roster pair, plus long stun/balance searches. These are valuable stress checks, poorly placed in an undifferentiated default suite.
- **Build/orchestration work:** the reported serial member execution limits utilization despite parallel compilation inside a member. CI separately invokes workspace build and test, then additional template, feature, agent, and packaging builds. No Actions cache is configured. Whether test invocation recompiles or merely replans needs measurement.

Crucially, `Headless::step()` does **not** automatically rasterize: pixels are lazy. Avoid attributing every simulated frame’s cost to rasterization.

Recommended execution:

1. **Instrument first:** emit machine-readable build, member, binary, and case durations; record planning, compilation, execution, and peak memory separately.
2. **Fast PR level:** affected packages plus reverse dependents; unit/semantic/layout tests, deterministic curve tests, and a small visual smoke set. Retain debug checks.
3. **Release integration level:** exhaustive rendered sequences, full-resolution pins, media encoding, balance tournaments, and throughput tests. Custom `check()` assertions remain active in release.
4. **Cross-platform determinism:** retain representative exact pins on every required compiler/platform; run exhaustive sequences on scheduled release shards. Keep full acceptance required before release.
5. **Build once per configuration:** construct test artifacts together, then schedule binaries/cases by historical duration. Use isolated output directories and cap nested task pools to prevent oversubscription.
6. **Cache correctly:** toolchains, downloaded dependencies, generated assets, and compatible build artifacts keyed by compiler, target, profile, lockfile, generator, and inputs. Never reuse incompatible module BMIs.
7. **Nightly levels:** complete ASan/UBSan, targeted TSan, fuzzing, and long stress campaigns. Split README-media freshness from routine interaction tests while preserving its gate.

Set a measured PR latency target—such as 10–15 minutes—then tune shards. Do not achieve it by silently skipping acceptance coverage.

## Top 10 actions

1. **Deliver native accessibility**, with real screen-reader interaction tests.
2. **Harden public boundaries:** stale entity mutation, net option validation, explicit input serialization, bounded agent queues, and coherent pipeline errors.
3. **Enforce active-frame allocation discipline**, starting with layout equality and watcher traversal.
4. **Separate test levels and schedule by duration**, keeping full release acceptance.
5. **Implement strict performance gates:** required cases, relative p99 regressions, representative hardware, memory, and presentation.
6. **Reconcile §19 with current scope**, including five-room breakout, video targets, and unavailable evidence.
7. **Prove external SDK consumption**, independent build-plugin installation, and reproducible packaging.
8. **Extract reusable rigging and validate mgame with another game**, before expanding engine claims.
9. **Make browser testing mandatory**, then add cross-browser, IME, accessibility, and device coverage.
10. **Publish complete developer documentation and portable CTS**, backed by an independent implementation.