# 仿真、动力学与统一计算层：架构与分期（D-133、D-134）

2026-11-08。来自维护者的三轮讨论：模拟与物理的归属、“看起来像”与真实模拟的划分、对照业界高级引擎的分层。**原则：架构与通用能力按“业界所有高级功能都接得上、都能做到最好”设计；实现按需求分期（P0–P3）。** 判断标准：以后加入刚体、FEM、MPM、流体、关节链、可微分、3D、神经网络推理中的任何一项，只是新增插件、求解器族或内核，不改核心契约。

本文细化[顶层方案](2026-11-06-top-level-assessment-convergence-design.md) §6.9（D-131）与 §6.10（D-132），并替代其中“软体求解核放在 `xengine.simulation`”的安排。

## 1. 概念与范围

| 概念 | 含义 | 在本项目 |
|---|---|---|
| 物理 | 领域：要描述的规律（牛顿力学、连续介质、热……） | 不是一个包 |
| 模型与方程 | 常微分方程（刚体）、偏微分方程（Navier–Stokes）、约束（XPBD） | 求解器族的内容 |
| 离散化 | 粒子、网格（有限差分、有限体积、LBM）、网格单元（FEM）、混合（FLIP、MPM） | 求解器族的内容 |
| 积分器与求解器 | 推进时间；每步解未知量（Gauss–Seidel、Jacobi、共轭梯度、牛顿、压力泊松、约束投影） | 积分器在 `mlibs.math`；求解器在 `xengine.dynamics` |
| 模拟（仿真） | 按时间推进、有状态的系统：步进、输入、检查点、回放、回滚；**不一定是物理** | 基座在 `mlibs.simulation`；内容在 dynamics、cellular、游戏逻辑 |
| CFD | 物理模拟中的流体分支 | **只做实时、图形级流体**（稳定流体、FLIP、SPH、LBM、高度场），作为 dynamics 的求解器族；**工程级 CFD（精度验证、HPC）不在范围内** |

“看起来像”和真实模拟按**有没有状态、谁来算**划分（§3）：着色效果归 xrender；程序化运动（时间的函数）归 hanim，解析场函数归 `mlibs.math`（CPU 查询与 GPU 着色同一个函数）；有状态的求解型模拟归 dynamics；离散规则模拟归 cellular；功能系统（水体、天气、破坏）只组合，不实现底层。

“材质”分两种：**渲染材质**（外观：颜色、粗糙度、透射）归 xrender；**物理材质**（密度、摩擦、恢复系数、刚度、塑性、相态）归 dynamics，cellular 读同一张表；两者用表面类型 ID 关联（同 Unreal 的 Physical Material 与 Material）。

## 2. 业界对照与本项目映射

| 业界角色 | 代表 | 本项目 | 现状（2026-11-08） |
|---|---|---|---|
| 步进与调度基座 | 引擎 tick；Unity DOTS 的 Simulation／Presentation 分组；Photon Quantum | `mlibs.simulation` | 有：`xengine.simulation` 的 `Clock`、`Timeline`（检查点、重放），待下沉 |
| GPU 内核框架 | Warp、Taichi | `mlibs.compute`（§6） | 无；元胞、hanim、xrender 各自直接用 `mlibs.rhi` |
| 动力学品牌 | Chaos、PhysX、Havok、Houdini DOPs | `xengine.dynamics` | XPBD 软体（在 simulation 里）、sweep（`xengine.physics`），分散两处 |
| 离散规则模拟 | Noita 式像素世界 | `xengine.cellular` | 有（含 GPU、上屏），名为 `xengine.simulation.cellular` |
| VFX 编排 | Niagara、VFX Graph | hanim（表示、编排、配方）＋ xrender（呈现） | 有基础 |
| 功能系统 | Water、Mass、载具、破坏 | `xengine.*` 功能模块；UI 侧为 hanim 配方 | 无 |
| 机器人多体 | MuJoCo、Drake | dynamics 的关节链求解器族、精确档 | 远期 |
| 神经网络推理 | ONNX Runtime Web、WebLLM、llama.cpp | `mlibs.compute` 的张量能力之上；完整 LLM 运行时不在 P0–P2（§6.3） | 无 |

