# AGENTS.md

**openmui**：一套开放的 UI 规范，加上一个用 C++ Modules 写的参考实现。它的核心是统一的语义层：像素、无障碍、agent、测试都是同一棵语义树的不同渲染。

- 仓库暂时是私有的 `Sunrisepeak/mui`。**身份用全名 `openmui`**（包、模块 `openmui.*`、总线接口）；**需要反复输入的地方用缩写 `omui`**（C++ 命名空间 `omui::`、C ABI `omui*` / `Omui*` / `OMUI_*`、`OMUI_EXT_*`、命令行短名）。不要使用裸的 `mui`。
- 设计记录在 `.agents/docs/`，从 `.agents/docs/README.md` 开始读。总体架构文档里的决策记录（D-xx）和待决问题（Q-xx）是当前的基线。
- 当前阶段：设计 review。代码按[实施计划](.agents/docs/2026-10-26-openmui-implementation-plan.md)从 M0 开始搭建。
- 工程文件只有 `mcpp.toml`（openmui 自己的声明放在 `[package.metadata.openmui]`）；构建、测试、打包都使用 mcpp；工具通过 xlings 安装。带命名空间的依赖写成 `[dependencies]` 里的点分键（`compat.sdl3 = "3.4.18"`、`nlohmann.json = "3.12.0"`），不单独开 `[dependencies.<ns>]` 表（lint 规则 `manifest`）。
- **包的身份就是模块名**：`[package] namespace = "openmui"`、`name = "core"` 得到 `openmui.core`，和 `import openmui.core;` 一致。
  - 组件用命名空间 `openmui.ui`（`openmui.ui.base`），宿主用 `openmui.host`（`openmui.host.headless`）。
  - 伞包是 `openmui.sdk`（`import openmui;`）。
  - 依赖写 `openmui.core = { path = "…" }`，命令行写 `mcpp test -p openmui.core`。
  - 库的 target 名（产物名）保持 `openmui-…`，保证产物不重名。
