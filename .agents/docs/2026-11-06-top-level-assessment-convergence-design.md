# omui／openmui 顶层评估与收敛方案

2026-11-06 起草，2026-11-08 第七版。第七版基线 `05b3921d`（PR #1 HEAD）。**维护者 review 稿，已含七轮维护者结论；第七轮新增的结论和进展见 §0.1 末尾几行与 §0.3，自我 review 见 §14。**

- 事实来自源码与清单阅读、PR #1 远端 CI 日志、评估台账、openas 0.2 规范和现有设计文档；本稿没有重跑全量测试，对 API 与架构的评价属于阅读判断。
- 已批准的条目在落地检查点中转成 D-xx。核心思想与规范已同步精简进 [AGENTS.md](../../AGENTS.md)；本文是完整依据。

## 0. 结论

### 0.1 维护者结论汇总

| 主题 | 结论 | 本文位置 |
|---|---|---|
| 主线与验证线 | **核心引擎和框架是主线；作品是对主线能力的验证**，承担一部分集成和验收测试的作用。产品自身的质量目标（例如格斗达到 SF6 水准）推迟到主线成熟之后 | §4.1 |
| 动画 | **通用动画能力收敛到 hanim**，做好 hanim 的架构和底层能力来支撑上层需求，避免重复实现和不一致 | §6 |
| Matter 归属 | 帧能力归 hanim，身份、语义和呈现归 UI 框架，组件只画状态 | §6.3 |
| 架构方法 | **局部 × 全局 × 分级**交叉设计，保证架构清晰、不重复、不一致 | §5 |
| xengine | **AI／Agent 原生的高级游戏引擎系统**（内部代号）：开发上人和 Agent 地位对等，部分场景 Agent 优先，人负责审查、可视化反馈和少量控制；游戏中 Agent 和人平等地玩，由开发者通过 openas 语义开启；同时完善 openas 标准 | §4.3、§8 |
| mgame | 并入 xengine | §9 |
| 渲染引擎 | 拥有独立品牌，内部代号 **xrender**。**RHI 若是纯通用抽象接口就留在 mlibs，具体实现归 xrender** | §7 |
| 通用工具链 | **通用的放 mlibs**：`mlibs.shader`（Slang 工具链）留在 mlibs | §7 |
| 成熟项目目录 | **在 `modules/` 下新增一个分组目录，专门放成熟、复杂、达到可独立成项目／仓库级别的品牌项目**：hanim、omui、gongfu、xrender，以及 **xengine（也作为独立品牌）**；每个分组目录（包括 `modules/base`）都有 README 说明定位和准入规则 | §9 |
| utils | 与领域无关、还不够独立成库的小工具放 `modules/base/utils`，按目录组织，复杂到一定程度再拆成独立库 | §9.3 |
| 首个 Agent 玩家 | **组件打砖块** | §8.4 |
| 成熟项目目录名 | **`modules/projects`** | §9 |
| omui 项目边界 | **`components/` 和 `lang/` 一起移入 `modules/projects/omui/`**；`tools/` 保持在顶层 | §9.2 |
| 毕业节奏 | **xrender、gongfu、xengine 先进入 `projects/`，M5 前补齐毕业标准** | §9.4 |
| 项目的标志性特性 | **能独立成项目放进 `projects/` 的，必须有几项明确的特性能在业界领先，甚至是为未来设计的** | §4.2、§9.5 |
| 一句话定义 | **每个项目都要能用一句话说清楚它是什么**；第六轮按维护者意见修订：omui 的强动画（由 hanim 提供）也是核心特性；hanim 是面向 UI 动画、视频和游戏动画的可组合、Agent 原生高级动画引擎 | §4.2、§9.5 |
| 根包与模板 | **根包 `src/` 和 `templates/` 移入 `projects/omui/`（`sdk/`、`templates/`），根 `mcpp.toml` 只保留 `[workspace]`** | §9.2 |
| 通用 CTS | 维护者提出做一套通用的 CTS 库／框架；本版给出设计：`mlibs.cts` 框架＋套件随契约 owner | §5.5 |
| 命名 | 一次改完，**不留兼容层**；不好分类或还不够独立的放 `xengine`，毕业标准见 §4.2 | §4.2、§10.2 |
| CI | **恢复 CI，但推进过程不被 CI 阻塞** | §10.1 |
| openas | 扩展从现在开始起草，用第一个实现验证后再定为规范 | §8.3 |
| PR | **单 PR，每个 commit 是一个检查点** | §12 |
| `mcpp.lock` | 不提交，加入 `.gitignore`（根目录和 `site/` 的锁都已从索引移除） | §10.5 |
| AGENTS.md | 精简为核心思想与规范，细节通过路径指向其他文档 | §12 |
| **官网重构（第七轮）** | 官网本身就是项目的作品：首页、组件墙、动效（招牌作品＋配方库）、Agent、文档五页；41 个规格样本移到文档；旧页面在新页就绪后整页替换。详见[官网重构与 Mui 方案](2026-11-08-site-redesign-mui-design.md) | §6.5、§11 |
| **质感（第七轮）** | 基础组件和动画要有质感，核心是设计细节；细节规范（动效时长与曲线、起点感知、打断连续、中间态、按压／悬停／焦点／拖拽、连续曲率圆角、分层阴影、发丝线）**作为 omui 框架的默认值**，官网只是第一个消费者；先做质感底座，再重做页面 | 官网方案 §3 |
| **品牌（第七轮）** | 标志是 O 形轮廓（可艺术化，不必是字母）内含 mui；品牌形象 **Mui（喵）**：像素风、每个像素都是矢量的猫，可与标志互相变形；AGENTS.md 品牌规则已同步 | 官网方案 §8 |
| **官网上的 Agent（第七轮）** | 官网不托管大模型，只接入访客本地的 Agent；由 Mui 引导（代替文字步骤）并作为 Agent 在页面上的身体；离线时播放标注为脚本的演示 | 官网方案 §9 |
| **动效词汇统一（第七轮）** | omui 的动效槽引用 hanim 的配方名，出现／消失／强调等参数由配方换算；状态之间的匹配与形变仍归 UI | §6.4 |
| **矢量截图（第七轮）** | 截图可以是 SVG：每个节点的绘制标出路径，SVG 表达不了的部分是标明原因的像素岛；已完成并进入 openmui 规范 | §7 |
| **mcpp（第七轮）** | 升级到 2026.11.7.2（2026.11.7.1 修复了 mcpp#897），删除为它加的绕过 | §10.5 |

### 0.2 总体判断

项目地基宽、方向对。当前主要矛盾是**收敛**：

- 动画领域有 7 套实现。
- 同一个全局概念（句柄、错误、时间、预算）在代码里有好几种形状。
- 路线有九套编号。
- CI 从 11-01 起持续红。
- 所有评估对象在 52–65 分之间。

下一阶段的目标：一个动画引擎、一套全局契约、一套命名、一张路线图、一条可信的 CI 主干。

### 0.3 2026-11-08 进展与判断

M0–M3 已完成，M3.5 的四个招牌镜头和 M4 的退出条件在本地满足，M5 进行中。

- **已经打通的**：一棵语义树驱动像素、无障碍、Agent 和测试；动画收敛到 hanim（UI、动画工作室、宣传片都已使用）；xrender 的 CPU 与 GPU 同源；每个公开产品都有操作目录和 skill；跨平台逐字节确定的像素；矢量截图带来源。
- **有实测支撑的“领先”只有两项**：跨平台确定性；hanim 的交互动效（同一 V8 中比 GSAP 平均快约 4.8 倍）。
- **主要短板**：
  - xrender 的 CPU 光栅速度比 Skia 慢 1.6–2.9 倍（画质相当）；
  - 官网的设计、质感和首帧；
  - 游戏作品的质量；
  - omui 与 hanim 两套动效词汇；
  - 各项目的标志性特性大多还缺对照证据。

M5 因此调整为：先补质感底座和统一动效词汇，再按[官网重构与 Mui 方案](2026-11-08-site-redesign-mui-design.md)重做官网（§11）。

## 1. 事实底稿

| 项 | 数据（基线 `810d1fc`） |
|---|---|
| 规模 | PR #1 共 509 个提交；+381,207／−330 行，2154 个文件；122 个 workspace 成员加根包；约 1082 个 C++ 源文件、约 21.9 万行 |
| 文档 | `.agents/docs` 55 份约 1.9 MB；面向用户的 `docs/` 只有 7 个文件；AGENTS.md 已从 19.4 KB 精简到 10.2 KB |
| CI | 最后一次全绿是 11-01 的 `26d1e9a`。HEAD 上：三平台各 5 个成员失败；macOS Metal 绑定失败；Windows／omui 打包因 `WeightedPaint` 失败；Web 因缺少 `hanim.render.vector.pcm` 规则失败；GCC 跑满 2 小时被取消；Pages 因锁文件失败（锁已改为不提交，§10.5）；Nightly 失败 |
| 评估 | ui-framework 63、animation 62、renderer 65、game-engine 62、site 62；作品 25–65；11-01 之后没有再评 |
| 命名 | 成员身份：openmui 71、mlibs 26、xengine 8、gongfu 7、mgame 5、hanim 2；166 个首方文件仍 import 兼容转发入口 |
| 动画 | 7 套实现（§6.1）；Program 契约版本在 3 天内从 1 升到 8 |
| 全局形状 | 句柄 3 种以上；19 个错误枚举，各配一个 Failure 结构；时间有 4 种表示，hanim.core 和模拟各有一个 Clock；9 个 Limits 结构且相互重复 |
| 依赖异味 | `mlibs.raster` 依赖 `hanim.core`，只为了用 `dm::atan`；`mlibs.text` 依赖 `xengine.render.vector` |
| openas 0.2 | 面向 verb 执行（参数、按行输出的 envelope、actor、grant）；§8 把令牌、过期、预算、停止 actor、运行记录列为“未定”；没有实时会话的概念 |
| 性能 | 约 60 项自定预算；没有竞品对照；GPU 只在 lavapipe 上测；Web 4,885,529／5,000,000 B |

## 2. 多视角评估

评级是评审判断：**强／较强／中／弱／缺**。

