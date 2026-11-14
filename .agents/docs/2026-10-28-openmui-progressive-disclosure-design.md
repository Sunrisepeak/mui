# openmui 渐进式披露：帧需求与帧节奏、晋升策略、构建期标记与构建插件

> 状态：草案 v1.0（2026-10-28），待 review
> 依据：[architecture](2026-10-26-openmui-architecture-design.md)、[特色与管线](2026-10-27-openmui-features-plugins-pipeline-design.md)（D-65、D-66）、[canvas-markdown](2026-10-26-openmui-canvas-markdown-design.md) §3、[component-binding](2026-10-26-openmui-component-binding-design.md) §2、§11
> 新决策：D-69 到 D-73

---

## 0. 原则：每项能力都分层暴露（D-69）

openmui 的每一项能力都从“什么都不写”开始，逐层开放控制：

| 层级 | 写什么 | 面向谁 |
|---|---|---|
| **L0 零配置** | 什么都不写，运行时根据它已经知道的事情做出合理选择 | 所有人 |
| **L1 声明式提示** | 一个属性、一个字段、一个标记：说“要什么”，不说“怎么做” | 应用开发者 |
| **L2 编程接口** | 带生命周期的对象（RAII 租约、回调、策略函数） | 组件作者、性能敏感的应用 |
| **L3 插件** | 替换整个策略（管线的 stage 插件、宿主插件，D-65 / D-66） | 框架和平台的扩展者 |

三条规则：

1. **只有一套机制。** L1 是 L2 的简写，L2 是 L3 默认插件的参数。不存在“简单模式”和“高级模式”两套实现，所以从 L1 换到 L2 时行为不会突变。
2. **每一层都可观测。** 任何一层做出的决定都能在 trace、HUD、inspector 里看到，并且带着原因，例如“为什么是 120Hz：camera spring”“为什么这个节点还没晋升：相机移动中”。
3. **随时可以下沉。** 任何一个 L1 写法都能原样改写成 L2，不需要推翻重来；反过来，L2 的常见用法应该沉淀成 L1。

下面四项能力都按这个原则设计。

---

## 1. 帧需求与帧节奏：软硬件协同（D-70）

### 1.1 为什么框架能做好

openmui 的动画是时间的纯函数（D-60），运行时在每一帧都知道**为什么**要画：

- spring、甩动惯性或补间还在动；
- 有输入：指针、滚轮、按键、手势；
- 有定时器到期（光标闪烁、进度）；
- 有流式内容在进来（Markdown 尾部追加、视频帧）；
- 或者什么都没有：不画（现在已经做到“没变化就不呈现”）。

因此，刷新率可以由框架**按需求**决定，而不是固定 60Hz 或一直 120Hz 空转。

### 1.2 FrameDemand

```cpp
struct FrameDemand {
    double      min_hz = 0;        // 低于它会看出卡顿
    double      max_hz = 0;        // 0：显示器能给的最高
    double      preferred_hz = 0;  // 0：由 min/max 决定
    std::string reason;            // 进 trace 和 HUD："camera spring"、"video 24 fps"
};
```

每一帧，运行时把所有来源的需求合并成一个区间，交给宿主：

| 来源 | 默认需求（L0） |
|---|---|
| 输入（拖动、滚动、缩放、手势）进行中 | 显示器最高 |
| spring / 甩动惯性 / 补间 | 显示器最高 |
| 缓慢变化（光标闪烁、进度条、时钟） | 按它自己的节奏（例如 2Hz） |
| 视频播放 | 片源帧率的整数倍（24 → 48 / 72 / 120），避免 3:2 拉帧 |
| 流式文本 | 30–60Hz 就够，追加内容不需要 120Hz |
| 空闲 | 0：不画，显示器降到最低档 |

### 1.3 分层接口

| 层级 | 写法 |
|---|---|
| L0 | 什么都不写，按上表自动决定 |
| L1 | `Transition{.frame_rate = FrameRate::smooth}`（`smooth` / `low` / 数字）；组件属性 `frame_rate = "video:24"`；`AppSpec.power = balanced \| performance \| saver` |
| L2 | `auto lease = services().frame_demand({.min_hz = 90, .reason = "scrub"});`，租约析构即撤销；`Runtime::frame_demand()` 读出当前合并结果 |
| L3 | 帧节奏是管线里的 `frame.pacer` 插件：输入是合并后的需求、显示器信息和呈现反馈，输出是下一帧的目标呈现时间和对平台的刷新率请求 |

