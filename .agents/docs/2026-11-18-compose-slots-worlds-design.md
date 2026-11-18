# compose：组合、扩展与裁剪的通用框架（子方案，D-238–D-246 提议）

2026-11-18 起草，**第三版**（§6a：构建一侧全部落在 mcpp 已有的能力上，世界就是组合根的 feature；§13：可配置的三层、能力清单、拆分与规划）；2026-11-18 review 通过三点（准入线 100 KiB、名字 compose 与 `mlibs.compose`、K0–K5 替换 B6 余下部分与 B7、B8 的做法），开始实施。基线：`e278b0e6`（PR #1）。compose 是内部代号。它是[顶层设计](2026-11-18-omui-meng-top-level-design.md) D-220（分级、可选、默认最好）、D-212／D-213（闭包由声明推出）与 D-221（三种取得方式）的机制一层；实施上它替换[实施方案](2026-11-18-omui-meng-implementation-plan.md)里 B6 余下的部分、B7 与 B8 的做法，任务的验收不变。

## 1. 问题

D-220 定了原则：能力分五级，选择有预设、开关、由声明推出三种粒度，走同一条构建路径。到 2026-11-18 为止，机制一层没有统一的规定，实现是各处各写的。核对到的事实：

| 事实 | 出处 |
|---|---|
| 21 个清单有 `[features]`，名字与方向各不相同：`engine`、`agent`（加）、`no-gamepad-hidapi`（减） | `grep '\[features\]'` |
| 无障碍桥第一版用一对互斥的 feature，要求 host.sdl3 与 sdk 两层都写 `default-features = false`；与目标配置里的排除规则叠加后，没有 AccessKit 的平台上模块计划出错 | 红灯 136，已回退 |
| 同一种模式有六处各自的写法：帧管线的阶段（`mlibs.pipeline`）、RHI 后端由宿主注入、无障碍的服务接口（进行中）、字体包（依赖加随带）、工具的贡献清单（D-203）、数据种类的归属登记（D-209） | 各包 |
| 关掉一项能力后能不能构建、出第一帧，没有检查；只有例子 01 一份手写的体积预算 | `examples/01-hello-im/size.budget.xdef.json` |
| 没有一处能回答“有哪些可以不带、归谁、默认带不带、省多少” | — |
| `omui.config.ts` 不影响构建（B7 未做） | — |

缺的不是又一个开关，而是一套各项目共用的机制：它同时是内部各引擎与框架组合的基础、对外扩展（插件、组件包、模组）的基础，以及裁剪的基础。

## 2. 调研

五类已有的做法，各取其思想与教训（出处见 §12）。

| 类别 | 代表 | 思想 | 教训或局限 |
|---|---|---|---|
| 集中的配置模型 | Kconfig；软件产品线研究 | 一份特性模型，带依赖与冲突，求解器检查 | Linux 的特性模型大到分析难以实用（TOSEM 2025）；108 个 coreutils 小程序里单个最多 76 个选项，增长的主要是编译期选项，建议减少选项并尽早决议（Null Variability，2026） |
| 微库与可替换的 API | Unikraft（EuroSys ’21） | 原语都是微库；API 本身也是微库，一个 API 多个实现；静态链接保住性能（镜像约 1 MB） | 选择靠 Kconfig 菜单；只有构建时一种绑定 |
| 清单与类型化的导入导出 | WebAssembly 组件模型（WIT 的世界、WAC）；Fuchsia 组件框架；VS Code 贡献点 | 世界是一组导入与导出，同一个世界既描述组件也描述宿主；导入未满足不能运行；用别的组件的导出填导入即虚拟化；没有环境权限，只有被路由来的能力 | 只管加载时与运行时，不管裁剪；每一跳都显式路由，较啰嗦（Fuchsia 用清单片段缓解） |
| 代码里组合 | Bevy 的插件组；Nix 的模块系统 | Bevy：有序的组，成员可禁用、替换、改配置。Nix：一处声明选项、多处定义、按类型合并，默认值靠优先级被覆盖，与书写顺序无关 | Bevy 的禁用只在运行时，代码仍在二进制里；手写的成员文档长期过时，后来改为生成 |
| 事后裁剪 | debloating 研究 | 按配置删去用不到的代码；把运行期选项在编译期定死（x264 上体积、攻击面、性能多数显著受益） | 事后删难以保证正确；自动化仍是未解决的问题 |

调研的结论：构建时的裁剪（Unikraft）与加载时、运行时的扩展（组件模型、Fuchsia）各自成熟，没有一家把两者做成同一套机制；代码里组合的做法（Bevy）两头脱节。

## 3. 按 D-185 的三级次序

**① 从本体系出发的做法**（五条，是本方案的核心）：

1. **缺席即不支持。** 一项能力被裁掉，和平台上没有它，是同一个状态，走同一条代码路径、给出同一种带出路的拒绝。无障碍桥在 Web、Android、Linux arm64 上本来就没有，所以缺席的路径在每一轮 CI 里已经被真实执行；裁剪不另建一套要测试的状态空间。
2. **一个槽贯穿四种绑定时刻。** 构建时（静态链接）、启动时、加载时（动态库）、运行时（脚本、远端、Agent 生成）用同一份契约，变的只是提供者的载体。裁剪是“构建时不绑定”，扩展是“较晚的时刻绑定”，两件事是一件事。
3. **世界是唯一的选择单位。** 预设、一项能力、一个平台、一个宿主，都是世界：一组“需要哪些槽被满足、由谁满足、哪些显式缺席”。世界可以并。使用者只学一个概念。
4. **成本是测出来的。** 每个提供者占多少字节，来自按归属统计的体积报告（`omui-gen size`，B3 已有），回写进组合的快照；清单里没有手写的成本。
5. **闭包由声明与语义树推出。** 应用的声明（`omui.config.ts`、用到的组件）求值成一个世界；运行期才知道的选项在构建时定成常量。这是把“运行期选项提前到编译期”做成自动的。

**② 前沿研究与组合**：世界与“导入未满足不能运行”取自组件模型；默认值按优先级被覆盖取自 Nix 的模块系统；“接口也是包”取自 Unikraft；外来的提供者没有环境权限取自 Fuchsia；选项要少、尽早决议取自产品线与 Null Variability 的结论；运行期选项提前到编译期取自特化研究。