| 维度 | 评级 | 优点 | 主要问题 |
|---|---|---|---|
| 产品定位 | 较强 | 一棵语义树、多种渲染；规范与实现分离；Agent 原生 | 范围远超收敛能力；“领先”缺少对照 |
| 架构／分层 | 较强 | 分层由 lint 强制；局部×全局准则；单一 owner 原则 | 执行分叉：动画 7 套、全局形状不统一、兼容入口不删、基础库反向依赖引擎 |
| UI 框架 | 中 | builder 简洁；viewdoc；四种语言；C ABI；无障碍；headless 像素 | `openmui.view` 单体（约 1.2 万行、17 个依赖）；组件库 65、Web 63、性能 67 |
| 动画 | 中 | core 原语完备；Program 编排和控制；UI 的显示列表形变强 | 原语、编排、表示、播放器分散在 7 处；公开 API 没用上 core 的能力 |
| 渲染 | 中 | 渲染图＋特性、RHI 抽象与三后端、Slang、CPU／GPU 对照 | Windows 和 Metal 有缺陷；只在软件 GPU 上验证；CTS 没有源码 |
| 游戏引擎 | 中 | 确定性 ECS、回滚联机、固定步长模拟 | 分散在 mgame 和 xengine 两个名字下；Agent 游玩没有设计 |
| 组合性 | 较强 | 编排树、作用域嵌套、可选控制层 | 配方返回类型不一致；两种 effect 共用一个动词；UI 与 hanim 手工接线 |
| 简洁／语义 | 中 | UI builder 很好；用 expected，不用宏 | 属性袋、别名、重载堆叠；用户面对 4 套命名前缀 |
| Agent 原生 | 较强 | UI 协议完整（UI 评估中该维 82 分，为 UI 各维最高） | 各引擎操作各自定义；没有 skill 格式；没有真实 LLM 和游戏 Agent |
| 性能 | 中 | 零分配基准、交付门禁 | 没有对照和真实设备；Web 余量小；CI 慢 |
| 标准 | 较强 | registry 生成链、CTS 分级 | 规范跟随实现；openas 缺少实时和预算语义 |
| 目录／包 | 中 | 路径即包名、层级 lint、D-99 | 极小包较多；品牌产品跨层散落；仓库根有散文件 |
| 工程治理 | 弱 | 证据报告详尽 | CI 红灯常态化；文档和编号失控；评估停在 11-01 |

## 3. 根因

1. **验证仪式代替了反馈闭环。** 本地证据很多，CI 结果没有回到推进流程里。
2. **“新增并保留旧的”成了默认迁移方式。** 兼容入口没有期限，组合面单调增长。
3. **路线编号叠加，没有一个排序的 backlog。**
4. **广度优先。** 产品线和主线争夺精力。
5. **全局契约只停在设计层面。** 六类共同契约已经在综合方案 §2 定义过，但没有落成代码，也没有 lint 和 CTS 强制，于是各引擎各自定义形状。
6. **领域内核缺少“唯一 owner 的分层结构”。** 消费者直接拿底层原语自建上层，产品层又没有建在底层的通用能力上。

## 4. 定位与体系

### 4.1 主线与验证线

| | 主线 | 验证线 |
|---|---|---|
| 内容 | hanim、xrender、xengine、omui、mlibs，以及工具的 CLI／openas 入口 | 官网、showcase、examples、格斗、PPet、打砖块、宣传片、工具的 GUI |
| 目标 | 能力、架构、性能、Agent 原生 | 证明主线能力在真实组合下可用、好用、够快 |
| 规则 | 新能力只在主线实现 | 只消费公开 API，不写引擎代码；遇到缺口作为主线需求提出 |

- **每个作品维护一张验证矩阵**：验证哪些主线能力、用什么场景、哪些招牌镜头、哪些指标。作品工作的排期跟随主线里程碑：主线某项能力落地，相关作品就迁移过去做验证。
- **作品能替代的测试**：集成测试、易用性、真实负载下的视觉和性能、Agent 自动试玩（§8.2）。
- **作品不能替代的测试**：单元测试、CTS、边界用例。
- 作品测试以语义和结构不变量为主，加少量固定帧，放在分片的独立通道里跑；失败时先分类：是主线回归，还是作品自身的变化。
- 作品质量目标（SF6 水准、“首先要好玩”等）在主线成熟后恢复。

### 4.2 命名分级与毕业标准

| 级别 | 用途 | 例子 |
|---|---|---|
| 规范 | 开放标准 | `openmui`（OpenLifa）、`openas` |
| 独立品牌（`modules/projects/`） | 成熟、复杂、按可独立成项目／仓库的级别经营的产品 | `omui`、`hanim`、`gongfu`、`xengine`；`xrender`（内部代号，§7） |
| `xengine.*` | xengine 品牌（AI／Agent 原生游戏引擎系统，名字为**内部代号**）的子系统，包括暂时不好分类、还不够独立的领域能力 | simulation、audio、video、timeline、world、input、physics、net |
| `mlibs.*` | 与领域无关的通用库和纯抽象接口 | math、geom、memory、rhi、shader、text、layout |
| `mlibs.utils` | 与领域无关、还不够独立成库的小工具（按目录放） | hash、log、trace、testing |

**毕业标准**（xengine 的子系统或 mlibs 库获得自己的品牌，成为 `modules/projects/` 下的独立项目；或 utils 中的工具拆成独立库）：
- **（仅对 `projects/`）能用一句话说清楚它是什么**：这句话写在项目 README 的第一行，也是对外介绍的第一句；说不清楚，往往说明边界还没想清楚；
- **（仅对 `projects/`）有几项明确的标志性特性**：在业界领先，或是为未来设计的（例如 Agent 原生）。每项都要写清楚“领先在哪里、和谁比、证据是什么”：可以对照的用竞品基准证明（§10.4）；面向未来的写明设计判断，并用真实任务演示。只有“写得好”但没有独特之处的库，留在 mlibs；
- 至少 2 个独立消费者；
- 公共 API v1 冻结；
- 有 ops 和 skill；
- 有 CTS、文档和基准。

**代号只用在包名和命名空间里**，不进入协议字段、数据文件或 C ABI，这样以后改名只是机械操作。

### 4.3 xengine：AI／Agent 原生的游戏引擎系统

- **基础层**（在 UI 之下）：simulation、audio、video、timeline，以后的 compute 和 AI 决策。omui 也可以使用。
- **游戏层**（在 omui 之上，原 mgame）：world、input、physics、net、ui（把游戏接入 omui），以后的玩法、场景和 Agent 游玩。
- **xengine 本身也是独立品牌**，和 omui、hanim 并列放在 `modules/projects/xengine`；它内部容纳尚未毕业的子系统。
- **体系内的其他品牌**：hanim（动画）、xrender（渲染）是 xengine 体系使用的引擎；gongfu 是建在游戏层之上的类型框架；omui 是体系的 UI，同时也独立服务非游戏应用。
- **Agent 原生体现在两方面**：开发引擎时 Agent 优先（§8.1）；游戏中 Agent 和人平等地玩（§8.2）。

## 5. 架构方法：局部 × 全局 × 分级

### 5.1 三条轴

- **局部**：每个引擎按本领域最优的方式设计内核模型、IR 和实时约束（动画的 Program、渲染图、模拟步进、音频图、UI 树），不了解自己的消费者。
- **全局**：凡是组合时必须一致的东西都统一形状和规则；**不做万能运行时，不强行统一领域语义**。
- **分级**：每个公开 API 提供 L0 一句话调用、L1 参数、L2 组合、L3 显式准备／检查／预算、L4 扩展，所有层走同一条执行路径。

### 5.2 全局契约矩阵（现状 → 统一方式）

| 契约 | 现状 | 统一方式 | owner |
|---|---|---|---|
| 句柄 | `mlibs::memory::Handle<Tag>` 已存在；hanim 的 `ObjectRef`／`PlaybackRef` 各自定义了 `{owner, slot, generation}`；`Set::Channel` 又写了一个 | 一个 `Handle<Tag>`，可选“所属引擎”字段；各领域只保留类型标签 | `mlibs.memory` |
| 错误 | 19 个枚举，各配一个 Failure 结构（嵌套多个可选原因）和一个取名函数 | 统一形状 `Error<Code>{code, where, causes}`，统一的名称和描述，统一映射成 openas 错误；错误码归各领域 | 新增小契约（放 `mlibs.utils/error`，有 2 个以上消费者后视情况独立） |
| 时间 | `seconds`（double）、Flicks（int64）、固定步长、样本时钟；hanim.core 和模拟各有一个 Clock | 统一类型、精确换算和映射；时钟语义仍归各领域（时间源 ≠ 步进器） | 新增 `mlibs.time` |
| 值和通道 | hanim 有 `lanes<T>`；UI 属性和音频增益自动化各成体系 | 可动画的值统一为 `lanes<T>`＋`Channel<T>`；UI 属性和音频自动化都经 hanim 求值 | `hanim` |
| 准备与执行 | hanim 和渲染已经自然形成同一种模式（10 个 `Prepared*` 类型） | 写成约定：`prepare(desc, budget) → expected<Prepared>`；计划不可变、可共享；执行 noexcept；失败不部分发布 | 约定＋契约 CTS |
| 预算 | 9 个 Limits 结构，`max_total_parts` 等字段在多层重复 | 统一预算词汇和逐层汇总规则 | 约定 |
| 资源生命周期 | 各引擎各自管理 | 资源身份、版本、租约（综合方案 §2 已设计） | `mlibs.memory`／后续 resource |
| 操作与观测 | UI 协议、hanim 13 项、cellular 12 项各写各的 | 操作目录＋skill（§10.3），统一 trace 和日志类别 | `mlibs.control`＋生成器 |
| 能力查询 | 各引擎各自表达 | 统一的能力和降级描述 | 约定 |

### 5.3 保证不重复、不一致的机制

1. **能力归属表**：一份机器可读的“能力 → 唯一 owner 包”清单，lint `owner` 检查 owner 之外有没有重新实现（缓动、弹簧、OKLab、槽位表、时钟、句柄等）。现有的 `math`、`hash`、`motion` 三条 lint 并入这条规则。
2. **契约 CTS**（基于 §5.5 的 `mlibs.cts`）：同一套测试跑在每个引擎上，例如过期句柄检出、错误映射、准备失败保持原状、预算失败不部分发布、能力不足时明确拒绝。
3. **分级检查表**：公开 API 评审时对照 L0–L4，确认每层都存在且走同一条执行路径。
4. **全局契约从真实消费者中提取**：至少 2 个消费者才提取；契约是只依赖标准库的小模块，不做大 SDK。
5. **依赖方向检查**：mlibs 不得依赖品牌引擎或 xengine（修复 §1 的两处异味：raster 改用 `mlibs.math`；text 只输出自己的排版结果，转成显示列表的适配放在 xrender 一侧）。