### 1.4 按预测的上屏时间求值

一帧不再按“开始画的时刻”求值，而是按“预计显示到屏幕上的时刻” `t_present` 求值。因为动画是 `f(t)`，这一点没有额外成本。

- 刷新率变化时（120 → 80 → 120）和 VRR 下，相邻两帧的运动距离和真实时间间隔一致，不会抖。
- `t_present` 由呈现反馈不断校准：Metal 的 drawable `presentedTime`，Vulkan 的 `VK_KHR_present_id` / `present_wait`（或 `VK_GOOGLE_display_timing`），DXGI 的 `GetFrameStatistics`，Wayland 的 `wp_presentation`。
- 没有反馈的平台退回到“开始时刻 + 一个刷新周期”。

### 1.5 平台映射

宿主的 native 分区负责（AGENTS：第三方 / 平台接口只在 native 分区）：

| 平台 | 机制 |
|---|---|
| macOS / iOS | `CAMetalDisplayLink`（macOS 14+）或 `CADisplayLink` 的 `preferredFrameRateRange`：ProMotion 在 120Hz 和低档之间切换 |
| Windows | DXGI flip model + `DXGI_PRESENT_ALLOW_TEARING`，配合 G-Sync / FreeSync 的 VRR；`DwmFlush` / waitable swapchain 控制节奏 |
| Android | `ANativeWindow_setFrameRate` / `Surface.setFrameRate`（兼容性档位：`fixed-source` 给视频） |
| Wayland | `wp_commit_timing`、`wp_fifo`、`wp_presentation` |
| X11 / 其他 | 固定刷新率，只做“空闲不画”和按需降低提交频率 |

SDL3 没有暴露这些接口，所以它们放在 `modules/ui/host/sdl3` 和 `modules/gfx/rhi/sdl` 的 native 分区，通过 SDL 拿到原生窗口句柄后调用。

### 1.6 可观测与测试

- trace 类别 `frame.pacing`：请求的区间、原因、实际上屏间隔、错过的 vsync、预测误差。
- HUD 和 inspector 显示当前刷新率、FPS、掉帧计数、请求原因。
- **确定性测试**（headless，进 CI）：
  - 合并规则：给定若干来源，需求区间是确定的；
  - 预测求值：给定呈现时间序列，每帧的 `t` 是确定的；
  - 空闲帧数为 0。
- **实时报告**（真机，不进门禁、进公开报告）：帧时间 p50 / p99、掉帧率（超过刷新间隔的帧占比）、最长连续掉帧、呈现延迟、平均刷新率和功耗代理指标（空闲时的唤醒次数）。
- 场景复用 `tests/bench` 的 `wall/*` 群体操作：缩放扫描、平移、工作区拖动、1000 个选中窗口拖动、甩动、流式输出。

---

## 2. 晋升策略（D-71）

画布节点从纹理晋升为实时组件，是同一种分层：

| 层级 | 写法 |
|---|---|
| L0 | 种类的 `tiers` 写一个 `promote` 阈值；相机移动时不晋升，停下 120ms 后再晋升；聚焦的节点立即晋升（已实现） |
| L1 | 画布属性 `promotion = "on_rest" \| "immediate" \| "never"`、`promote_after`（毫秒）、`promote_budget`（每帧最多新晋升几个） |
| L2 | `Kind::promote_policy(const PromoteQuery&) -> bool`：参数有节点、屏幕矩形、相机状态（静止 / 移动 / 动画中）、本帧剩余预算 |
| L3 | 档位插件：自己管理实时节点池（复用组件实例而不是重建） |

**预热**：设计目标是“布局已缓存时晋升延迟 < 1 帧”。纹理档位已经在线程池上的离屏 Runtime 里为同一个元素算过布局，晋升时可以复用这份布局：

- 离屏 Runtime 把它算出的布局结果（节点尺寸、文本段落）交给实时节点；
- 实时节点挂载时只做 reconcile，不重新测量；
- 测量指标是“晋升那一帧的 UI 线程耗时”，进 `tests/bench`。

---

