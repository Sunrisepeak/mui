# 领先度（lead）

由 `tools/eval/lead.py summary` 从台账生成（2026-11-09）。判定规则见第二部分方案 §2.2。

## 指标

| 项目 | 指标 | 我方 | 最强竞品 | 比值 [95% 区间] | 判定 |
|---|---|---|---|---|---|
| hanim | motion.drive-1000.web.mean_ms | hanim-wasm 0.0285 | gsap 0.14106 | 4.94× [4.85, 4.98] | ahead |
| hanim | motion.drive-1000.web.p95_ms | hanim-wasm 0.0236 | gsap 0.06704 | 2.83× [2.48, 3.58] | ahead |
| hanim | motion.drive-1000.web.max_ms | hanim-wasm 1.4636 | gsap 3.89762 | 2.72× [2.48, 2.84] | ahead |
| hanim | motion.drive-10k.web.mean_ms | — | gsap, motion | — | planned |
| hanim | motion.document.bytes | — | lottie, rive | — | planned |
| hanim | motion.playback.ms | — | rive, lottie-web | — | planned |
| hanim | motion.gpu.morph-1m.ms | — | hanim-cpu | — | pending_hardware |
| xrender | raster.fills-1000.median_ms | xrender 8.903 | skia 11.611 | 1.30× [1.30, 1.33] | ahead |
| xrender | raster.gradients-200.median_ms | xrender 5.346 | skia 7.161 | 1.35× [1.34, 1.37] | ahead |
| xrender | raster.small-5000.median_ms | xrender 1.877 | resvg 7.429 | 3.96× [3.94, 4.01] | ahead |
| xrender | raster.strokes-500.median_ms | xrender 10.427 | skia 10.271 | 1.00× [0.98, 1.02] | par |
| xrender | raster.joins-300.median_ms | xrender 9.395 | skia 10.164 | 1.09× [1.07, 1.13] | par |
| xrender | raster.fills-1000.error | xrender 0.339 | resvg 0.627 | 1.85× [1.85, 1.85] | ahead |
| xrender | raster.gradients-200.error | xrender 0.28 | resvg 0.27 | 0.96× [0.96, 0.96] | par |
| xrender | raster.small-5000.error | xrender 0.188 | resvg 0.255 | 1.36× [1.36, 1.36] | ahead |
| xrender | raster.strokes-500.error | xrender 0.922 | resvg 0.741 | 0.80× [0.80, 0.80] | behind |
| xrender | raster.joins-300.error | xrender 0.383 | resvg 0.597 | 1.56× [1.56, 1.56] | ahead |
| xrender | raster.cjk-5000.median_ms | — | skia | — | planned |
| xrender | raster.web.submit-5000.ms | — | canvaskit | — | planned |
| xrender | raster.wasm.bytes | — | thorvg | — | planned |
| xrender | gpu.fills-1000.ms | — | vello, graphite | — | pending_hardware |
| omui | ui.cold-start.ms | — | slint, flutter | — | planned |
| omui | web.first-paint.mobile.ms | — | flutter-web, react-ssr | — | planned |
| omui | web.interactive.mobile.ms | — | flutter-web | — | planned |
| omui | web.first-load.bytes | — | flutter-web, react-ssr | — | planned |
| omui | ui.list-10k.p99_ms | — | flutter, compose | — | planned |
| omui | ui.memory-10k.bytes | — | flutter | — | planned |
| omui | ui.input-latency.frames | — | flutter, slint | — | pending_hardware |
| omui | ui.hot-reload.ms | — | flutter, react-vite | — | planned |
| omui | agent.ui-tasks.success | — | a11y-tree-agent, screenshot-agent | — | planned |
| omui | agent.ui-tasks.tokens | — | a11y-tree-agent, screenshot-agent | — | planned |
| omui | agent.connect.lines | — | webmcp | — | planned |
| omui | agent.build-ui.success | — | react-vite-playwright, flutter | — | planned |
| omui.components | components.canvas-1000.frame_ms | — | figma-like-dom | — | planned |
| xengine | physics.pyramid.step_ms | — | box2d-v3, rapier | — | planned |
| xengine | world.iterate-100k.ms | — | bevy | — | planned |
| xengine | cellular.4096.fps | — | noita-like | — | pending_hardware |
| xengine | agent.game-tasks.success | — | godot-mcp | — | planned |
| gongfu | rollback.8-frames.ms | — | ggrs | — | planned |
| mlibs | text.paragraph.ms | — | skparagraph | — | planned |
| mlibs | layout.flex-grid-5000.ms | — | taffy, yoga | — | planned |
| mlibs | markdown.html.web.median_ms | mlibs.markdown-wasm 8.151 | md4w 8.211 | 1.01× [0.99, 1.03] | par |
| mlibs | markdown.commonmark.pass | — | md4c, cmark-gfm | — | planned |
| mlibs | i18n.data.bytes | — | icu4x | — | planned |

