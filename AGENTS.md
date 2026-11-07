# AGENTS.md

**openmui** 是开放的 UI 规范，**omui** 是它的 C++ Modules 实现。核心思想：**一棵语义树，多种渲染**——像素、无障碍、Agent、测试都是同一棵语义树的视图。围绕它构建一组 **AI／Agent 原生**的引擎与框架：人和 Agent 都是一等用户。

本文件只写最核心的思想与规范。细节见 [`.agents/docs/README.md`](.agents/docs/README.md)；当前进度与 CI 红灯清单见 [STATUS](.agents/docs/STATUS.md)；路线与决策见[顶层收敛方案](.agents/docs/2026-11-06-top-level-assessment-convergence-design.md)。

## 1. 产品与命名

| 身份 | 是什么 | 位置（目标） |
|---|---|---|
| `openmui` | UI 规范（OpenLifa 标准，`openmui.spec`） | `spec/`（子模块） |
| `omui` | 用一棵语义树同时驱动像素、强动画、无障碍、Agent 和测试的 UI 框架（动画由 hanim 提供） | `modules/projects/omui`（含 `components/`、`lang/`、`sdk/`、`templates/`） |
| `hanim` | 可组合、Agent 原生的高级动画引擎，同一套能力支撑 UI 动画、视频和游戏动画；**唯一的通用动画能力归属** | `modules/projects/hanim` |
| `xrender` | CPU 与 GPU 同源、Web 优先、像素可供 Agent 审查的渲染引擎（内部代号） | `modules/projects/xrender` |
| `xengine` | 人和 Agent 地位对等地开发与游玩的 Agent 原生游戏引擎系统（内部代号）；也容纳暂时不好分类、还不够独立的领域能力 | `modules/projects/xengine` |
| `gongfu` | 确定性、可回滚、角色即数据的格斗游戏框架 | `modules/projects/gongfu` |
| `mlibs` | 与领域无关的通用库、**纯抽象接口**（如 `mlibs.rhi`）和通用工具链（如 `mlibs.shader`） | `modules/{base,gfx,content}` |
| `mlibs.utils` | 还不够独立成库的小工具，按目录放在 `src/<工具>/`，复杂到一定程度再拆成独立库 | `modules/base/utils` |
| 作品 | 官网、展示、示例：**验证线** | `site`、`showcase`、`examples` |

- **`modules/` 分两类目录**，每个分组目录都有 README 说明定位和准入：层级分组（`base`／`gfx`／`content`，放 mlibs）；成熟项目（`projects/`，放成熟、复杂、可独立成项目／仓库级别的品牌，品牌内部再容纳尚未毕业的子系统）。**进入 `projects/` 必须能用一句话说清楚它是什么，并有几项明确的、业界领先或面向未来设计的标志性特性**；一句话定义写在项目 README 第一行，特性附对照证据或真实任务演示。判定：通用的放 mlibs，具体实现归所属品牌。
- **毕业**：xengine 子系统或 mlibs 库满足“有标志性特性、至少 2 个独立消费者、API v1 冻结、有 ops／skill、CTS、文档、基准”后，可以拥有自己的品牌，成为 `modules/projects/` 下的独立项目；拿不出有证据的标志性特性的，留在 xengine 或 mlibs。
- **包的身份就是主要模块名**（`omui.core` ↔ `import omui.core;`）；**路径就是包名**（叶子目录 = 包名最后一段，lint `layout`）；按角色命名，不按实现命名；不用裸的 `mui`。
- **不留兼容层**：内部开发阶段，改名和 API 调整一次改完，不保留转发入口和旧别名（lint `legacy-import`）。M1 已完成命名与目录迁移：omui 的包是 `omui.*`，`openmui` 只指规范（包 `openlifa.openmui`、模块 `openmui.spec`）。**公开名按层面取**：属于规范层面的（协议与格式标识、C ABI、规范 stubs 定义的脚本 API：`import openmui`、`openmui:native/*`）用 `openmui`；具体实现、非规范层面的（包、C++ 模块、命令行、工具）用 `omui`。
- 保持不变的外部契约：协议字段、schema、`[package.metadata.openmui]`、`spec/include/openmui.h`、C ABI 的 `omui*`／`OMUI_*`、命令行 `omui`、数据文件后缀。
- 品牌：官网和应用用 **omui**，介绍标准用 **openmui 规范**；标志是大写 **O** 内含小写 **mui**，用框架自身的矢量代码构造。

