# 统一动效词汇：omui 动效槽与 hanim 配方的映射（B1）

2026-11-08。[官网重构与 Mui 方案](2026-11-08-site-redesign-mui-design.md) §11 检查点 B1 的设计，落实[顶层方案](2026-11-06-top-level-assessment-convergence-design.md) §6.4 第七轮补充。本文替代 §6.4 中“槽引用配方 ID”的笼统说法，给出具体的映射、归属和步骤。

## 1. 现状：两套都叫“配方”的词汇

| | 规范 `registry/motion.toml` 的 `[[recipe]]`（M-8） | hanim 的 L6 配方（`hanim::anim`） |
|---|---|---|
| 是什么 | UI 过渡：一组原语加一个时间（`default.panel` = 匹配、淡入淡出、`panel_lift`、`grow`，配 `motion.spring.gentle`） | 对象上的动画程序：`rise_in(object, 24)`、`pop_in`、`bounce`… 共 40 个 |
| 出现和消失的样子 | 原语里写死的常数（`omui.view` theme.cpp 的 `add_primitive`）：`lift` 升 8、`panel_lift` 升 20、`grow` 从 0.96、`check_grow` 从 0.72、`drop` 落 24、`fade` 在前 30% 淡入／后 60% 淡出、`press` 压 0.06 | 每个配方函数里写死的姿态：`rise_in` 从下方 24、0.4 s、`out_cubic`，配组透明度效果淡入 |
| 谁在用 | omui 的槽、风格包、组件默认值 | 动画文档（hanim.studio）、宣传片、官网的程序动画 |

同一种动作（“从下面升起并淡入”）有两份定义、两组常数，违背单一归属；风格包也无法说“这里用 hanim 的 `rise_in`”。

更准确地说，出现和消失的**算法**已经在 hanim 里：`hanim.visual`（`morph.cpp`）按进度计算一块绘制的升起、缩放和淡入淡出（`Options::rise`、`grow`、`fade_in`、`fade_out`、`drop`、`write`）。重复的是 hanim 内部的两份**定义**（visual 的参数化出现方式，和 L6 配方函数里各自的姿态），以及 omui 那组常数。所以统一主要发生在 hanim 内部：visual 的出现与消失改由课程驱动；omui 只把规范 ID 翻译成课程引用。

## 2. 归属：一份“出现／消失／强调”的课程，归 hanim

- **课程（course）**：一个配方相对静止姿态的姿态随进度的变化，即 0→1 的进度上的关键帧：位移、缩放（可带锚点）、旋转、不透明度、描边裁剪（书写）、压扁（落地），每段一条曲线。它只描述“怎么动”，不关心动的是对象还是 UI 的一块绘制。
- **hanim 拥有课程**：`hanim::anim::course(name, params)` 给出课程，名字和参数就是 L6 配方的名字和参数。L6 的对象配方改为由课程生成（`rise_in(object, …)` = 对象上播放 `course("rise_in", …)`），保证两边是同一份定义。
- **hanim.visual 采样课程**：UI 过渡中出现或消失的每一块绘制，由 `hanim.visual` 按它自己的进度采样它的课程，代替 `Options` 里分散的 `rise`／`grow`／`fade_in`／`fade_out` 等参数；omui 只负责把规范 ID 和参数翻译成课程引用，代替 `add_primitive` 里的常数。
- **规范登记 ID**：课程的名字作为规范级的 ID 登记进 `registry/motion.toml`（新的 `[[course]]` 表，参数与默认值写明），风格包和数据文件就能引用。UI 过渡的 `[[recipe]]` 保留，但它的出现／消失／强调部分改为引用课程，而不是原语常数。

## 3. 映射

### 3.1 原语 → 课程