## 3. 分层（依赖只向下）

```
L6 编辑与 Agent   求解图（数据文件 *.dynamics.omui.toml，类似 DOP 网络）、工作台 GUI/TUI、ops/skill、CTS、基准
L5 功能系统       水体、天气、破坏、载具、角色控制器、群体：只组合
L4 桥接           dynamics/cellular → hanim（笼形形变、姿态通道）
                                   → xrender（缓冲区、纹理、实例、调试绘制；不回读）
                                   → 音频（接触事件）
                  omui 的布局与语义 → 碰撞表面（适配器归 omui，经 hanim）
L3 求解器族       xpbd │ rigid │ articulation │ fem │ mpm │ sph/flip │ grid │ heightfield │ particles
L2 动力学核心     数据模型、求解管线（阶段插件）、耦合协议、物理材质表、碰撞与查询
L1 计算层         mlibs.compute：缓冲区与张量、内核、调度图、并行原语；CPU 参考与 GPU 同源
L0 基础           mlibs.math（确定性数学、小型与稀疏线性代数、积分器、随机、噪声、解析场；以后自动微分）
                  mlibs.geom（形状、距离、相交、扫掠、BVH、三角化、SDF、网格）
                  mlibs.time（时间值）、mlibs.simulation（步进与历史）
```

### 3.1 包与层（目标）

| 包 | 内容 | 层（lint 映射） |
|---|---|---|
| `mlibs.simulation`（`modules/base/simulation`） | 固定步长与多速率调度、子步、`Timeline`（检查点、跳转、重放、回滚、分支）、输入记录、确定性档位声明；**不含任何求解器**（README 第一句与归属 lint 守护） | base |
| `mlibs.compute`（`modules/gfx/compute`） | §6 | gfx（只依赖 `mlibs.rhi` 接口与 `mlibs.shader`） |
| `xengine.dynamics`（`projects/xengine/dynamics`） | L2 核心＋ L3 求解器族（`src/<族>/`，按 feature 选择，D-99） | CPU 部分 base；GPU 部分 gfx（`dynamics/rhi`，同 cellular 的做法） |
| `xengine.cellular`（`projects/xengine/cellular`，含 `rhi`、`present`） | 由 `xengine.simulation.cellular*` 改名 | 同现在 |
| 功能系统（`xengine.water` 等） | 按第一个需要的作品建 | game |

`xengine.simulation` 与 `xengine.physics` 在 P0 拆完后删除（不留转发）。名字表示归属（都属 xengine 品牌），层由映射表决定；dynamics 的 CPU 核心在 base 层，hanim、omui 才能使用。

## 4. 八条核心契约

| # | 契约 | 内容 | 保证接得上的能力 |
|---|---|---|---|
| 1 | 统一数据模型 | 结构化数组（SoA）：粒子、刚体、形状、约束、场与网格、接触；句柄统一（`mlibs.memory`）；状态是纯数据，可整体复制 | 求解器族共存；检查点与回滚 |
| 2 | 管线化求解 | 每步由阶段插件组成（宽相 → 窄相 → 积分 → 约束或压力求解 → 后处理），按批调用，不按对象（D-65、D-66）；阶段有 trace 与预算 | 新求解器、自定义阶段 |
| 3 | 显式耦合协议 | 族与族之间只经共享约束、力场、边界条件交换；调度决定顺序与迭代次数 | 多物理耦合（刚体与流体、MPM 与刚体、布与刚体、元胞与刚体） |
| 4 | 四轴分级 | 保真度（装饰／可信／精确）、确定性（无／同二进制／跨平台逐位／定点）、后端（CPU 参考／SIMD／GPU）、速率（子步、多速率）；每个场景、每个族声明自己的档位 | 同一能力服务 UI 软体、联机格斗、机器人仿真 |
| 5 | 权威路径有 CPU 参考 | GPU 是加速档，经 CTS 证明在容差内；权威、可回滚的状态只走确定性路径 | 回滚联机、Agent 回放、跨平台一致 |
| 6 | 维度策略 | 天然与维度无关的（XPBD、SPH、网格、粒子、碰撞宽相）按维度参数化；刚体与关节链 2D、3D 分开实现，接口一致 | 先 2D，3D 不推翻 |
| 7 | 可微分预留 | 内核写成可生成伴随的形式；Slang 自带前向与反向自动微分；数据模型保留梯度缓冲区的位置 | 反问题、参数辨识、学习控制、神经网络 |
| 8 | Agent 原生 | 每个族有 ops（查询、施力、暂停、单步、跳转、重放、解释接触与约束）、场景即数据、trace 与日志类别、CTS、基准；权限经 openas | 人和 Agent 对等调试与游玩 |