**③ 退路**（①② 的某一步验证不过时用）：组合根手写——`omui.sdk` 用 mcpp 的 feature 选提供者，组合单元手写。这是 Cargo 社区的通行做法，接缝的形状不变，以后换成生成不改契约。

## 4. 模型

四个概念。框架不认识 UI、渲染、动画、游戏。

| 概念 | 是什么 | 归谁 |
|---|---|---|
| **槽** | 一份有类型、带版本的契约：别人可以在这里提供东西 | 唯一的拥有者包 |
| **提供者** | 槽的一个实现：代码、数据，或两者 | 任何包；仓库内外一样 |
| **世界** | 一组定义：某个槽由谁提供、带什么配置，或显式缺席；可以并入别的世界 | 声明它的包、应用、命令行 |
| **组合** | 世界求值后的结果，是一个值：每个槽装了谁、为什么、多大、什么许可 | 组合根（应用、SDK、工具、测试） |

**槽**的属性：标识 `<拥有者>.<名字>`（如 `omui.accessibility.service`、`mlibs.image.decoder`）；基数（一个、按键的多个、有序的阶段）；契约版本；允许的绑定时刻（影响确定性的槽只允许构建时，D-221）；成熟度（稳定／预览／实验，D-189）；缺席时的行为（必需，或可缺席并给出拒绝的原因与出路）。

**提供者**的属性：实现哪个槽的哪个版本；载体（静态、动态库、脚本、远端、数据）；需要哪些别的槽；在哪些目标上存在（由它的包的目标配置决定，使用者不写）；是否自荐为默认。

**提供者的三种粒度**：服务（一张函数表）、阶段（按批调用）、数据（没有代码：字体包、主题、风格包、法则包、配方）。代码与数据同构，裁剪与扩展对两者是同一件事。

## 5. 语义

**5.1 求值**（确定、线性，不需要求解器）：

1. 从依赖图里各包的声明收集槽、提供者、世界。
2. 每个槽上的定义按优先级取最高的一层：提供者自荐的默认 < 被并入的世界 < 应用的声明 < 命令行。同一层对同一个“一个”型的槽给出不同的定义是错误，报出两处来源。“缺席”也是一种定义。
3. 沿“需要”求闭包。关系只有“需要”，没有“冲突”，所以是一次遍历。
4. 在目标平台上不存在的提供者按缺席处理，原因记为“平台”。
5. 检查闭合：每个必需的槽有提供者；每个可缺席的槽要么有提供者，要么有缺席的原因。不闭合不构建，报出缺哪个槽、谁能提供。

**5.2 规则**（各由 lint 检查）：

- **只有组合根选择提供者。** 中间的库只依赖槽的拥有者包，不依赖提供者包（lint `compose-root`）。
- **三条轴正交。** 带不带（世界）、这个平台用哪个实现（提供者包内的目标配置）、怎么取得（构建配置，D-221），不在同一个包里叠加选择同一组源文件（lint `compose-axes`；红灯 136 的根因）。
- **没有减法的开关。** 默认靠优先级被替换或拿掉；`no-*` 只允许出现在 `compat.*` 对第三方库内部开关的包装上，并在世界里作为该提供者的配置出现。
- **不自注册，表不在启动时初始化。** 提供者不靠静态初始化登记自己；安装是组合单元里的显式调用；服务的表在安装时才构造（lint `compose-install`）。任何在程序启动时运行的初始化都会被链接器当作根，把整个提供者留在不安装它的程序里（V1 实测到的）。表里只放拥有者会调用的函数：表里点名的都会留在装了它的程序里。
- **准入线。** 一个新的槽或提供者，要么省下可测的体积（提议 ≥ 100 KiB），要么隔离一项系统依赖、许可或平台；否则不立，交给链接器回收。
- **与 D-99 的分工。** 包内的层次用 feature（只做加法，如 hanim 的 `object → … → engine`）；跨包的可选实现用提供者。

**5.3 三级裁剪**：自动（链接器回收，没有开关）→ 推导（由声明求出世界）→ 显式（世界里写明）。只有链接器看不出来的东西才成为槽：经表或运行时分发的、数据、第三方库的子系统。

## 6. 机制

规范的每一条都落在可执行的东西上。下表是第一版的划分；“声明”与“求值与生成”两行的落法以 §6a 为准（声明在清单里，世界不需要求值）。

| 部件 | 是什么 | 位置 |
|---|---|---|
| 运行时 | `Slot<Service>` 与 `Composition`：安装、取用、是否可用、缺席的原因、自描述、冻结 | 新包 `mlibs.compose`，只依赖标准库与 `mlibs.error` |
| 声明 | 一种 xdef 数据 `compose`：包拥有的槽、给出的提供者、定义的世界 | 各包；种类的拥有者是 `mlibs.compose` |
| 求值与生成 | 构建插件：求值世界，生成组合单元（一串显式的安装调用）、组合的快照（`*.composition.xdef.json`）、Agent 的描述数据 | `mlibs.build` 的一个插件 |
| 检查 | 三条 lint；`mlibs.cts` 里每个槽一套测试（缺席的行为、安装一次、冻结后只读、描述完整）；外部提供者通过拥有者的 CTS 才算合格 | `tools/devtools`、各拥有者 |
| 矩阵 | 全量、每个世界、每个可缺席的槽单独缺席：N + 世界数个配置，各一条“构建、第一帧、体积与链接清单”的冒烟 | CI，由数据生成 |

运行时的形状（示意，不是最终的声明）：

```cpp
export module mlibs.compose;
export namespace mlibs::compose {
template <class Service> struct Slot { std::string_view id; int version; };
class Composition {
public:
    template <class Service> Result<void> install(const Slot<Service>&, const Service&, Origin);
    template <class Service> [[nodiscard]] const Service* get(const Slot<Service>&) const noexcept;
    template <class Service> [[nodiscard]] std::span<const Service* const> all(const Slot<Service>&) const noexcept;
    [[nodiscard]] Absence absent(std::string_view slot) const;  // 原因与出路
    void freeze() noexcept;
    [[nodiscard]] Description describe() const;
};
[[nodiscard]] Composition& process() noexcept;  // 进程的默认组合
}
```

