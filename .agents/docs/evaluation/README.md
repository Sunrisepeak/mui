# openmui 的评估标准与评分记录

维护者的要求（2026-10-31）：评分标准要细化，并保留评估和评分记录。每个评估对象要和它所在领域的顶尖做法比：
- UI 框架对标主流 UI 框架；
- 引擎对标主流游戏引擎；
- 动画对标最好的动画引擎；
- 每个作品和官网对标各自领域里最好的作品。

评估要从多个角度按统一标准打分，逐项和该领域的前 3 名对比，并给出反馈。所有作品每一项都要达到 90 分以上。

这里取代各处临时写的评分提示。[格斗游戏的验收评分表](../2026-10-29-fighting-acceptance-rubric.md) 的 11 项并入了格斗游戏的维度（`subjects/fighting.md`）。

## 1. 分数和锚点

每个维度 0–100 分。分数相对于这个维度上对标的前 3 名：

| 分数 | 含义 |
|---|---|
| 95–100 | 定义标准：这一维度超过前 3 名中的每一个，有证据 |
| 90–94 | 与前 3 名中最好的那个持平：内行逐项看过，提不出该改的地方 |
| 80–89 | 专业水准，但明显落后于最好的那个；发布前还有一张修改清单 |
| 70–79 | 能用的原型，和前 3 名放在一起一眼能看出差距 |
| 60–69 | 粗糙，或只有骨架 |
| < 60 | 缺失，或明显不对 |

一个对象有两个分数：
- **总评**取最低的那个维度，它是门槛，每一项都要到 90 才算过；
- **均分**是所有维度的平均，用来看进展。

## 2. 每个维度要交什么

每个维度的评分都要包含：

1. **分数**。
2. **依据**，证据分四种，评分者要标明是哪一种：
   - 使用：评分者亲手用过——程序在跑，评分者经 openmui 的 MCP 工具像人一样操作（`ui_input`：点击、拖动、滚轮、按键、输入文字，走真实的输入路径：命中测试、焦点、手势），也像 agent 一样操作（`ui_act` / `ui_invoke`：按 openas 带上 actor、效果和需要的答复），再截图核对结果；
   - 实测：跑出来的数字、测出来的结果；
   - 查看：读过的代码（路径加行号），或看过的图（文件名加画面上的位置）；
   - 推断：从代码推断、还没有运行验证，这种分数标为暂定。
3. **和前 3 名逐个对比**：领先、持平或落后，各用一句话说明在哪里。
4. **到 90 分要改什么**：具体、可以动手做，按对分数的影响排序。

人会直接碰到的维度（交互、可用性、上手、动效、体感性能）必须有“使用”证据。没有的话分数是暂定的，最高 70。

## 3. 每份报告的结构

1. `Overall: N/100` 和 `Mean: M/100`。
2. **使用记录**（对象有 Run 列表时）：一张表，列出任务、怎么做的（简要的工具调用）、预期、实际、通过或失败；再列出用的时候发现的每个缺陷，写明位置和复现步骤；最后是“框架和 openas 的反馈”：用的过程中碰到的框架、引擎和 openas 能力，哪些好用，哪些缺失或不对，建议加什么。
3. 维度表：维度、分数、证据类型、依据、和前 3 名的对比、到 90 要改什么。
4. **对标总结**：前 3 名各自最强的是什么；我们在哪些地方领先，在哪些地方落后最多。
5. **相对上一次的变化**：评分者拿到上一次的记录，逐维度说明涨跌和原因；第一次评估时省略。
6. **最优先的修改**（7–10 条）。
7. **细节缺陷**：每一个看到的具体小问题，写明在哪个文件或画面的哪个位置。
8. 最后一个代码块，用来写入台账：

```json
{"overall": 0, "mean": 0, "dimensions": {"<维度名>": 0}}
```

## 4. 流程

