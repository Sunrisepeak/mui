# tools

The repository's own tools (plan §4.5): lint, ci, devtools, eval — and, until the composing toolchain project (RC3.10), the inspector. A project's tools live with the project: omui's command line, MCP server and generator are in [modules/projects/omui/tools](../modules/projects/omui/tools), hanim's in `modules/projects/hanim/studio`.

Every tool has two faces that read the same records: a command line (JSON, exit codes) and an interface written in omui (D-61).

| Package | |
|---|---|
| [devtools](devtools) | `omui-devtools`: the contributors' tool — `ci` reads a CI run row by row and sets each failure against the red list (R12); `lint` checks the repository's rules; `lead` writes the lead pages and checks the features |
| [inspector](inspector) | Semantic tree, node details, frame times and time control; Reaction lab with editable cellular projects, CLI and actual process capture |

`omui-devtools lint` ([devtools](devtools)) checks the repository's rules (`--list`, `--self-test`); [lint](lint) holds the tables they read (capability owners, licences, the C99 check of openmui.h); [ci](ci) holds the scripts CI runs.

The [dual-user tools plan](../.agents/docs/2026-11-04-dual-user-tools-validation-design.md)
selects six core tool families and their real validation workflows. Future
designers, animation/audio/MIDI editors and simulation/material workbenches
reuse public engine APIs, shared project operations and openas semantics.
They follow their engine capabilities; the table above lists existing tools,
not a claim that the planned editors are already implemented.

## dev

`omui-devtools govern` ([devtools](devtools)) runs a local build or test within 90% of the machine (AGENTS.md §5): `MCPP_JOBS` at most 90% of the cores and of what 90% of the memory holds at about 2.25 GB a compile, at low CPU and I/O priority, and the whole process tree held to that count (mcpp builds workspace groups and host tools side by side, each with its own job count): the newest compilers and linkers beyond it are paused and continued as others finish, and more of them while available memory is under 10% — `mcpp run omui-devtools -p tools/devtools -- govern -- mcpp test -p omui.view`.

`omui-devtools gpu` ([devtools](devtools)) gives the device tests a GPU: CI's software device (lavapipe; Metal on macOS) written to `$GITHUB_ENV`, or with `--real` this machine's NVIDIA GPU (the driver linked by xlings, SDL's offscreen video driver) — `omui-devtools gpu --real -- mcpp test -p xrender.rhi.sdl`: performance numbers come from it, not from lavapipe. `omui-devtools browser` runs a page's test program in Chromium (its server on the loopback, the program's lines relayed); with `--headed` under `omui-devtools gpu --real`, the full Chromium in a window on that GPU — where a display is: headless Chromium reaches Vulkan only through `VK_EXT_headless_surface`, which NVIDIA's driver lacks (`showcase/quay`'s `web_frame_test`: Quay's frame on the page's WebGPU).