### 5.4 以 hanim 为例的三轴交叉

| 层级 | 局部（动画自身） | 全局（必须遵守） |
|---|---|---|
| L0 | `engine.play(anim::pulse(card))` | 统一的句柄和错误 |
| L1 | `.duration(300ms).easing(...)` | 统一的时间类型 |
| L2 | `sequence`、`parallel`、`composite` | 组合期间无副作用 |
| L3 | `prepare(animation, budget)`、`inspect` | 准备与执行约定、预算、观测 |
| L4 | `ch::define<T>`、注册表示和算子 | 能力查询、操作目录、版本 |

xrender、xengine、omui 按同一张表设计各自的 L0–L4，例如渲染的 L0 是“渲染场景得到图像”，模拟的 L0 是“按规则推进 N 步”，音频的 L0 是“播放一个声音”。

### 5.5 通用一致性测试框架 `mlibs.cts`

**现状**：四套一致性测试各写各的。

| 现有 | 形态 | “不支持”的处理 | 报告 |
|---|---|---|---|
| `mlibs.rhi.cts` | `run(device)` 返回 `{passed, name, detail}` 列表 | 没有区分 | 由调用方的 check 打印 |
| `xengine.simulation.cellular.rhi.cts` | `run(device)` 返回类型化报告或第一个失败 | 明确“不可用不算通过” | 自定义报告 |
| `xengine.render.cts` | 只是测试成员，没有库，src 为空 | 环境变量 `OMUI_REQUIRE_GPU` 决定 | 测试输出 |
| `tests/cts`（`openmui.cts`） | 规范 S 级：JSON 场景＋固定 trace | 无 | 差异列表 |

此外，§5.3 的契约 CTS、D-65／66 要求的“每类插件都有 CTS”、§9.5 的标志性特性证据，都需要同一种能力。所以**做一套通用框架是对的**：这是一个典型的“全局契约”。

**框架与套件分开**：

| 部分 | 内容 | 归属 |
|---|---|---|
| 框架 `mlibs.cts` | 套件和用例的注册（稳定 ID、版本、所检查的契约及其版本）；一致性等级（沿用规范的 S 语义／A 逐位一致／B 带声明容差）；被测对象适配（模板／concept 注入，例如一个 `rhi::Device&`）；**三态结果：通过／失败（位置和原因）／不支持（缺哪项能力）**，“必需／可选”由运行方策略决定，不可用永远不算通过；比较工具（逐位、带预算的容差、固定哈希、图像差异、按时间采样的过程帧）；数据驱动场景（`*.scenario.omui.json` 加固定 trace）；机器可读报告 `*.cts.omui.json`（套件、版本、被测对象身份、平台／后端／工具链、每条结果、证据哈希） | `modules/base/cts`：通用库，因为需要 JSON 等第三方依赖，所以不进 utils，单独成库 |
| 套件 | 某个契约的具体用例 | **随契约 owner**：放在 owner 包的 `src/cts/`，用 feature `cts` 选择，只在测试中启用 |

**各套件的去向**：

| 套件 | 去向 | 运行方 |
|---|---|---|
| RHI 接口一致性 | `mlibs.rhi` 的 `src/cts/`（取代 `mlibs.rhi.cts` 包） | xrender 的三个后端 |
| 渲染一致性 | `xrender` 的 `src/cts/`（取代空的 `xengine.render.cts`） | CPU 和 GPU 路径 |
| 模拟 GPU 一致性 | `xengine.simulation` 的 `src/cts/`（取代 `cellular.rhi.cts` 包） | SDL_GPU 和浏览器 WebGPU |
| 规范 S 级 | 场景和 trace 仍在规范仓库（纯数据，规范仍只依赖标准库）；运行适配在 `omui` 的 `src/cts/`（取代 `tests/cts`） | 四种语言 |
| 全局契约（句柄、错误、准备与执行、预算） | 各契约的 owner | 每个引擎注册自己的被测对象 |
| 插件种类 | 插件种类的 owner | 每个插件实现 |

**带来的好处**：
- 删除 3 个只为 CTS 存在的小包。
- “不支持”的语义全仓统一。
- 规范的其他实现也可以直接用 `mlibs.cts` 跑 openmui 的场景。
- 报告可以直接作为 STATUS 的能力矩阵和 §9.5 标志性特性的证据。
- **Agent 原生**：`omui cts list／run --json` 输出结构化结果和 openas envelope；失败时附带图像和过程帧，可以 headless 审查；框架本身也提供 ops 和 skill。

**约束**：
- 框架只做测试，作为开发依赖使用，不进入任何发布产物。
- 不用宏；不在被测热路径上插入虚函数调用。
- 性能基准不放进 CTS（基准有自己的预算机制）；两者共用报告格式和平台信息。

## 6. 动画能力收敛到 hanim

### 6.1 重复地图（现状）

| # | 实现 | 位置 | 问题 |
|---|---|---|---|
| 1 | `Set` | hanim.core，UI 属性过渡在用 | 一个独立的播放器 |
| 2 | `Motion<T>`、Spring、Decay | hanim.core | Program 用不上弹簧和速度延续 |
| 3 | `Timeline`、`TimeMap`、`Fill` | hanim.core，宣传片在用 | 和 Program 是两套编排模型 |
| 4 | `Program`、`Engine` | hanim | 只有 4 个固定通道＋闭合轮廓 |
| 5 | `Matter`、`Morph` | `openmui.motion` | 形变最强，但只给 UI 用 |
| 6 | Binding、Playback | `omui.hanim` | 每个 Playback 各持一个 Engine，需手动接线 |
| 7 | `Bone`、`Clip` | gongfu.present2d | 独立的骨骼系统 |

### 6.2 目标分层

```text
L6 配方库      40 个公共动画（pulse/follow/enter/exit/reorder…）
L5 执行        一个宿主一个 Engine：timeline 模式（可 seek）＋ interactive 模式（替代 Set，SoA、零分配）
               统一通道仲裁、预算、原子发布、观测、control feature
L4 编排        Program ＋ 时间映射节点（吸收 Timeline 的 clip/TimeMap/Fill/marker）；约束与驱动（M3.5）
L3 表示        property · shape · visual（Matter 的通用部分）· image scope；
               M3.5 补齐：glyph／path adapter · stroke（trim／书写）· 拓扑对齐 morph · 选择器＋形变器
               · 匹配修饰与路径扰动核；像素网格（方块部件、散开与拼回、像素↔平滑路径，第七轮从 M6 提前到 M5）；
               后置（M6）：rig/particles/field
L2 通道        Channel<T> = 目标 × 通道 ID × lanes<T>，用户可自定义；同通道混合层（M3.5）
L1 曲线        Ease · Tween · Spring · Decay · Motion<T> · Path（hanim.core，不变）
L0 数值        lanes<T> · OKLab · Transform2D · mlibs.math（hanim.core，不变）
```

- L2–L6 放在 hanim 包内，按 `src/<职责>/` 和 feature 组织；`visual` feature 依赖 xrender 的显示列表，默认闭包不受影响。
- interactive 模式和 timeline 模式共用通道表和仲裁规则：timeline 可以显式接管 interactive 通道，结束时带着速度交还。入口用 `drive()` 和 `play()` 两个动词区分。

### 6.3 Matter 归属

| 职责 | 内容 | 归属 |
|---|---|---|
| 绘制格式 | 显示列表、节点标记 | xrender（vector 特性） |
| **帧能力** | 片段分类、变换／裁剪／不透明度、对应匹配、字形 LCS、OKLab、矩形／路径／字形插值、出现和消失、stretch、stagger、弹簧、按时间采样 | **hanim `visual` 表示** |
| **显示归属** | 节点 key 和语义 key 的来源、何时变化、嵌套放置、布局跟随、文字对比度保护、reduced／off、命中／焦点／IME 一致、发布到呈现 | **UI 框架**（view 的 motion 分区） |
| 组件 | 只画状态，可选声明部件名（D-93） | 组件 |

### 6.4 UI 接入

UI Runtime 帧管线：input → state → **motion 阶段（唯一 Engine）** → 布局与呈现 → paint。
- 节点槽声明 motion 意图，风格包给出配方 ID 和参数，UI 提供表示和身份，hanim 负责求值。
- **第七轮补充：统一动效词汇。** 现状是两套词汇：omui 的槽保存一组过渡参数（升起距离、缩放、弹簧、错开顺序），hanim 有 40 个具名配方（`fade_in`、`rise_in`、`pop_in`…），二者常常描述同一件事。改为：槽引用 hanim 的配方 ID 和 `RecipeOptions`，出现、消失、强调等参数由配方换算；状态之间的匹配与形变（同一对象在两种布局或两个值之间）仍是 UI 的能力，不进配方库。风格包按配方 ID 覆盖参数。
- `Set` 的用法改为 interactive 模式；`openmui.motion` 包删除；Playback 改为轻量句柄。
- spec 中定义 motion 槽的语义，并和配方 ID 建立映射。

### 6.5 其他消费者

| 消费者 | 收敛后 |
|---|---|
| 宣传片 | **已完成（2026-11-07）**：曲线全部来自 hanim（`smoothstep`、`window`），每个镜头的时间编排声明在一条 hanim Timeline 上（`Choreography`，70 个区间，可由 `omui-promo choreography` 列出）；画面逐字节不变 |
| `xengine.timeline`（编辑模型） | 只保留编辑职责，动画轨道交给 hanim 求值 |
| 音频增益自动化 | 经 `Channel<T>` 由 hanim 曲线求值，样本时钟仍归音频 |
| gongfu | hanim 有 rig 表示后迁移；在此之前别处不再新建骨骼系统 |
| canvas 相机 | 保留 MOTION-OK 例外，改用 L1 曲线的公开入口 |
| 官网 | **第七轮修订**：原“40 个设计／20 个创意组合”改为 8–12 个招牌作品＋配方库（hanim 的 40 个配方，活的缩略图），41 个规格样本移到文档；全部经统一动效词汇使用配方（官网方案 §7） |