## 3. 构建期标记：各语言写法不同，语义对齐（D-72）

### 3.1 目标

开发者在自己的语言里，用**合法**的语法写一个标记，就能得到注册、绑定、契约、stub 和 agent 描述。没有任何工具时，代码照样能编译、通过类型检查；有工具时，构建期把标记展开。

### 3.2 统一的词汇

所有语言共用 `spec/registry/markers.toml` 里的一套词汇：

| 标记 | 字段 | 生成什么 |
|---|---|---|
| `verb` | `name`、`effect`、`answer`、`summary`（文档注释）；参数来自函数签名 | 注册代码、C ABI 条目、三种语言 stub、agent 能看到的描述和效果分级 |
| `component` | `name`、`role` | 契约骨架、builder、注册 |
| `prop` | `default`、`bindable`、`doc` | 契约里的属性 |
| `event` | `doc`；参数来自签名 | 契约里的事件 |
| `action` | `effect`、`answer` | 契约里的动作 |
| `model` | `key` | `ValueOf` 转换、JSON schema、三种语言的类型 |
| `native` | `async` | `*.native.omui.toml` |
| `embed` | 路径 | 嵌入分区（相当于 `#embed`） |

字段名和取值在四种语言里**完全相同**，所以读任何一种语言的代码都能一眼认出同一个意思：

```cpp
// C++：自定义命名空间的属性（标准语法；不是宏）
[[omui::verb("notes.new", effect = reversible)]]
Note new_note(std::string title);
```

```ts
// TypeScript：装饰器
@verb("notes.new", { effect: "reversible" })
newNote(title: string): Note { … }
```

```python
# Python：装饰器 + 类型注解（运行时也能读）
@omui.verb("notes.new", effect="reversible")
def new_note(title: str) -> Note: ...
```

```lua
-- Lua：LuaLS 注解注释
---@omui.verb notes.new effect=reversible
---@param title string
---@return Note
local function new_note(title) … end
```

### 3.3 规则

- **只生成，不改源。** 生成物只写进 `out_dir`；`omui gen --explain <文件>` 打印展开结果，能单步调试。
- **永远有显式写法。** 每个标记都对应一段可以手写的 L2 代码（例如 `verbs().add(VerbDesc{…}, fn)`），可以随时退回。
- **扫描要便宜。** 按 token 找 `[[omui::`、`@verb`、`---@omui.` 这类标记，不做完整解析；参数类型以生成代码的编译结果为准。GCC 16 的反射稳定后，在 `modules/ui/reflect` 里做编译期交叉校验。
- **编译器安静。** C++ 未知属性默认会警告：插件给编译加上 `-Wno-attributes=omui::`（GCC）和 `-Wno-unknown-attributes`（Clang）；MSVC ABI 下的 clang 同样处理。
- **词汇只有一份。** 各语言的扫描器、校验和文档表格都由 `markers.toml` 生成，加一个字段时四种语言同时加。

### 3.4 契约方向

AGENTS.md 规定组件和 native 模块“契约优先”。标记让契约可以从代码生成，方向相反。规则是：

- **库和组件包**（会被别人依赖、跨语言、跨版本）：仍然契约优先，契约是公开接口。
- **应用内部**的组件、verb、模型：允许“代码优先 + 生成契约”；`omui check` 保证两边一致；任何时候都能把生成的契约导出成正式文件，转为契约优先。

---

## 4. 构建插件的架构（D-73）

### 4.1 现状和问题

拆分之前，所有构建期能力都在 `mcpp/rules` 一个包里：contracts、natives、themes、behaviors、embed、dist-toy。继续往里加（Slang、标记、i18n、资源烘焙），会让每个应用的构建依赖越来越重，也无法单独发布和升级。2026-10-30 已按下表拆分（`mcpp/README.md`）。

### 4.2 拆分