- **组合是值。** 一个进程可以有多个：两个世界各带各的模组、每个测试各用各的、开发宿主与应用分开。与“世界是值，复制即分叉”一致。
- **不做的事**：依赖注入容器、按字符串找服务、启动时扫描目录、规定服务的形状。服务的类型由拥有者定义，框架只管“谁装在哪个槽上”。运行时应当小到一个文件读得完。

## 6a. 落在 mcpp 上（第二版的架构，2026-11-18）

compose 的构建一侧不另造机制，全部落在 mcpp 已有的能力上。依据是 mcpp 2026.11.18.1 给构建程序的接口（`mcpp.cppm`）与命令行；标“未用”的只读过声明。

**6a.1 mcpp 自己已有同一个模式**，三处：

| mcpp 的机制 | 相当于 | 它的规则，compose 照用 |
|---|---|---|
| 打包格式：`provides_pack_format(名字)`，`mcpp pack --format` 分发给提供它的包 | “按键的多个”型的槽 | 无条件声明、有条件提交：提供什么不随本次选了什么而变，否则回答不了“有哪些可选” |
| 运行器：`runner(…)`，只能由一个依赖提供，两个时报错并点名两者 | “一个”型的槽 | 冲突是配置错误，不合并 |
| 事实与下限：`fact` 与 `floor` | 闭合检查 | 包只测量，引擎比较；不满足在编译之前拒绝，原因可查（`mcpp why`） |

**6a.2 mcpp 的两条原则决定分工**：

- **根决定依赖图。** 解析后的依赖图（`graph_file`：各包的 feature、目标、`[package.metadata]` 原文）与每个依赖的取得方式（`dep_linkage`）只给根包的构建程序。
- **feature 在解析之前就定了。** 构建程序不能改 feature；一个包带哪些 feature 由根的清单和命令行（`--features <依赖>/<feature>`，只加不减）决定。

所以构建程序不做决定，只做两件事：按已定的 feature 生成，以及报告与诊断。

**6a.3 五层，各落在哪里**：

| 层 | 内容 | 落在 | 状态 |
|---|---|---|---|
| 契约 | 槽、提供者、组合；缺席的拒绝 | `mlibs.compose`（运行时）；`mlibs.cts:slots` | K1 已做 |
| 声明 | 谁拥有槽、谁提供、谁是组合根 | 清单的 `[package.metadata.openmui.compose]`，无条件；lint `compose` | K2 已做 |
| 选择 | 带不带；哪个平台的实现；怎么取得 | 三条轴各用 mcpp 的一种机制，见 6a.4 | K0 用了前两种 |
| 生成 | 安装单元：一串显式的安装调用 | 组合根的构建程序：`has_feature`、`target_os`、`generated`；经 `mlibs.build` 的一个通用插件，不逐个手写 | K0 手写了一个 |
| 证据 | 组合的快照；必需的槽没有提供者时的诊断；每个提供者的字节数；矩阵 | 应用（图的根）的构建程序读 `graph_file`，用 `report` 诊断（未用）；`omui-gen size`；CI | 体积已有 |

**两种根**要分开说：**组合根**是声明世界、生成安装单元的包（`omui.sdk`；不用 SDK 的工具自己就是）；**图的根**是应用，它在清单里选世界，也只有它拿得到依赖图。

**6a.4 三条轴，各一种机制**：

| 轴 | mcpp 的机制 | 写在哪 |
|---|---|---|
| 带不带（世界） | 组合根的 feature，只做加法；世界是带 `implies` 的 feature | 组合根的 `[features]`；应用的一行依赖 |
| 这个平台用哪个实现 | 目标配置块 | 提供者的包内部；组合根里平台专有的依赖 |
| 怎么取得（D-221） | 构建配置；根的 `dep_linkage`（未用） | 工作区的 profile |

**世界就是 feature**（D-220：“预设只是 feature 的组合”）：

```toml
# 组合根（omui.sdk）
[features]
default = ["full"]
lean = {}                                               # 核心与像素
app  = { implies = ["lean", "accessibility-bridge"] }   # 加上 Agent 与无障碍 …
full = { implies = ["app"] }                            # 加上脚本与设计 …
accessibility-bridge = {}                               # 一项能力，一个加法的 feature

# 应用：不写就是 full；要少带，写一行
omui.sdk = { path = "…", default-features = false, features = ["app"] }
```

- 预设不需要求值，也不改写清单：这是 mcpp 原有的语义，直接用 mcpp 与经 `omui` 构建结果相同。
- 单项的取舍也是这一行：`features = ["lean", "accessibility-bridge"]`。
- **开发永远全量**：`omui dev` 调用 mcpp 时加 `--features omui.sdk/full`（命令行只加不减，正好）；清单里的世界只约束发布。
- 减法只有 `default-features = false` 这一处，一行。

**6a.5 一个提供者用哪种做法**（按它的性质定，不逐个讨论）：

| 提供者 | 做法 | 理由 |
|---|---|---|
| 代码，构建不重 | 组合根一直依赖它的包，安装单元按 feature 点名；不点名的由链接器回收（V1） | 没有互斥的源文件；开发时命令行加一个 feature 就全量 |
| 构建重，或带系统依赖、许可 | `[feature-deps.<feature>]`：feature 打开才进依赖图 | 不选就不构建、不进许可清单 |
| 数据（字体、主题、法则包） | 构建程序里按 feature 调 `deploy`（未用），或数据包经 `[feature-deps]` | 清单的 `[runtime] deploy` 不能按 feature 写，构建程序的可以 |
| 第三方库内部的子系统 | `compat.*` 的 feature，经 `forward` 传到组合根 | 只有这里允许 `no-*` |
| 包内的层次 | 包自己的 feature 带 `sources` 与 `implies`（D-99） | 不是提供者 |

**6a.6 生成与证据的两个插件**（都在 `mlibs.build`，与领域无关）：

```cpp
// 组合根的 build.mcpp：安装单元由数据生成
mlibs::build::compose::unit({
    .module = "omui", .imports = {":shown"}, .function = "omui::hosting::install_services",
    .providers = {{.feature = "accessibility-bridge", .package = "omui.accessibility.bridge",
                   .call = "omui::accessibility::bridge::install()", .targets = compose::desktop}}});

// 应用的 build.mcpp：快照与诊断（只读依赖图，不做决定）
mlibs::build::compose::report();   // 写 <out>/composition.xdef.json；必需的槽没有提供者 → mcpp 的诊断，带出路
```