### 6.6 hanim v1 API（不保留兼容名；语法是候选，先测编译、BMI 和 Wasm 成本）

```cpp
auto a = anim::to(card, ch::position = {120, 40}, ch::fill = rgba(0x3355ffff)).duration(300ms);
auto b = anim::by(card.part("icon"), ch::rotation = 0.5);
auto c = anim::along(card, route);
auto d = anim::spring(card, ch::position = {0, 0}, springs::snappy);
auto all = sequence(a, wait(100ms), parallel(b, c)).repeat(3).alternate();
auto img = composite(all).with_effect(fx::blur(0, 4));
inline constexpr auto glow = ch::define<float>("app.glow");
engine.drive(node, ch::opacity).spring_to(1.0);
```

- 一个 `Animation` 值类型；类型化通道；时间映射是 Program 节点；统一错误（§5.2）。
- 直接删除 `Morph`、`MorphTo`、`PreparedObjectMorph`、多余的 `play`／`retarget` 重载。
- v1 冻结后只做加法。

### 6.8 表示与形变能力补齐（M3.5，2026-11-07 维护者确认）

M2 交付了 L2–L5、visual 表示与 v1 形状后复核（D-129）：hanim 是扎实的 2D 矢量对象＋属性＋编排＋播放内核，但“任意可视对象顶点化后做 A→B 变换、用 effect 改变匹配与过程、字体局部动画、轮廓描边动画、跟手的几何物理形变”还做不到。缺口与归属：

| # | 能力 | 现状 | 归属 | 验收例 |
|---|---|---|---|---|
| 1 | glyph／path adapter：字形轮廓 → `mlibs.geom` 路径 → hanim 对象（每个字形一个部件，带来源映射） | xrender `GlyphSource::outline()` 有轮廓，hanim 无适配 | 几何归 `mlibs.geom`；字形归 xrender text；对象化归 hanim | 一行文字的每个字形可单独做属性和形变动画 |
| 2 | stroke 表示：描边的 trim 起点／终点／偏移、宽度、颜色通道；按弧长裁剪 | 渲染层有 `dashes`／`dash_offset`，hanim 无描边表示 | 动画归 hanim，按弧长裁剪与绘制归 xrender | “一”的轮廓上一段红色绕行一圈；焦点环跑圈；书写效果 |
| 3 | 拓扑对齐 morph：轮廓数／孔数不同时按契约拆分、从点生长、孔的出现与闭合 | 轮廓数不同直接拒绝（`contour_count_mismatch`） | hanim（匹配策略），几何算法归 `mlibs.geom` | A→B、“一”→“二” 连续，无静默跳变 |
| 4 | 选择器＋形变器：软选择（区域／锚点半径＋衰减）、位移／晶格／弯曲／扭曲，位移是类型化通道；几何允许随弹簧过冲 | 无区域选择；morph 几何限制在两端内 | hanim | A 的横杠被拖下、松手后皮筋式来回弹（`drive` 跟手） |
| 5 | 匹配修饰与路径扰动核（`MatchModifier`、noise／curl／绕轴／沿法线＋包络） | 只有 `vertex_path`／`translation_path` | hanim（IR 设计 §3） | 同一 A→B 换匹配规则仍合法；扰动端点与切线保持、乱序 seek 一致 |
| 6 | 同通道混合层（replace／add／multiply／blend，带权重和顺序） | 重叠写入被拒绝 | hanim（L2） | 呼吸动画叠加在拖动上 |
| 7 | 约束与驱动（跟随、朝向、表达式依赖图，检测环） | 无 | hanim（L4） | 一个对象跟随另一个，环被拒绝 |
| 8 | GPU 批量求值（形变、匹配、扰动在设备上） | GPU 只用于渲染 | hanim 计划＋`mlibs.rhi` 计算 | 与 CPU 参考同语义，误差声明 |
| 9 | 弹性体（任何可视对象像皮球、橡胶一样：掉落、触地挤压、反弹、回稳），由下面的通用能力组合，不为某种对象单独做：①笼形／晶格形变载体（第 4 项的一种形变器，包住任何顶点化对象：形状、字形、组件外形，以后的位图网格）；②确定性软体求解：粒子＋约束（距离、面积保持、形状匹配回复）、重力、阻尼、碰撞体（平面、矩形，由宿主给出，例如布局边界），固定步长、checkpoint 与重放，其结果写笼的控制点通道；③由运动导出的形变：按速度、加速度和接触做保面积的挤压与拉伸，纯时间函数的动画（取曲线导数）和模拟同样可用；④解析的抛落与弹跳曲线（重力、恢复系数、次数，闭式求值），不需要模拟时使用，`drive` 增加落到地面的目标 | 只有弹簧过冲的属性；无形变载体、无模拟表示 | 按 §6.9 拆分：①③归 hanim；②的求解核归 `xengine.simulation`（base 层，与 xengine.physics 共用），hanim 只把它包成一种模拟表示，步进与重放用 `xengine.simulation` 的时钟和 timeline；④归 hanim.core 的曲线；积分器归 `mlibs.math`，碰撞查询归 `mlibs.geom` | 同一能力用在圆、字母 A 和一张卡片上，零对象专用代码：过程帧覆盖下落、触地挤压（面积误差声明）、反弹、回稳；中途被拖动（`drive` 接管）再松手继续；同输入重放像素逐位一致 |

顺序：1、2 先行（改动最小，组件立即可用：焦点、进度、勾选的描边动画），然后 3、4、5，再 6、7、9（9 依赖 4 的形变器和 7 的驱动），最后 8；rig、粒子、场仍在 M6。每项验收：按时间采样的过程帧（端点、多个中间态、打断与反向）、CPU／GPU 实际像素、`mlibs.cts` 用例、基准；组件只经运动槽配方使用这些能力（D-93），语义与命中由 UI 保证一致。

### 6.9 动画与物理／模拟的通用基础能力：拆分与归属（D-131）

高级动画引擎（hanim）和物理／模拟引擎（`xengine.simulation`、`xengine.physics`）一般所需的基础能力，按“两边都要的放在两边之下、只有一边要的归那一边”拆到不同模块，在不同阶段实现；上层只组合这些能力，不重复实现（§2 原则 3，lint `owner` 随 owner 落地增加行）。依赖仍只向下：`mlibs.math`、`mlibs.geom`、`mlibs.time`、`xengine.simulation`（CPU 部分在 base 层）← hanim（gfx）← omui ← xengine 游戏层（含 `xengine.physics`）。

| 基础能力 | 归属 | 动画侧消费者 | 物理／模拟侧消费者 | 阶段 |
|---|---|---|---|---|
| 确定性数值：det 数学、随机、噪声（value／gradient／curl，带 seed）、求积（Gauss–Legendre）、积分器（半隐式欧拉、Verlet、RK4）、小型线性求解与求根 | `mlibs.math` | 路径扰动核、曲线导数、弹簧 | 求解核、刚体、粒子 | 噪声与求积随 M3.5 第 3、5 项；积分器随第 9 项 |
| 几何度量与查询：Bezier／路径的弧长、按弧长取点、有符号面积、质心、最近点、包围盒；距离、相交与扫掠（圆、线段、多边形）；空间索引（网格、BVH 宽相）；三角化与布尔（M6） | `mlibs.geom`（补 double 精度；hanim.core 改为可依赖 `mlibs.geom`，形状度量迁入后删除自己的副本） | 拓扑对齐 morph、描边 trim、选择器、命中 | 碰撞检测（`xengine.physics::sweep` 的几何部分迁入，physics 只留响应与规则） | 面积／质心随第 3 项；扫掠迁移随第 9 项；空间索引、三角化随 M6 |
| 时间与步进：Flicks／Rate；固定步长时钟、checkpoint 与输入重放 | `mlibs.time`；`xengine.simulation` | hanim 的有状态表示（软体，以后的粒子） | 游戏、模拟 | 已有；hanim 首次使用随第 9 项 |
| 粒子＋约束求解核（XPBD：距离、弯曲、面积／体积保持、形状匹配、碰撞与摩擦；固定步长、确定性、可 checkpoint） | `xengine.simulation`（新 feature，base 层） | 弹性体、绳、布（UI 与动画） | 软体、布娃娃、粒子 | 第 9 项（hanim 是第一个消费者）；physics 在游戏阶段消费 |
| 刚体动力学（积分、接触求解、关节） | `xengine.physics`（只有游戏需要） | 不用：UI 的掉落用解析曲线或求解核 | 游戏 | 游戏阶段 |
| 曲线与运动学：缓动、弹簧解析解、解析抛落与弹跳、路径运动、曲线导数（速度） | `hanim.core` | hanim、omui（经 hanim）、时间线 | 镜头、游戏里的补间 | 弹跳随第 9 项 |
| 表示、形变载体与映射：矢量／字形／描边／笼形与晶格、选择器、位移通道、由运动导出的形变、配方 | `hanim` | 组件经运动槽配方 | 游戏角色的表现层 | M3.5 第 1–7、9 项 |
| 场（噪声场、流场、力场、SDF） | 采样函数归 `mlibs.math`；网格模拟归 `xengine.simulation`（已有 cellular） | 形变器、粒子的力 | 流体、烟雾 | M6 |
| GPU 批量求值 | 接口 `mlibs.rhi`、着色器 `mlibs.shader`；每个 owner 自带同源 GPU 版本 | hanim 的形变、匹配、扰动 | 求解核、场 | 第 8 项、M6 |

模拟的结果只经类型化通道进入动画（例如求解核写笼的控制点），动画也只经通道把目标交给模拟；两边不互相持有内部状态。渲染只在 xrender。

### 6.10 元胞自动机、像素反应与大规模像素：拆分与归属（D-132）

2026-11-07 复核现状：规则与步进、GPU 内核、步进与重放、局部刷新（damage）、并行光栅、像素 effect 各有 owner 且已实现；缺口在“场如何上屏”和“材料反应规则放在哪”。Inspector 的元胞视图每格一条 `fill_rect`（512×512 即约 26 万条命令，每 tick 都要哈希和比较 damage），是大规模像素的主要瓶颈；GPU 内核的输出也没有直接上屏。