## 标志性特性

| 项目 | 特性 | 主线 | 状态 | 对照判定 |
|---|---|---|---|---|
| omui | A 先预演，再授权：Agent 的操作先在分支里执行并画出来，人同意才提交 | use-safely | evidence | ahead |
| omui | B 用 AI 写 UI 最顺手：确定的像素与语义、像素到源码，写看改闭环 | build-reliably | evidence | ahead |
| omui | C 零代码的 Agent 接口：语义树自动投影为 MCP、WebMCP 的类型化操作，授权由 openas 判定 | use-safely | evidence | ahead |
| omui | D 人机同屏共在：Agent 的身体与意图可见，人随时接管，冲突有规则 | together | evidence | — |
| omui | omui.one-tree 一棵语义树驱动像素、读屏、Agent 和测试四种视图 | foundation | planned | — |
| omui | omui.universal-motion 通用动效：0 声明过渡，打断时速度连续 | foundation | planned | — |
| omui | omui.pixel-audit 像素可审查、可回放：CPU 像素五平台逐位，像素归属到节点 | foundation | planned | — |
| omui.components | omui.liquid-glass 真正液态的玻璃，所有平台（含 Web） | craft | planned | — |
| omui.components | omui.canvas-objects 组件即画布对象：缩小后依然是活的 | craft | planned | — |
| omui.components | omui.style-packs 风格包同时改变质感和动效配方 | craft | planned | — |
| omui.components | H.components Agent 批量做出带全部状态、动效、无障碍和契约的组件变体，人并排挑选和微调 | build-reliably | planned | — |
| hanim | F AI 产出可编辑的动画程序：读回计划、精确修改、确定、可嵌入 | build-reliably | evidence | — |
| hanim | H Agent 驱动的设计与动效工作台：分支方案、画面指认、参数微调 | build-reliably | evidence | — |
| hanim | hanim.one-engine 一个引擎服务 UI、视频和游戏，三处逐位一致 | craft | planned | — |
| hanim | hanim.morph 跨表示、拓扑对齐的形变 | craft | evidence | ahead |
| hanim | hanim.composable 可组合：叶子、组合、配方同一种值；配方 id 进入规范 | foundation | planned | — |
| xrender | B.pixels 为 Agent 审查而生的像素：像素归属到节点，带语义的矢量截图 | build-reliably | evidence | ahead |
| xrender | xrender.same-source CPU 与 GPU 同源：CPU 参考逐位，GPU 差 ≤ 1/255 | craft | evidence | — |
| xrender | xrender.web-first Web 优先：每次提交只跨一次 wasm 边界 | craft | planned | — |
| xengine | G Agent 是一等执行者：开发引擎、做游戏、试玩与平衡、游玩 | together | evidence | ahead |
| xengine | xengine.equal-play 人与 Agent 平等游玩：同一套感知和反应约束，逐位回放 | together | evidence | ahead |
| xengine | xengine.native 原生胜过适配：场景是数据、编辑是事务、模拟确定、离屏同源 | build-reliably | planned | — |
| xengine | xengine.rollback 全游戏可回滚、可分支 | foundation | planned | — |
| gongfu | gongfu.rollback-kernel 确定性回滚内核，可以原样拆出 | craft | evidence | ahead |
| gongfu | gongfu.characters-as-data 角色即数据，由 Agent 制作，诊断带行列号 | build-reliably | planned | — |
| gongfu | gongfu.sparring Agent 陪练：人类档约束，能解释自己的选择 | together | planned | — |
| gongfu | gongfu.replay-tutor 回放即教程：逐帧解释判定框、帧优势与取消 | together | planned | — |
| spec | spec.model 模型完整度：事务、预演、撤销、执行者、授权、存在、接管、确定性（openas + openmui） | use-safely | planned | — |
| spec | spec.second-implementation 第二个实现：只按规范文本写的 DOM 语义适配器 | craft | planned | — |
| spec | E 生成式 UI 的安全运行时（候选）：Agent 生成的界面以 omui 树渲染 | together | planned | — |