- 安装单元点名的每个包必须是这个根的依赖，并在它的清单里写了 `provides`：lint `compose` 核对构建程序里的包名与清单。
- 快照的内容：每个槽、提供者的包、由哪个 feature 带入、请求它的链（依赖图的 `requested_by`）；体积报告把按归属统计的字节数并进去，得到“为什么在、多大”。

**6a.7 仍有的限制**：

- feature 在整张图上合并：图里别的包若带着默认 feature 依赖组合根，`full` 会被带回来。规则：只有应用依赖组合根（加进 lint `compose`）。
- “一直依赖、不点名”的提供者，不带它时仍会被编译（不进程序）。构建时间成为问题的改用 `[feature-deps]`。
- 由声明推导（K5）才需要“算出的世界与清单不一致”的核对：那时由应用的构建程序比对，失败时给出该写的那一行，`omui build` 代写。
- 向 mcpp 提需求（通用的“提供”出口；按构建配置选 feature）先不做，等 K3–K4 有了实例再议。

## 7. 性能

- 槽只开在服务或批次的粒度，不开在节点、像素、采样上（AGENTS §2.6）。
- 拥有者在准备阶段取一次指针，热路径上是直接的函数指针；冻结后读不加锁。
- 构建时绑定的提供者是静态链接、显式安装，链接器照常回收与内联；没装的提供者零字节。
- 没有全局的配置头，改一个世界只重编组合单元。

## 8. 一套机制，不同标识

各项目用自己的词，底下是同一份数据。标识一层只做命名、文档与命令行入口，不得有自己的登记逻辑。

| 项目 | 对外的叫法 | 槽的例子 |
|---|---|---|
| omui | 插件、组件包、宿主 | 无障碍的服务、组件、脚本引擎、宿主 |
| xrender | 后端、渲染特性 | RHI 后端、渲染图的特性 |
| hanim | 表示、配方包 | 表示、配方 |
| xengine（meng） | 模组、法则包 | 法则、系统 |
| 工具 | 贡献（D-203） | 命令、面板 |
| mlibs | 提供者 | 图像解码器、字体包 |

**对外扩展**：仓库外的包声明提供者，应用依赖它，组合里多一项；不需要另一套插件接口。外来的、较晚绑定的提供者没有环境权限：只拿到组合路由给它的槽，安装经 openas 授权，脚本在沙箱里，范围受 `omui.config.ts` 的开放程度约束。

**Agent 面**：组合是可查询的事实（有哪些槽、装了谁、谁缺席、怎么补上）；改组合是一个操作，有因果记录；缺席的拒绝带出路。

## 9. 与当前项目

**9.1 已有的实例**（收敛的对象；逐个迁移，不一次改完）：

| 实例 | 现在 | 迁入后 |
|---|---|---|
| 无障碍桥 | `omui.accessibility` 留映射与服务接口，`omui.accessibility.bridge` 是提供者包，手写的安装（进行中，未提交） | 第一个服务型实例；手写的接口换成 `Slot` |
| 字体包 | `mlibs.text.fonts.<包>`，靠依赖随带（B4） | 数据型的提供者 |
| SDL 的 HIDAPI | `compat.sdl3` 的 `no-hidapi`，经两层 `forward`（B6） | SDL 提供者的一项配置 |
| 帧管线的阶段 | `mlibs.pipeline`，运行时替换 | 阶段型的槽；形状待核对 |
| RHI 后端 | 宿主注入 | 服务型的槽；形状待核对 |
| 图像解码器、Agent 总线、GPU 路径 | B6 未做 | 直接按本方案做 |
| 宿主（窗口、Web、Android、无头） | `omui.sdk` 里按目标配置选源文件 | 槽 `omui.host`，平台的世界选提供者 |
| 脚本引擎（QuickJS；Web 上是浏览器自己的，D-237） | G1 未做 | 同一个槽、按平台不同的提供者 |
| 组件的注册表 | B7b 未做 | “按键的多个”型的槽；推导只登记用到的 |
| 工具的贡献清单、数据种类的归属登记、法则包 | 各有登记 | 同一种声明；后迁 |

**9.2 世界与 D-220 的对应**：五级（核心、像素、设备与效果、Agent 与无障碍、脚本与设计）各是一个世界片段；`full`、`app`、`lean` 是它们的并；`headless`、`terminal`、`web` 是平台与宿主的世界。开发模式永远是 `full`（Live Design 与运行时生成需要全部）；裁剪只发生在发布构建，`omui.config.ts` 的开放程度决定闭包的下限。

**9.3 以后的需要**：meng 的模组与法则包；第三方的组件包与后端；动态共用的运行时（D-221，同一个槽经 C ABI）；Agent 在运行时生成的组件与动作（D-144，运行时的提供者）；拆仓后 omui 与 meng 各自是组合根，共用基础层里的 compose（D-231）。WebAssembly 组件可以作为外来提供者的沙箱载体，接口描述由槽的契约生成；只留位置（槽带版本、提供者带载体），不预先实现。

## 10. 先验证的四件事

做框架之前在无障碍桥上验证，结果决定走 ①② 还是退到 ③：

| 编号 | 问题 | 判据 |
|---|---|---|
| V1 | 依赖了提供者包但组合单元不引用它，链接器回收后的字节数是否等于不依赖它 | 例子 01 两种做法的装入字节数相同（已知不带桥是 16,452 KiB） |
| V2 | 构建插件能否按求值的结果增减一个包的依赖或 feature；不能时由 `omui build` 在调用 mcpp 前求值并写入清单，直接用 mcpp 构建时检查清单与组合一致 | 一个例子两种跑法结果一致 |
| V3 | 数据型的提供者（字体包、动态库）不能靠回收去掉，必须在依赖一层去掉 | 发行包检查（`check distribution`）反映出来 |
| V4 | 缺席的路径与平台不支持的路径是否确实是同一条 | 无障碍桥在 Linux x64 上缺席时的行为与 Web 上相同，同一条 CTS 通过 |

**结果（2026-11-18，K0，例子 01，Linux x64，`size` 配置，装入的字节数）**：

