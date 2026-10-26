# AGENTS.md

**openmui**：一套开放的 UI 规范，加上一个用 C++ Modules 写的参考实现。它的核心是统一的语义层：像素、无障碍、agent、测试都是同一棵语义树的不同渲染。

- 仓库暂时是私有的 `Sunrisepeak/mui`。**包名、模块名、命令、文档一律使用 `openmui`**，不要使用裸的 `mui`。
- 设计记录在 `.agents/docs/`。总体架构见 `2026-10-26-openmui-architecture-design.md`；其中的决策记录（D-xx）和待决问题（Q-xx）是当前的基线。
- 当前阶段：设计 review。代码会按里程碑 M0 开始搭建。
- 工程文件只有 `mcpp.toml`；构建、测试、打包都使用 mcpp；工具通过 xlings 安装。
- 第一阶段不修改 xdesk 或 xlings 仓库。

## 规则（会随着代码落地逐步改成 lint）

- 规范（`spec/`）只依赖标准库。实现层负责把 openmui 和 Luban 的规范（opensal / openas / openbus）连接起来。
- 依赖只能向下：spec ← runtime ← gfx / host；component 只使用 runtime 的公开 API；bridge 只依赖 spec；lang 只依赖 `openmui.h`。
- 核心代码不用异常、不用 RTTI，错误用 `std::expected`。C 头文件只出现在 `native/` 绑定模块里。
- 组件和 native 模块都是“契约优先”：先写 `*.contract.toml`，绑定由生成器产出，不手写。
- 脚本永远不进入每帧路径。