## 能力对照

| 能力 | 项目 | 竞品 | 判定 | 证据 |
|---|---|---|---|---|
| C: every application an MCP server from its semantic tree: typed operations with their openas effects, no tool code written | omui | webmcp: app-defined, playwright-mcp: partial, mcp-apps: app-defined | ahead | `tools/mcp/tests/mcp_test.cpp` |
| C: transactions, undo and dry_run as protocol operations, any application | omui | webmcp: app-defined, playwright-mcp: none, app-intents: partial, a2ui: none | ahead | `tools/mcp/tests/mcp_test.cpp` |
| C: actions only a person may take: refused to every request over the bus, whatever actor it names | omui | webmcp: partial, playwright-mcp: none, app-intents: partial | ahead | `tools/mcp/tests/mcp_test.cpp` |
| xengine.equal-play: an agent player at a declared human tier (reaction, rate, what it sees), its record replayed to the same checksum | xengine | godot: none, unity-ml-agents: partial | ahead | `showcase/widget-breakout/tests/agent_player_test.cpp` |
| B.pixels: a frame as SVG, every shape a group named by its node, what SVG cannot say a marked pixel island | xrender | skia: partial, resvg: none | ahead | `modules/projects/xrender/vector/tests/svg_test.cpp` |
| hanim.morph: topology-aligned morphs between shapes of different contour and hole counts, strokes and fills, CPU and GPU the same frames | hanim | gsap-morphsvg: partial, flubber: partial | ahead | `modules/projects/hanim/studio/tests/signature_test.cpp` |
| gongfu.rollback-kernel: fixed-point fighting kernel: 10,000 random-depth rollbacks byte-identical, extractable whole | gongfu | ggpo: app-defined, ggrs: app-defined | ahead | `modules/projects/gongfu/tests/gongfu_test.cpp` |
| A: any application's steps previewed before they are allowed: run unseen and taken back (no journal, no history, motion off), the pictures before and after and the semantic changes; a step that asks the person shown, not done | omui | webmcp: none, playwright-mcp: none, mcp-apps: none, app-intents: app-defined | ahead | `tools/mcp/tests/mcp_test.cpp` |
| B: a pixel to the node that drew it, the nodes it sits in, and the line of the program that built each (ui_drew), headless | omui | react-devtools: partial, flutter-devtools: partial, playwright-mcp: none | ahead | `tools/mcp/tests/mcp_test.cpp` |
| C: every omui page offers its operations as WebMCP tools where the browser has navigator.modelContext — no application code; what needs the person asked through the browser | omui | webmcp: app-defined, mcp-apps: app-defined, playwright-mcp: partial | ahead | `modules/projects/omui/host/web/tests/js/page.test.mjs` |
| G: an agent playtests a game headless at a person's tier, at many times real time, and reports where it is hard or tedious, each problem with a record that replays to the same bytes | xengine | godot-mcp: none, unity-ml-agents: partial, modl-ai: partial | ahead | `showcase/widget-breakout/tests/agent_player_test.cpp` |