| 编号 | 结果 |
|---|---|
| V1 | 成立，有一个条件。带桥 18,049 KiB；依赖了桥的包但不安装 16,461 KiB（此前完全不依赖时是 16,452 KiB）。第一次量是 18,049 KiB，没有回收：服务的表是一个在启动时初始化的全局对象（它的名字来自另一个单元的函数），初始化函数成了根。改成安装时才构造后回收。另一处：调试文本的函数放在表里时多留 24 KiB，移出表后只有调用它的测试带着 |
| V2 | 部分成立。构建程序拿得到本包的 feature 与目标（`mcpp::has_feature`、`mcpp::target_os`），也拿得到解析后的依赖图（`mcpp::graph_file`：各包的 feature、目标与 `[package.metadata]`；这一项只读了声明，还没有用）；它不能增减依赖。做法：提供者包写成组合根的依赖（平台专有的写在目标配置下），组合单元由构建程序生成，不点名的由链接器回收（V1）。`omui.sdk` 已这样做：一个加法的 feature `accessibility-bridge`（默认开），没有互斥的一对，也没有 `no-*` |
| V3 | 未做（字体包在 B4 已按依赖一层去掉；动态库的提供者还没有实例） |
| V4 | Linux x64 上不安装时：`available()` 为假，`Adapter::attach` 被拒绝并说明原因（`accessibility_test`）；与没有 AccessKit 的平台走同一段代码（桥的包在那些平台上 `install` 不安装任何东西）。同一条 CTS 在两处运行要等 K1 |

**K1 的结果（2026-11-18）**：`mlibs.compose` 的运行时已有（`Slot`、`Composition`、三种缺席、冻结、`describe`；预览级），槽的契约进了 `mlibs.cts:slots`（`slot.declared`、`slot.absent`、`slot.installed`）。第一个使用者是无障碍的服务（`omui.accessibility.service`），拒绝里带槽的名字与出路。运行时在例子 01 里占 13 KiB（18,049 → 18,062 KiB），是所有槽共用的一次性成本。

**图像解码器不立槽。** 实测例子 01 里没有 JPEG 与 WebP 的字节：各格式是单独点名的函数（`decode_png` 等），没人调用的由链接器回收，只有 libpng（175 KiB）与 zlib（约 52 KiB）在，它们来自 Agent 的截图。按 D-245 这属于“自动”一级，不需要槽；`links` 清单里的解码器包只是被构建，没有字节进程序。K1 的第二个使用者改为 K4 里的 Agent 总线或 GPU 路径。

**K2 的结果（2026-11-18）**：声明写在清单里，`[package.metadata.openmui.compose]`：拥有者写 `slots`，提供者的包写 `provides`，不是作品的组合根（SDK）写 `root = true`。没有另立数据文件：构建程序经 `mcpp::graph_file` 拿到各包的 `[package.metadata]` 原文（V2），K3 的求值直接读它。三条 lint 合成一条 `compose`：槽只有一个拥有者、以拥有者命名、标识在它的源码里；提供者点名的槽有人声明；测试之外只有组合根依赖提供者的包；同一个清单不同时用 feature 与目标配置的排除选源文件（红灯 136）。“表不在启动时初始化”不能靠读源码查，改为 K5 矩阵里的一条实测：不安装某个提供者的构建里没有它的字节。已有声明的实例是无障碍（拥有者、提供者、SDK 三处）；字体包、帧管线、RHI 后端迁入时补。

**K3 的结果（2026-11-18）**：`mlibs.build.compose` 的 `unit()` 已有，`omui.sdk` 的安装单元由它生成（一行一项能力）。三个世界是 `omui.sdk` 的 feature（`default = ["full"]`；`app`、`full` 经 `implies` 带上 `accessibility-bridge`，`lean` 不带）。例子 01（`size` 配置，装入字节数）：`full` 与 `app` 18,062 KiB，`lean` 16,468 KiB；`full` 与 `lean` 各在离屏窗口上出了第一帧。清单写 `lean`、命令行加 `--features omui.sdk/full` 时桥在程序里（1,185 个 AccessKit 符号），不加时为 0：`implies` 传到了构建程序的 `has_feature`，命令行的加法成立。`omui dev` 的开发宿主在应用直接依赖 `omui.sdk` 时加这个参数；由播放器运行的 TS 应用依赖的是 `omui.entry.player`，它的世界归 K5。lint `compose` 加了两条：库不依赖组合根；组合根的构建程序点名的包是它依赖的、写了 `provides` 的包。`app` 与 `full` 现在相同，因为可选的能力只有一项；K4 的两项进来后才分开。

**K4 第一步的结果（2026-11-18）**：`mlibs.build.compose` 的 `report()` 已有。应用的构建程序调用它，读 mcpp 给的依赖图，在构建输出目录写 `composition.json`（`composition@1`）：图里声明的每个槽、拥有者、提供者，以及每个提供者在这次构建里是否被某个组合根安装、或为什么没有；没有任何提供者被安装的槽标 `absent`（`left-out` 或 `none`）。组合根的清单多写一张表 `[package.metadata.openmui.compose.installs]`（提供者的包 → feature 与目标），报告靠它判断；lint `compose` 把它与构建程序里的行对齐。清单的 metadata 里不能写表的数组（mcpp 拒绝），所以是以包名为键的表。体积检查（`check size --native`）打印组合，并按预算里的 `installs` 守住：例子 01 的预算写了 `["omui.accessibility.bridge"]`，改成 `lean` 构建时检查失败并说明。`full`、`lean`、再 `full` 三次交替构建，报告每次都与构建一致。还欠：必需的槽缺提供者时的诊断（现在没有标为必需的槽）；每个提供者的字节数并进报告。

**GPU 路径与 Agent 总线的现状（核对代码，未改）**：GPU 一支直接写在 `omui.host.sdl3` 的 `desktop.cpp` 里（建设备、渲染、呈现、读回），要先抽到一张服务表后面；`agent::Endpoint` 是具体类型，出现在宿主的公开接口、SDK 与 view 里，要先把 `omui.agent` 拆成接口与实现。顺序：GPU 路径在前；Agent 总线动手前先有一页拆分的设计。

