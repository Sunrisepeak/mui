# omui.bench.competitors.terminal

The smallest terminal application side by side (CF1's F4; [terminal design](../../../../.agents/docs/2026-11-19-terminal-host-design.md) §5, size design §5.4): the same counter — a count and two buttons, Tab and Enter, the mouse where the library has one — written with ratatui, FTXUI and Ink, built here at pinned versions as each ecosystem builds a release, stripped, and weighed beside omui's [`tests/size/terminal`](../../../size/terminal) (the first tier: no agent endpoint) and [`tests/size/terminal-agent`](../../../size/terminal-agent) (the second). The verdicts are lines of [the lead ledger](../../../../.agents/docs/evaluation/lead.ledger.xdef.jsonl) (`terminal.size.counter.*`).

| | |
|---|---|
| `ratatui/` | ratatui 0.30.2 on crossterm 0.29.0 (`Cargo.lock` pins the rest). ratatui has no button: two bordered paragraphs and their hit test, the focused one reversed |
| `ftxui/` | FTXUI v7.0.3: two `Button`s in a `Container::Horizontal`, `ScreenInteractive::Fullscreen()`. FTXUI is written in headers, so its unit is a native one (lint `headers`) |
| `ink/` | Ink 8.0.0 with React 19.3.1 (`package-lock.json`), on Node 26.7.0. Ink has no mouse: the buttons are focusable boxes |

## How it is measured

Taken by hand on 2026-11-26 (the harness that builds the three sides is not written yet: `omui-devtools bench apps` is the place, its metrics `planned` until then). From this directory, outputs under the checkout's `target/`:

- **omui**: `omui-devtools check size --native -p omui.size.terminal --budget tests/size/terminal/size.budget.xdef.json` — the profile the budget's row names (since 2026-11-27, later: the workspace's `small`, `-Oz`, NDEBUG, sections collected, as the terminal design states the tiers are built; before, the size profile, dist's `-O3` code) with its symbols kept — then the program `strip --strip-all`, which is what is shipped. Both tiers take the system's C++ runtime (`cxx_runtime = "host-coupled"`, D-221), as a C++ program on Linux does.
- **ratatui**: `CC=/usr/bin/cc CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER=/usr/bin/cc cargo build --release --locked` (Rust 1.99.1; the profile is the other Rust sides', apps/slint's: link-time optimisation, one code unit, stripped), then `strip --strip-all`.
- **FTXUI**: `cmake -S ftxui -B … -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++ [-DFTXUI_SOURCE=DIR]` and `cmake --build` (the system's GCC 13.3 and its libstdc++, shared, as a CMake project on Linux is built; FTXUI's units in sections and collected, as omui's are), then `strip --strip-all`.
- **Ink**: `npm ci --omit=dev` (npm 11.19.0): the directory as installed; and `esbuild counter.js --bundle --splitting --platform=node --format=esm --minify --external:react-devtools-core` (esbuild 0.25.12; the banner `import{createRequire as __cr}from'module';const require=__cr(import.meta.url);` for the CommonJS inside) — the smallest form Node runs it in, three files (the devtools chunk only loads under `DEV=true`).

**What is counted.** *Program*: the application's own files — a native program stripped, or Ink's bundle. *At run time*: the program and every file it needs that a Linux system does not already have. libc, libm, libgcc_s and libstdc++ are the system's and not counted, for every side (omui's programs also need the toolchain's glibc, 2.43 or later, as every program mcpp builds does: not counted, as in apps/). Node is not a system's: its official binary for 26.7.0, 147,887,312 B, is counted for Ink. Fonts are not counted: a terminal draws its own glyphs (omui's distribution still carries Inter, 876,576 B, beside the program — mlibs.text deploys it; F2's leftover, until Inter is a pack of its own).

Every side was run on a pseudo-terminal (util-linux 2.39.3's `script`, 60 × 20) and moved by Tab and Enter (FTXUI, ratatui, Ink and omui from 0 to 1 or 2) before it was weighed.

## Results (2026-11-26, Linux x64, this machine)

| side | program B | at run time B | notes |
|---|---:|---:|---|
| omui, `tests/size/terminal` | 3,392,536 | 3,392,536 | loads 3,382,445 B (`check size`); with libstdc++ linked in, as mcpp links by default: 4,162,920 |
| omui, `tests/size/terminal-agent` | 5,190,968 | 5,190,968 | the second tier: the agent endpoint, which no other side has |
| FTXUI 7.0.3 | 334,208 | 334,208 | with omui's toolchain and linkage (GCC 16, libstdc++ linked in): 967,808 |
| ratatui 0.30.2 | 622,424 | 622,424 | cargo's default release profile, stripped: 721,032 |
| Ink 8.0.0 | 679,267 | 148,566,579 | the bundle and Node 26.7.0; as installed by npm: 8,324,126 B in 5,033 files, and Node |

**The ledger's verdicts**: the program, against FTXUI (the strongest), 0.099× — **behind**, ten times the bytes; at run time, the same against FTXUI and ratatui, and 44× smaller than Ink with its Node. The terminal design's bars — 1 MB for the first tier, 2 MB with the endpoint — are not met: a red (§7's write-up below).

**2026-11-27** (UI7, CF1; the same build and counting): omui's first tier 2,223,472 B stripped (2,211,981 loaded), the second 3,858,288 B (3,849,938 loaded) — the view's pixel parts, the agent's captures and the terminal's agent face read at slots a terminal program does not install, a paragraph's ranges sorted by their places. Against FTXUI 0.15× (behind, 6.7 times the bytes; the ledger's 2026-11-27 lines); at -Oz (the workspace's `web` profile), 1,221,440 and 2,006,144 B stripped.

**2026-11-27, later** (CF1; built at the `small` profile, `-Oz`, as the design states the tiers): omui's first tier 1,053,440 B stripped (1,047,242 loaded; 1,225,536 and 1,215,758 at 1d69ab4e's code built the same way), the second 1,846,336 B (1,835,968 loaded) — the second within its 2 MB, the first still over its 1 MB. Cut since: only the primitives a program names registered, the view's motion choice and morph pass behind the morph slot, mlibs.unicode's tables in three stages. Against FTXUI 0.317× (behind, 3.2 times the bytes), ratatui 0.591×, Ink's bundle 0.645× (the ledger's lines of the day).

## Where omui's bytes are, and what was tried

The first tier's code by owner (`omui-gen size`; an aliased symbol is listed under each of its names, so the shares are of the owner list): omui.view 26%; the view's pixel parts that a terminal never draws 26% — the effects' SPIR-V shaders (247 KiB), hanim.visual (180 KiB), PNG and zlib (124 KiB and more), the raster and vector renderers' entry points, mlibs.pipeline; the standard library's instantiations 15%; text (the Unicode tables, SheenBidi, paragraphs) 9%; JSON and the spec's values 5%; hanim's core and engine 3%; the terminal host and mrender.cells 3%; the rest — omui.core, layout, the display list's types, the components used — 10%. The second tier adds the endpoint: omui.agent, mlibs.agent, omui.semantic and openkal's rights table, 16% of its code, and the screenshot's PNG encoder.

| build of the first tier | stripped B |
|---|---:|
| dist (`-O3`), libstdc++ linked in | 4,162,920 |
| dist, the system's libstdc++ (what the member builds) | 3,392,536 |
| `-Oz` (the workspace's `web` profile), libstdc++ linked in | 2,705,160 |
| `-Oz`, the system's libstdc++ | 1,938,888 |

The terminal design's §7 write-up: the target is 1 MB; measured 3.39 MB (1.94 MB at `-Oz`); tried — the providers of F2 (no HarfBuzz, FreeType, shaper or font in the link), the system's C++ runtime, `-Oz`; not enough because the pixel parts are reached through omui.view (its effects by name in props and motion, its Image, hanim.visual's morphs) and std::format with std::locale is on the path — both in their owners' packages, not the terminal's (its host and cells are 3%); graduation when the view's pixel parts are behind providers (terminal design §9 step ③), the size check can build a size-optimised profile, and the first tier measures ≤ 1 MB. 2026-11-27, later: the size check builds the `small` profile (-Oz) and the view's pixel and motion parts read at slots; the first tier measures 1,047,242 B loaded — 47,242 B over. What is left and what would take it off: the `why` of [tests/size/terminal's budget](../../../size/terminal/size.budget.xdef.json) (openkal-linux's table of roots in .bss, mcpplibs/openkal-linux#61; the theme's framework motion defaults and the view's pixel effects at slots; std::format off the path; unwind tables only where an exception passes).