工程约束对所有层成立：每帧零分配（arena）、热路径无虚函数、禁用 fast-math 与浮点收缩、Web 优先（WASM SIMD、WebGPU 计算、单线程可用；多线程用确定性分区与有序规约）。

## 5. 各视角

| 视角 | 需要 | 满足方式 |
|---|---|---|
| omui | 软 UI、拖拽惯性、磁吸、Mui；布局盒作为碰撞表面；减弱动效；首帧与体积预算 | 只经 hanim 使用（D-131）；布局到表面的适配器归 omui；UI 构建只带 xpbd 族 |
| hanim | 有状态表示、物理驱动动画、二次运动、布娃娃 | L4 类型化通道；笼形形变已有（`Engine::carry`）；以后加姿态通道 |
| xrender | 粒子实例、流体表面、水面着色、调试绘制 | 计算缓冲区直接作为渲染资源（同一 rhi 设备，不回读）；着色只在 xrender |
| xengine 游戏 | 游戏物理、角色控制器、载具、破坏、回滚联机、Agent 玩家 | 权威档＋ `mlibs.simulation` 回滚；功能系统在 L5；Agent 经 ops 看到的状态与人同源 |
| gongfu | 确定性判定与回滚 | 只用 `mlibs.simulation`（判定框不是物理，业界做法）；可保持定点核心，独立出仓库不受影响 |
| cellular | 像素反应、相变 | 与 dynamics 平级，共用物理材质表与计算层；P2 起与 dynamics 耦合 |
| 工具与作品 | 工作台、录制、视频导出 | L6；Timeline 自带重放与导出；headless 招牌镜头验收 |
| Web | 体积、首帧 | 每个族、每类内核是独立 feature；wasm 按包体积预算（D5 一起做） |
| Agent | 发现、观察、控制 | ops 与 skill；场景文件；解释接触为什么发生 |

## 6. 统一计算层 `mlibs.compute`（D-134）

### 6.1 能不能把图形学与神经网络（含 LLM）统一

**在计算层可以统一，在运行时层不必统一。**

- 共同的底子：GPU 缓冲区与内存池、内核编译与派发、异步与同步、调度图；张量视图（形状、步长、数据类型）；并行原语（map、reduce、scan、排序、直方图）；分块矩阵乘、softmax、layernorm、注意力。粒子与网格求解、元胞、hanim 的批量形变、xrender 的计算 pass，以及神经网络推理，用的都是这些。
- 图形学正在吸收神经网络：神经着色、神经纹理压缩、去噪、小型 MLP 直接写进着色器（Slang 支持，NVIDIA 的 cooperative vectors 方向）；可微分渲染与可微分模拟也依赖同一套自动微分。
- 差异在规模与运行时：完整 LLM 推理需要 GB 级权重、int4／int8 量化内核、KV 缓存分页、高度优化的 flash attention、分词器与模型格式（GGUF、safetensors）。这是独立运行时的工作量（llama.cpp、WebLLM、ONNX Runtime Web 的规模）。

**结论**：`mlibs.compute` 以**张量为一等公民**设计，使神经图形、小模型（嵌入、分类、≤ 数亿参数的小语言模型）、可微分模拟与渲染都能直接建在它上面；**完整 LLM 运行时不在 P0–P2**。原因：产品原则是访客自带 Agent、官网不托管模型（官网方案 §9.3）；而且业界已有成熟的开源运行时。确有需要时，原生端经 mwrap 接入 llama.cpp（MIT），或在 `mlibs.compute` 之上建 `mlibs.ml`（满足“至少两个消费者”再立）。