| 包 | 内容 | 类型 |
|---|---|---|
| `openmui.build.plan`（`mcpp/plan`） | 共用基础：扫描文件、计划 edge、`submit`、生成器、路径文本（目录不叫 `build/`：那是全仓库忽略的输出目录名） | 内置，已完成 |
| `openmui.build.contracts`（`mcpp/contracts`） | `*.contract.omui.toml`、`*.native.omui.toml` → C++ / C ABI / stub | 内置，已完成 |
| `openmui.build.embed`（`mcpp/embed`） | 主题、行为、任意文件 → 嵌入分区 | 内置，已完成 |
| `openmui.build.toy`（`mcpp/toy`） | dist-toy 打包格式：B 站小游戏包，或不带 SDK 的静态页面（官网） | 内置，已完成 |
| `openmui.shader.rules`（`modules/gfx/shader/rules`） | `*.slang` → SPIR-V / MSL / DXIL / WGSL / ESSL + 反射。它跟着独立组件 openmui.shader 走（D-55：组件随时能拆成独立仓库，构建插件也一起带走），不搬进 `mcpp/` | 内置 |
| `openmui.build.markers` | 第 3 节的标记 | 内置 |
| `openmui.build.i18n` | 提取 `tr("key")`，生成消息目录，检查缺失 | 可选，经 mcpp-index 发布 |
| `openmui.build.assets` | 字体子集、图片烘焙、按目标裁剪 | 可选，经 mcpp-index 发布 |

- 每个插件都是一个 `host-module = true` 的 mcpp 包，只依赖 `openmui.build.plan`（和它调用的生成器），互相不依赖。
- 应用在 `build.mcpp` 里只组合自己需要的插件。
- 不设伞包：每个包在 `[build-dependencies]` 里只写自己用到的插件（组件包写 contracts，主题包写 embed，B 站小游戏再加 toy）。原来设想保留 `openmui.rules` 作伞包，但那样大家还是会依赖全集，拆分就失去意义；仓库里没有外部用户，所以一次迁移完。
- 第三方也可以写插件（例如公司内部的代码规范检查），通过 mcpp-index 分发。

### 4.3 插件契约

- **输入**：清单（`mcpp::manifest_dir`）、选项结构体、`out_dir`。
- **输出**：edge 列表（命令、输入、输出、提供的模块、导入），统一 `submit`。
- **要求**：
  - 确定性：同样的输入得到逐字节相同的输出；
  - 增量：每个 edge 声明全部输入；
  - 诊断：使用 `文件:行:列: 信息 (提示)` 格式；
  - 可观测：每个插件一个 trace 类别 `build.<插件>`。
- 每个插件有自己的测试：`tests/plugins/contracts`、`tests/plugins/embed` 各是一个第三方包的夹具；toy 由 `tools/ci/toy.sh` 和 `tools/ci/site.sh` 检查打出的包。可选插件进 mcpp-index 时，必须有独立仓库的 CI。

---

## 5. 实施顺序

1. 帧需求 L0–L2 和 `frame.pacer` 默认插件、按预测上屏时间求值、HUD 和 trace。先接 macOS ProMotion（`CAMetalDisplayLink`）和 Windows VRR，再接 Wayland 和 Android。
2. 晋升 L1 属性、L2 策略、预热（复用离屏布局），以及“晋升那一帧耗时”基准。
3. 构建插件拆分：`openmui.build.plan` 基础加 contracts、embed、toy 三个内置插件，不设伞包（已完成，2026-10-30）。
4. 标记试点：先做 C++ 的 `[[omui::verb]]`，四种语言的 `verb` 一起对齐；再做 `model`、`embed`。

## 6. 决策记录

| 编号 | 决策 |
|---|---|
| D-69 | 每项能力分层暴露：L0 零配置、L1 声明式提示、L2 编程接口、L3 插件；只有一套机制，每层可观测，可以随时下沉 |
| D-70 | 帧需求与帧节奏：运行时合并各来源的 FrameDemand，交给 `frame.pacer` 插件，按预测上屏时间求值；宿主映射到 ProMotion / VRR / setFrameRate / Wayland 时序协议；空闲不画 |
| D-71 | 晋升策略分层（阈值 → 属性 → 策略函数 → 档位插件）；相机移动时不晋升；晋升复用离屏布局（预热） |
| D-72 | 构建期标记：各语言用合法语法（属性、装饰器、注解注释），字段语义按 `markers.toml` 对齐；只生成不改源；库契约优先，应用内部允许代码优先 |
| D-73 | 构建插件按功能拆成多个包：`openmui.build.plan` 基础 + 内置插件 + 经 mcpp-index 分发的可选插件；不设伞包，每个包只依赖自己用到的插件 |
