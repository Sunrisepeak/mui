# AGENTS.md

**openmui** 是开放的 UI 规范，**omui** 是用 C++ Modules 写的具体实现及其官网品牌。核心是统一的语义层：像素、无障碍、agent、测试都是同一棵语义树的不同渲染。

- 品牌（2026-11-03）：官网、实现介绍、应用界面使用 **omui**；介绍标准时使用 **openmui 规范**。omui 标志是大写 **O** 内包含小写 **mui**，用框架自身的矢量代码构造，作为品牌和官方可交互作品共用同一来源。`mui` 只允许作为该标志内部字形，不是独立产品名。现有 `openmui.*` 包、模块、协议、schema 和仓库标识保持兼容；品牌变更不等于全仓库标识迁移。

- 仓库暂时是私有的 `Sunrisepeak/mui`。**命名按产品职责归属**（D-100）：`openmui` 专属 UI 协议／规范，UI 框架、组件和宿主使用 `omui`；已有独立名字的引擎／库使用自身名字（如包／模块／C++ 命名空间 `hanim`）。尚未独立命名的通用基础库放 `mlibs.<职责>`／`mlibs::<职责>`，领域引擎放 `xengine.<领域>`／`xengine::<领域>`；它们是归类命名空间，不是强制依赖的大伞包。暂名须见名知意，有明确 owner，不把同一能力在两处实现。不要使用裸的 `mui`。详见[命名与迁移方案](.agents/docs/2026-11-05-product-namespace-migration-design.md)。
- **新代码按新归属命名，既有公开身份按依赖顺序迁移**：迁移期间 `openmui.*` 实现包／import 是兼容旧名，不再是新增实现的默认前缀；共享控制层为 `mlibs.control`；hash／math／log／trace／testing／geom／memory／unicode／fuzzy／session／pipeline／image 已迁为对应 `mlibs.*`，旧 import 在同包内转发，依赖键使用新身份。协议字段、schema、`[package.metadata.openmui]`、生成头文件路径、数据文件后缀和第三方包不随命名空间批量替换。UI C ABI 的 `omui*` / `Omui*` / `OMUI_*`、`OMUI_EXT_*` 和命令行 `omui` 保持；C++ Modules 迁移须重新构建，不承诺二进制兼容。
- 设计记录在 `.agents/docs/`，从 `.agents/docs/README.md` 开始读。总体架构文档里的决策记录（D-xx）和待决问题（Q-xx）是当前的基线。
- 当前阶段：按[官网重构方案](.agents/docs/2026-11-03-openmui-site-redesign-implementation-design.md)的 R0–R6 推进实现与评审，同时保留[实施计划](.agents/docs/2026-10-26-openmui-implementation-plan.md)的技术与质量基线。开始实施不等于 C2 或后续检查点已验收，完成状态必须有证据。
- 工程文件只有 `mcpp.toml`（openmui 自己的声明放在 `[package.metadata.openmui]`）；构建、测试、打包都使用 mcpp；工具通过 xlings 安装。带命名空间的依赖写成 `[dependencies]` 里的点分键（`compat.sdl3 = "3.4.18"`、`nlohmann.json = "3.12.0"`），不单独开 `[dependencies.<ns>]` 表（lint 规则 `manifest`）。
- **包的身份就是主要模块名**：命名迁移目标如 `[package] namespace = "omui"`、`name = "core"` 得到 `omui.core`，和 `import omui.core;` 一致；当前既有 UI 包仍通过 `openmui.core` 等旧入口兼容，新建控制库已经使用 `mlibs.control`。
  - 这里指包的主要公开入口；源码子目录、实现单元和 C++ 模块分区不因此各自成为包。
  - UI 组件目标为 `omui.ui`（`omui.ui.base`），宿主目标为 `omui.host`（`omui.host.headless`）；迁移前仍使用对应 `openmui.*` 旧名。
  - UI 伞包目标为 `omui.sdk`（`import omui;`）；当前入口是 `openmui.sdk`（`import openmui;`）。
  - 依赖键、测试 `-p` 参数使用该包清单的实际身份；例如 `mlibs.control = { path = "…" }`、`mcpp test -p mlibs.control`。
  - 新产物按 owner 加前缀（如 `mlibs-control`、`omui-core`、`hanim-engine`），保证不重名；已发布产物名按迁移表处理。