**K4 第二步的结果（2026-11-18）：GPU 路径是提供者。** 槽 `omui.host.sdl3.gpu` 归 `omui.host.sdl3`（`GpuService`：开始做设备、现在做设备、认领窗口、做渲染器）；提供者是新包 `omui.host.sdl3.gpu`（SDL_GPU 的设备与 xrender 的渲染器）；`omui.sdk` 的 feature `gpu`，`app` 与 `full` 带、`lean` 不带。没有提供者时宿主用 CPU 画每一帧，和没有 GPU 的机器一样；坚持要 GPU（`Renderer::gpu`）时被拒绝，拒绝里带槽名与出路。例子 01（`size` 配置，装入字节数）：`full` 与 `app` 18,069 KiB，`lean` 15,809 KiB。设备上的四个用例在 lavapipe 与本机 RTX 4080 上各通过一次。

三件事值得记下：

- **拥有者不能依赖自己的提供者，测试也不行**（mcpp 拒绝这个环）。宿主的测试改为只守缺席的路径；要真提供者的用例搬到提供者的包里。这成为一条规则。
- **`lean` 里仍有约 0.8 MB 与 GPU 有关的字节**：SDL 自己的 Vulkan 2D 渲染驱动 292 KB（宿主只用它的软件驱动呈现 CPU 的帧），以及效果库与着色器工具约 0.5 MB（宿主直接持有 `xrender::effect::Library`，而效果只有 GPU 渲染器会画）。前一项是 `compat.sdl3` 的一个开关，对每个世界都省；后一项要把效果库也放到提供者后面。
- **默认的 `full` 比裁剪之前大了约 29 KB**（18,473,342 → 18,502,597 B），是运行时（约 10 KiB）与两个接缝的代价，超出例子 01 的预算（18,500,000）2,597 B。预算不改；先做 SDL 的那个开关（每个世界少 292 KB）把它收回来。在此之前这一项记为红灯。

**效果库放到 GPU 提供者后面（2026-11-18）**：`GpuService` 多一项 `effects()`，宿主不再直接持有 `xrender::effect::Library`。例子 01 的 `lean` 15,175 KiB（上一步 15,809），`full` 18,080 KiB（18,513,569 B，超预算 13,569 B：每多一个接缝，`full` 多约 10 KB）。`lean` 里还剩与 GPU 有关的约 0.65 MB：SDL 的 2D 渲染驱动与它带入的 Vulkan 后端 306 KB、`xrender::shaders` 253 KB、`xrender::rhi` 与 `mlibs.rhi` 87 KB——后两项是 view 里画在设备上的组件引用的，还没有查到具体是谁。

**Agent 总线：不需要拆包（2026-11-18，读代码后的结论，未改代码）。** 先前的判断是要把 `omui.agent` 拆成接口包与实现包、并改公开 API；核对后不必：

- V1 已经说明，依赖一个包而不点名它的代码，链接器会回收。所以“接口与实现分包”只在要隔离依赖、许可或平台时才需要（无障碍桥带 AccessKit，GPU 提供者带 SDL_GPU 的后端）；`omui.agent` 没有这样的依赖要隔离。
- 做法：槽与安装函数都放在 `omui.agent` 里（`omui::agent::install()`，表在安装时构造）。宿主对端点的七处调用（构造、析构、`start`、`stop`、`pump`、`take_input`，以及 `vector_capture`）改为经槽的表；公开的类型不变（`Desktop::endpoint()` 仍返回 `agent::Endpoint*`）。没人安装时，这些函数没有被点名，协议（1,357 行）与它带入的部分不进程序。
- 规则相应多一条：**提供者可以与拥有者同包**，条件是没有要隔离的依赖；安装函数只由组合根调用，拥有者自己的代码不调用（lint `compose` 现在只认“提供者的包”，要加上这种情形）。
- 范围：三个宿主（窗口、无头、Web）各自持有端点，`omui.sdk` 的 `app.cpp` 还用到 `TaskBroker`、`tree_hash`、`pixels_hash`；都要经表才省得下来。能省多少要做了才知道：`omui.agent` 自己 244 KiB，PNG 与 zlib 约 222 KiB 只为截图而在，JSON 另有别的使用者。
- 世界：`app` 与 `full` 带 Agent（D-213：默认保留），只有 `lean` 不带。

**Agent 总线第一步的结果（2026-11-18，本地）**：槽 `omui.agent.endpoint` 与同包的提供者（`omui::agent::install()`）已有；窗口宿主的端点与两种截图都经槽的表；`omui.sdk` 的 feature `agent`（`app`、`full`）。lint `compose` 认得“拥有者给自己的槽提供”，依赖它的库不算依赖提供者。测试：`omui.agent` 60 例、`omui.host.sdl3` 13 例、`omui.host.sdl3.gpu` 5 例通过。**字节上还没有效果**：例子 01 的 `lean` 仍是 15,178 KiB，`omui.agent` 的 255 KiB 还在，因为另有三处直接点名它——无头宿主的端点与截图，`omui.sdk` 的后台任务（`TaskBroker`）与回放的两个哈希，以及运行时生成（它用任务）。要省下来这三处都得经槽，其中运行时生成应当是它自己的一项能力。`full` 因这个接缝又多 6 KB（18,519,893 B）。

## 11. 决策（提议）与任务

| 编号 | 决策 |
|---|---|
| D-238 | 组合、扩展、裁剪用同一套通用机制 compose，放在基础层（`mlibs.compose`），内部、对外、以后的扩展都用它 |
| D-239 | 四个概念：槽、提供者、世界、组合；预设、能力、平台、宿主都是世界 |
| D-240 | 缺席即不支持：裁掉与平台没有是同一个状态、同一条路径 |
| D-241 | 一个槽贯穿四种绑定时刻；0.1 只实现构建时与启动时，其余只留位置 |
| D-242 | 默认按优先级被替换或拿掉；关系只有“需要”；没有减法的开关（`compat.*` 的包装除外） |
| D-243 | 只有组合根选择提供者；三条轴正交；不自注册；各由 lint 检查 |
| D-244 | 组合是值，求值的结果存成快照，成本取自实测；Agent 可查询 |
| D-245 | 准入线：新的槽或提供者省下 ≥ 100 KiB，或隔离一项依赖、许可、平台 |
| D-246 | 各项目用自己的标识，底下一份数据；外来的提供者没有环境权限，合格以拥有者的 CTS 为准 |

