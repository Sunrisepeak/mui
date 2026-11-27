# tests/size

Programs whose weight is what is measured: each the least that does one thing, built as a release is, its bytes held to a budget beside it.

| | |
|---|---|
| [yard](yard) | `omui.size.yard`: the least program that shows a 3D scene on a page — a yard drawn once by `mrender.mesh.gpu` on the page's WebGPU or WebGL2, with no interface. `omui-devtools check size` holds it to `yard/size.budget.xdef.json` (610,306 B of Wasm, 65,797 B of JavaScript on 2026-11-14) |
| [terminal](terminal) | `omui.size.terminal`: the least application in a terminal — a count and two buttons, the window's components in character cells, no agent endpoint (terminal design §5, the first tier). `omui-devtools check size --native` builds it size-optimised (the `small` profile its budget's row names: -Oz, as the terminal design states the tiers) and holds it to `terminal/size.budget.xdef.json`, 1 MB: 1,040,662 B on 2026-11-27, over it (3,382,445 B at -O3 on 2026-11-26; beside it: [ratatui, FTXUI, Ink](../bench/competitors/terminal)). Its C++ runtime is the system's on Linux and Windows, stated per target (`[target.<triple>] cxx_runtime`); on macOS, where mcpp's LLVM compiles against its own libc++ headers and the system's libc++ lacks what they declare, mcpp's default (red 161; to confirm on macOS) |
| [terminal-agent](terminal-agent) | `omui.size.terminal-agent`: the same with its agent endpoint (the second tier), held to 2 MB at the same profile: 1,835,968 B on 2026-11-27, within it (5,183,427 B at -O3 on 2026-11-26); its runtime stated as terminal's |