| 能力 | 归属 | 现状 | 阶段 |
|---|---|---|---|
| 元胞规则与步进（整数通道、邻域条件、有序规则、tile 与 halo、变化计数） | `xengine.simulation.cellular`（base 层 CPU） | 已有，标量 oracle 与基准 | — |
| 元胞 GPU 执行（同一 Slang 内核：SPIR-V、WGSL；同一规则描述） | `xengine.simulation.cellular.rhi`（gfx 层，只依赖 `mlibs.rhi` 接口） | 已有；D3D12 结果错误仍在红灯清单 11 | 一致性套件并入 `xengine.simulation` 的 `src/cts/`（§5） |
| 固定步长、checkpoint、重放 | `xengine.simulation` | 已有（元胞、软体、游戏共用） | — |
| 材料与反应规则预设（火、沙、水、烟等，数据而非内核） | `xengine.simulation.cellular` 下的可选预设包（新） | 只有消费者自写的规则（Inspector 模型、官网 reactions 页） | M4 起，随第一个需要它的作品 |
| 场上屏适配：场 → 像素（调色与通道映射）；CPU 写入 `ImageStore` 的一张 surface，一条 `DrawImage`；GPU 内核输出直接作为 `ExternalTexture`，不回读 | 位于 cellular 与 xrender 资源接口之间的小适配包（新；渲染器不认识“格子”，元胞内核不认识绘制） | 无；Inspector 逐格画矩形 | M4 性能工作 |
| 脏 tile：每步变化的 tile 掩码，只重传、只重画变化的 tile | 掩码归 cellular（已有变化计数的扩展）；图像与纹理的局部上传归 xrender 资源层 | 无 | 同上 |
| 局部刷新（按节点哈希与边界的 damage，输出整行带；增量帧与全量帧逐字节一致） | `xrender.vector::DamageTracker`＋`xrender.raster` 的 `restrict_rows` | 已有，SDL3／Web／headless／C ABI 四个宿主都在用 | 扩展：图像源的局部更新进入 damage |
| 并行与增量 CPU 光栅（行带并行、同字节） | `xrender.raster` | 已有 | — |
| GPU 渲染与覆盖缓存（按键缓存的覆盖 mask，后端经 `mlibs.rhi` 注入） | `xrender.vector` GPU 路径 | 已有 | — |
| 像素 effect（blur、metaball、wobble …） | `xrender.effect`；动画只挂载（travel） | 已有 | — |
| 位图网格与像素场作为动画表示（hanim 形变图像或场） | hanim（M6：mesh、粒子、场）；场采样归 `mlibs.math`，网格模拟归 `xengine.simulation`（§6.9） | 规划中 | M6 |

验收：同一 1024×1024 场在 CPU 与 GPU 两条路径上每 tick 一条绘制命令，脏 tile 之外零上传；增量帧与全量帧逐字节一致；基准 `simulation/cellular-1024` 与上屏帧时间进入 `tests/bench`，与业界同类（Noita 类落沙）对照后才宣称领先。

### 6.7 性能约束

- interactive 模式不慢于 `Set`；简单过渡不走 prepare。
- UI 引入 hanim 的 Wasm 净体积必须测量，删除旧实现的收益在同一检查点兑现。
- 基准：`ui/frame/animate-all`、`motion/switches-100`，以及像素和过程采样基线。

## 7. xrender：渲染引擎

| 组成 | 归属 | 说明 |
|---|---|---|
| RHI 接口：描述、校验、命令列表、wire 格式 | **`mlibs.rhi`**（留在 mlibs） | README 写明“不依赖任何图形库”，是纯抽象；模拟的 GPU 计算直接用它，不经过渲染器 |
| RHI 一致性测试 | `mlibs.rhi.cts` | 接口的一致性测试，由各后端运行 |
| SDL_GPU／WebGPU／WebGL 后端 | **`xrender.rhi.{sdl,webgpu,webgl}`** | 具体实现 |
| 渲染图、纹理池 | `xrender` | 原 `xengine.render` |
| vector、effect 特性；以后的 mesh、particle | `xrender.vector`、`xrender.effect`… | |
| CPU 光栅器 | `xrender.raster` | 原 `mlibs.raster`，是渲染器的 CPU 后端；改为依赖 `mlibs.math`，不再依赖 hanim.core |
| 渲染 CTS | `xrender.cts` | 已是 owner 的套件库（D-118） |
| 描边裁剪与字形轮廓供给（M3.5） | `xrender.vector`、`xrender.text` | 按弧长裁剪描边（trim 起点／终点／偏移）作为绘制能力；字形轮廓以 `mlibs.geom` 路径交给 hanim 的 glyph adapter |
| Slang 着色器工具链 | **`mlibs.shader`（已确认留在 mlibs）** | 渲染和模拟计算共用，不含渲染语义；`shader.rules` 是模拟的整数规则 kernel，并入模拟包 |

- 判定规则：**通用的抽象和工具放 mlibs，具体的渲染实现归 xrender。**
- 目录：`modules/projects/xrender/`，层级属于 gfx。
- 宿主选择后端，以接口的形式注入给渲染、模拟和 UI；模拟只依赖 `mlibs.rhi`。
- **名字：** xrender 作为内部代号可以用。若要对外作为品牌，需注意 X11 的 XRender 扩展（`libXrender`，pkg-config 名 `xrender`）：容易混淆，产物在大小写不敏感的文件系统上也会撞名。建议在 API 冻结时另定对外名字。
- **毕业前要补的：** API v1、CTS、Windows 和 Metal 修复、真实 GPU 数据。
- **第七轮进展与新增：**
  - 首批竞品对照（`tests/bench/competitors`）：同样的五个场景，CPU 参考与 Skia 的接近程度和 resvg 相当；速度只在大量小图形上领先，大面积填充、渐变、描边比 Skia 慢 1.6–2.9 倍。对照发现的描边结构问题已修（快 3.2 倍）。
  - 矢量截图：`xrender.vector.svg` 把显示列表写成 SVG，节点标出路径，SVG 表达不了的部分是标明原因的像素岛（候选特性③的第一份证据）。
  - 待做：连续曲率圆角（squircle，CPU 与 GPU 一致，SVG 同样表达，质感底座需要）；大面积填充的光栅成本；GPU 渲染器与 Vello 对照。

## 8. xengine：Agent 原生的游戏引擎系统

### 8.1 Agent 优先的引擎开发

- 编辑器的每个操作都在操作目录（§10.3）里，都能无 GUI 执行。
- 场景、素材、玩法数据都是可读的文本格式。
- “构建 → 运行 → 试玩 → 截图和过程审查”整个循环可以 headless 完成。
- 人以审查和可视化反馈为主（过程帧、差异、回放），只做少量直接控制。
- 复用 omui 已有的事务、undo、dry-run、capture，不另造。

### 8.2 Agent 玩游戏

**Agent 玩家 = 一个输入设备 + 一个观察通道**，两者都由游戏开发者声明，通过 openas 开启。

| 要素 | 设计 |
|---|---|
| 动作 | 和人类玩家使用同一套动作词汇（input 的 action），不开后门；按 tick 对齐提交 |
| 观察 | 默认是语义观察；按需提供离屏像素观察 |
| 公平档位 | **人类同等**：信息范围与玩家相同，有反应延迟和操作频率上限。**特权**：可看到完整状态，用于开发和测试机器人。由开发者声明 |
| 开关与授权 | 开发者开启；玩家或用户配置是否允许；多人对局中披露 Agent 身份 |
| 确定性 | 输入记录和回放；回滚联机让 Agent 可以作为远端对手接入 |
| 人的角色 | 观战，并能看到叠加的 Agent 意图和决策 |
| 热路径 | Agent 输入在 tick 边界进入输入队列；控制请求不进入逐帧求解 |

Agent 玩家同时就是**自动试玩测试**，对应 §4.1“作品替代一部分测试”。

### 8.3 openas 标准扩展（现在起草，验证后定稿）

openas 0.2 面向“一次 verb 执行”，游戏和实时场景需要补：
1. **实时会话**：按 tick 订阅观察流，带 tick 编号提交动作，设置截止时间。
2. **观察与公平声明**：观察范围、反应延迟、频率上限、特权档位。
3. **预算、频率限制和停止 actor**（0.2 §8 已列为未定）。
4. **授权过期**、多 Agent 身份披露、委派链。

流程：先在 `openlifa/openas` 的 `openas-design` 分支起草设计；用 xengine 的第一个 Agent 玩家（组件打砖块，§8.4）验证；验证通过后写成规范条文。实现提案不宣称为已发布标准。

### 8.4 首个 Agent 玩家：组件打砖块

**现有基础**：`showcase/widget-breakout` 已经有 field 契约（`field.contract.omui.toml`）和语义动作（`launch`、`aim`、`move_to`、`step`、`rewind`、`order`、`choose`…）；room 5 的 copilot 以 `agent:copilot` 身份通过运行时动作操控上挡板，动作写入 journal；模拟是确定性的，五秒回放到同样的字节。

**和目标的差距（也就是这次要验证的内容）：**

| 目标（§8.2） | 现状 | 要做的 |
|---|---|---|
| 和人用同一套动作 | 人用相对移动控制挡板；Agent 用 `move_to(0…1)` 直接定位，相当于特权 | 把人的输入映射（input 的 action）作为“人类同等”档的动作集；`move_to` 保留为特权档 |
| 公平档位 | 没有声明 | 声明观察范围、反应延迟、操作频率上限；两档都能运行，并记录在回放里 |
| 由开发者开启 | copilot 内置在 room 5 | 游戏清单声明“允许 Agent 玩家”和可用档位；用户配置是否允许；权限走 `mlibs.control` |
| 实时会话 | 以单次动作调用为主 | 按 tick 订阅观察、带 tick 编号提交动作：先做本地实现，同步起草 openas 扩展 |
| 外部 Agent 可接入 | 只有内置 copilot | 外部 Agent 通过操作目录（§10.3）和实时会话完成一局，不开 GUI |
| 可审查 | inspector 显示 Agent 最近的动作 | 回放、过程帧和 Agent 意图叠加，都能 headless 导出 |
| 自动试玩 | 无 | 用 Agent 玩家跑一组种子，作为打砖块的验证矩阵之一（§4.1） |