## 2. 核心设计思想

1. **主线与验证线。** 核心引擎与框架是主线；作品是对主线能力的验证，承担一部分集成和验收测试的作用。作品只消费公开能力，不写引擎代码；遇到缺口就回到主线实现。
2. **局部 × 全局 × 分级。** 每个引擎按本领域最优的方式设计内核（局部）；跨引擎只通过统一形状的契约组合（全局）：句柄、错误、时间、值与通道、准备与执行、预算、资源生命周期、操作与观测、能力查询；每个公开 API 都提供 L0 一句话调用、L1 参数、L2 组合、L3 显式准备与检查、L4 扩展，所有层走同一条执行路径。统一的是契约和模式，不做万能对象。详见[核心设计准则](.agents/docs/2026-11-05-local-global-composable-architecture-design.md)。
3. **单一归属，不重复实现。**
   - 每种能力只有一个 owner：数学和随机在 `mlibs.math`，hash 在 `mlibs.hash`，通用动画（取值、曲线、通道、表示、编排、播放、配方）在 `hanim`，渲染在 `xrender`，设备抽象在 `mlibs.rhi`。
   - 别处发现需要就去补 owner，不复制一份（lint `math`／`hash`／`motion`，以后扩展为归属检查）。
4. **人与 Agent 双第一优先级。**
   - 先定义 Agent 可发现、可观察、可控制的语义契约（经 openas），再用 GUI／TUI 呈现同一能力。
   - Agent 不开 GUI 也能完成完整任务，并用同源 renderer 的真实离屏像素审查画面。
   - 人负责审查、决策、可视化反馈和少量控制。
   - 每个产品随公开能力提供可发现的 ops 与最小 skill／SOP。
   - 游戏里 Agent 可以和人平等地玩，由游戏开发者通过 openas 语义开启。
   - 细则见[核心设计准则 §9](.agents/docs/2026-11-05-local-global-composable-architecture-design.md#9-人与-agent-双第一优先级的设计与验收)。
5. **权限由人掌握。** 开发模式对可信会话开放全部已声明能力；发布范围 = 开发者允许 ∩ 用户配置，再受会话约束；委派不能扩大权限，记录身份、委派链和结果。
6. **一切皆插件，管线可编排**（D-65、D-66）。核心只规定模型、数据契约和管线；插件按阶段或按批调用，不按节点或像素调用；每类插件都有 CTS、trace 和日志类别。一致性测试统一用 `mlibs.cts` 框架，套件放在契约 owner 处，结果分为通过／失败／不支持（不可用永远不算通过）。控制请求不进入逐像素或逐采样的热循环。
7. **确定性与可观测。** 纯动画是时间的函数；有状态的部分走显式模拟、checkpoint 和重放；新子系统要有 trace、日志和基准预算（`tests/bench`）。

## 3. 架构与依赖

- **依赖只能向下**（lint `layering`）：`base`（含 `hanim.core`：只依赖标准库、`mlibs.math` 和 `mlibs.utils`）← `gfx`（含 `hanim`、`xrender`）← `content` ← `media` ← `spec` ← `ui`（omui）← `components` ← xengine 游戏层 ← 作品。UI 永远不依赖游戏层；`spec/` 只依赖标准库；除 omui 及其上层外，引擎和库都不依赖 UI；`projects/` 中的项目随时可以拆成独立仓库（D-55）。
- **名字表示归属，目录表示分组，层级由 lint 的“路径 → 层”映射表判定**：一个项目的包可以跨层（例如 `hanim.core` 在 base 层，`hanim` 在 gfx 层）。
- **动画**：组件不写动画（D-93，lint `motion`），只画状态和可选的部件名；变化怎么动由节点的 motion 槽和风格包配方决定，由 hanim 统一求值。UI 框架负责身份、语义、呈现和命中一致性。每个宿主只有一个 hanim Engine。表示与选择策略见[多表示方案](.agents/docs/2026-11-05-hanim-representations-selection-policy-design.md)。画布相机飞行这类导航运动是例外，需在行内写 `MOTION-OK:` 和理由。
- **渲染器只写一次**：xrender 的渲染图承载所有渲染特性（vector、effect，以后的 mesh、particle）；GPU 路径只依赖 `mlibs.rhi` 接口，后端（SDL_GPU、WebGPU、WebGL）是 xrender 的实现，由宿主注入；模拟等计算只依赖接口，不依赖 xrender。
- **标准**：`spec/` 是 [`openlifa/openmui`](https://github.com/openlifa/openmui) 子模块，openas 来自 [`openlifa/openas`](https://github.com/openlifa/openas)。改标准就在标准仓库各自的单 PR 分支（`openmui-design`／`openas-design`）提交，提交人为 `Sunrisepeak <speakshen@outlook.com>`，带 `-s`；代码 Apache-2.0，规范文本 CC BY-SA 4.0。actor、效果、信封、授权以 openas 为准，运行时用 `openas::decide` 判定请求。

## 4. 代码规范

- **C++23** 是基线；C++26（反射）只在 omui 的 `reflect` 包使用。
- **不用宏**：用模块分区、`inline constexpr`、`if constexpr`、`consteval`、`std::source_location`。全仓库唯一的头文件是生成的 `spec/include/openmui.h`；第三方头文件只出现在 `native/` 实现分区里。
- 核心代码不用异常、不用 RTTI，错误用 `std::expected`；热路径没有虚函数，每帧零分配；动画、布局、光栅模块禁用 fast-math 和浮点收缩。
- 契约优先：组件与 native 模块先写 `*.contract.omui.toml`／`*.native.omui.toml`，绑定由生成器产出。
- 视图代码优先（各语言原生的 builder）；数据文件命名 `<名字>.<种类>.omui.{toml,json,jsonl}`；着色器用 `.slang`；可交互节点要写显式 ID，ID 不依赖显示文本；脚本不进入每帧路径。
- 平台相关代码经由 `mcpplibs.os`；窗口和输入经由 SDL3。

## 5. 工程与包

- 工程文件只有 `mcpp.toml`；构建、测试、打包都用 mcpp，工具经 xlings 安装。带命名空间的依赖写成点分键（`nlohmann.json = "3.12.0"`）；父包和子包同时出现时键要加引号。`mcpp.lock` 不提交（已在 `.gitignore`）。根 `mcpp.toml` 只有 `[workspace]`，SDK 伞包是 omui 的 `sdk/`（`import omui;`）；`mcpp/` 只放构建插件；程序入口在 `src/entry/`。
- **按库／产品边界建包，不按文件拆包**（D-99）：内部职责放在所属包的 `src/<职责>/`，用 feature 选择。只有形成可独立复用的库、要单独发布，或确实需要依赖／平台／许可／构建阶段隔离时，才新建包，并在 README 写明理由。与领域无关的小工具放 `mlibs.utils`；与领域相关的小块放回所属包。成员之间只通过声明的 `path` 依赖引用。
- **优先用 mcpp 生态**（D-56）。第三方 C 库经 mwrap 使用（D-83，lint `thirdparty`）：上游先发布，再在本仓库引用；不用 GPL／AGPL（动态链接也不行），LGPL 只能作为可选插件动态链接，MPL 不修改即可用；默认选免版税的格式。
- **本地构建、测试等任何重负载不得占用超过本机 90% 的 CPU 或内存**，避免机器卡顿、卡死：经 `tools/dev/capped.sh` 运行（`tools/dev/capped.sh mcpp test -p …`），它按核数和内存把并发编译数限制在 90% 以内（`MCPP_JOBS`），并以低 CPU／IO 优先级执行；自己启动的并行任务（多个构建、测试、后台循环）合计也不得超过这一上限。
- 工具都有两种界面（D-61）：完整的 CLI／openas 入口（结构化输出、退出码）和可选的 omui GUI／TUI。作品必须是真实可用的东西（D-63），并有能在 headless 下确定重现的招牌镜头。

## 6. 协作与验收

- 所有工作都在**同一个 PR**（分支 `openmui-design`）里，**每个 commit 是一个检查点**，提交信息写清范围和验收依据。第一阶段不修改 xdesk 和 xlings 仓库。
- **CI 不阻塞推进，但必须对账**：每轮开头查看上一检查点的 CI 结果，新失败记入红灯清单，并在后续检查点修复（fix-forward）；修复要找根因，不改期望值、不降阈值。
- **双视角验收**：新能力要有无 GUI 的真实操作和结果验证；视觉能力要有真实离屏图像；动画要按时间采样整个过程（端点、多个中间态、打断和反向），不能只看两端；GUI／TUI 另外验收人的体验。能自动检查的都进 CI；完成状态必须有证据，证据规模与改动规模成比例。
