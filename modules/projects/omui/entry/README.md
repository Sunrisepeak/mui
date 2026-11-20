# omui's programs

The composition roots of omui (component-binding §10.2): programs, not libraries — each depends on the SDK and what
it hosts, and nothing depends on it.

| Package | |
|---|---|
| [player](player) | `omui.entry.player`: runs a TypeScript application from its manifest (`[package.metadata.openmui]`), registers the official components and the script languages, and runs it with omui's options; saved files reload in place. Its entry `player` registers every base component; its entry `closure` registers only those the application's build derived from its scripts (`main = { from = "omui.entry.player/closure" }` with `omui.build`'s `closure()`, B7b). Its program of its own, `omui-player` (`--manifest DIR`), is what the omui tool ships (E2) and, without the designer, the shared runtime (E3b: `--runtime` says its version, the specification it implements and its capabilities, runtime@1) |
| [devhost](devhost) | `omui.entry.devhost`: the development host (`omui dev`, D-154): it holds the window, the application is its render target, and the designer is linked (preview: `omui.designer`) |
| [launcher](launcher) | `omui.entry.launcher` (preview): the shared runtime's launcher (E3b, D-250), `omui-launch [--which] [APP] [-- ARGS]`. It reads what the application declares (`[package.metadata.openmui.runtime]`: `openmui = "0.4"`, the capabilities it `needs`, or one `runtime = "=0.1.0"`), finds the runtimes installed side by side (xlings: `data/xpkgs/*-x-omui-runtime/<version>/`, each with its `runtime.json`), and becomes the player of the newest that satisfies it. With none, it says why each installed one does not and the command that installs one, exit 2. It links no runtime; packed for size it is 1.2 MB against the ≤ 200 KB goal (STATUS red) |
