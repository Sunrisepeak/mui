# openmui 设计记录

建议按下面的顺序阅读：

| 文档 | 内容 |
|---|---|
| [2026-10-26-openmui-implementation-plan.md](2026-10-26-openmui-implementation-plan.md) | **实施计划**：技术基线和规则（不用宏、头文件最少、C++23 / 26）、workspace 成员划分和目录树、协作流程、各模块的实施、通用动画引擎、渲染、平台、语言绑定、agent、openmui-video、五个展示（含游戏和宣传片）、文档、CI、跨平台、生态补充、里程碑、**验收标准** |
| [2026-10-26-openmui-promo-video-design.md](2026-10-26-openmui-promo-video-design.md) | **宣传片**：三部爆款 agent 代码动画视频的对标拆解、8 个共同点、约 60 秒的创意和分镜、视觉语言、配乐、制作流程、验收标准（自动检查 + 对标评分） |
| [2026-10-26-openmui-architecture-design.md](2026-10-26-openmui-architecture-design.md) | **总体架构**：定位、原则、分层、规范和实现的关系、仓库和包、构建和生态、一致性分级、三根支柱、里程碑、决策记录（D-xx）、待决问题（Q-xx） |
| [2026-10-26-openmui-spec-design.md](2026-10-26-openmui-spec-design.md) | **规范**：registry、6 个契约、总线接口、动画语义、Core 配置档和扩展、发行版配置档、CTS |
| [2026-10-26-openmui-component-binding-design.md](2026-10-26-openmui-component-binding-design.md) | **组件、绑定与工程模型**：组件分层、契约优先、`[package.metadata.openmui]`、脚本薄接口、三种语言、native 模块、多语言混合、构建流水线 |
| [2026-10-26-openmui-canvas-markdown-design.md](2026-10-26-openmui-canvas-markdown-design.md) | **画布、Markdown 与旗舰示例**：画布的机制和策略分离、Markdown 组件、markdown-wall 基准测试、示例目录 |
| [2026-10-26-openmui-view-data-files-design.md](2026-10-26-openmui-view-data-files-design.md) | **视图与数据文件**：代码优先、可读性要求、`*.view.omui.json` 视图文档、表达式（组合函数 + CEL 字符串）、行为状态机、后缀体系 |

`research/` 目录下是前期的调研和讨论稿。其中的命名（openui / mui）已经过时，一切以上面的设计文档为准。

新的设计记录按 `YYYY-MM-DD-<主题>-design.md` 命名，和 xlings 的 `.agents/docs` 保持一致。
