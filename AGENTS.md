# AGENTS.md

**openmui**：一套开放的 UI 规范，加上一个用 C++ Modules 写的参考实现。它的核心是统一的语义层：像素、无障碍、agent、测试都是同一棵语义树的不同渲染。

- 仓库暂时是私有的 `Sunrisepeak/mui`。**身份用全名 `openmui`**（包、模块 `openmui.*`、总线接口）；**需要反复输入的地方用缩写 `omui`**（C++ 命名空间 `omui::`、C ABI `omui*` / `Omui*` / `OMUI_*`、`OMUI_EXT_*`、命令行短名）。不要使用裸的 `mui`。
- 设计记录在 `.agents/docs/`，从 `.agents/docs/README.md` 开始读。总体架构文档里的决策记录（D-xx）和待决问题（Q-xx）是当前的基线。
- 当前阶段：设计 review。代码按[实施计划](.agents/docs/2026-10-26-openmui-implementation-plan.md)从 M0 开始搭建。
- 工程文件只有 `mcpp.toml`（openmui 自己的声明放在 `[package.metadata.openmui]`）；构建、测试、打包都使用 mcpp；工具通过 xlings 安装。
- 第一阶段不修改 xdesk 或 xlings 仓库。

## 规则（会随着代码落地逐步改成 lint）

- 规范（`spec/`）只依赖标准库。实现层负责把 openmui 和 Luban 的规范（opensal / openas / openbus）连接起来。
- 依赖只能向下：modules ← spec ← runtime ← host；component 只使用 runtime 的公开 API；bridge 只依赖 spec；lang 只依赖 `openmui.h`。
- **C++23 是基线**；C++26（反射等）只在 `runtime/reflect` 里使用（GCC 16，`-freflection`）。
- **不用宏，用 constexpr**：平台和特性用清单选择的模块分区 + `inline constexpr` 常量 + `if constexpr`；编译期校验用 `consteval`；断言和日志用函数加 `std::source_location`。唯一的例外是生成的 `openmui.h`。
- **全仓库只有一个头文件 `spec/include/openmui.h`**；其他全部是模块。第三方头文件只出现在 `native/` 实现分区里，宏在那里转换成 constexpr。
- 核心代码不用异常、不用 RTTI，错误用 `std::expected`；热路径没有虚函数，每帧零分配。
- 每个目录都是一个 workspace 成员（独立的 `mcpp.toml`），只通过 `path` 依赖互相引用，不允许跨成员用相对路径引用源文件。
- **独立组件放在 `modules/` 下**（geom、anim、layout、unicode、text、raster、image、render、gpu、gpu-*、record、encode）：不允许依赖 openmui 的任何其他包，只依赖标准库、彼此和第三方库，所以随时可以拆成独立仓库（D-55）。
- **优先使用 mcpp 生态**：mcpp-index 里已经有知名、跨平台的库时直接使用（JSON 用 `nlohmann.json`，TOML 用 `marzer.tomlplusplus`，任务用 `taskflow`……），不自己实现；只有生态里没有、或者无法满足确定性要求时才自己写（D-56）。
- 动画引擎 `modules/anim/` 只依赖标准库，不允许依赖任何 UI 模块；动画是时间的纯函数（可以任意跳转、结果确定）；动画、布局、光栅器模块不允许使用 fast-math 和浮点收缩。
- 协作：当前的工作都放在同一个 PR（分支 `openmui-design`）里，每一轮修改提交一个 commit 作为检查点。
- 验收标准见实施计划 §19；能自动检查的都要进 CI。
- 组件和 native 模块都是“契约优先”：先写 `*.contract.omui.toml` / `*.native.omui.toml`，绑定由生成器产出，不手写。
- 脚本永远不进入每帧路径。
- ID 不能依赖显示文本；可交互的节点要写显式 ID。
- 视图代码优先（各语言原生的 builder）；数据文件一律命名为 `<名字>.<种类>.omui.{toml,json}`（逐行记录用 `.jsonl`，例如 `*.session.omui.jsonl`）；着色器用 `.slang`；逻辑代码保持原生后缀。builder 和数据文件都要让人和 agent 容易读懂（`view-data-files` 设计文档 §2）。
- 仓库根目录就是 `openmui` 伞包；`mcpp/` 只放构建系统的代码；入口在 `src/entry/`。
- 渲染器只写一次（`modules/render`），GPU 路径基于 `omui.gpu` 抽象层，各后端只依赖这个抽象层的接口。
- **平台相关的代码一律经由 `mcpplibs.os`**（文件、目录、进程、管道、IPC）；窗口和输入经由 SDL3（D-57）。