任务（替换 B6 余下部分、B7、B8 的做法；估计的单位是检查点）。K0–K3 已完成；K4 之后的拆分以 §13.4 为准：

| 任务 | 内容 | 依赖 | 验收 | 估计 |
|---|---|---|---|---|
| K0 | 无障碍桥按手写的接口收尾；V1–V4 | — | 四项各有结论；例子 01 两种配置的体积 | 1 |
| K1 | `mlibs.compose` 的运行时与 CTS；无障碍是第一个使用者，第二个在 K4（解码器经实测不需要槽） | K0 | 使用者的测试与 CTS 通过；缺席的拒绝带出路 | 2 |
| K2 | 清单里的声明；lint `compose` | K1 | lint 0；自测能抓到种下的违例；迁入的实例都有声明 | 1 |
| K3 | 生成：`mlibs.build` 的 `compose::unit`，`omui.sdk` 的安装单元改由它生成；世界 `lean`／`app`／`full` 作为带 `implies` 的 feature；`omui dev` 加 `--features omui.sdk/full`；lint 加“只有应用依赖组合根”“安装单元点名的包有声明” | K2 | 例子 01 三个世界各能构建、出第一帧；直接用 mcpp 与经 `omui` 结果一致 | 2 |
| K4 | 证据：`compose::report` 写组合的快照与诊断；体积报告并入“为什么在、多大”；B6 余下的两项（Agent 总线、GPU 路径）作为提供者，是 K1 的第二、三个使用者 | K3 | B8 的验收：三条冒烟在 CI，`app` ≤ 8 MB、`lean` ≤ 6 MB 或差距写明；必需的槽缺提供者时构建前报出 | 4 |
| K5 | 由声明推导（B7b）：算出的世界与清单比对，`omui build` 代写那一行；CI 矩阵由世界的清单生成，含“不安装的提供者没有字节”的实测 | K4 | B7b 的验收；矩阵的每一格通过或记入红灯 | 4 |

**风险**：过早通用化——K1 之前必须有两个真实的使用者，形状由它们定；框架变成万能对象——运行时保持一个文件的规模，服务的形状归拥有者（AGENTS §2.2）；mcpp 的限制（feature 只加不减、不能按目标带不同的依赖）——提供者包绕开后者，V2 回答前者；包数增加——每个提供者包按 D-194 写明成立的理由。

**已定（2026-11-18）**：准入线取 100 KiB；名字 compose 与 `mlibs.compose`；K0–K5 替换 B6 余下部分与 B7、B8 的做法。

## 13. 收口：可配置的三层、能力清单、拆分与规划（第三版，2026-11-18）

review 的两点结论：例子 01 的预算按可裁剪的代价调到实测值（红灯 138 关闭）；SDL 的加速 2D 渲染驱动做成开关而不是替所有人选一种。由此定下的总原则：**框架给选择，不替应用决定**——每一项可以不带的东西都有名字、有默认、能单独开关，世界只是方便的起点。

### 13.1 可配置的三层

应用在一行依赖上能做三种粒度的选择，三种可以混用：

| 层 | 写法 | 含义 |
|---|---|---|
| 世界 | `features = ["app"]` | 一条包含链：`lean` ⊂ `app` ⊂ `full`（默认）。只做加法，上一级 `implies` 下一级 |
| 能力 | `features = ["lean", "agent"]` | 在某个世界之上再加某几项；每项能力是组合根的一个加法的 feature |
| 削减 | `features = ["lean", "no-gamepad-hidapi", "software-present"]` | 第三方库内部的开关：只减不加，所以不进包含链（进了就会沿 `implies` 传到 `full`）；应用按需点名，或用 `minimal`——`lean` 加上全部削减，不被任何世界包含 |

- 默认什么都不写就是 `full`：D-220 的“默认最好”。
- 开发永远全量：`omui dev` 在命令行加 `full`。削减类的开关是减法，命令行抵消不了，所以**削减只应出现在发布用的清单行里**；这一条写进 `omui.sdk` 的 README，K8 的推导生成清单行时遵守。
- 为什么削减是减法：`compat.*` 的包被别的包（`mcpplibs.mwrap.*`）带着默认 feature 依赖，feature 在整张图上合并，正向的默认关不掉；所以第三方库内部只能用 `no-*`（D-242 的例外），组合根原样转发。

### 13.2 能力清单（omui，窗口应用；例子 01、Linux x64、`size` 配置的实测）

| 能力 | 做法（§6a.5） | 组合根的 feature | 世界 | 实测或估计 | 状态 |
|---|---|---|---|---|---|
| 无障碍的平台服务 | 提供者的包（AccessKit） | `accessibility-bridge` | `app`、`full` | 1,594 KiB | 已做（K0） |
| GPU 路径与效果 | 提供者的包（SDL_GPU、渲染器、效果库） | `gpu` | `app`、`full` | 1,293 KiB | 已做（K4） |
| Agent 面：端点、截图 | 同包的提供者 | `agent` | `app`、`full` | 估 0.9 MB（`omui.agent` 312、`mlibs.agent` 181、PNG 与 zlib 222 KiB 等） | 槽已有，窗口宿主已接；无头宿主与 SDK 未接（K6） |
| 后台任务、回放 | 并入 Agent 面的表 | `agent` | 同上 | 含在上一行 | 未做（K6） |
| 运行时生成 | 同包的提供者（在 `omui.sdk` 里） | `generate` | `full` | 138 KiB 起 | 未做（K6） |
| 手柄的 HIDAPI 驱动 | `compat.sdl3` 的开关 | `no-gamepad-hidapi`（削减） | `minimal` | 241 KiB | 已做（B6） |
| SDL 的加速 2D 渲染驱动 | `compat.sdl3` 的开关 | `software-present`（削减） | `minimal` | `full` 约 236 KiB，`lean` 约 370 KiB | 未做（K5） |
| 语言环境的数据（CLDR） | 数据的提供者 | 待定 | 待定 | `mlibs.i18n` 394 KiB | 未核对（K7） |
| 动画的求解器 | 包内的 feature（hanim） | 待定 | 待定 | B5 的欠项 | 未核对（K7） |
| 字体包 | 数据包，按依赖 | 应用自己的依赖 | — | 每包 0.3–9 MB | 已做（B4） |
| 图像解码器 | 自动（链接器回收） | 无 | — | 0（没人调用就不在） | 不需要槽 |
| 设计器 | 播放器的 feature | `omui.entry.player/design` | 开发 | — | 已有（D-149） |