**前置条件**：打砖块的 CI 回归（room 3 哈希、run 流程、脚本热重载）在 M0 修复。

**验收**：
- 外部 Agent 在“人类同等”档完成一局，输入记录可以逐字节回放；
- 两档的观察和动作差异可以被检查；
- 关闭开关后 Agent 无法接入；
- 全程不需要打开 GUI。

## 9. 目录与包结构

### 9.1 `modules/` 的两类分组

`modules/` 存放所有引擎、框架和库的代码；仓库顶层只放消费者（作品、示例、官网）、规范、构建插件、入口和工具。`modules/` 下分两类目录，**每个分组目录都有 README**，说明定位、准入规则和下面的每个包：

| 分组 | 放什么 | 准入 | 目录 |
|---|---|---|---|
| **层级分组** | `mlibs.*`：与领域无关的通用库、纯抽象接口和通用工具链 | 通用、无领域语义 | `modules/base`（含 `utils/`）、`modules/gfx`、`modules/content` |
| **成熟项目** | 品牌引擎、框架和模块：成熟、复杂、按可独立成项目／仓库的级别经营，**有几项业界领先或面向未来的标志性特性** | §4.2 毕业标准（或维护者指定后按标准补齐） | `modules/projects/`：hanim、omui、xengine、xrender、gongfu |

- **xengine 是品牌项目，内部再容纳尚未毕业的子系统**（模拟、音视频、时间线、世界、输入、物理、联网），也是“暂时不好分类的领域能力”的去处。
- **流动方向**：xengine 子系统或 mlibs 库满足毕业标准，就成为 `modules/projects/<品牌>/` 下的独立项目；utils 中的工具复杂到一定程度，就拆成 `modules/<层>/<库>`。

### 9.2 目标目录树

```text
modules/
  README.md      两类分组与准入规则；层级映射表
  base/          README：mlibs 基础库（math geom memory unicode image session pipeline control time cts）
    utils/       mlibs.utils：hash log trace testing error …（按目录，feature 选择）
  gfx/           README：mlibs.rhi（＋cts）、mlibs.shader
  content/       README：mlibs.text layout markdown i18n
  projects/      README：成熟项目的准入、毕业标准和每个项目应具备的内容
    hanim/       core/（原 modules/base/hanim/core）＋ 引擎（原 modules/gfx/hanim）
    omui/        原 modules/ui：core view semantic agent accessibility abi expr player reflect native theme host/…
      components/  原顶层 components/（omui.ui.*）
      lang/        原顶层 lang/（omui.lang.*）
      sdk/         原仓库根包 src/（omui.sdk，`import omui;`）
      templates/   原顶层 templates/（应用模板）
      src/cts/     规范一致性的运行适配（原 tests/cts），基于 mlibs.cts
    xengine/     README：Agent 原生游戏引擎系统的定义、子系统与毕业路径
      simulation/  原 modules/base/simulation（含 cellular）＋ 原 modules/gfx/simulation/cellular/rhi
      audio/ video/ timeline/   原 modules/media/*
      world/ input/ physics/ net/ ui/   原 mgame/*
    xrender/     渲染图（原 xengine.render）、vector、effect、raster（原 mlibs.raster）、
                 rhi/{sdl,webgpu,webgl}（原 mlibs.rhi.*）、cts
    gongfu/      原 mgame/gongfu：core data rules ai training present2d studio
tools/ site/ showcase/ examples/ spec/ src/entry/ mcpp/ tests/ docs/
mcpp.toml        只含 [workspace]（已验证 mcpp 支持没有 package 的根清单）
```

- `modules/media`、`modules/ui`、顶层 `mgame/`、`components/`、`lang/` 取消；第三版设想的顶层 `xengine/` 不再需要。
- 删除 `modules/ui/motion`（§6.4）。
- **层级 lint 改为“路径前缀 → 层”映射表**，名字表示归属、目录表示分组，层级由映射表判定：

| 层 | 路径 |
|---|---|
| base | `modules/base/**`、`projects/hanim/core`、`projects/gongfu`（核心只依赖 std）、`projects/xengine/simulation`（CPU 部分） |
| gfx | `modules/gfx/**`、`projects/hanim`、`projects/xrender/**`、`projects/gongfu/{present2d,studio}`、`projects/xengine/simulation/cellular/rhi` |
| content | `modules/content/**` |
| media | `projects/xengine/{audio,video,timeline}/**` |
| spec | `spec/` |
| ui | `projects/omui/**`（下面三行除外） |
| lang | `projects/omui/lang/**`：只依赖生成的 C ABI（`openmui.h`），规则不变 |
| components | `projects/omui/components/**` |
| sdk | `projects/omui/sdk`：依赖组件和宿主 |
| game | `projects/xengine/{world,input,physics,net,ui}` |
| app | `site/`、`showcase/`、`examples/`、`tools/`、`src/entry/` |

（gongfu 现有包都不依赖 UI 和 mgame，核实结果：core 无依赖；present2d／studio 依赖 hanim.core、mlibs 和渲染器；ai／data／training／rules 只依赖 gongfu 自身。）

### 9.3 `mlibs.utils` 的规则

| 当前包 | 行数 | 消费者 | 去向 |
|---|---|---|---|
| hash | 125 | 26 | utils |
| testing | 203 | 仅测试 | utils（只保留单元检查；一致性测试框架是独立的 `mlibs.cts`，§5.5） |
| log | 330 | 5 | utils |
| trace | 346 | 24 | utils |
| fuzzy | 340 | 1 | 并入它唯一的消费者 `ui/native/fuzzy` |
| math／geom／memory | 447–562 | 8–10 | 保持独立：有发展方向的基础库，也是唯一 owner |
| session／control／image／pipeline | 250–1233 | 1–9 | 保持独立：有外部依赖需要隔离，或职责还在增长 |

- **模块名不变**：包叫 `mlibs.utils`，里面的模块仍然叫 `mlibs.hash` 等。拆成独立库时 import 不用改，只改依赖键。
- **每个目录一个 feature，默认都不开**，不会因为用一个工具就拉进全部。
- **准入**：与领域无关、规模小、没有需要隔离的第三方依赖、不单独发布。**拆出**：§4.2 的毕业标准，或需要自己的第三方依赖。
- 与领域相关的小包（shader.rules、i18n.cldr、timeline.audio、audio.device、theme.styles／compact、render.cts、cellular.rhi.cts、gongfu.rules 等）不进 utils，而是按 D-99 复核：没有隔离收益的并回所属包。

### 9.4 `modules/projects/` 中每个项目应具备的内容

1. 项目根 README：**第一行是一句话定义**，接着是**标志性特性及其证据（§9.5）**、职责、公开入口（L0–L4）、依赖闭包、毕业标准的完成情况。
2. `ops.omui.toml` 和 `skill.omui.md`（§10.3）。
3. CTS、基准预算、独立消费者测试。
4. 不跨项目用相对路径引用源码，只通过声明的包依赖。
5. 内部按 `src/<职责>/` 和 feature 组织；子包只在确有隔离需求时建立（D-99）。

### 9.5 各项目的一句话定义与候选标志性特性

| 项目 | 一句话定义 |
|---|---|
| **omui** | 用一棵语义树同时驱动像素、强动画、无障碍、Agent 和测试的 UI 框架（动画由 hanim 提供） |
| **hanim** | 可组合、Agent 原生的高级动画引擎，同一套能力支撑 UI 动画、视频和游戏动画 |
| **xrender** | CPU 与 GPU 同源、Web 优先、像素可供 Agent 审查的渲染引擎 |
| **xengine** | 人和 Agent 地位对等地开发与游玩的 Agent 原生游戏引擎系统 |
| **gongfu** | 确定性、可回滚、角色即数据的格斗游戏框架 |

一句话定义和标志性特性要互相印证：定义里的每个关键词，都应当在下表中有对应的特性和证据。

下表是**候选**，依据是现有代码和设计；每项都要在 M5 前拿到“对照证据”或“真实任务演示”，拿不到就修改或删去。领先的说法只在证据成立后才能对外使用。

| 项目 | 候选标志性特性 | 类型 | 需要的证据 |
|---|---|---|---|
| **omui** | ① 一棵语义树同时驱动像素、无障碍、Agent 和测试，四者不可能不一致 | 面向未来 | 同一任务分别由人、读屏、Agent 和测试完成，状态逐项一致 |
| | ② Agent 原生协议：事务、undo、dry-run、区域像素、授权都内建，开发与发布权限可分级 | 面向未来 | 真实 LLM 无 GUI 完成任务的成功率；与 Flutter、Qt 等需外部自动化框架的方案对比接入步骤 |
| | ③ **强动画（由 hanim 提供）**：任意状态变化自动成为高质量动画——按节点、语义和字形自动对应的通用形变，可打断、可减弱、可逐帧审查，组件不写一行动画代码 | 领先 | 与 Flutter Hero、SwiftUI matchedGeometry、Framer Motion 的同场景对照（覆盖面、中间态质量、打断、成本） |
| | ④ 跨平台逐字节确定的 headless 像素 | 领先 | 三平台和 Web 的像素哈希一致（CI） |
| **hanim** | ① 跨平台逐位确定：任意时刻 seek，结果相同 | 领先 | GCC／LLVM／Wasm 哈希一致；与 Lottie／Rive 对比确定性 |
| | ② 一个引擎同时服务 UI、视频和游戏：可 seek 的编排与可打断的交互通道（带速度延续）共用仲裁 | 领先 | 同一套配方在三类消费者中运行；与 GSAP、Rive 的能力矩阵对照 |
| | ③ Agent 可检查、可控制的计划：不开 GUI 审查任意中间帧 | 面向未来 | Agent 用过程帧发现并修复动画问题的真实任务 |
| | ④ 跨表示的通用形变（属性、轮廓、显示列表片段、文字字形） | 领先 | 与 Manim、Lottie 的对应能力对照 |
| | ⑤ 可组合：叶子、组合和配方是同一种值，类型化通道可由用户扩展，组合规则在准备阶段就检查 | 领先 | 与 GSAP、Framer Motion 的组合表达力和错误诊断对照 |
| **xrender** | ① CPU 与 GPU 后端像素可对照，且有确定性的 CPU 参考路径 | 领先 | CPU／GPU 差异报告；与 Skia、Vello、ThorVG 的质量和吞吐对照 |
| | ② Web 优先：一次提交以紧凑字节流跨越 Wasm 边界（每次提交一次调用，而不是每次绘制一次调用） | 领先 | 与同类 Web 渲染方案的 CPU 开销对照 |
| | ③ 区域像素带来源信息，供 Agent 审查 | 面向未来 | Agent 审查任务演示 |
| **xengine** | ① Agent 和人同等开发：编辑、构建、试玩、审查全部可以 headless 完成 | 面向未来 | 一条完整的无 GUI 开发任务 |
| | ② Agent 和人同等游玩：同一套动作、可声明的公平档位、由 openas 授权 | 面向未来 | §8.4 的验收；openas 扩展进入规范 |
| | ③ 确定性模拟、回放和分支是默认能力 | 领先 | 跨平台字节一致的回放；与主流引擎的回放／回滚方案对照 |
| **gongfu** | ① 定点数、确定性的回滚格斗内核，可原样拆出 | 领先 | 10,000 次随机深度回滚字节一致（已有）；与 GGPO 类方案对照 |
| | ② 角色和规则以数据表达，并给出带行列号的诊断 | 领先 | 角色文件的编辑和诊断任务 |
| | ③ Agent 作为回滚联机的远端对手接入 | 面向未来 | M6 的实时对战验收 |