### 6.2 最小框架（P1 落地，契约 P0 定稿）

| 构件 | 内容 |
|---|---|
| 资源 | `Buffer`（类型化、可映射、可作为渲染资源共享）；`Tensor` 视图（形状、步长、数据类型 f32／f16／i32／u8／int4 打包） |
| 内核 | Slang 源码与入口；同一份源码编出 SPIR-V、WGSL、MSL、DXIL，以及 **CPU 目标**（Slang 的 C++／host 目标）作为参考实现；确定性内核登记为确定性 |
| 调度 | 派发、屏障、命令录制为调度图（可重放，可在 trace 里看到）；预算与计时 |
| 原语库 | map、reduce、scan、基数排序、直方图、空间哈希、稀疏网格、分块矩阵乘、softmax、layernorm、基础注意力 |
| 自动微分 | Slang 的前向与反向微分；梯度缓冲区约定 |
| 一致性 | `mlibs.cts` 套件：每个原语 CPU 参考对照各后端，结果分通过／失败／不支持 |
| 观测 | trace 类别 `compute`；每次派发的时间、字节、占用 |

**范围约束**：不做通用图编译器（TVM 那样的），不做自动调优；只做框架与原语。**落地时机**：P1，当 dynamics 的 GPU 粒子成为第二个消费者（第一个是 cellular 的 GPU 路径，届时迁入）。

## 7. 分期

| 阶段 | 内容 | 驱动作品 | 退出条件 |
|---|---|---|---|
| **P0 骨架**（M5 内，3 个检查点） | 见 §7.1 | Mui、打砖块 | 现有行为逐位不变；层级与归属 lint 通过；CTS 与基准骨架 |
| **P1 游戏需要**（M6 前段） | 2D 刚体（对标 Box2D v3）、角色控制器、2D 破坏（Voronoi 碎裂）、高度场水面、GPU 装饰粒子；`mlibs.compute` 落地（cellular GPU 迁入，粒子为第二个消费者）；wasm 按包体积预算 | 打砖块、格斗道具与场景、官网动效作品 | 刚体基准达标（同场景、同硬件，逐项不输 Box2D v3）；粒子吞吐有对照；GPU 原语 CTS 通过 |
| **P2 丰富** | 网格流体或 SPH（2D）、完整布与绳、元胞与动力学耦合、WebGPU 计算档、求解图数据文件与工作台 | 动效作品、游戏 | 耦合 CTS；过程帧；工作台完成一条无 GUI 任务 |
| **P3 远期** | 3D（跟随 xrender 网格能力）、关节链、FEM、MPM、可微分、精确档；按需 `mlibs.ml` | 3D 作品、研究用例 | 对标 Jolt、PhysX、MuJoCo 的选定指标 |

### 7.1 P0 的三个检查点（2026-11-08 全部完成：cbf16b7f、da116d2d、S3 本次）

| 检查点 | 内容 | 验收 |
|---|---|---|
| **S1 基座下沉** | `xengine.simulation` 的 `Clock`、`Timeline`、输入重放迁成 `mlibs.simulation`；档位声明类型（保真度、确定性、后端）；所有消费者一次改完（hanim、打砖块、Inspector、元胞测试），不留转发 | 全部消费者测试通过；打砖块与 Inspector 的回放逐位不变 |
| **S2 动力学核心** | `xengine.dynamics` 立包：数据模型（SoA，2D，维度参数化的骨架）、物理材质表、碰撞形状与查询（平面、台面、圆、包围盒、胶囊；扫掠从 `xengine.physics` 迁入）、阶段管线；XPBD 软体迁入为 `src/xpbd/`；hanim 改依赖 dynamics；删除 `xengine.physics` | Mui、打砖块像素与过程逐位不变；`bodies_test`、`body_test`、`mui_test` 通过 |
| **S3 改名与观测** | `xengine.simulation.cellular*` 改名 `xengine.cellular*`；dynamics 与 cellular 的 ops 雏形（查询、单步、跳转、解释接触）；`mlibs.cts` 动力学套件骨架；`tests/bench` 加 `dynamics/softbody-*`；层级映射与归属 lint 更新；删除 `xengine.simulation` | lint 0；CTS 与基准进 CI；ops 在 headless 下完成一次“暂停—单步—查询接触” |