- **对象和维度**：每个对象一份 `subjects/<对象>.md`，写明它是什么、读哪些文件、维度（每个维度下的细项）、对标的前 3 名（以及只在某些维度上参考的项目）、要附哪些图。
- **使用**：对象文件的 `**Run:**` 列出要启动的程序（``- `程序 参数` (包)``），`**Use:**` 列出评分者要亲手做的任务。`eval.sh` 先用 `--profile release` 构建这些程序，再以 `--serve --agent-input` 在无头模式下启动它们。每个程序在一个临时目录里提供 agent 端点，codex 通过 `omui mcp` 操作它们。用框架写的应用默认就有这些能力：agent 端点，以及 agent 像人的设备那样输入（`AppSpec::agent_input`，开发者可以关掉，运行时用 `--no-agent-input` 关掉）。这些输入做的事按 openas 算作 agent 自己的（`agent:codex-evaluator;via=service:input`），按它的请求同样裁决：人自己的动作拒绝（S-10），受上限和授权约束（S-11），不可撤销的动作要人的答复。所以评分者在评一个对象的同时，也在评框架、引擎和 openas 的能力，报告里单列这部分反馈。
- **运行**：`tools/eval/eval.sh <对象> [图…]` 把本文第 1–3 节、对象文件和上一次的台账记录拼成提示，用 codex 只读运行。报告存进 `reports/<日期>-<对象>.md`，分数追加到 `ledger.eval.omui.jsonl`，一次评估一行：

  ```json
  {"date":"…","subject":"…","evaluator":"codex","commit":"…","overall":…,"mean":…,"dimensions":{…},"report":"reports/…"}
  ```

- **评分者**：codex 是一个独立来源，不是裁判。它的意见要和项目的规则、实测数据一起权衡。每次评估之后，在报告末尾的“维护方的处理”里写清楚：
  - 采纳了什么；
  - 没有采纳什么，为什么；
  - 哪些分数需要实测才能确认。
- **频率**：一轮修改完成后对相关对象重评。只看分数，不追着单次波动改；主观项（设计、艺术表达）至少看两次评估的趋势。

## 5. 评估对象和对标

| 对象 | 是什么 | 前 3 名 | 只在部分维度参考 |
|---|---|---|---|
| `ui-framework` | openmui 框架：运行时、语义树、组件、宿主、工具 | Flutter、Qt 6（Qt Quick / QML）、Jetpack Compose（含 Compose Multiplatform） | SwiftUI、Slint、egui、React Native |
| `renderer` | 渲染：`render`、`render/vector`、`raster`、`rhi`、`shader` | Skia（Graphite）、Vello、Blend2D | ThorVG、Rive Renderer、Impeller |
| `animation` | 动画引擎 `hanim` 和它在组件里的用法 | Rive（运行时和状态机）、GSAP、Lottie | Spine（骨骼）、Framer Motion、Core Animation |
| `game-engine` | `mgame` 和 `gongfu`（作为引擎和格斗框架） | Unreal Engine 5、Unity 6、Godot 4 | Bevy（代码优先、ECS）；GGPO 及同类回滚框架（格斗部分） |
| `site` | 官网 `site/` | linear.app、vercel.com、rive.app | bun.sh、tldraw.com、stripe.com |
| `fighting` | 格斗游戏 `showcase/fighting` | Street Fighter 6、Guilty Gear -Strive-、Skullgirls 2nd Encore | Them's Fightin' Herds、Pocket Bravery（独立游戏的可达水准） |
| `widget-breakout` | 《组件打砖块》 | Peggle Deluxe、Baba Is You、Breakout: Recharged | There Is No Game（界面即玩法） |
| `markdown-wall` | 文档墙 | Figma / FigJam、tldraw、Obsidian Canvas | Miro、Heptabase |
| `style-gallery` | 风格包和风格工坊 | Apple 的 Liquid Glass（系统级材质）、Material Theme Builder、tweakcn（shadcn/ui 主题编辑器） | Figma Variables、Tokens Studio |
| `promo-video` | 宣传片和 `omui-promo` | Apple 的产品片（如 Vision Pro 发布片）、Linear 的发布片、Stripe Sessions 的开场片 | Vercel Ship、Figma Config 开场 |

## 6. 记录

- [台账](ledger.eval.omui.jsonl)：每次评估一行。
- `reports/`：原始报告和维护方的处理。
- [第一轮汇总](reports/2026-10-31-round-1.md)：10 个对象的分数、采纳和不采纳的、排进后面轮次的。
- [第二轮汇总](reports/2026-10-31-round-2.md)：评分者亲手使用（v2），用出来的问题和处理。
- [第三轮汇总](reports/2026-10-31-round-3.md)：文档墙复评、打砖块、两个 codex 用回合制对战格斗游戏。
- [第四轮汇总](reports/2026-10-31-round-4.md)：复评；按 SF6 标准逐帧评格斗游戏（fighting-presentation）；调研和决定（D-90），调研报告在 [research/](research/)。

2026-10-31 是第一轮：
- 五个作品、官网和框架的首评用的是细化之前的提示，结果按同样的格式补进了台账（标为 `rubric: "v0"`）；
- 细化之后的标准从 `v1` 开始。