不满足条件时的处理：如果某个项目在 M5 时仍拿不出至少两项有证据的标志性特性，就退回 xengine 子系统或 mlibs，不保留品牌目录。

## 10. 其他决策

### 10.1 CI：恢复，不阻塞

1. **M0 恢复全绿**（这是一项工作）：修复 Windows、Web、Metal 的构建问题，修复 viewdoc、breakout、promo、styles 的回归；site 测试分片；GCC 时长问题；Pages 改为无锁解析（已完成）。
2. **之后不阻塞，但对账**：
   - 每轮开头看上一检查点的 CI 结果，新失败记入 STATUS 的红灯清单（首个失败提交、原因、类型）；
   - 下一检查点修复（fix-forward）；
   - 红灯超过 3 个检查点未处理，升到下一轮计划的最前面。
3. **提速**：PR 快车道目标不超过 30 分钟；GCC 和 sanitizer 放在 Nightly；作品测试走独立分片通道（§4.1）。
4. **修复原则**：找根因，不改期望值、不降阈值。

### 10.2 命名：一次迁完

| 现在 | 目标 |
|---|---|
| `openmui.*`（UI、组件、语言、工具、构建、入口、示例、作品、官网） | `omui.*`；根包改为 `import omui;` |
| `mgame.*` | `xengine.*`（world／input／physics／net／ui） |
| `openmui.audio／video／timeline`（含子包） | `xengine.audio／video／timeline` |
| `xengine.render.*`、`mlibs.raster`、`mlibs.rhi.{sdl,webgpu,webgl}` | `xrender.*` |
| `hanim.core`（`modules/base/hanim/core`）、`hanim`（`modules/gfx/hanim`） | 身份不变，目录合并到 `modules/projects/hanim` |
| `modules/ui`（omui）、`components/`、`lang/`、`mgame/gongfu` | 目录移到 `modules/projects/omui`、`modules/projects/omui/components`、`modules/projects/omui/lang`、`modules/projects/gongfu` |
| 根包 `openmui.sdk`（`src/`）、`templates/` | `modules/projects/omui/sdk`（`omui.sdk`）、`modules/projects/omui/templates`；根 `mcpp.toml` 只保留 `[workspace]` |
| `tests/cts`、`mlibs.rhi.cts`、`xengine.render.cts`、`xengine.simulation.cellular.rhi.cts` | 框架统一为 `mlibs.cts`；套件移入各自契约 owner 的 `src/cts/`（feature `cts`），见 §5.5 |
| `modules/media`、`modules/base/simulation`、`modules/gfx/simulation`、`mgame/*` | 目录移到 `modules/projects/xengine/` |
| `openmui.motion` | 删除（§6.4） |
| 小型 mlibs | `mlibs.utils`（§9.3） |
| 全部转发入口 | 删除；lint `legacy-import` 防止回流 |

保持不变：规范 `openmui.spec`、协议字段、schema、`[package.metadata.openmui]`、`openmui.h`、C ABI。已提交的历史报告不改写。

### 10.3 Agent 操作目录与 skill

- 每个产品包附带 `ops.omui.toml`（操作、类型、openas effect、权限、成本、可撤销性、取消）和 `skill.omui.md`（SOP 与验证依据）。
- CLI、MCP、openas 绑定都由它生成；授权复用 `mlibs.control`；lint 检查公开能力是否都有条目。
- xengine 的编辑器操作和游戏的 Agent 玩家声明使用同一套目录格式。
- 验收：真实 LLM 不开 GUI 完成一次动画创作任务；记录成功率。

### 10.4 性能对照

- 参照对象：UI 对 Flutter、Slint、Qt Quick；动画对 Rive、Lottie、GSAP；渲染对 Skia、Vello、ThorVG；Web 对同类框架。
- 至少用一台物理 GPU 桌面和一台中端移动设备。只有数据成立之后才宣称“领先”。
- 对照的双方都放在 `tests/bench/competitors`（`omui.bench.competitors`，竞品版本固定），`run.sh` 一键复现；第一批结果见 §0.3 和 §7。

### 10.5 `mcpp.lock`

- 已完成：`.gitignore` 加入 `mcpp.lock`；根目录和 `site/` 的锁都执行了 `git rm --cached`，本地文件保留；`tools/ci/site.sh` 去掉了 `--locked`。
- 影响：CI 每次按清单重新解析依赖。清单里的依赖都是精确版本，但传递依赖可能漂移，固定字节的测试可能因此变化，出现时按环境类红灯处理。
- mcpp 版本固定在 `.xlings.json`。第七轮升级到 2026.11.7.2：2026.11.7.1 修复了 mcpp#897（feature 的依赖在包同时是选中的根时被丢掉），三处绕过随升级删除；mcpp#874（target 目录体积、逐成员重编）仍未修，本地按“只保留一个构建目录”控制磁盘。

## 11. 路线 M0–M7（以退出条件驱动）

| 里程碑 | 内容 | 退出条件 |
|---|---|---|
| **M0 止血** | §10.1 第 1 项；STATUS.md；文档索引精简；AGENTS.md 精简（已完成） | 全矩阵一次全绿；Pages 部署 |
| **M1 结构与命名** | §10.2 全部改名；§9 目录调整（`modules/projects/{hanim,omui,xengine,xrender,gongfu}`、utils，各分组 README）；修复依赖异味；删除转发入口；层级 lint 改为映射表 | lint 为 0；全部成员构建和测试通过 |
| **M2 全局契约与 hanim 内核** | §5.2 的句柄、错误、时间、预算、准备与执行约定；lint `owner`；`mlibs.cts` 框架与现有四套 CTS 的迁移（§5.5）；契约 CTS；§6.2 的 L2–L5、visual 表示和 v1 API | 独立消费者通过；`Set` 和 Morph 的基准、像素基线不退化 |
| **M3 UI 动画迁移** | §6.4；37 类组件；删除旧实现 | 中间态、打断、reduced／off、命中、焦点、IME 一致；帧时间和 Wasm 对照 |
| **M3.5 hanim 表示与形变** | §6.8：glyph／path adapter、stroke（trim／书写）、拓扑对齐 morph、选择器＋形变器（几何过冲）、匹配修饰与路径扰动核、同通道混合层、约束与驱动、弹性体（笼形形变＋软体求解核＋运动导出形变＋解析弹跳）、GPU 批量求值；组件经配方使用（焦点环跑圈、描边书写、按压局部回弹、文字替换时的字形形变） | 每项的过程帧、打断与反向、CPU／GPU 像素和 CTS 通过；A→B、“一”的描边跑圈、A 横杠皮筋回弹、任何对象掉落挤压反弹四个招牌镜头可 headless 重现；基准无退化 |
| **M4 Agent 目录、首个工具与首个 Agent 玩家** | §10.3；动画工具 CLI 完整任务；openas 扩展草案（§8.3）；组件打砖块的 Agent 玩家（§8.4）；元胞场上屏适配与脏 tile 局部上传（§6.10） | 真实 LLM 任务成功率有记录；所有公开产品都有 ops 和 skill；§8.4 的验收通过 |
| **M5 公共库与官网**（检查点见官网方案 §11） | 已完成：40 个公共配方、20 个可带走工程（动画文档）、宣传片迁移、首批竞品基准、矢量截图。进行中（第七轮修订）：**质感底座**（官网方案 §3：组件与默认动效逐项推敲、squircle、分层阴影）；**统一动效词汇**（§6.4）；像素网格表示；“Agent 在场”组件；**官网重构**（官网方案 P1–P6：美术探索、首页、组件墙、Mui、动效页、按页交付）；打砖块与 timeline 迁移；xrender 补齐毕业条件 | UI、动画、渲染、官网复评 ≥85，其中“质感／细节”单列一项；官网新的交付预算通过 |
| **M6 游戏引擎与 Agent 游玩** | xengine 游戏层；Agent 游玩推广到格斗等实时对战（回滚联机接入）；rig／粒子／场；simulation GPU；audio／MIDI；Toy／TUI／移动端 | 实时对战中的 Agent 玩家可回放；openas 扩展进入规范 |
| **M7 全面验收** | 恢复作品质量目标；≥90 评审循环 | 每维 ≥90；CI 稳定运行 4 周 |

**旧编号映射：** R0、C1 剩余 → M0；N0–N4 → M1；S1–S3、P1、P2-A、A1–A4 → M2–M3；S6、P3、F1 → M4；C2–C4、R1–R4、S8、P4、F3 → M5；S4、S5、S7、S9、S10、P2-B、P2-C、P5、F2、F4、K0–K5、C5、R5 → M6；C6、R6、P6 → M7。旧编号此后只读。

## 12. 治理

