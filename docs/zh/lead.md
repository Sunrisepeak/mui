# 领先度：和最强的比

每个指标都是我们对最强竞品，在同一台机器、同一个作业里交替测量；判定看比值的 95% 区间：低于 0.95× 为落后，到 1.2× 为持平，1.2× 起为领先，10× 起或竞品做不到为远超。落后的照样列在这里；没有设备测的如实写明。由领先度台账生成（2026-11-27）。

## 指标

| 项目 | 指标 | 我方 | 最强竞品 | 比值 [95% 区间] | 判定 |
|---|---|---|---|---|---|
| hanim | `motion.drive-1000.web.mean_ms` | hanim-wasm 0.0564 | gsap 0.26535 | 4.72× [4.59, 4.79] | **ahead** |
| hanim | `motion.drive-1000.web.p95_ms` | hanim-wasm 0.0626 | gsap 0.12313 | 1.91× [1.86, 2.14] | **ahead** |
| hanim | `motion.drive-1000.web.max_ms` | hanim-wasm 5.448 | gsap 10.539 | 1.88× [1.69, 2.05] | **ahead** |
| hanim | `motion.drive-10k.web.mean_ms` | — | gsap, motion | — | 计划中 |
| hanim | `motion.document.bytes` | — | lottie, rive | — | 计划中 |
| hanim | `motion.playback.ms` | — | rive, lottie-web | — | 计划中 |
| hanim | `motion.gpu.morph-1m.ms` | — | hanim-cpu | — | 待真机验证 |
| hanim | `playback.lottie-200.median_ms` | hanim 2.099 | thorvg 1.666 | 0.79× [0.79, 0.80] | **behind** |
| mrender | `raster.fills-1000.median_ms` | mrender 18.158 | skia 23.147 | 1.27× [1.25, 1.28] | **ahead** |
| mrender | `raster.gradients-200.median_ms` | mrender 11.723 | skia 11.632 | 0.99× [0.99, 1.00] | **par** |
| mrender | `raster.small-5000.median_ms` | mrender 3.867 | resvg 12.905 | 3.35× [3.34, 3.38] | **ahead** |
| mrender | `raster.strokes-500.median_ms` | mrender 21.689 | skia 19.67 | 0.92× [0.91, 0.93] | **behind** |
| mrender | `raster.joins-300.median_ms` | mrender 19.87 | skia 19.551 | 0.99× [0.97, 0.99] | **par** |
| mrender | `raster.fills-1000.error` | mrender 0.339 | resvg 0.627 | 1.85× [1.85, 1.85] | **ahead** |
| mrender | `raster.gradients-200.error` | mrender 0.28 | resvg 0.27 | 0.96× [0.96, 0.96] | **par** |
| mrender | `raster.small-5000.error` | mrender 0.188 | resvg 0.255 | 1.36× [1.36, 1.36] | **ahead** |
| mrender | `raster.strokes-500.error` | mrender 0.922 | resvg 0.741 | 0.80× [0.80, 0.80] | **behind** |
| mrender | `raster.joins-300.error` | mrender 0.383 | resvg 0.597 | 1.56× [1.56, 1.56] | **ahead** |
| mrender | `raster.small-5000.exact_error` | mrender 0.034 | resvg 0.187 | 5.50× [5.50, 5.50] | **ahead** |
| mrender | `raster.strokes-500.exact_error` | mrender 0.238 | resvg 0.839 | 3.53× [3.53, 3.53] | **ahead** |
| mrender | `raster.cjk-5000.median_ms` | — | skia | — | 计划中 |
| mrender | `raster.web.submit-5000.ms` | — | canvaskit | — | 计划中 |
| mrender | `raster.wasm.bytes` | — | thorvg | — | 计划中 |
| mrender | `gpu.fills-1000.ms` | — | vello, graphite | — | 待真机验证 |
| omui | `ui.cold-start.ms` | — | slint, flutter, nsh | — | 计划中 |
| omui | `web.first-paint.mobile.ms` | — | flutter-web, react-ssr | — | 计划中 |
| omui | `web.interactive.mobile.ms` | — | flutter-web | — | 计划中 |
| omui | `web.first-load.bytes` | — | flutter-web, react-ssr, nsh | — | 计划中 |
| omui | `ui.list-10k.p99_ms` | — | flutter, compose | — | 计划中 |
| omui | `ui.memory-10k.bytes` | — | flutter | — | 计划中 |
| omui | `ui.input-latency.frames` | — | flutter, slint, nsh | — | 待真机验证 |
| omui | `ui.hot-reload.ms` | — | flutter, react-vite, nsh | — | 计划中 |
| omui | `agent.ui-tasks.success` | — | a11y-tree-agent, screenshot-agent | — | 计划中 |
| omui | `agent.ui-tasks.tokens` | — | a11y-tree-agent, screenshot-agent | — | 计划中 |
| omui | `agent.connect.lines` | — | webmcp | — | 计划中 |
| omui | `agent.build-ui.success` | — | react-vite-playwright, flutter | — | 计划中 |
| omui | `core.live-dashboard.frame.p95_ms` | — | nsh | — | 计划中 |
| omui | `core.list-scroll.frame.p95_ms` | — | nsh | — | 计划中 |
| omui | `core.rest.cpu_percent` | omui-app 0.39913 | flutter 0 | 0.00× [0.00, 0.00] | **behind** |
| omui | `ui.start.hello.cold.ms` | omui-app 409.716 | slint 242.495 | 0.58× [0.53, 0.64] | **behind** |
| omui | `ui.start.hello.warm.ms` | omui-app 354.897 | slint 250.892 | 0.71× [0.63, 0.81] | **behind** |
| omui | `ui.click-to-frame.counter.ms` | omui-app 7.48333 | slint 6.72734 | 0.94× [0.51, 1.10] | **behind** |
| omui | `core.memory.hello.uss.bytes` | omui-app 7.4498e+07 | slint 3.54017e+07 | 0.48× [0.47, 0.48] | **behind** |
| omui | `core.memory.hello.pss.bytes` | omui-app 7.95412e+07 | slint 4.12134e+07 | 0.52× [0.52, 0.52] | **behind** |
| omui | `core.rest.wakeups_per_s` | omui-app 113.8 | flutter 0 | 0.00× [0.00, 0.00] | **behind** |
| omui | `ui.live-board.present.p95_ms` | omui-app 17.0479 | nsh-ts 38.1661 | 2.15× [1.85, 2.30] | **ahead** |
| omui | `ui.live-board.cpu-per-frame.ms` | omui-app 3.23391 | nsh-ts 8.15129 | 2.12× [1.83, 2.85] | **ahead** |
| omui | `ui.list-10k.scroll.present.p95_ms` | omui-app 8.74651 | slint 16.9997 | 1.94× [1.81, 2.00] | **ahead** |
| omui | `ui.list-10k.scroll.cpu-per-frame.ms` | omui-app 7.32389 | slint 1.5244 | 0.23× [0.17, 0.29] | **behind** |
| omui | `core.engine-start.us` | — | nsh | — | 计划中 |
| omui | `core.first-frame.counter.ms` | — | nsh | — | 计划中 |
| omui | `core.tap-to-commit.ms` | — | nsh | — | 计划中 |
| omui | `core.memory.hello.private-dirty.bytes` | omui-app 2.10432e+07 | slint 1.92102e+07 | 0.91× [0.91, 0.91] | **behind** |
| omui | `core.size.hello.executable.bytes` | omui-app 1.72139e+07 | slint 1.74696e+07 | 1.01× [1.01, 1.01] | **par** |
| omui | `core.size.hello.package.bytes` | omui-app 8.14642e+06 | slint 8.62023e+06 | 1.06× [1.06, 1.06] | **par** |
| omui | `core.size.hello.app-code.bytes` | — | nsh | — | 计划中 |
| omui | `terminal.size.counter.program.bytes` | omui-terminal 1.05344e+06 | ftxui 334208 | 0.32× [0.32, 0.32] | **behind** |
| omui | `terminal.size.counter.runtime.bytes` | omui-terminal 1.05344e+06 | ftxui 334208 | 0.32× [0.32, 0.32] | **behind** |
| omui.components | `components.canvas-1000.frame_ms` | — | figma-like-dom | — | 计划中 |
| meng | `physics.pyramid.step_ms` | — | box2d-v3, rapier | — | 计划中 |
| meng | `world.iterate-100k.ms` | — | bevy | — | 计划中 |
| meng | `cellular.4096.fps` | — | noita-like | — | 待真机验证 |
| meng | `agent.game-tasks.success` | — | godot-mcp | — | 计划中 |
| gongfu | `rollback.8-frames.ms` | — | ggrs | — | 计划中 |
| mlibs | `text.paragraph.ms` | — | skparagraph | — | 计划中 |
| mlibs | `layout.flex-grid-5000.ms` | — | taffy, yoga | — | 计划中 |
| mlibs | `markdown.html.web.median_ms` | mlibs.markdown-wasm 21.336 | md4w 20.788 | 0.97× [0.96, 0.98] | **par** |
| mlibs | `markdown.commonmark.pass` | — | md4c, cmark-gfm | — | 计划中 |
| mlibs | `i18n.data.bytes` | — | icu4x | — | 计划中 |
| oweb | `site.build.ms` | oweb 26.957 | mdbook 40.5365 | 1.50× [1.44, 1.54] | **ahead** |
| oweb | `site.route.transfer.bytes` | oweb 2424 | starlight 53891 | 22.23× [22.23, 22.23] | **far_ahead** |
| oweb | `site.route.blocking.bytes` | oweb 2535.5 | starlight 18389.5 | 7.25× [7.25, 7.25] | **ahead** |
| oweb | `site.route.requests` | oweb 1 | starlight 10 | 10.00× [10.00, 10.00] | **far_ahead** |
| oweb | `site.route.fcp.mobile.ms` | oweb 238 | starlight 474 | 1.98× [1.98, 2.15] | **ahead** |
| oweb | `site.route.lcp.mobile.ms` | oweb 238 | starlight 496 | 2.08× [2.07, 2.54] | **ahead** |
| oweb | `site.written.bytes` | oweb 91025 | mdbook 1.12953e+06 | 12.41× [12.41, 12.41] | **far_ahead** |
| oweb | `site.engine.route.transfer.bytes` | oweb-engine 2.45363e+06 | starlight 53891 | 0.02× [0.02, 0.02] | **behind** |
| oweb | `site.engine.build.ms` | oweb-engine 688.912 | mdbook 40.5365 | 0.06× [0.06, 0.07] | **behind** |
| oweb | `site.scale.build.ms` | oweb 1277.11 | mdbook 4256.81 | 3.37× [2.43, 3.39] | **ahead** |
| oweb | `site.scale.page.sent.bytes` | oweb 2638 | mdbook 5791 | 2.20× [2.20, 2.20] | **ahead** |
| oweb | `site.scale.written.bytes` | oweb 1.54278e+07 | mdbook 3.13308e+07 | 2.03× [2.03, 2.03] | **ahead** |