- 第一阶段不修改 xdesk 或 xlings 仓库。
- **规范是 OpenLifa 的标准**（D-78）：`spec/` 是 [`openlifa/openmui`](https://github.com/openlifa/openmui) 的 git 子模块（包 `openlifa.openmui`，模块 `openmui.spec`），改规范就在子模块里提交并推到它的单 PR 分支 `openmui-design`，再更新本仓库的子模块指针；openas 是 [`openlifa/openas`](https://github.com/openlifa/openas)，经 mcpp-index 依赖 `openlifa.openas`。标准仓库的提交用 `Sunrisepeak <speakshen@outlook.com>` 并加 `-s`；代码 Apache-2.0，规范文本 CC BY-SA 4.0。本仓库是第一个直接使用这两个标准的实现。

## 人与 Agent 都是一等用户

- **双第一优先级，交互侧重不同**：库、引擎、框架、组件、工具及其产物，从开发、测试、调试到使用，都同时为人和 Agent 设计。先明确 Agent 原生可发现、可观察、可控制的语义契约，再以 GUI／TUI 呈现同一能力；人的可用性与体验同属第一优先级。支持“人 → Agent → 系统”：Agent 直接承担高效、重复、复杂的操作，人通过可视化审查、决策、必要控制，以及有反馈和情绪价值的交互参与。
- **openas 是原生控制契约**：公开能力具有稳定身份、类型化输入输出、状态／错误、权限与操作生命周期，并通过 openas 映射到同一领域实现。同等授权下，人能观察、控制的能力须有 Agent 原生等价入口；不能只提供截图点按自动化。共性缺口可推动 openas 规范迭代，领域参数仍归各产品；不另造重复协议，也不把实现提案宣称为已发布标准。
- **无 GUI 完整工作，视觉结果真实可见**：Agent 必须能通过 API／CLI／openas 完成所选能力的完整任务，不以启动 GUI 工具、创建窗口、显示服务器或人工点按为前提。视觉组件、设计器与场景同时提供语义信息和同源 renderer 的真实离屏像素，支持区域、时间／帧与版本定位，使 Agent 不打开 GUI 也能审查其画面。所选后端不足时明确报告能力限制；JSON 或示意图不能代替视觉验收。
- **内置最小 skill／SOP**：各产品／模块随公开能力提供可发现、版本化的最小操作指南和任务流程，说明输入、前置条件、权限、执行／查询、验证依据、失败恢复及支持的取消／撤销。复用契约和共同流程，覆盖独立使用，不要求先阅读 GUI 教程；指南不能代替能力实现与真实测试。
- **权限与隐私由人掌握**：开发模式默认对可信开发会话开放所属实例的全部已声明开发能力（full access），正常开发操作不逐次审批；显式收窄及宿主实际权限仍有效，不等于远端请求自行获得授权。发布应用的范围为**开发者允许范围 ∩ 用户配置**，再受宿主／会话约束；人可配置、审查和撤销。查询、像素、日志、导出与控制遵守同一授权边界；Agent 委派给 Agent 不得扩大权限，保留身份、委派链和结果记录。
- **同一能力，按需成本**：人和 Agent 共用状态、领域操作、资源与观测实现；GUI／TUI、CLI、openas 只是入口与视图。纯库通过公开契约和 adapter 接入，不强制依赖 UI、网络、模型或内置 AI；关闭可选内置 AI 不等于关闭 Agent 原生支持，控制请求不进入逐像素／逐采样热循环。
- **双视角分别验收**：新增能力须有无 GUI 的真实操作／结果验证；视觉能力还须有实际离屏图像，GUI／TUI 另验人的审查和交互体验。只在 GUI 中可用、只有协议描述或只返回成功 JSON，均不能认定 Agent 原生完成。细则与评审清单见[核心设计准则 §9](.agents/docs/2026-11-05-local-global-composable-architecture-design.md#9-人与-agent-双第一优先级的设计与验收)。
- **动画验收覆盖整个过程**：Agent 必须用同源 renderer 按时间采样真实画面，覆盖端点、多个中间态，以及启动／结束、编排交界、打断／反向等相关关键时刻；按时长、变化速度和事件加密采样，不能只凭 A／B 两个端点通过。检查非预期跳变／闪烁、轨迹、遮挡／裁剪、文字可读性及呈现与命中的一致性；像素步进等有意离散变化须明确。保留可复现的时间、配置、后端／DPI、帧序列与可回放证据，支持无 GUI 审查。离线采样验证视觉过程，实速播放另验节奏、帧间隔和交互；采样通过不自动等于人的观感已达标。详见[核心设计准则 §9.6](.agents/docs/2026-11-05-local-global-composable-architecture-design.md#96-动画以过程采样和实际播放验收)。

## 规则（会随着代码落地逐步改成 lint）

- **局部能力为中心，全局契约组合**：各引擎围绕本域核心能力，用可控的类型化数据、通用算子／管线和自定义扩展组合场景；跨系统通过公开契约与 adapter 协作，共享同职责的底层实现，显式映射时间／状态／资源，保留领域语义与实时约束，按需引入依赖。新增能力先确定通用基础、领域插件或上层配方的归属；场景逻辑不硬编码进内核。“统一”统一契约与组合／控制／观测模式，不强制万能对象或重复系统。详见[核心设计准则](.agents/docs/2026-11-05-local-global-composable-architecture-design.md)。
- 规范（`spec/`）只依赖标准库。实现层负责把 openmui 和其他规范（openas / opensal / openbus）连接起来：actor、效果、信封、授权以 openas 0.2 为准，运行时用 `openas::decide` 决定总线上的请求（S-11）。
- 依赖只能向下（D-82，lint `layering`）：`modules/base` ← `gfx` ← `content` ← `media`（独立组件）← spec ← `modules/ui`（openmui；宿主在 `modules/ui/host`）← components ← mgame（游戏引擎，以 openmui 作为 UI）← 作品。openmui 永远不依赖 mgame；格斗框架 gongfu 的核心（`mgame/gongfu`）只依赖标准库，以后可以原样拆出；component 只使用 runtime 的公开 API；bridge 只依赖 spec；lang 只依赖 `openmui.h`。
- **C++23 是基线**；C++26（反射等）只在 `modules/ui/reflect` 里使用（GCC 16，`-freflection`）。
- **不用宏，用 constexpr**：平台和特性用清单选择的模块分区 + `inline constexpr` 常量 + `if constexpr`；编译期校验用 `consteval`；断言和日志用函数加 `std::source_location`。唯一的例外是生成的 `openmui.h`。
- **全仓库只有一个头文件 `spec/include/openmui.h`**；其他全部是模块。第三方头文件只出现在 `native/` 实现分区里，宏在那里转换成 constexpr。
- 核心代码不用异常、不用 RTTI，错误用 `std::expected`；热路径没有虚函数，每帧零分配。
- **按库／产品边界建包，不按文件或目录拆包**（D-99）：少量 `.cppm`／`.cpp`、几百行的内部职责，默认放所属包的 `src/<职责>/`，用源码目录、实现单元或模块分区组织，共用一个 `mcpp.toml`。只有形成有足够内聚实现的可复用库、明确计划独立使用／发布的产品，或确需依赖、平台／工具链、许可、构建阶段隔离时，才新建 workspace 成员；小型必要契约／适配包可以例外，但要写明消费者、隔离收益和边界，不以“以后可能复用”作为空泛理由。不设机械行数门槛，插件也不要求一插件一包；优先用已有包内目录和 feature，实际需要时再提取。详见[包粒度准则](.agents/docs/2026-11-05-local-global-composable-architecture-design.md#51-目录语言模块与独立包分开判断)。
- **已成立的独立包**各有一个 `mcpp.toml`，成员之间只通过声明的 `path` 依赖引用，不允许跨成员用相对路径引用源文件。内部整理与包迁移分开；合并既有包需检查公开 import／包名兼容、依赖闭包、feature 裁剪与消费者，不为减少清单数量破坏真正的独立性。
- **独立组件放在 `modules/{base,gfx,content,media}` 下**（hanim、geom、memory、unicode、trace、session、pipeline、image；rhi、shader、raster、render；text、layout、markdown、i18n）：不依赖 UI 框架／组件／宿主或 openmui UI 规范包，只依赖标准库、彼此和外部库／标准，所以随时可以拆成独立仓库（D-55）。命名归属按 D-100，依赖层级仍由实际职责与目录判定。
- **路径就是包名**（D-80，lint `layout`）：库的叶子目录名是包名的最后一段（下划线写成连字符），包里面的包以它命名（`openmui.rhi.sdl` 在 `modules/gfx/rhi/sdl`）；分组目录只做归类；有包的目录都有 README，列出下面的每个包。按角色命名，不按实现命名（`shader` 不叫 slang）；动画引擎叫 `hanim`（`import hanim;`，命名空间 `hanim::`）。父包和子包同时出现在一个依赖表里时，键要加引号（`"openmui.rhi"`、`"openmui.rhi.sdl"`），否则不是合法的 TOML。
- **优先使用 mcpp 生态**：mcpp-index 里已经有知名、跨平台的库时直接使用（JSON 用 `nlohmann.json`，TOML 用 `marzer.tomlplusplus`，任务用 `taskflow`……），不自己实现；只有生态里没有、或者无法满足确定性要求时才自己写（D-56）。
- **第三方 C 库经 mwrap 使用**（D-83，lint `thirdparty`）：`compat.<库>` 加 `mcpplibs.mwrap.<库>`，只在 `native/` 单元里 `import mcpplibs.mwrap.<库>;`；上游先做好、发布，再在本仓库使用。选高质量、跨平台、依赖少、高性能的库；**不用 GPL / AGPL（动态链接也不行）**，LGPL 只能作为可选插件动态链接，MPL 不修改即可；默认用免版税的格式。
- 动画内核 `modules/base/hanim/core/` 只依赖标准库和 `mlibs.math`（D-87：它的确定性函数在那里）；完整产品入口 `hanim` 在 `modules/gfx/hanim/`，导出对象、库、效果、编排和引擎，renderer显式可选，不允许依赖任何 UI 模块。全仓库的数学（确定性函数、随机数）在 `mlibs.math`，FNV-1a 在 `mlibs.hash`，不自己再写一份（lint 规则 `math`、`hash`）；纯动画是时间的函数（可以任意跳转、结果确定），有状态反应遵循显式模拟／重建契约；动画、布局、光栅器模块不允许使用 fast-math 和浮点收缩。
- 协作：当前的工作都放在同一个 PR（分支 `openmui-design`）里，每一轮修改提交一个 commit 作为检查点。
- 验收标准见实施计划 §19；能自动检查的都要进 CI。
- 组件和 native 模块都是“契约优先”：先写 `*.contract.omui.toml` / `*.native.omui.toml`，绑定由生成器产出，不手写。
- **组件不写动画**（D-93，lint `motion`）：组件只画出自己的状态（和可选的部件名）；变化怎么动由节点的槽决定（`motion` 属性、风格包的动效规则、框架默认），由 `openmui.motion` 在运行时统一完成。画布相机的飞行这类导航运动是例外，要在行内写 `MOTION-OK:` 和理由。
- 脚本永远不进入每帧路径。
- ID 不能依赖显示文本；可交互的节点要写显式 ID。
- 视图代码优先（各语言原生的 builder）；数据文件一律命名为 `<名字>.<种类>.omui.{toml,json}`（逐行记录用 `.jsonl`，例如 `*.session.omui.jsonl`）；着色器用 `.slang`；逻辑代码保持原生后缀。builder 和数据文件都要让人和 agent 容易读懂（`view-data-files` 设计文档 §2）。
- 仓库根目录当前是 `openmui.sdk` 伞包（`import openmui`），命名目标是 `omui.sdk`（`import omui`，D-100）；`mcpp/` 只放构建系统的代码；入口在 `src/entry/`。
- 渲染器只写一次：`modules/gfx/render` 是渲染架构（渲染图和纹理池，D-81），2D 矢量渲染是它的渲染特性 `render/vector`，以后的 sprite、mesh、particle 也是；GPU 路径基于 `omui.rhi` 抽象层，各后端只依赖这个抽象层的接口；每个渲染特性的 pass 都记录进同一张渲染图。
- **平台相关的代码一律经由 `mcpplibs.os`**（文件、目录、进程、管道、IPC）；窗口和输入经由 SDL3（D-57）。
- **开发和分析工具都有两种界面**：完整的 CLI／openas 控制入口（结构化输出，CLI 提供退出码，给 Agent 和 CI）与可选启动的 omui GUI／TUI 视图（给人审查和交互）；共用领域操作与数据，遵循上面的双第一优先级规范。每个工具的界面都要突出框架的特色，工具本身就是展示（D-61）。
- **作品双视角**（D-63）：示例和展示要做成真实可用的工具、组件或内容，同时展示一项框架特色，并且有能在 headless 下确定重现的招牌镜头；不做只为演示而存在的 demo。
- **一切皆插件，管线可编排**（D-65、D-66）：核心只规定组件模型、数据契约和管线；动画、布局、文本、damage、光栅化、GPU 后端、宿主、效果、编解码、传输、脚本都是插件，按阶段或按批调用（不按节点、不按像素）；每个插件种类要有 CTS、trace 类别和日志类别。
- **可观测性是框架的一部分**（D-59）：新的子系统要有 trace span 和日志类别；性能声明要有 `tests/bench` 里的基准和 `budgets.bench.omui.json` 里的预算。
