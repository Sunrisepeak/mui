# AGENTS.md

**openmui**：一套开放的 UI 规范，加上一个用 C++ Modules 写的参考实现。它的核心是统一的语义层：像素、无障碍、agent、测试都是同一棵语义树的不同渲染。

- 仓库暂时是私有的 `Sunrisepeak/mui`。**身份用全名 `openmui`**（包、模块 `openmui.*`、总线接口）；**需要反复输入的地方用缩写 `omui`**（C++ 命名空间 `omui::`、C ABI `omui*` / `Omui*` / `OMUI_*`、`OMUI_EXT_*`、命令行短名）。不要使用裸的 `mui`。
- 设计记录在 `.agents/docs/`，从 `.agents/docs/README.md` 开始读。总体架构文档里的决策记录（D-xx）和待决问题（Q-xx）是当前的基线。
- 当前阶段：设计 review。代码会按里程碑 M0 开始搭建。
- 工程文件只有 `mcpp.toml`（openmui 自己的声明放在 `[package.metadata.openmui]`）；构建、测试、打包都使用 mcpp；工具通过 xlings 安装。
- 第一阶段不修改 xdesk 或 xlings 仓库。

## 规则（会随着代码落地逐步改成 lint）

- 规范（`spec/`）只依赖标准库。实现层负责把 openmui 和 Luban 的规范（opensal / openas / openbus）连接起来。
- 依赖只能向下：spec ← runtime ← gfx / host；component 只使用 runtime 的公开 API；bridge 只依赖 spec；lang 只依赖 `openmui.h`。
- 核心代码不用异常、不用 RTTI，错误用 `std::expected`。C 头文件只出现在 `native/` 绑定模块里。
- 组件和 native 模块都是“契约优先”：先写 `*.contract.omui.toml` / `*.native.omui.toml`，绑定由生成器产出，不手写。
- 脚本永远不进入每帧路径。
- ID 不能依赖显示文本；可交互的节点要写显式 ID。
- 后缀：`<名字>.<种类>.omui.toml`、`*.omui`、`*.omui.json`；逻辑代码保持原生后缀。`.omui` 必须保持人和 agent 都容易读懂（`omui-language` 设计文档 §2）。