## 8. 将来的标志性特性（毕业成独立品牌的依据）

1. 一个数据模型、一条管线，刚体、软体、流体、粒子显式耦合（对照 Chaos、PhysX 的联邦式拼接）。
2. 确定性分级到跨平台逐位一致，含 Web，回滚内建（对照 Box2D v3、Jolt）。
3. Agent 原生：可查询、可解释、可控制，场景即数据，CTS 全覆盖。
4. CPU 参考与 GPU 同源，容差可证（与 xrender 同一套方法论）。
5. 只为用到的求解器付费，Web 体积最小。

每项都要有对照基准，逐项不输最强竞品（记忆：领先指标按竞品逐项实测）。拿不出证据之前，dynamics 留在 xengine 品牌下。

## 9. 风险

| 风险 | 对策 |
|---|---|
| 范围爆炸 | 契约先定；实现严格按阶段的退出条件推进；插件化，不预先实现 |
| 数值方法门槛 | 每个族先有教科书级 CPU 参考与 CTS，再优化 |
| GPU 速度与确定性冲突 | 用分级处理：权威档不上 GPU，装饰档可以 |
| 维度通用的复杂度 | 只对天然与维度无关的部分参数化 |
| Web 体积 | feature 编译；wasm 按包体积预算 |
| 计算层膨胀成通用 ML 框架 | §6.2 的范围约束；LLM 运行时不在 P0–P2 |
| 迁移打断官网主线 | P0 只有 3 个检查点，行为逐位不变，之后回到官网主线 |

## 10. 自我 review（多视角）

| 视角 | 发现 | 处理 |
|---|---|---|
| 架构一致性 | 之前说“hanim 反向依赖 xengine”，核实：`xengine/simulation` 的 CPU 部分在映射表里属于 base 层，**并没有违反层级**；真正的问题是“基座”与“物理求解器”混在一个包里 | §3.1 改为“拆分职责”，并保持 dynamics 的 CPU 核心在 base 层 |
| 单一归属 | dynamics 与 cellular 都要物理材质表，若各写一份就是重复 | 表归 dynamics；cellular 的核心（整数通道与规则）不依赖它，只有 cellular 的材料预设包读这张表（同在 base 层，不成环） |
| 依赖方向 | `mlibs.compute` 在 gfx 层，dynamics 的 CPU 核心在 base 层；GPU 族不能放进核心包 | 同 cellular：`dynamics/rhi` 子包在 gfx 层，核心不依赖它 |
| 性能 | 统一数据模型可能让简单场景（UI 里一个软体）付出通用框架的开销 | 管线按批，阶段只在有数据时运行；P0 基准对比迁移前后，Mui 每帧开销不得退化 |
| Web 体积 | dynamics 立包后 UI 构建会不会带上刚体、流体 | 每族独立 feature，官网只开 xpbd；体积预算守护 |
| 确定性 | GPU 原语（规约、排序）天然不确定 | 确定性内核单独登记（有序规约、稳定排序）；权威档只用登记过的 |
| Agent 原生 | ops 若只在 P2 工作台才有，前期就是黑箱 | P0 S3 就给 ops 雏形 |
| 过度设计 | 八条契约在 P0 是否都需要实现 | P0 只实现 1、2、4、5、8 的最小部分；3、6、7 定接口与数据位置，不实现 |
| 命名 | `mlibs.simulation` 易被误解为含物理 | README 第一句与归属 lint；业界“仿真／呈现”划分是依据 |
| LLM 期望管理 | “统一图形与 LLM”容易被理解为要做 LLM 运行时 | §6.1 明确：计算层统一，运行时不做，产品原则不托管模型 |
| 与官网主线的冲突 | M5 的官网（D3、D4）还没做 | P0 三个检查点后回到官网主线；P1 起放 M6 |
