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
| [2026-10-27-openmui-features-plugins-pipeline-design.md](2026-10-27-openmui-features-plugins-pipeline-design.md) | **特色、旗舰作品、可拔插架构与管线**：九项特色和各自的双视角旗舰作品（真实可用 + 展示 + 招牌镜头）、液态玻璃的原理和通用的背景效果能力、契约⑦（插件与管线）、插件种类、`*.pipeline.omui.toml` 管线编排 |
| [2026-10-28-openmui-progressive-disclosure-design.md](2026-10-28-openmui-progressive-disclosure-design.md) | **渐进式披露**：每项能力分 L0–L3 四层；帧需求与帧节奏（ProMotion / VRR，按预测上屏时间求值）；画布晋升策略和预热；构建期标记（四种语言语法不同、语义对齐）；构建插件拆分和经 mcpp-index 分发 |
| [2026-10-28-openmui-i18n-window-engine-design.md](2026-10-28-openmui-i18n-window-engine-design.md) | **国际化、自绘窗口与引擎分层**：i18n 引擎和数据分开（可独立发布，不用 ICU，从 CLDR 生成）、en / zh 的顶级支持（中文排版、格式、输入法）；框架自绘窗口外框为默认、原生可切换，窗口关闭时溶解且不阻塞输入；基础层、引擎服务、框架三层，openmui 作为游戏引擎的 UI 模块 |
| [2026-10-28-openmui-structure-render-web-game-design.md](2026-10-28-openmui-structure-render-web-game-design.md) | **目录与命名、统一渲染、Web 与 Toy、游戏引擎与格斗框架、媒体**（草案，待 review）：路径就是包名、`modules/` 按层分组、hanim；rhi → shader → 渲染图 → 渲染特性，2D 和 3D 同一套架构；产品分层（基础组件、openmui、mgame、gongfu、作品）；第三方库的 mwrap 规范和许可规则；wasm、`host.web`、WebGPU / WebGL2、Toy 能力映射、创意官网；mgame 引擎服务；gongfu 确定性回滚格斗框架和 5 个角色；音频、视频、时间线 |
| [2026-10-28-gongfu-characters-design.md](2026-10-28-gongfu-characters-design.md) | **格斗游戏的角色：墨和霓**（草案）：严肃而简约的原则、共同系统（段位、取消、冲刺、投技契约、倒地、追打点数、停顿和冻结、能量）、两个角色的完整招式表和帧数据、赢法和弱点、画面和打击感、对 gongfu 核心的新需求（G-1..G-13）、`gongfu.rules` 从动画烘焙受击框、实施顺序、验收、codex 评审记录 |
| [2026-10-26-openmui-view-data-files-design.md](2026-10-26-openmui-view-data-files-design.md) | **视图与数据文件**：代码优先、可读性要求、`*.view.omui.json` 视图文档、表达式（组合函数 + CEL 字符串）、行为状态机、后缀体系 |

`research/` 目录下是前期的调研和讨论稿。其中的命名（openui / mui）已经过时，一切以上面的设计文档为准。

新的设计记录按 `YYYY-MM-DD-<主题>-design.md` 命名，和 xlings 的 `.agents/docs` 保持一致。