| 规范原语 | 现在的常数 | 对应的课程 | 说明 |
|---|---|---|---|
| `fade` | 出现：前 30% 淡入；消失：前 60% 淡出 | `fade_in`、`fade_out`（加 `fade` 参数：淡入／淡出所占的进度比例） | 作为其他课程的透明度通道复用 |
| `lift` | 升 8 | `rise_in(8)`、`rise_out(8)` | |
| `panel_lift` | 升 20 | `rise_in(20)`、`rise_out(20)` | |
| `grow` | 从 0.96 | **新课程 `settle_in` / `settle_out`**（从 0.96 缩放到 1，不超调） | `pop_in` 会超调，不适合安静的 UI 出现；`settle_in` 是通用的，进 hanim 配方库 |
| `check_grow` | 从 0.72，按块自身 | `settle_in(0.72)`，锚点在块自身 | `local_presence` 对应课程的锚点 |
| `drop` | 落 24，`fall(0.45, 2)` | `drop_in(24)` | 已同源（`Ease::fall`） |
| `write` | 描边沿长度书写 | `write_in`、`unwrite` | 已同源（描边裁剪） |
| `press` | 压 0.06，随手指位置 | **新课程 `press`**（按压点为锚点压入，松开时弹回并振荡收敛） | hanim.visual 的 `pulse` 按压版；通用，进配方库 |
| `noise` | 平移路径 `jitter(3, 40, 7)` | 不是课程：是运动路径（`PointPath`），作为过渡的路径参数保留 | |

### 3.2 留在 UI 的（不是课程）

`match_drawing`、`match_glyphs`、`readable_text`、`stretch`（指示物沿路拉伸）、`reshape`（字形轮廓互变）、`stagger`、`distance`（编排顺序）：这些描述**同一对象在两个状态之间**怎么对应和过渡，是 UI 框架的匹配能力（顶层方案 §6.3），不进配方库。

### 3.3 新的槽写法（风格包与主题）

```toml
# 出现、消失、强调各引用一个课程及其参数；匹配部分不变
[motion.rule.toast]
update = "default.notice"
enter  = { course = "rise_in", distance = 8, also = "settle_in" }
exit   = { course = "fade_out" }

[motion.rule."role:switch"]
state  = { course = "press", depth = 0.06 }
```

- `also`：同时叠加第二个课程（升起＋缩放）；更多组合仍在 hanim 一侧用 `parallel` 组合。
- 时间：课程的时长与曲线来自**动效令牌**（下节），也可以直接写。

## 4. 动效与视觉令牌（主题）

| 令牌 | 默认 | 用途 |
|---|---|---|
| `motion.duration.xs/s/m/l/xl` | 120 / 180 / 260 / 360 / 450 ms | 时长阶梯；组件按大小和移动距离取档（官网方案 §3.1） |
| `motion.duration.exit_ratio` | 0.8 | 消失比出现快 |
| `motion.spring.snappy/gentle/…` | 已有 | 弹簧预设 |
| `motion.stagger` | 24 ms，总长上限 240 ms | 成组出现的错开 |
| `shadow.1–4` | 环境光＋主光两层，带物体色 | 分层阴影配方（B3） |
| `corner.smoothing` | 0（圆）… 0.6（iOS 风格） | 连续曲率圆角（B2） |
| `line.hairline` | 1 物理像素 | 发丝线 |

风格包可以覆盖任何令牌。令牌名同样登记在规范的令牌表中（`[[token]]` 已有这一机制）。

## 5. 步骤与验收

| 步 | 内容 | 验收 |
|---|---|---|
| 1 | hanim：`course` 与课程数据；L6 的出现／消失配方改由课程生成；新课程 `settle_in/out`、`press` | 对象配方的过程采样与改前**逐位一致**；新课程的过程帧 |
| 2 | 规范：`[[course]]` 表与参数；`[[recipe]]` 的出现／消失部分可以引用课程；令牌表新增 §4 的令牌 | openmui 规范仓 PR（`openmui-design` 分支）；生成的注册表文档 |
| 3 | hanim.visual：`Options` 的 `rise`/`grow`/`fade_in`/`fade_out`/`drop`/`write` 合并为出现、消失两个课程（`press` 为强调课程）；omui：`MotionSpec` 相应合并，`add_primitive` 的这些原语改为生成课程引用（参数取现在的常数）；解析新的槽写法 | **这一步不改任何像素**：常数和数学与现在相同，所有钉住值不变（与描边、宣传片两次迁移的做法相同） |
| 4 | 删除被取代的字段和常数，不留兼容层；`motion` lint 检查 UI 代码不再自写出现／消失的常数 | lint 通过 |
| 5 | 之后的质感调整（B4–B6）只改令牌和课程参数 | 改动可追溯到具体令牌 |
