# Agent 时代收敛方案（TS 优先）：现行摘要

[Agent 时代收敛方案](2026-11-09-agent-era-ts-first-convergence-design.md)（第四版，约 16 万字节）的一页摘要，2026-11-15 编写（收敛与发布方案 RC1.7）。决策以原文 §1 为准；进度以 [STATUS](STATUS.md) 为准；0.1 的范围与标注以[收敛与发布方案](2026-11-15-convergence-first-release-design.md)为准。

## 它决定了什么

| 决策 | 内容 |
|---|---|
| D-138 | TS/JS 是 omui 唯一的上层语言；Lua 与 Python 的应用绑定删除（lint `legacy-import`）。TS 即代码也即数据，求值后的 JSON 快照是规范形式 |
| D-139 | 分层：C++ 负责引擎、组件与性能；契约、操作目录、主题、动画用 TS 声明，构建期由内嵌 QuickJS 求值成 JSON；TS 应用层最薄，不进每帧，可热重载；只允许可擦除语法，转写器自研 |
| D-140、D-141 | 界面内容全平台自绘；窗口与系统集成面可原生可自绘；webview 只作外部表面组件 |
| D-142、D-145 | 观察三模态（语义优先、任意区域截图、SVG），不做 computer use；自描述（`omui describe --api`）取代 skill 与 llms.txt，skill 只留 SOP |
| D-143 | AVX2 运行时分派 |
| D-144 | 运行时生成默认支持代码，开发模式下完全开放；发布时开放程度由 `omui.config.ts` 选择，再与用户配置取交集；沙箱、能力表、预演、可撤销 |
| D-146 | 可选的原生化属于 release 构建，以逐字节等价校验为前提 |
| D-147 | 测试整体归 mtest；初期只做局部测试 |
| D-148 | 交互记录与事实确认：从输入到效果的因果链，`ui.timeline`、`ui.explain`；记录分五级，读人的记录需要授权 |
| D-149 | 应用即设计器，Live Design 是首选范式：`omui dev` 默认可视，release 编译期去掉；TS 源码是唯一事实来源；组件级增量求值 ≤ 1 帧；设计过程由 hanim 呈现 |
| D-150、D-153 | `omui` 命令行是唯一的顶级入口，输出 JSON；omui 专属的构建步骤是 mcpp 插件，`omui build` 与 `mcpp build` 结果一致 |
| D-151、D-152 | 设计历史与设计回放（提交、分支、回退、对比，回放可编辑并导出）；UI、应用、动画、动画视频用同一套 Live Design |
| D-154 | 开发宿主持有窗口，应用是渲染输出目标；C++ 改动增量构建后按语义 ID 接回状态 |
| D-155 | 开发模式下 Agent 默认拥有全部控制权，是否问人由它判断；发布模式按授权管理 |
| D-156 | 工程结构：每类内容一个归宿（`app/`、`design/`、`contracts/`、`native/`、`ops/`、`assets/`、`tests/`），生成物在 `generated/`，本地状态在 `.omui/` |
| D-157 | 公开名按层面分：规范层面用 `openmui`，实现层面用 `omui` |
| D-158 | TS 接口两级：习惯的简洁层覆盖约 80%，完整层对接 native，同一条路径 |
| D-159 | 规范的注册表以 JSON 快照为规范形式 |
| D-160 | Live Design 的底座与领域无关（主体、历史、编辑、参与者与在场、细粒度认领），实现只沿 UI 推进 |

这些决策中长期有效的部分已写入 AGENTS §2（第 4 条）、§4、§5。

## 工作流

原文 §5 把实施拆成 W0–W16，P0 为 W0（mcpp 与 mtest）、W14（Live Design）、W15（`omui` 命令行）、W1（TS 顶级化）、W2（热重载）、W3（自描述）、W4（两组对照实验）、W12（交互记录）；P1 为 W5–W8；P2 覆盖面；P3 生态与原生化（W13）。各工作流的完成情况记在 [STATUS 归档](STATUS-archive.md)的“Convergence plan”一节。

## 与 0.1 的关系

收敛与发布方案把这里的能力按切片标注：TS 与视图文档、`omui` 命令行、Agent 面为稳定；运行时生成（`omui.generate`）、设计器（`omui.designer`）为预览；原生化为实验（§2.3）。W4 的 token 与步数对照在 0.1 重测（RC2.10）。