## 标志性特性

| 项目 | 特性 | 状态 | 判定 |
|---|---|---|---|
| omui | A: 先预演，再授权：Agent 的操作先在分支里执行并画出来，人同意才提交 | evidence | ahead |
| omui | B: 用 AI 写 UI 最顺手：确定的像素与语义、像素到源码，写看改闭环 | evidence | ahead |
| omui | C: 零代码的 Agent 接口：语义树自动投影为 MCP、WebMCP 的类型化操作，授权由 openas 判定 | evidence | ahead |
| omui | D: 人机同屏共在：Agent 的身体与意图可见，人随时接管，冲突有规则 | evidence | — |
| omui.components | omui.liquid-glass: 真正液态的玻璃，所有平台（含 Web） | planned | — |
| omui.components | omui.canvas-objects: 组件即画布对象：缩小后依然是活的 | planned | — |
| omui.components | omui.style-packs: 风格包同时改变质感和动效配方 | planned | — |
| omui.components | H.components: Agent 批量做出带全部状态、动效、无障碍和契约的组件变体，人并排挑选和微调 | planned | — |
| hanim | F: AI 产出可编辑的动画程序：读回计划、精确修改、确定、可嵌入 | evidence | — |
| hanim | H: Agent 驱动的设计与动效工作台：分支方案、画面指认、参数微调 | evidence | — |
| hanim | hanim.one-engine: 一个引擎服务 UI、视频和游戏，三处逐位一致 | planned | — |
| hanim | hanim.morph: 跨表示、拓扑对齐的形变 | evidence | ahead |
| mrender | B.pixels: 为 Agent 审查而生的像素：像素归属到节点，带语义的矢量截图 | evidence | ahead |
| mrender | mrender.same-source: CPU 与 GPU 同源：CPU 参考逐位，GPU 差 ≤ 1/255 | evidence | — |
| mrender | mrender.web-first: Web 优先：每次提交只跨一次 wasm 边界 | planned | — |
| meng | G: Agent 是一等执行者：开发引擎、做游戏、试玩与平衡、游玩 | evidence | ahead |
| meng | meng.equal-play: 人与 Agent 平等游玩：同一套感知和反应约束，逐位回放 | evidence | ahead |
| meng | meng.native: 原生胜过适配：场景是数据、编辑是事务、模拟确定、离屏同源 | planned | — |
| gongfu | gongfu.rollback-kernel: 确定性回滚内核，可以原样拆出 | evidence | ahead |
| gongfu | gongfu.characters-as-data: 角色即数据，由 Agent 制作，诊断带行列号 | planned | — |
| gongfu | gongfu.sparring: Agent 陪练：人类档约束，能解释自己的选择 | planned | — |
| gongfu | gongfu.replay-tutor: 回放即教程：逐帧解释判定框、帧优势与取消 | planned | — |
| oweb | oweb.one-content: 一份内容三种读法：画布、HTML、Markdown 是同一棵语义树的三种写法，互不丢失 | evidence | — |
| oweb | oweb.agent-door: Agent 与人走同一个地址：读 llms.txt 与每页的 Markdown，查同一份搜索索引，或连到页面 | planned | — |
| spec | spec.model: 模型完整度：事务、预演、撤销、执行者、授权、存在、接管、确定性（openas + openmui） | planned | — |
| spec | spec.second-implementation: 第二个实现：只按规范文本写的 DOM 语义适配器 | planned | — |
| spec | E: 生成式 UI 的安全运行时（候选）：Agent 生成的界面以 omui 树渲染 | planned | — |