- 第一阶段不修改 xdesk 或 xlings 仓库。
- **规范是 OpenLifa 的标准**（D-78）：`spec/` 是 [`openlifa/openmui`](https://github.com/openlifa/openmui) 的 git 子模块（包 `openlifa.openmui`，模块 `openmui.spec`），改规范就在子模块里提交并推到它的单 PR 分支 `openmui-design`，再更新本仓库的子模块指针；openas 是 [`openlifa/openas`](https://github.com/openlifa/openas)，经 mcpp-index 依赖 `openlifa.openas`。标准仓库的提交用 `Sunrisepeak <speakshen@outlook.com>` 并加 `-s`；代码 Apache-2.0，规范文本 CC BY-SA 4.0。本仓库是第一个直接使用这两个标准的实现。

## 规则（会随着代码落地逐步改成 lint）

- 规范（`spec/`）只依赖标准库。实现层负责把 openmui 和其他规范（openas / opensal / openbus）连接起来：actor、效果、信封、授权以 openas 0.2 为准，运行时用 `openas::decide` 决定总线上的请求（S-11）。
- 依赖只能向下（D-82，lint `layering`）：`modules/base` ← `gfx` ← `content` ← `media`（独立组件）← spec ← `modules/ui`（openmui；宿主在 `modules/ui/host`）← components ← mgame（游戏引擎，以 openmui 作为 UI）← 作品。openmui 永远不依赖 mgame；格斗框架 gongfu 的核心（`mgame/gongfu`）只依赖标准库，以后可以原样拆出；component 只使用 runtime 的公开 API；bridge 只依赖 spec；lang 只依赖 `openmui.h`。
- **C++23 是基线**；C++26（反射等）只在 `modules/ui/reflect` 里使用（GCC 16，`-freflection`）。
- **不用宏，用 constexpr**：平台和特性用清单选择的模块分区 + `inline constexpr` 常量 + `if constexpr`；编译期校验用 `consteval`；断言和日志用函数加 `std::source_location`。唯一的例外是生成的 `openmui.h`。
- **全仓库只有一个头文件 `spec/include/openmui.h`**；其他全部是模块。第三方头文件只出现在 `native/` 实现分区里，宏在那里转换成 constexpr。
- 核心代码不用异常、不用 RTTI，错误用 `std::expected`；热路径没有虚函数，每帧零分配。
- 每个目录都是一个 workspace 成员（独立的 `mcpp.toml`），只通过 `path` 依赖互相引用，不允许跨成员用相对路径引用源文件。
- **独立组件放在 `modules/{base,gfx,content,media}` 下**（hanim、geom、memory、unicode、trace、session、pipeline、image；rhi、shader、raster、render；text、layout、markdown、i18n）：不允许依赖 openmui 的任何其他包，只依赖标准库、彼此和第三方库，所以随时可以拆成独立仓库（D-55）。
- **路径就是包名**（D-80，lint `layout`）：库的叶子目录名是包名的最后一段（下划线写成连字符），包里面的包以它命名（`openmui.rhi.sdl` 在 `modules/gfx/rhi/sdl`）；分组目录只做归类；有包的目录都有 README，列出下面的每个包。按角色命名，不按实现命名（`shader` 不叫 slang）；动画引擎叫 `hanim`（`import hanim;`，命名空间 `hanim::`）。父包和子包同时出现在一个依赖表里时，键要加引号（`"openmui.rhi"`、`"openmui.rhi.sdl"`），否则不是合法的 TOML。
- **优先使用 mcpp 生态**：mcpp-index 里已经有知名、跨平台的库时直接使用（JSON 用 `nlohmann.json`，TOML 用 `marzer.tomlplusplus`，任务用 `taskflow`……），不自己实现；只有生态里没有、或者无法满足确定性要求时才自己写（D-56）。
- **第三方 C 库经 mwrap 使用**（D-83，lint `thirdparty`）：`compat.<库>` 加 `mcpplibs.mwrap.<库>`，只在 `native/` 单元里 `import mcpplibs.mwrap.<库>;`；上游先做好、发布，再在本仓库使用。选高质量、跨平台、依赖少、高性能的库；**不用 GPL / AGPL（动态链接也不行）**，LGPL 只能作为可选插件动态链接，MPL 不修改即可；默认用免版税的格式。
- 动画引擎 `modules/base/hanim/` 只依赖标准库，不允许依赖任何 UI 模块；动画是时间的纯函数（可以任意跳转、结果确定）；动画、布局、光栅器模块不允许使用 fast-math 和浮点收缩。
- 协作：当前的工作都放在同一个 PR（分支 `openmui-design`）里，每一轮修改提交一个 commit 作为检查点。
- 验收标准见实施计划 §19；能自动检查的都要进 CI。
- 组件和 native 模块都是“契约优先”：先写 `*.contract.omui.toml` / `*.native.omui.toml`，绑定由生成器产出，不手写。
- 脚本永远不进入每帧路径。
- ID 不能依赖显示文本；可交互的节点要写显式 ID。
- 视图代码优先（各语言原生的 builder）；数据文件一律命名为 `<名字>.<种类>.omui.{toml,json}`（逐行记录用 `.jsonl`，例如 `*.session.omui.jsonl`）；着色器用 `.slang`；逻辑代码保持原生后缀。builder 和数据文件都要让人和 agent 容易读懂（`view-data-files` 设计文档 §2）。
- 仓库根目录就是 `openmui` 伞包；`mcpp/` 只放构建系统的代码；入口在 `src/entry/`。
- 渲染器只写一次（`modules/gfx/render`），GPU 路径基于 `omui.rhi` 抽象层，各后端只依赖这个抽象层的接口。
- **平台相关的代码一律经由 `mcpplibs.os`**（文件、目录、进程、管道、IPC）；窗口和输入经由 SDL3（D-57）。
- **开发和分析工具都有两种界面**：命令行（JSON 输出、退出码，给 agent 和 CI）和用 openmui 自己写的界面（给人看）；两者读同一份数据（agent 协议、trace、bench 报告）。每个工具的界面都要突出框架的特色，工具本身就是展示（D-61）。
- **作品双视角**（D-63）：示例和展示要做成真实可用的工具、组件或内容，同时展示一项框架特色，并且有能在 headless 下确定重现的招牌镜头；不做只为演示而存在的 demo。
- **一切皆插件，管线可编排**（D-65、D-66）：核心只规定组件模型、数据契约和管线；动画、布局、文本、damage、光栅化、GPU 后端、宿主、效果、编解码、传输、脚本都是插件，按阶段或按批调用（不按节点、不按像素）；每个插件种类要有 CTS、trace 类别和日志类别。
- **可观测性是框架的一部分**（D-59）：新的子系统要有 trace span 和日志类别；性能声明要有 `tests/bench` 里的基准和 `budgets.bench.omui.json` 里的预算。