平台与宿主（窗口、Web、Android、无头）不在这张表里：它们是平台轴，由目标配置选，不是能力。

**现状与目标**：`full` 18,086 KiB，`lean` 15,178 KiB。上表全部做完后 `lean` 的估计是 13.5–14 MB，`minimal` 再少约 0.6 MB。B8 的目标（`app` ≤ 8 MB、`lean` ≤ 6 MB）靠裁剪到不了：`lean` 里最大的几项是标准库 1.7 MB、SDL 1.4 MB、基础组件 1.4 MB、view 1.0 MB，都在核心与像素之内。差距要由 B1 的第二步（相同代码折叠，需要 lld）、B5（断开越级的依赖）、组件按用到的登记（K8）与标准库的用法来收，不属于组合框架；这里如实写明，不改目标。

### 13.3 框架自身的收口

| 项 | 现状 | 要做 |
|---|---|---|
| 运行时 `mlibs.compose` | 有，预览级；三个槽在用 | 两个使用者以上的条件已满足；K9 评审后定稿 API、升稳定级 |
| 槽的契约 `mlibs.cts:slots` | 有；无障碍的槽在跑 | GPU 与 Agent 的槽补上（K6） |
| 声明与 lint `compose` | 有；认得同包的提供者 | 加“削减类的 feature 不被世界包含”（K5） |
| 生成 `compose::unit` | 有 | — |
| 报告 `compose::report` | 有；体积检查按 `installs` 守住 | 每个提供者的字节数并进报告；必需的槽缺提供者时的诊断（K7） |
| 矩阵 | 没有；三个世界是手工量的 | CI 里每个世界与每项能力单独缺席各一条冒烟（K7） |
| 推导 | 没有 | 由 `omui.config.ts` 与用到的组件算出清单行（K8，即 B7） |
| 接缝的代价 | 每个 6–11 KB，`full` 共多约 46 KB | 记入预算；K9 评审时看能否压低（表的形状、错误类型的实例化） |

### 13.4 拆分与规划

已完成：K0–K3，K4 的报告与 GPU 路径，Agent 面的第一部分。余下按依赖排：

| 任务 | 内容 | 依赖 | 验收 | 估计 |
|---|---|---|---|---|
| K5 | 削减层：`compat.sdl3` 加 `no-render-accelerated`（mcpp-index）；`omui.host.sdl3` 与 `omui.sdk` 转发为 `software-present`；`minimal` 世界；lint 的那一条；CPU 帧两种呈现方式的耗时各量一次并写进 README | — | 例子 01 的 `minimal` 能构建、出第一帧；呈现耗时有数 | 2 |
| K6 | Agent 面接完：无头宿主与 Web 宿主经槽；后台任务与回放并入表；运行时生成成为 `generate`；GPU 与 Agent 的槽跑契约的用例 | — | 例子 01 的 `lean` 里没有 `omui.agent` 与 PNG 的字节；三个宿主的测试通过 | 3 |
| K7 | 证据与矩阵：报告并入字节数；必需的槽的诊断；CI 里每个世界与每项能力缺席各一条冒烟（构建、第一帧、预算）；CLDR 与求解器两项核对后入表或写明不立 | K5、K6 | 矩阵的每一格通过或记入红灯；B8 的三条冒烟在 CI，差距写明 | 3 |
| K8 | 推导（B7a、B7b）：从 `omui.config.ts` 与用到的组件算出世界与清单行，`omui build` 代写，直接用 mcpp 时构建程序核对；组件的注册表只登记用到的；由播放器运行的 TS 应用的世界 | K7 | B7b 的验收 | 5 |
| K9 | 定稿：整体评审（接缝的代价、API、命名）；`mlibs.compose` 升稳定级；其余实例迁入或写明不迁（帧管线的阶段、RHI 后端在 Web 上的选择、工具的贡献清单、法则包） | K7 | 评审的结论进 STATUS；lint `maturity` 里不再有 `lower = ["mlibs.compose"]` | 2 |

顺序：K5 与 K6 互不依赖，先 K5（小，且定下削减层的形状），再 K6；K7 之后才有 CI 的矩阵守着，K8 最大、排在它后面。合计约 15 个检查点。

**对其他项目**（不在 0.1 的范围，K9 里只决定迁不迁）：xrender 的后端（SDL_GPU 已是提供者；WebGPU、WebGL 在 Web 上由宿主注入）、hanim 的包内分层（保持 feature）、meng 的模组与法则包（数据的提供者，运行时绑定，等 D-241 的后两种时刻）、工具的贡献清单（D-203）。

## 14. 出处

- Kuiter 等，How Configurable Is the Linux Kernel? Analyzing Two Decades of Feature-Model History，TOSEM 2025：<https://dl.acm.org/doi/full/10.1145/3729423>
- Tërnava 等，Small Yet Configurable: Unveiling Null Variability in Software，2026：<https://arxiv.org/abs/2604.15957>
- Tërnava 等，Specialization of Run-time Configuration Space at Compile-time，2022：<https://arxiv.org/abs/2210.14082>
- Alhanahnah 等，SoK: Software Debloating Landscape and Future Directions，2024：<https://arxiv.org/pdf/2407.11259>
- Kuenzer 等，Unikraft: Fast, Specialized Unikernels the Easy Way，EuroSys ’21：<https://arxiv.org/pdf/2104.12721>
- WebAssembly 组件模型，Worlds：<https://component-model.bytecodealliance.org/design/worlds.html>；WAC：<https://github.com/bytecodealliance/wac/blob/main/LANGUAGE.md>
- Fuchsia，Capabilities：<https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities>；CML：<https://fuchsia.dev/reference/cml>
- Bevy，PluginGroups（PR #744）：<https://github.com/bevyengine/bevy/pull/744>；`plugin_group!`（PR #14339）：<https://github.com/bevyengine/bevy/pull/14339>
- nixpkgs，`lib/modules.nix`：<https://github.com/NixOS/nixpkgs/blob/master/lib/modules.nix>