## 能力对照

| 能力 | 竞品 | 判定 |
|---|---|---|
| every application an MCP server from its semantic tree: typed operations with their openas effects, no tool code written | webmcp (app-defined), playwright-mcp (partial), mcp-apps (app-defined) | **ahead** |
| transactions, undo and dry_run as protocol operations, any application | webmcp (app-defined), playwright-mcp (none), app-intents (partial), a2ui (none) | **ahead** |
| actions only a person may take: refused to every request over the bus, whatever actor it names | webmcp (partial), playwright-mcp (none), app-intents (partial) | **ahead** |
| an agent player at a declared human tier (reaction, rate, what it sees), its record replayed to the same checksum | godot (none), unity-ml-agents (partial) | **ahead** |
| a frame as SVG, every shape a group named by its node, what SVG cannot say a marked pixel island | skia (partial), resvg (none) | **ahead** |
| topology-aligned morphs between shapes of different contour and hole counts, strokes and fills, CPU and GPU the same frames | gsap-morphsvg (partial), flubber (partial) | **ahead** |
| fixed-point fighting kernel: 10,000 random-depth rollbacks byte-identical, extractable whole | ggpo (app-defined), ggrs (app-defined) | **ahead** |
| any application's steps previewed before they are allowed: run unseen and taken back (no journal, no history, motion off), the pictures before and after and the semantic changes; a step that asks the person shown, not done | webmcp (none), playwright-mcp (none), mcp-apps (none), app-intents (app-defined) | **ahead** |
| a pixel to the node that drew it, the nodes it sits in, and the line of the program that built each (ui_drew), headless | react-devtools (partial), flutter-devtools (partial), playwright-mcp (none) | **ahead** |
| every omui page offers its operations as WebMCP tools where the browser has navigator.modelContext — no application code; what needs the person asked through the browser | webmcp (app-defined), mcp-apps (app-defined), playwright-mcp (partial) | **ahead** |
| an agent playtests a game headless at a person's tier, at many times real time, and reports where it is hard or tedious, each problem with a record that replays to the same bytes | godot-mcp (none), unity-ml-agents (partial), modl-ai (partial) | **ahead** |
