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
| [templates](templates) | Application, component, native-module and theme templates (`omui new`) |

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

| Capability | Label | Packages |
|---|---|---|
| The semantic tree, views, layout, signals, themes | stable | `omui.core`, `omui.view`, `omui.semantic`, `omui.theme` |
| Components (base, window, markdown, canvas) | stable | `omui.ui.*` |
| TypeScript and JavaScript, view documents | stable | `omui.lang.*`, `omui.viewdoc`, `omui.expr` |
| Desktop and Web hosts, headless | stable; the desktop host's system notifications go through `mpal` (preview, `lower`) | `omui.host.sdl3`, `omui.host.web`, `omui.host.headless` |
| The agent surface, the `omui` command, MCP, the inspector | stable; `omui video` and `omui audio` (xengine's media kernels), the inspector's reaction lab (`xengine.cellular`) and hanim studio, and the dev host's designer are preview (`lower`) | `omui.agent`, `tools/cli`, `tools/mcp`, the repository's `tools/inspector`, `entry/devhost` |
| The SDK (`import omui;`) | stable; carries `omui.generate` and the Android host as preview (`lower`) | `omui.sdk` |
| Runtime generation, the designer (development mode) | preview | `omui.generate`, `omui.designer` |
| The Android host | preview (tier-2 platform) | `omui.host.android` |
| C++26 reflection | experimental | `omui.reflect` |
