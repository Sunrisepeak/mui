# omui

A UI framework in which one semantic tree drives pixels, strong motion, accessibility, agents and tests (its motion is hanim's).

The reference implementation of the [openmui](../../../spec) specification: a view, its semantics and its pixels come from one tree. Depends on the mlibs layers, [hanim](../hanim), [xrender](../xrender), [xengine](../xengine)'s media subsystems and [spec](../../../spec); never on a game layer.

**Signature features — candidates** (plan §9.5; none is claimed outside the repository before its evidence exists):

| Feature | Kind | Evidence needed |
|---|---|---|
| One semantic tree drives pixels, accessibility, agents and tests, so they cannot disagree | for what comes next | one task done by a person, a screen reader, an agent and a test, the state the same at every step |
| Agent-native protocol: transactions, undo, dry runs, region pixels and authorisation built in; development and release permissions in tiers | for what comes next | a real LLM's success rate on GUI-less tasks; integration steps against Flutter and Qt with external automation |
| Strong motion (hanim's): any state change becomes a good animation — node-, semantics- and glyph-matched morphs, interruptible, reducible, reviewable frame by frame — with no animation code in components | leading | the same scenes against Flutter Hero, SwiftUI matchedGeometry, Framer Motion (coverage, intermediate frames, interruption, cost) |
| Headless pixels byte-identical across platforms | leading | pixel hashes equal on three platforms and the Web (CI) |

| Part | |
|---|---|
| [sdk](sdk) | `omui.sdk`: the umbrella package users name (`import omui;`): the runtime, the base components, the default theme, `omui::run`; runtime generation (D-144, `omui.generate`, preview) |
| [entry](entry) | omui's programs: the player (a TypeScript application run from its manifest) and the development host (`omui dev`: the window held, the designer linked) |
| [tools](tools) | omui's toolkit: the `omui` command, the MCP server, the generator `omui-gen` |
| [components](components) | The component libraries (`openmui.ui.*`) |
| [lang](lang) | The language binding: TypeScript, over the C ABI |
| [cts](cts) | `omui.cts`: the specification's level S run in every language, on mlibs.cts |

| Package | |
|---|---|
| [core](core) | Signals, verbs, input events, JSON, diagnostics, tasks; who may do what (openas) |
| [expr](expr) | Expressions: a CEL-compatible parser, printer and evaluator for bindings |
| [view](view) | The retained view runtime: elements, components, theme tokens, frames, input, actions, windows |
| [hanim.visual](../hanim/visual) | Universal motion is hanim's visual representation now (D-120): matter from what a node drew, matched and morphed between two drawings (D-93) |
| [semantic](semantic) | The semantic tree: snapshots, deltas, queries, actions, waits, checks, recording |
| [accessibility](accessibility) | The tree served to the platform's assistive technology: UIA, NSAccessibility, AT-SPI (AccessKit) |
| [agent](agent) | The agent endpoint: `openmui.tree` over local IPC, JSON lines, envelopes |
| [viewdoc](viewdoc) | View documents: checking, loading, reloading, component documents, the schema |
| [theme](theme) | Themes as data (`*.theme.xdef.ts`); the default, compact and style packs inside it |
| [native](native) | The native-module registry: contract-typed C++ functions scripts call; the official modules inside it |
| [abi](abi) | The C ABI (`openmui.h`) |
| [player](player) | The player: an application's views, components and scripts, run and reloaded in place |
| [designer](designer) | Live Design's designer: the design verbs, history, previews, variants and timeline of an application in development mode; linked by `omui dev`, left out of a release |
| [reflect](reflect) | C++26 reflection: forms from structs (GCC 16) |
| [host](host) | Hosts: headless and the SDL3 desktop |

Start with [view](view/README.md).

## How failure is answered

A stable package's API answers why it failed with the error contract, `mlibs.error` (a code, its kind — `invalid-argument`, `not-found`, `refused`, `failed` … the codes an agent's `error` record carries —, where, a message, causes), each package with its own codes: `ThemeFailure`, `QueryFailure` (semantic queries and conditions), `ViewFailure` (a model's rows, input attribution, captures and reloads, and the hosts' windows, devices and files), `TsFailure` (reading, compiling, evaluating and editing TypeScript), `AgentFailure`, `VerbRegistrationFailure`, `CtsFailure`. Work an application, a plugin or a platform provides — a verb, a task, a language of the player, the SDK's options and renderers, an accessibility service — answers `omui::Failure`: a kind and a message, a plain string being a `failed` one, a typed failure keeping its kind. Both are written by `std::format("{}", failure)`. A native module's function answers a message (`std::expected<T, std::string>`): at the script boundary it is the message of the `Error` the script catches. Each package's surface is its `api.omui.txt` (`omui-devtools api`).

## Labels in 0.1 (release plan §2.3)

Each package's manifest says its label (`maturity`); lint `maturity` holds stable to stable. Stable: kept source-compatible within 0.1.x. Preview: complete within its stated scope, its interface may change (each change in the release notes). Experimental: ships with its target and gap written.

| Capability | Label | Packages | Evidence |
|---|---|---|---|
| The semantic tree, views, layout, signals, themes | stable | `omui.core`, `omui.view`, `omui.semantic`, `omui.theme` | [semantic_test](semantic/tests/semantic_test.cpp), [view tests](view/tests/view) |
| Components (base, window, markdown, canvas) | stable | `omui.ui.*` | [base components' tests](components/base/tests/base), [the wall's keyboard walk](../../../site/tests/site/wall_test.cpp) |
| TypeScript and JavaScript, view documents | stable | `omui.lang.*`, `omui.viewdoc`, `omui.expr` | [define_test](lang/ts/tests/define_test.cpp), [viewdoc_test](viewdoc/tests/viewdoc_test.cpp) |
| Desktop and Web hosts, headless | stable; the desktop host's system notifications go through `mpal` (preview, `lower`) | `omui.host.sdl3`, `omui.host.web`, `omui.host.headless` | [sdl3_test](host/sdl3/tests/sdl3_test.cpp), [headless_test](host/headless/tests/headless_test.cpp), [the Web's drawing_test](host/web/tests/drawing_test.cpp) |
| The agent surface, the `omui` command, MCP, the inspector | stable; `omui video` and `omui audio` (xengine's media kernels), the inspector's reaction lab (`xengine.cellular`) and hanim studio, and the dev host's designer are preview (`lower`) | `omui.agent`, `tools/cli`, `tools/mcp`, the repository's `tools/inspector`, `entry/devhost` | [agent_session_test](agent/tests/agent/agent_session_test.cpp), [cli_test](tools/cli/tests/cli_test.cpp) |
| The SDK (`import omui;`) | stable; carries `omui.generate` and the Android host as preview (`lower`) | `omui.sdk` | [agent_startup_test](sdk/tests/agent_startup_test.cpp) |
| Runtime generation, the designer (development mode) | preview | `omui.generate`, `omui.designer` | [generation_test](sdk/tests/generation_test.cpp), [designer_test](designer/tests/designer_test.cpp) |
| The Android host | preview (tier-2 platform) | `omui.host.android` | [example 01 as an APK on the emulator](../../../.github/workflows/ci.yml) (CI's Android job) |
| C++26 reflection | experimental | `omui.reflect` | [reflect_test](reflect/tests/reflect_test.cpp) |

## Known limitations

Each names the level it is solved at (D-179: foundation, part, specialised, application); the platform table says what is verified where ([platforms](../../../docs/en/platforms.md)).

- **Screen readers off Linux** (foundation): the accessibility bridge is run against AT-SPI on Linux; UI Automation, NSAccessibility and the Web's are implemented to the contract but not verified on machines.
- **Input methods on machines** (foundation): composition and candidates are tested with synthetic events on every row; a real IME on each platform is not.
- **A caret's blink is a hard cut** (part): the text box's caret is drawn on or off, not faded on the runtime's clock (component quality B6).
- **A tree's chevron morphs instead of turning** (foundation): a matched morph interpolates the path's points in a line; a turn needs an arc path in hanim.
- **A lost GPU device** (foundation): the desktop host lets it go and makes it again, else the CPU draws; no CI row can provoke a loss, so the path is unverified.
- **The first frame is not timed** (foundation): the runtime's part is benchmarked (`ui/mount`), the process start to the window's first pixels is not.
