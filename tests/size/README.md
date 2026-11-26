# tests/size

Programs whose weight is what is measured: each the least that does one thing, built as a release is, its bytes held to a budget beside it.

| | |
|---|---|
| [yard](yard) | `omui.size.yard`: the least program that shows a 3D scene on a page — a yard drawn once by `mrender.mesh.gpu` on the page's WebGPU or WebGL2, with no interface. `omui-devtools check size` holds it to `yard/size.budget.xdef.json` (610,306 B of Wasm, 65,797 B of JavaScript on 2026-11-14) |
| [terminal](terminal) | `omui.size.terminal`: the least application in a terminal — a count and two buttons, the window's components in character cells, no agent endpoint (terminal design §5, the first tier). `omui-devtools check size --native` holds it to `terminal/size.budget.xdef.json`, 1 MB: 3,382,445 B on 2026-11-26, over it (beside it: [ratatui, FTXUI, Ink](../bench/competitors/terminal)) |
| [terminal-agent](terminal-agent) | `omui.size.terminal-agent`: the same with its agent endpoint (the second tier), held to 2 MB: 5,183,427 B on 2026-11-26, over it |
