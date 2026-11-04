# hanim 通用性、风格与异构执行：架构比较稿

2026-11-04。**讨论／review，未定案。** 维护者明确：对象中心或动画中心是供比较的思路，需要从局部引擎与全局系统评估；目标是在通用性、效果质量和性能上超出 Manim 及常见动画／游戏动画系统。不能把一个好看的 API 或首条 CPU 形变链路当作目标已完成。

配套[play／组合细案](2026-11-04-hanim-play-api-design.md)、[完整动画产品](2026-11-04-hanim-animation-library-playback-design.md)、[整体引擎／模拟](2026-11-04-omui-engine-framework-architecture-design.md)及[多产品架构](2026-11-04-multi-engine-product-architecture-design.md)。本稿重新比较主入口，前稿四参数为主的推荐不再作为定案；不自行新增 D-xx 决策。

## 1. 对照真实能力，而非弱化对手

Manim Community 已提供 Cairo 与 OpenGL 渲染器，也有 TransformMatchingShapes 等匹配动画。不能将“常见作品风格接近”推断为不能 GPU 渲染或不能换风格。[渲染器](https://docs.manim.community/en/stable/reference/manim.constants.RendererType.html)、[匹配动画](https://docs.manim.community/en/stable/reference/manim.animation.transform_matching_parts.TransformMatchingShapes.html)。

成熟游戏动画系统的基线同样很高。例如 Unreal 的 AnimGraph 支持姿态混合、局部骨骼叠加及覆盖，并有顶点着色器蒙皮与 compute Skin Cache 路径。[动画混合](https://dev.epicgames.com/documentation/en-us/unreal-engine/blending-animations-in-unreal-engine)、[骨骼渲染路径](https://dev.epicgames.com/documentation/unreal-engine/skeletal-mesh-rendering-paths-in-unreal-engine?lang=en-US)。独立 ozz-animation 已采用面向数据的运行时，强调性能／内存及与 renderer、游戏引擎解耦；“独立 C++ 库”本身也不是领先证明。[ozz 官方仓库](https://github.com/guillaumeblanc/ozz-animation)。

上述是代表性对照，不代表各产品的全部范围。hanim需要以同功能、同质量、同设备的可重复任务证明优势；不能用新CPU标量case对比对手完整渲染，或把离线准备排除后只报采样速度。

## 2. 三种入口的比较与推荐方向

| 视角 | 优点 | 需要解决的限制 | 建议地位（待review） |
|---|---|---|---|
| 对象中心：play(object, animation, effects, config) | 输入角色明确，UI状态／部件适配简单，常规单目标直接 | 多目标、嵌套时间与效果作用域不宜都塞在外层参数 | 保留为对象接入／便捷适配 |
| 动画中心：play(animation, config)，X(obj)／X(a,b) | 创作对象就是可组合动画，便于顺序／并行／路径与局部效果 | 要分清未绑定配方、绑定描述、输出作用域；双对象参数角色必须定义 | 倾向作为创作层主入口 |
| 编译图／Program中心 | 版本、依赖、通道冲突、批次、成本和Agent查询明确 | 普通调用者手写图会增加负担；不能逐帧解释图 | 执行内核的规范表示；高级入口可公开 |

推荐方向是**创作层动画中心、编译／执行层能力与数据中心、对象接入层兼容便利形式**。三者规范化到同一计划和播放器，不分别实现插值、时钟或后端。`hengine`可作为示例变量，类型暂保持`hanim::Engine`；是否更名不影响上述取舍。

以下仅为候选语法，当前原型尚未提供这些callable配方及with_effect作用域：

```cpp
auto animation = parallel(
    anim::move_to(point)(title).with_effect(fx::blur(...)),
    sequence(
        anim::morph_to(panel)(card),
        anim::reveal()(label)))
    .with_effect(fx::tone(...));

auto prepared = hengine.prepare(animation, prepare_config);
if (prepared) {
    auto playback = hengine.play(*prepared, playback_config);
}
// 日常形式可在内部准备同一计划：hengine.play(animation, config)。
```

`X(obj)`只构造绑定动画描述，无启动／渲染／业务副作用。`X(a,b)`的source／target、主体／参照等角色由动画契约定义；作用到两个独立对象应显式parallel／foreach，对组本身施加变换另有含义。不能只靠参数个数猜测。

未绑定配方可复用，绑定动画拥有目标引用与参数描述，PreparedPlan拥有对应关系／资源和执行数据，Playback拥有时间及状态。给它们同一个日常“动画”称呼可以简洁，但类型和生命周期必须分明。

## 3. 内核围绕六个职责组织

| 职责 | 核心契约 | 单一实现／模块归属 |
|---|---|---|
| 表示与绑定 | 类型化对象域、部件／通道、身份与版本、资源、局部／世界空间 | HObject及各领域adapter；不强迫UI树、rig、像素场采用同一布局 |
| 匹配／对应 | A↔B的语义、几何、字形、像素／场、骨骼对应与质量／成本 | MatchingPlan可插拔；复用已有几何／内容／rig算法，不每个动画重写 |
| 插值／求值 | 时间、曲线、类型策略、约束、解析／采样／有状态类别 | core＋math＋领域evaluator；不同值域不强行标量lerp |
| 动画组合 | 顺序／并行／错峰／循环／路径、通道读写、冲突、打断及控制 | AnimationExpr编译成同一Program／执行IR；无逐帧AST或脚本解释 |
| 效果／风格 | 几何修改、材料、后处理、时间／运动特征及其typed作用域 | 描述／配方独立，现有renderer／shader／领域算子负责实际实现 |
| 异构执行 | CPU／SIMD／线程／GPU批次、资源驻留、同步、能力、预算与观测 | 共享计算与RHI／render基础；hanim不另写设备／纹理池／renderer |

统一的是契约和计划。具体轮廓匹配、IK、流体求解或图像重建仍是领域算法，按需加载；只用轻量UI动画不引入全部rig／模拟／3D／工具。

同一执行计划可以有对象层级、时间、通道、效果和资源依赖等视图，它们通过typed引用关联，不必同构。当前首批Program的分轨／分段数组只覆盖其中严格时间调度的一部分；完整组效果／GPU资源编译需保留相应作用域和依赖，不能把平铺clip数组直接当作最终通用IR。

## 4. 匹配质量决定很多效果的上限

建议优先级：显式部件映射／稳定身份→领域语义对应→几何或视觉特征候选→用户选择的优化算法。算法不以显示文本作为唯一身份；字体glyph、UI部件、轮廓、mesh、像素场都有自己的空间和拓扑。

轮廓要处理采样密度、方向、起点、弧长、局部特征、孔洞／拓扑及交叉；骨骼要处理层级、bind pose、比例和约束；像素／场需明确输运／光流／重建或显式对应。颜色交叉淡化不等于像素物质迁移。大距离矩阵或候选搜索可由GPU加速，复杂约束／全局指派也可采用CPU或混合算法；无需规定所有matcher只能一种后端。

MatchingPlan记录算法／参数／资源版本、误差或置信度、未匹配项和容量。严格失败、自适应重组、明确替代分别报告，不能把失败静默退成淡入淡出。前后段连续与交互打断从当前合成输出取源；速度连续须有导数／状态契约。

对应缓存以实际表示／资源版本、算法版本及相关策略为键。纯材料／颜色风格变化通常不需要重做几何匹配；改变几何或对象域的modifier才使相关对应计划失效。是否在canonical表示或派生表示匹配由契约明确，避免换一个主题就重算全部对应关系。

“像素级”描述可控制的数据粒度与视觉精度；跨设备逐位确定性是另一条要求。二者分别验收。

## 5. 多种风格来自可组合的表示、外观与运动

工程判断：常见示例的相似观感不足以说明底层不通用。hanim应让同一运动意图可以组合真实材料、采样方式、光照、几何修改、纹理与不同运动特征，而非只增加颜色／缓动变体。

| 风格构成 | 举例 | 对架构的要求 |
|---|---|---|
| 几何／表示 | 矢量、网格、像素、SDF、粒子／场 | 域adapter、对应与变形能力明确；表示切换有可控成本 |
| 材料／光照 | 纸张、金属、玻璃、半透明、液态材质 | 复用renderer材料／资源；玻璃背景依赖进入同一渲染图 |
| 空间效果 | 模糊、辉光、位移、折射、像素化 | 有序处理、局部／组合结果作用域、halo、隔离与合成 |
| 运动特征 | 弹性、惯性、停格、机械节奏、流动／波动 | 时间变换、值域modifier、约束／模拟适配，不能都变成最终屏幕滤镜 |
| 设计配方 | 多种风格下的同一组件／内容任务 | 风格包展开上述能力及motion配方；组件继续画状态／部件 |

`animation.with_effect(e)`解释为对该动画声明的当前输出域添加处理。单对象blur与对parallel合成结果blur不同；pose modifier、时间映射和屏幕后处理通过typed stage区分，不能靠一个无类型filter回调随意插入。风格质量同时依赖素材、构图与工具工作流，丰富算法不自动保证美术达标。

## 6. GPU是执行后端的一等能力

**可以并且应当GPU加速；GPU渲染、GPU动画求值、GPU匹配／模拟是不同的阶段。** Manim存在OpenGL路径不能证明所有算法都在GPU上，当前hanim的CPU参考实现也不应被当作最终执行上限。

WebGPU标准明确同时提供渲染与计算，并允许compute输出storage buffer／texture；Web优先架构可以通过共享RHI使用它。[WebGPU规范](https://www.w3.org/TR/webgpu/#gpucomputepipeline)。本仓库RHI已定义compute管线／storage资源／dispatch，WebGPU声明compute=true、WebGL路径声明false；这是底座能力，尚非hanim对象采样已GPU化的证明。

| 工作 | 通常适合的路线（按实际负载选择） |
|---|---|
| 少量UI对象、低延迟控制／语义匹配 | CPU／SIMD，避免为少量数值增加提交与同步 |
| 大量点／顶点／像素／场插值、批量姿态混合与蒙皮 | GPU compute／vertex，或CPU SIMD／任务批次 |
| 大规模匹配候选、距离／特征与场重建 | GPU并行阶段＋CPU／GPU对应求解器 |
| blur／glow／位移／材料／合成 | renderer GPU pass；CPU参考及能力降档 |
| 粒子、物理／流体／反应格点 | 独立求解模块的CPU／GPU后端，固定tick／检查点，hanim编排输入或采样结果 |

准备时完成匹配、通道分析、kernel选择／编译、缓冲与资源绑定；运行时按数据域和kernel合批。大数据尽量驻留GPU，插值结果直接供后续效果／renderer消费；语义／控制查询以必要的摘要、版本与按需区域读回提供，避免每帧整场CPU↔GPU往返。官方CUDA实践也强调减少主机与设备传输、保留中间数据并合并传输。[数据传输实践](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#data-transfer-between-host-and-device)。这里借鉴执行原则，不把CUDA作为跨平台必选依赖。

编译器可做共享子结果、常量折叠、无用工作剔除、相容算子融合及dirty range计划，但须保持通道依赖、数值等级和效果次序。求值批次顺序与最终绘制顺序分开；背景依赖、clip、blur或材料混合不能为减少dispatch而任意交换。插件按阶段／批次调用，执行数据采用适合SIMD／GPU的布局，避免逐节点／像素虚调用。

CompiledPlan应记录输入／输出驻留、批次、依赖、scratch与资源预算、CPU提交／GPU时间／排队／读回、降档原因。选择能力不足明确拒绝或按配置降档；没有GPU仍可headless、CLI和TUI。GPU任务加入现有图／fence／资源池，不创建hanim私有设备或renderer。

中途retarget需从实际GPU输出版本取源：可在设备内捕获，或显式等待／异步读回供需要CPU匹配的阶段使用。不能无说明地改用可能有误差的CPU重算姿态并宣称完全连续。Agent像素审查也记录submitted／completed／presented等实际边界、资源版本与后端；只有CPU参考图时必须说明输出路径。

异步prepare另有捕获／提交策略：可以保持捕获姿态等待，或让旧计划继续并在提交时从最新输出重新绑定／rebase。后者需算法明确支持并检查版本；准备开始时的源已过期时，不能直接发布后仍宣称C0连续。取消、更新目标、失败及过期计划都有可观察状态，资源与权限限制不被子配方放宽。当前CPU retarget是同拥有线程上的同步事务，不冒充已实现上述异步机制。

数值策略明确精度、坐标范围、误差、颜色／alpha空间和确定性要求。CPU double与常见GPU float路径的几何误差在实际视口／缩放下验收；高精度需求按能力选择CPU、受约束的GPU表示或明确拒绝，不默认所有设备都有相同浮点能力。

确定性至少区分：CPU严格可重现、特定后端受约束重现、跨后端有误差预算的视觉结果。GPU浮点／纹理结果不无条件承诺逐位相同；权威模拟选择有证明的实现。只有真GPU时间／端到端证据才能声明加速，软件Vulkan与CPU编码时间另列。

## 7. 局部架构与全局架构

hanim局部以动画的描述、匹配、求值、组合与控制为产品核心；全局作为UI、游戏、影片及工具的普通可选库。UI拥有交互／布局／业务语义，模拟拥有权威状态，renderer拥有视觉呈现；hanim通过公开adapter使用它们的资源／表示，不能因动画中心接口而接管全部系统状态。

公共math／geom／memory／trace／compute／RHI的职责继续收敛单一实现。mcpp是构建／GPU或HPC插件工具，不是运行时计算引擎。Agent通过轻量provider暴露实际支持的操作、计划／通道／资源及profile；纯core不依赖UI或网络。所有工具可只用CLI／API，视觉审查由真实离屏渲染及有界区域输出提供。

## 8. 领先目标怎样验收

| 维度 | 必需证据 |
|---|---|
| 通用性 | 属性、路径、轮廓／字形、像素／场、rig／mesh、程序化及模拟适配支持矩阵；同组合／控制契约；独立UI、游戏、影片和CLI消费者 |
| 效果质量 | 同一内容跨多种真实风格；轮廓／孔洞／文字、材料、打断与中间帧；实际物质迁移、误差与美术人工审图，不只比较端点 |
| 性能 | 准备、匹配、采样、混合、渲染、输出、包体、CPU／GPU／内存／传输逐项预算；相同数据规模和质量下对照，冷暖分别测 |
| 扩展与裁剪 | 只用core／普通UI／rig／大规模场分别测真实依赖、产物与驻留；新增算法不修改核心调度器或复制基础模块 |
| 可控制／可观察 | 任意seek／打断／暂停／倒放与模拟恢复的正确语义；trace、归因、CLI／openas、真实区域像素及失败／降档诊断 |

代表性对照包括Manim的矢量／匹配创作、成熟游戏系统的姿态／混合／GPU路径、独立高性能库的CPU采样，以及本项目像素／场和UI交互负载。所有声明支持的场景均建立预算；单case领先不足以证明全引擎领先。

当前已实现独立core、对象、严格morph／效果、首批对象Program与CPU播放器／vector adapter，以及RHI的GPU基础。完整通道混合、通用matcher族、真实像素迁移、rig、hanim GPU evaluator、效果作用域、40项公共库、UI迁移及完整工具尚未交付。后续重点是把上述抽象编译到可裁剪的实际后端，用消费者与对照证据证明目标，而非先批量增加名称相近的动画。