1. **单 PR，每个 commit 是一个检查点**：包含代码、测试、STATUS 更新（当前里程碑、红灯清单、能力矩阵）和简短的证据摘要。
2. **AGENTS.md 只写核心思想与规范**（不超过约 10 KB），细节通过路径指向 `.agents/docs`；新的规范先进 AGENTS 再进 lint。
3. **STATUS.md**（不超过 200 行）是唯一的进度入口；台账只追加；新设计文档必须声明它替代什么。
4. **每个里程碑结束时复评**；证据规模与改动规模成比例；不留兼容层。
5. **每个分组目录都有 README**：`modules/README.md` 写两类分组、准入、毕业标准和迁移对照（已更新）；`modules/base/README.md` 写基础层定位和 utils 规则（已更新）；`projects/` 及各项目的 README 在 M1 迁移时创建（内容按 §9.4）。

## 13. 风险

| 风险 | 缓解 |
|---|---|
| hanim 或 xengine 范围膨胀 | 严格按 §6.2 和 §4.3 分层，用 feature 选择；UI 策略留在 UI；rig 和粒子后置 |
| M1 大规模结构调整打断工作 | CI 恢复后单独做；脚本化；分 2–3 个检查点（改名、目录、utils） |
| 全局契约过度设计 | 至少 2 个消费者才提取；只统一形状，不统一领域语义 |
| UI 迁移带来视觉回归 | 像素基线和过程采样等价对照；按步骤分检查点 |
| interactive 模式变慢、Wasm 变大 | 基准对照；删除旧实现的收益在同一检查点兑现 |
| 非阻塞 CI 再次积累红灯 | 红灯清单、修复前移、升级规则、提速 |
| Agent 游玩的公平性和安全 | 由开发者开启；公平档位明确声明；披露 Agent 身份；权限 = 开发者 ∩ 用户 |
| 作品质量目标推迟，影响对外展示 | 作品保留招牌镜头和 CI 绿；官网在 M5 复评 |
| 质感底座改动框架默认值，引起大面积像素重钉 | 按组件组分检查点，每个附改前改后的过程条带；钉住值变化须由本轮改动说明（与描边那次相同的做法） |
| Mui 成为打扰或让项目显得玩具化 | 默认安静；文档与规范页面不出现；反打扰规则写进测试（官网方案 §9.4） |
| 官网重构范围过大，拖住 M5 | 分 P1–P6 独立交付；首页和组件墙优先；按页交付提前到 P4 |

## 14. 多视角自我 review

### 14.1 第七轮（2026-11-08）：官网重构、质感与统一动效词汇

对象是第七轮的新增结论和[官网重构与 Mui 方案](2026-11-08-site-redesign-mui-design.md)。每条质疑的结论若需要改动，已改进对应文档（“改动”一列）。

| 视角 | 质疑 | 结论 | 改动 |
|---|---|---|---|
| 定位 | Mui 会不会让一个规范和框架项目显得玩具化 | 正式标志保持几何的 O 形轮廓；Mui 默认安静，**文档与规范页面不出现**；它的价值是让 Agent 的行动看得见，不是装饰 | 官网方案 §9.4 加“文档与规范页不出现” |
| 设计 | §3 写死了时长、缩放比例等数字，会不会和风格包冲突 | 这些数字是默认风格的取值，不是硬编码：做成主题的**动效与视觉令牌**（时长阶梯、弹簧预设、阴影配方、圆角平滑度），风格包可以覆盖 | 官网方案 §3 开头加“令牌”说明 |
| 设计 | “与 Apple、Linear 等对标”能否进仓库 | 对标只在评审时使用，记录发现和打分，不提交他人产品的截图 | 官网方案 §3.4 写明 |
| 架构 | Mui 在页面元素上行走，会不会让 omui 直接依赖 xengine | 不会：软体与碰撞由 hanim 的 `Engine::simulate` 求解（D-131，hanim 已依赖 xengine.simulation），omui 只把布局盒作为“表面”交给 hanim；omui 不直接依赖模拟包 | 官网方案 §10 该行归属改为“hanim（求解）＋ omui（表面）” |
| 架构 | squircle 能否做到 CPU 与 GPU 一致 | CPU 和 SVG 用超椭圆路径；GPU 的圆角矩形是解析距离场（ui.slang 的 rring），需要超椭圆的距离场近似。一致性按渲染 CTS 的容差验收，不能只做 CPU 一侧 | 官网方案 §10 写明 GPU 一侧与 CTS |
| 架构 | 像素网格表示和元胞模拟（D-132）是否重复 | 不重复：元胞是**被模拟的状态**，像素网格是**绘制好的形象被拆成部件来动画**。二者只共享“像素→颜色”的规则（`pixel_of` 一类），像素网格不做模拟 | §6.2 L3 已注明归属；无需另改 |
| 动效词汇 | omui 的槽里有些能力不是“动画配方”（形变匹配、指示物拉伸、文字重排、背景效果），能全部换成配方吗 | 不能，也不应该。第一步做一张映射表：出现、消失、强调、节奏这类通用动作换成配方；匹配、拉伸、字形重排这类 UI 特有的留在 UI；介于两者之间且通用的（如“落下”）进配方库 | §6.4 已写边界；P2 的第一项是映射表 |
| 性能 | Mui、组件墙和效果都会增加体积和首帧成本，交付预算放在 P5 太晚 | 按页交付和静态首屏提前到 P4，与首页同时做；先给出**暂定目标**：首屏可见 ≤ 1.0 s、可交互 ≤ 3 s（20 Mbit/s、40 ms），P4 实测后定稿 | 官网方案 §11、§12 |
| Agent | Mui 自己拨动开关，算谁的操作？它能不能替人回答确认 | 两种驱动方式（维护者确认后修订）：Mui 自己时是 `site:mui`，只做不改变状态的反应；**访客的 Agent 驱动时，官网上凡是能动的都能动**（身份是访客的 Agent，Mui 不增加权限），特意的权限演示是唯一例外；二者都**永远不能**回答“需要人确认”（S-10） | 官网方案 §9.2、§9.2.1 |
| 无障碍 | 一个会动的形象和一面画布墙，读屏和键盘用户怎么办 | Mui 不抢焦点、不在用户没操作时移动焦点；它的提示以礼貌级别的播报给出；引导的每一步都能用键盘完成；画布墙已有语义项和键盘焦点 | 官网方案 §9.4 |
| 工程 | 质感底座会改动框架默认值，像素钉住值大面积变化，全量测试约 50 分钟、磁盘只剩约 4 GB | 先完成 mcpp 升级并清理旧构建目录；按组件组分检查点，每个附改前改后的过程条带；全量运行放在组末 | §13 已加风险项 |
| 证据 | M5 退出要求复评 ≥85，评估依赖 codex（11-13 恢复额度） | 11-13 前用 Claude 做 Agent 任务；涉及评分的复评仍等 codex，改用 Claude 前先征得维护者同意（评分可比性） | 无需改动；STATUS 已记录 |
| 治理 | 顶层方案又长了一节 | 细节只写在官网方案里，顶层方案只记结论、归属和路线 | 本轮遵守 |

### 14.2 第六轮

| 视角 | 质疑 | 结论 |
|---|---|---|
| 定位 | omui 的一句话定义里加入“强动画”，会不会和 hanim 的定位重叠 | 不重叠：omui 的强动画是**用户体验层面的特性**（组件不写动画，任意状态变化自动得到高质量动画），hanim 是**提供这项能力的引擎**。omui 的特性表中标明“由 hanim 提供”，证据也共享：omui 用界面场景证明，hanim 用独立消费者证明 |
| 定位 | hanim 的定义里同时出现 UI、视频、游戏三个场景，会不会太宽 | 三个场景正是“一个引擎、多个消费者”的价值所在，也和 §6 的收敛方向一致；目前宣传片和游戏还没迁移，README 必须标为目标，M5 有证据后再对外使用 |
| 架构 | `mlibs.cts` 会不会变成一个大测试平台 | 框架只提供注册、等级、三态结果、比较工具、场景和报告；用例内容全部归契约 owner。和 `mlibs.testing`（单元检查）分开：后者保持小而无依赖，留在 utils |
| 包粒度 | 套件改成 owner 包的 feature，会不会让发布产物带上测试代码 | feature `cts` 默认关闭，只在测试的开发依赖里启用。**已用 mcpp 2026.11.2.2 试验验证**：`[dev-dependencies] a = { path, features = ["cts"] }` 在测试中可以使用 `a.cts`，普通构建不会编译 `src/cts/` |
| 一致性 | 三态结果里“不支持”会不会被滥用来跳过失败 | “不支持”必须说明缺少哪项能力，并且只能来自能力查询，不能来自运行时出错；运行方声明为“必需”的能力缺失时，直接判为失败 |
| 标准 | 规范的场景数据放在规范仓库，运行器在 omui，会不会让规范依赖实现 | 不会：规范仓库只有数据和 schema，仍只依赖标准库；`mlibs.cts` 是独立通用库，其他实现也可以使用 |
| Agent | CTS 的结果 Agent 能不能直接用 | 报告是机器可读的，失败时附带图像和过程帧，CLI 输出结构化结果；Agent 可以不开 GUI 定位问题。这本身也是 omui、hanim 和 xrender 标志性特性的证据来源 |
| 迁移 | 四套 CTS 同时迁移会不会出问题 | 放在 M2，逐套迁移；每套迁移前后的用例数量和结果必须一致，作为迁移本身的验收 |
| 独立性 | 根包移入 omui 之后，示例和官网对 `openmui.sdk = { path = "../.." }` 的依赖都要改 | 属于 M1 第④步的机械修改：依赖键改为 `omui.sdk`，路径指向 `modules/projects/omui/sdk` |
| 文档 | 本稿继续增长（约 50 KB） | 同第五轮结论：批准后把决策转成 D-xx、路线移到 STATUS.md，本稿冻结为历史依据 |

## 15. 待确认

第六轮的两项（一句话定义、`mlibs.cts`）已确认并落地。第七轮的两项已于 2026-11-08 确认：

1. 官网交付的暂定目标：首屏可见 ≤ 1.0 s、可交互 ≤ 3 s（20 Mbit/s、40 ms），P4 实测后定稿。
2. Mui 可以由访客的 Agent 控制，官网上凡是能动的都能动；特意的权限演示是唯一例外；只有人能回答确认。

M5 的实施计划（检查点 K0、A1–A2、B1–B6、C1–C3、D1–D6、E1–E2、F1、G1–G2）见[官网方案 §11](2026-11-08-site-redesign-mui-design.md#11-实施计划2026-11-08-综合梳理)。当前没有待确认项。
