# hanim 通用对象动画引擎：像素插值、动画库与效果

2026-11-04。**状态：讨论草案，目标 API 尚未实现。** 维护者本轮进一步明确：**hanim 是 core＋基础动画库＋动画效果组成的通用对象动画产品；统一入口为 `hengine.play(hobject, animation, effect)`；对象、部件、几何与真实像素均可插值；UI 全部使用该引擎；完整覆盖 Manim 功能并在可衡量的维度上超过它。** 当前位于 base 的时间／数值模块只是 core 的已有实现，不能据此收窄整个 hanim 的职责。

动画必须是框架用户可以真实复用的能力；官网是消费者与验收场，不是动画库的归宿。本文纠正“官网 40 个设计已等于 40 个通用库条目”的口径，不将新增提议写成已完成。

与[通用动画设计 D-93](2026-11-01-openmui-universal-motion-design.md)、[官网重构方案](2026-11-03-openmui-site-redesign-implementation-design.md)、[中间态修复记录](2026-11-04-omui-motion-readability-presentation-design.md)共同阅读。保留原范围、40／20 数量下限、本地真实验收优先、PR #1 单分支协作；同时工作的 subagent 上限为 **2**。

维护者后续补充的骨骼／模拟／硬件加速、渲染／UI／Agent 总体边界与 Noita 类材料反应压力标准，见[整体引擎与框架架构讨论](2026-11-04-omui-engine-framework-architecture-design.md)。core 统一时间、值域、计划和批处理；rig／physics／fluid 等独立模块提供算法；有状态模拟须以检查点和输入重建，不能假装都能解析 O(1) 寻址。

## 1. 当前实现到底在哪里

以下是当前工作树源码核对，不是最终发布验收声明。

| 目录／模块 | 已有真实能力 | 尚缺的公共产品边界 |
|---|---|---|
| `modules/base/hanim/`，`import hanim;` | Tween、Track、Spring、Decay、Motion；Timeline 编排、时间映射与确定性采样；新增 PreparedShapeMorph 的单闭合轮廓准备／无分配采样，已有独立图形消费者 | 尚无完整 HObject／Engine；形状检查点不等于任意拓扑、像素对应或完整动画库 |
| `modules/ui/motion/`，`import openmui.motion;` | Matter 提取显示列表；Morph 对两个绘制状态匹配、采样、绘制 | 通用轮廓归一化／对应关系不完整；可复用 clip 图与统一播放句柄未收成用户产品 |
| `modules/ui/view/` | `motion` 槽、默认规则、暂停／寻址／倒放节点 motion、`pixel_effect` 及效果列表 | 不同动画通道尚未统一为一个完整 AnimationPlan／Handle；同属性混合不能统称已支持 |
| `modules/gfx/render/vector/` | 16 个内置图层／像素效果、CPU／GPU 后端、效果参数 | 效果处理器不等于完成设计的时间动画；不同效果类型的材质插值需要独立契约 |
| `modules/ui/theme/`、`modules/ui/theme/styles/` | 主题令牌、作用域、5 个风格包与效果规则 | 整套风格交叉淡变不等于所有风格属性可连续插值 |
| `site/src/basic_motion.cpp`、`creative_motion.cpp` | 40 个基础设计场景、20 个创意任务场景 | 场景含 endpoint builder／应用模型；不能直接作为 40 个独立可绑定动画库资产交付 |

`hanim` 是真实且可独立使用的时间／数值引擎。缺口在它与图形、对象和用户 API 之间的产品层。框架用户不应依赖 `openmui.site` 才能取得通用动画。

现有 `compose_motion`／数据 `compose` 组合一组内置操作与参数，不等于任意 clip 图；`motion` 数组是候选选择／降档。现有 `hanim::Timeline` 同一 target 的多个 clip 按放置顺序应用，后者覆盖前者，不能将其描述为已经具备通用加法混合。

## 2. 三角形 → 圆的核对结果

本轮开始前，`mix_paths` 要求 verb 数、点数、verb 序列均相同。普通三角形是 move／line／line／close；圆是 move／四段 cubic／close，二者不兼容。旧分支绘制 A、B 两份形状并交叉淡化，不是真正的轮廓变形。

**第一块公共能力（已提交）**：`hanim:shape` 的 PreparedShapeMorph 可独立准备 line／quad／cubic 单闭合轮廓，保形细分后采样真实 triangle→circle；图形消费者以单条 Path 绘制并组合既有材质／效果，三配置通过，见整体架构稿 §11 与 hanim README。

**后续本地整合候选**：公开 `sample_segment` 与 `DisplayList::record_path` 将准备后的曲线直接写入调用者 arena，UI Morph 已消费同一个 hanim 计划；不在 UI 再写轮廓算法。`PreparedPaintMix` 把不同 solid／gradient／image／Material 展平为有界叶子材料，在当前一条几何上着色、再应用一次 coverage／clip。CPU 复用既有 shader，GPU 使用共享无资源着色函数与独立绑定入口；不是两端轮廓截图混合。快照保留当前几何和材料，连续打断测试不再残留旧轮廓。严格准备超过 16 个不同叶子报告错误、旧计划仍可采样；当前 UI Morph 的失败诊断与未来 Engine::play 的失败保留 handle 契约仍须区分。

本地 dev／release 各 **4 包、28 个程序、194 组通过**（hanim 63／raster 19／vector 60／motion 52）。材质打断探针原有 2 条失败已转绿；真实 Vulkan/lavapipe 的 8 张材质读回最大每通道差 **1/255**，声明阈值 2/255 之外为 0 像素。LLVM sanitizer 的用例断言通过，但 3 个 GPU 程序退出被 LeakSanitizer 判失败；已符号定位到 driver／LLVM JIT 分配，仍用纯 RHI 生命周期基线复核，不记为 sanitizer 整套通过，也不关闭泄漏检测。以上均未证明实体显卡性能、HObject／Engine 或像素域物质迁移已完成。

纯 RHI 生命周期复核已实跑：`modules/gfx/rhi/sdl/tests/lifetime_probe_test.cpp` 的 4 个 case 分别独立进程，无设备退出 0／无泄漏；仅创建设备与仅创建后销毁一条管线均为 2800B／28 次分配；提交读回并销毁后为 3512B／36 次分配。每个 case 的断言均通过，后三者退出 1。符号与加载地址映射定位到 lavapipe 26.2.1 所用 LLVM 22.1.8 的 `createIntelJITEventListener`，提交另含 Mesa `u_mmInit`／SSE2 emit 分配；设备基线不依赖 vector、Material 或 UI。报告 `/tmp/omui-rhi-lifetime-baseline.report.omui.json` 保留环境、二进制 SHA、原始及符号日志。该复现将驱动残留与本轮消费者分开，仍不抹掉 GPU sanitizer 失败记录。

更广的官网 sanitizer 还捕获了独立的真实 UAF，不能归给 driver：嵌套 travel Morph 共用单一 TLS DisplayList，重入时向正在遍历的列表追加命令，扩容后读取失效的 152B Command。真实特性页及新 `travel_reentry_test` 均先复现。修复为每个活动递归深度独占稳定 scratch／params 的 RAII 租约池；6 组检查父子效果、12 层、同一 Morph 重入、寻址／倒放／端点、并发和预热后标量／数组分配不增。修复后 SDK／motion／site 在 dev、release、LLVM sanitizer 各 27 程序、187 组通过，原特性页 6 组全部恢复。该池仍是旧 UI travel 作用域的 TLS 实现，不替代未来 Engine 的实例／调用者 scratch、容量预算与观测契约。

同类圆角矩形通过 radius 改成圆可以连续插值，这不能作为任意三角形→圆已经支持的证据。`hanim::MotionPath` 解决沿路径的位移与朝向，不负责路径 A→B 的轮廓对应。

孔洞／拓扑变化仍明确报告不支持；dash 的起点／方向重排连续性尚未验收，不能用实线 stroke 的通过代替。公共 HObject／统一播放入口、真实像素域及完整动画库继续按下文交付。

## 3. 完整动画产品的分层与目录建议

**按维护者要求，hanim 对外就是完整引擎，而不是只能算数值的工具库。** 对内保持可裁剪分层：core 只依赖标准库与 `openmui.math`；对象、配方、效果、播放器是同一产品的其它包。UI／视频／其它框架是消费者。为统一入口让 core 反向依赖 UI 或复制一套渲染器，都不能解决缺口。

| 层 | 职责 | 位置建议 |
|---|---|---|
| 时间与数值核心 | 曲线、插值、轨道、编排、确定性时间求值 | 建议 `hanim.core`，`modules/base/hanim/core/` |
| 通用对象与准备计划 | 身份／层级、几何与像素域、资源引用、能力、对应关系 | 建议 `hanim.object`，`modules/base/hanim/object/`；不依赖 UI |
| 可组合动画库 | 属性／变换／轮廓／字形／像素／粒子与编排配方 | 建议 `hanim.library`，`modules/base/hanim/library/`；数据与算法不依赖 view 或具体 GPU |
| 动画效果 | 效果描述、材质计划、CPU／GPU 实现绑定、成本与降档 | 建议 `hanim.effect`，`modules/gfx/hanim/effect/`；复用 gfx 中现有单一实现 |
| 通用引擎与播放器 | 场景、计划准备、对象绑定、生命周期、统一句柄、按批采样与提交 | 建议 `hanim.engine`，`modules/gfx/hanim/engine/`；不依赖 UI |
| 统一产品入口 | 导出 object／library／effect／engine，对调用者呈现完整 hanim | 建议 `import hanim;`，伞包 `modules/gfx/hanim/` |
| 内容适配 | 真实 shaping 的字形、公式／图表／数学对象、内容资产 | content 层适配包；不把字体／公式依赖塞进 core |
| UI 适配与组件默认配方 | UI 状态／部件 → hobject；输入／语义协调；按组件用途选库配方 | `modules/ui/motion/`、view、theme；全部消费通用引擎 |
| 消费者 | 原生／Web 应用、独立动画、官网、影片与测试工程 | 只调用公开 API |

以上是**目标归属草案，不是当前目录**。现有 `modules/base/hanim/` 的 `import hanim;` 是时间核心；若将相同名字变成完整伞模块，必须先迁移为 `hanim.core`，逐项修改依赖与导入，再引入高层伞模块，不能让旧核心导入新伞模块形成环。包身份、路径、过渡兼容与生成绑定需要单独迁移检查点，不在本轮讨论中批量移动文件。

实际已有 `raster → hanim`、`render.vector → hanim + raster`，因此在当前 `hanim` 直接 import `render.vector` 会形成真实循环。core 提纯后，这些低层消费者只导入 `hanim.core`；高层引擎再使用 renderer。HObject 的通用数据契约不能依赖高层 DisplayList 类型；DisplayList 是一种 adapter 输入／输出，资源表示与契约也要避免倒置。

`openmui.view` 目前依赖 `openmui.motion`；后者的通用 Matter／Morph 部分应下沉为 hanim 的对象／计划能力，UI 语义匹配与布局事务留在适配层。不能把 Runtime 绑定实现塞回引擎形成循环。渲染器、shader 与主题令牌仍各有单一实现，hanim 通过契约与适配使用它们。

base 中的库配方只引用中立的对象域、效果标识与参数契约，不 import gfx 的具体 shader／纹理／DisplayList 类型；实际实现由效果／渲染插件绑定。独立性意味着可只取 core，也可单独使用整个 hanim 产品，不能要求使用完整动画库时先构造 UI Runtime。

## 4. 建议的公开入口

主入口按维护者给出的 **`hengine.play(hobject, animation, effect)`** 收敛；第 8 节给出对象与引擎契约及目标 API 草案，尚不能直接复制运行。上一版 `animplay` 提议最多作为便利函数，必须委托同一 Engine／Plan，不另立播放器。C++ 准备失败使用 `std::expected`，不使用异常。

单对象形式绑定一个计划，不要求调用者手写每帧赋值；A→B 形式明确绑定两个端点或状态，不需要用户先手工归一化路径。底层仍可直接构造／采样计划，支持 headless 与离线渲染，不只有交互式 play。

句柄统一控制几何、字形、效果与风格轨道。至少包括 play／pause／seek(time)／seek_progress／reverse／rate／retarget／cancel／状态查询。cancel 是结束、保持当前视觉还是恢复初始视觉必须明确；不能暗中撤销业务数据。UI 模型／动作语义及时提交，视觉过渡不阻塞输入，当前呈现几何用于真实交互。

节点引用、参数、资源与随机种子是计划输入。脚本只构造计划和发命令，逐帧由原生引擎按批采样。句柄和计划也应能通过 CLI／agent 查询与控制。

## 5. 三类可组合对象

### 5.1 AnimationClip／AnimationPlan

动画库返回可绑定、可组合的描述，至少支持 sequence、parallel、stagger、delay、repeat、reverse、嵌套与时间重映射。不是通过函数名包装几个官网 builder。

同属性冲突必须声明 replace／add／multiply／blend 等策略及优先级；位移相加、缩放相乘、局部／世界变换顺序与颜色混合不能共用一个含糊的“叠加”。不兼容的组合在准备阶段报告错误或明确降级。

### 5.2 ShapeMorphPlan

准备阶段完成形状适配、轮廓方向／起点、分段与对应关系。线段转 cubic、曲线细分与受保护角点须保证端点形状；不能仅靠采样点数相同就宣称形状正确。每帧只求既定计划，不重新匹配或分配。

首个实现检查点严格支持单个简单闭合轮廓，真实交付 triangle→circle、star→rounded shape 等跨基本形状的例子；随后必须覆盖多轮廓、孔洞、拓扑分裂／合并及像素／粒子域的对应关系。首个检查点的支持范围不能冒充整个产品范围。路径标准化算法使用已有数学／几何基础，不能引入依赖 UI 的 hanim 核心。

准备结果明确返回 continuous_morph／explicit_fade／unsupported 及原因。显式 morph 的严格模式不能偷偷成功为淡化。测试必须检查中间态确实是一个连续变化的几何对象，另检查原始 A／B 端点像素与打断连续性。

### 5.3 MaterialTransition／StyleTransition

同类型效果的数值参数可插值；颜色、圆角、阴影、渐变等有各自类型规则。不同 shader 类型不能直接插值程序代码，应使用明确的材质混合策略。

两种材料都使用 `ShapeMorphPlan` 采样出的同一个当前形状与 mask；需要混合时混合它们的材质结果，不能简单叠两份各自固定的 A／B 几何。`fx::chain(A,B)` 的先后处理与 `material::mix(A,B)` 的并行混合是不同操作。

完整风格还包括字体与布局：同字体的可变轴可以连续，字体族切换、重新 shaping、布局约束和可读性有明确策略。没有统一映射的字段使用声明过的切换／替代方式，不能把 whole-layer crossfade 统称为完整风格插值。

## 6. 真正的动画库怎样交付

40 个基础设计逐项拆解并迁移为公开可复用配方；官网中的任务模型和 A／B 应用状态可保留在官网，但 motion／effect 实现不能只存在那里。

每个库条目至少具备：

1. 稳定 ID、版本、公开工厂／数据描述及参数范围。
2. 适用目标与能力要求：值、变换、形状、字形、任意可绘制子树等；不适用的对象明确报告。
3. 可与其它条目组合的通道、冲突策略、时间语义和性能成本。
4. 默认、reduced、off 与分档替代；不能用换颜色或改时长重复凑数量。
5. 独立库 CTS、确定性采样、端点／中间态／打断／取消／倒放证据。
6. 仓库外消费者的真实使用示例，依赖不含 `openmui.site`。

组件的默认配方也调用这些条目：按角色、形状与用途组合，不在组件里写时钟。必须有稳定的部件身份和布局／状态分解；不能依靠绘制序号碰巧匹配，或响应式重基时把内部选择状态带错位置。显式父级布局动画与嵌套控件默认动效需要统一编排，不能互相吞掉。

## 7. 调整后的实施与验收顺序

| 阶段 | 交付 | 通过依据 |
|---|---|---|
| A0 · 公共契约 | plan／clip／handle／对象绑定、组合、能力结果、库归属与语言边界 | API 与依赖图可 review，不把伪代码标为可用 |
| A1 · 最小闭环 | 独立工程调用统一入口完成三角→圆、同类型材料参数插值、不同材料共用当前几何的过渡 | 不依赖 site；真实单形状中间态、风格命中、暂停／寻址／倒放／打断与输入均验证 |
| A2 · 库化 | 40 个可复用条目及默认组件配方迁移 | 每项有库 CTS 与异构消费者，网站不再是唯一实现来源 |
| A3 · 完整组合 | 20 个创意任务使用同一公开库，组件部件与布局／状态协调 | 不重复实现动画；真实人／agent 操作与动态质量闭环 |
| A4 · 语言与产品收口 | 契约生成的 C／TS／Python／Lua 接口、最小消费者、目录／文档／诊断 | 干净工程实际运行；本地 release／sanitizer／浏览器／资源预算；其它平台 CI 异步反馈 |

这些实施包归入原 R0–R6／C2–C6，不另行宣布旧检查点完成。维护者本轮明确的像素域和完整 Manim 功能范围纳入第 8–11 节，不能止于 A1 的单轮廓闭环。规范变更遵循 OpenLifa 子模块的既有提交流程；本仓库仍向 PR #1 追加检查点。当前中间态修复是必要基础，但不能替代这一整套公开动画产品的交付。

## 8. hobject、animation、effect 与 Engine 的契约

### 8.1 hobject 不是 UI 节点，也不是只有一张截图

HObject 是通用可动画对象的稳定身份与多种表示的集合。对象可以来自原生形状、图片、点云、公式、3D、UI 子树或其它框架；Engine 不认识 Runtime／控件类型。

| 契约 | 至少包含什么 |
|---|---|
| 身份与部件 | 稳定 object／part ID、带代次的句柄、父子关系、版本；部件名优先于绘制序号，旧句柄不得误指复用槽的新对象 |
| 变换与空间 | 局部／世界变换、pivot、尺寸／分配、clip／mask、顺序；3D 额外包含深度与相机空间 |
| 可选表示 | 矢量轮廓、字形／子路径、图像／纹理、采样点／粒子／碎片、mesh；按能力按需生成 |
| 外观 | 填充／描边、渐变／材料、透明度、效果绑定；几何与材料独立采样 |
| 域 | parts／glyphs／paths／points／pixels 等可选择域，供 stagger、匹配与逐元素函数使用 |
| 资源 | 字体、图片、纹理、缓冲的句柄、版本与生命周期；删除 UI 节点不立刻销毁仍在退出动画中的资源 |
| 能力与代价 | 能否提取路径／像素／深度、确定性等级、准备与每帧预算、设备要求与替代计划 |

UI adapter 提供状态快照与命名部件，并保留到 UI ID 的映射；引擎输出呈现几何给 adapter 驱动实际命中。模型／语义立即到 B，动画中的旧物质不成为第二个业务控件。不能每帧通过文字或不稳定绘制顺序重新识别对象。

当前 Matter／Piece 是可复用的绘制状态原料，不是完整 HObject；不同图片资源在当前 Morph 中仍退成交叉淡化。内置 particles 的图层像素块飞散，也不能证明已经实现任意 A→B 像素对应和重组。已有 Vec3／Vec4、RHI depth／compute 或二维 mesh 绘制同样不能算作完整 3D 场景、相机和曲面动画。

### 8.2 一个主入口，两种对象过渡写法

以下仍为目标接口草案：

```cpp
// 单对象：在同一对象上执行可组合计划和有序效果链。
auto result = hengine.play(
    object,
    anim::parallel(anim::move_to(...), anim::rotate_by(...)),
    fx::chain(fx::glow(...), fx::blur(...)));

// A → B：目标对象属于 animation 的显式输入。
auto morph = hengine.play(
    triangle,
    anim::morph_to(circle, {.domain = Domain::contour}),
    material::transition(ink, glass));

// 像素物质：真实内容打散，经同一计划重组成另一对象。
auto regroup = hengine.play(
    card,
    anim::morph_to(badge, {.domain = Domain::pixels}),
    fx::chain(fx::trail(...), fx::glow(...)));

if (morph) {
    morph->pause();
    morph->seek_progress(0.5);
    morph->reverse();
}
```

`play(A, B, animation, effect)` 可以是便捷重载，必须先规范化为相同 Plan；不允许两套变形实现。`prepare` 可独立调用，返回不可变的 PreparedPlan，便于资源预热、离线渲染与多帧并行。`play` 是准备加播放的易用入口，准备错误必须可观察。

animation 决定如何随时间变化；effect 处理当前物质／像素。效果的参数也可以成为 animation 的通道。`fx::chain(A,B)` 是有顺序的处理；`material::mix(A,B)` 是同一当前几何上的材质混合；风格 transition 还会展开为几何、颜色、字体／布局策略和效果参数轨道，不能当成一个 shader 名称。

### 8.3 有状态播放器与纯函数动画并不冲突

Engine 管理对象、时钟、句柄、资源和事件；**PreparedPlan 在确定的输入版本、资源、种子与时间下纯函数采样**。播放／暂停／调速改变时间映射；retarget 在指定时刻采样当前物质，建立新计划并保留所需历史。输入不固定、直接读取实时随机数或任意外部状态的函数，不能被称为可任意跳转的确定性动画。

准备阶段完成对应关系、路径细分、像素采样、资源与缓冲分配；暖路径按批写入预分配输出，不逐像素调用脚本、不逐对象分配。非解析物理／累积更新使用确定性求解器、检查点或烘焙缓存，不能假装所有算法都是 O(1) 的解析函数。尺寸／资源变更需要显式重新准备和预算，不能藏进每帧热路径。

对象、匹配器、采样器、效果与 renderer adapter 使用按阶段／按批的插件契约与各自 CTS；每个阶段有 trace／日志类别，预算进入 `tests/bench` 与统一预算文件。无需 RTTI 或每个像素一次虚调用。像素域准备所需的 A／B 离屏捕获作为共享渲染图的资源任务，缓存版本与生命周期；正常帧仍复用同一渲染器、纹理池与图，不能为 hanim 再做一套渲染实现。

简单属性动画应由同一计划编译成已有 Set／SoA 的快速路径，不要求每帧重新捕获整棵 UI 或生成全部粒子；像素表示只在所选动画确实需要时准备。可视采样不反复调用业务 setter／action，不制造额外 undo 记录。Engine 的交互历史可以有界；完整离线重放需要持久化完整计划／输入历史，不能把短窗口交互缓存误报为无限历史。

## 9. 像素级插值的技术含义与必达能力

早期约定已经包含这一范围：[实施计划 §6.3](2026-10-26-openmui-implementation-plan.md)明确对象／子对象／像素三层及真实像素拆粒子；[D-93 §4、§6](2026-11-01-openmui-universal-motion-design.md)明确多粒度物质、轮廓重采样与像素粒子配对。当前缺失属于未完成项。

“像素级”至少包括以下三个方面，不用截图淡化代替后两项：

1. **逐像素处理与双端过渡**：任意 HObject／子域可得到真实内容、mask 与坐标，在任意时间执行像素函数；能访问 A／B 与映射，支持采样、置换、遮罩、溶解、材料处理。颜色混合、alpha、边缘覆盖、色彩空间必须有明确定义。
2. **真实像素物质的空间插值**：由内容采样得到带身份的点／碎片，保存位置、颜色、alpha、来源坐标与面积／权重，准备阶段建立 A→B 的对应。中间状态改变位置／形状／材料，可以沿路径打散再聚合，不是只改变两张图的透明度。
3. **亚像素几何插值并正确光栅化**：路径／字形／mesh 按更适合的连续表示变化，最终得到对应像素。矢量不强制先栅格化，但同一对象随时可选择像素域。这样放大保持轮廓质量，并能进入像素特效与粒子管线。

像素域可以表为 `q_i(t) = F(source_i, target_match(i), t, seed)`，其中 q 包含位置、颜色、alpha、尺寸及 UV；渲染器按覆盖与合成规则输出当前图像。不同尺寸／分辨率／像素数须定义增补、消失、权重与采样密度；颜色／空间配对算法的成本与质量可分级。没有语义信息的两张图片不存在唯一正确的对应关系，必须提供显式映射、可选择的确定性匹配器和可观察的质量结果。

三角→圆至少要验收两条路径：**轮廓域**产生单一连续变化轮廓；**像素域**由真实着色内容生成物质并变到目标轮廓／图像。每条都能在同一当前几何上叠加或过渡材料。孔洞、不同部件数、分裂／合并不能靠兼容点数检查掩盖，按明确计划处理；严格 morph 不允许静默淡化成功。

CPU 为确定性参考；GPU 通过同一计划与渲染图执行，按声明的数值／像素容差验收。跨任意 GPU 逐位相同不能仅凭 core 的确定性承诺；非确定性扩展须单独标注，不能进入严格重放声明。交互画面与离线输出使用同一对象／计划，支持以更高采样率、密度和 motion-blur 子帧导出。

## 10. 三种独立的分级

| 轴 | 分级与约束 |
|---|---|
| 使用难度 | L0 组件默认配方；L1 选库条目／参数；L2 组合计划、对应关系和句柄；L3 原生／shader 插件。全部进入同一 Engine |
| 对象粒度 | 对象／部件／字形／轮廓点／像素／粒子／mesh。由动画意图与对象能力选择，不把像素域当作另一套动画系统 |
| 质量与设备 | T0–T4 的分辨率、样本／粒子数、效果阶数与 backend。完整产品保留所有能力；具体设备可选择已声明替代，不能隐瞒降级 |

调用者至少可选择 strict（能力不足报告错误）、adaptive（允许质量调整、报告实际计划）、fallback（允许指定替代）。不能让 triangle→circle 在任何设备悄悄变为截图淡化，还向调用者报告 continuous morph。分级降低代价与使用门槛，不缩减总体目标。

组件默认动画使用相同库中的角色配方：button 的 hover／press／focus，toggle 的 track／thumb／label，segmented 的 indicator／item，drawer 的 panel／backdrop，tree 的展开与插入等。优先级与同通道冲突须明确；布局重基与控件状态动画分通道，不能 resize 后指示器与标签分离，也不能让默认子动画吞掉显式父动画。

## 11. Manim 全覆盖与“更好”的验收范围

本轮在线核对的是 **Manim Community v0.22.1** 官方参考；它与 **3b1b/manim（ManimGL）** 是两条实现，必须分别记录版本／commit 与差异，不能只挑一个教程作为“全部功能”。以下是必含的范围分组，**不是已经完成的逐 API 全量对照表**。下一步应导出正式功能清单，为每项建立 hanim 对应能力、独立消费者、原生／Web／离线输出及差距，未建立全表前不能报告 100%。

| 范围 | 必含内容 | 当前可复用与缺口 |
|---|---|---|
| 对象系统 | group、层级、2D 形状／路径、点云、图像、文字、公式、3D 对象／曲面 | 有矢量、文字与图片原料；统一 HObject、完整内容与 3D 动画适配未交付 |
| 动画与对应 | transform、matching parts、沿路径、矩阵／逐点变换、homotopy、显现／书写／强调／退场 | 时间核心与部分 Morph 已有；任意轮廓、通用点域与像素配对、完整库仍缺 |
| 编排与联动 | sequence／parallel／lag／nest、时间映射、tracker、updater、依赖关系 | Timeline 可复用；对象绑定、冲突混合和可重放联动图未统一 |
| 数学与图解 | 坐标系、函数／参数曲线、图／网络、矩阵、向量场、流线、公式语义匹配 | 不属于换几个基本图形即可覆盖的能力；完整内容库与匹配需单列交付 |
| 场景与相机 | 2D／3D 相机、移动／缩放、多视角与场景组织 | 复用共享渲染图；3D／多相机及完整场景契约仍缺 |
| 材料与渲染 | fill／stroke／gradient、光照／深度、CPU／GPU、像素与自定义效果 | 16 效果及 CPU／GPU 基础可复用；通用材质计划、mesh／深度与完整组合矩阵未交付 |
| 输出与工作流 | 静帧／视频、音轨／时间、资产、分段输出、预览、扩展与调试 | 媒体模块已有基础；独立 hanim 消费者、统一场景／资源／导出闭环仍缺 |

官方证据：[参考目录](https://docs.manim.community/en/stable/reference.html)、[Mobject.interpolate](https://docs.manim.community/en/stable/reference/manim.mobject.mobject.Mobject.html#manim.mobject.mobject.Mobject.interpolate)、[Transform](https://docs.manim.community/en/stable/reference/manim.animation.transform.Transform.html)、[Homotopy](https://docs.manim.community/en/stable/reference/manim.animation.movement.Homotopy.html)、[内部实现说明](https://docs.manim.community/en/stable/guides/deep_dive.html)、[ManimGL 源仓库](https://github.com/3b1b/manim)。Manim 的对象点与颜色插值不能简单等同于任意截图逐像素对应；逐像素物质能力仍按本产品自身契约独立验收。

“更好”转为同场景可测目标：交互与离线同计划、任意时刻重放、打断连续、组件和语义无需额外绑定、agent 查询与控制、原生／Web 一致的组合、明确性能／内存／下载预算和诊断。它们是目标，不是当前性能胜出的声明。40 个库条目和 20 个创意消费者是产品下限，不能替代 Manim 全覆盖清单。

功能覆盖需要等价原生能力，不能把它误报成任意 Manim Python 程序原样兼容。特别是任意 Python updater 与“脚本永远不进入每帧路径”的既有规则存在执行方式冲突：应提供原生批处理、表达式／依赖图或构建期烘焙的对应能力；无法转换的任意脚本回调须明确不兼容，不能声称已自动解决。数学公式／字体／外部资产的依赖与许可证也进入内容适配契约。

建议 review 后按以下顺序展开，而非继续增加仅官网可见的动画：

1. **H0 契约与全表**：HObject／Plan／Effect／Engine、三轴分级、像素语义、Manim Community＋GL 功能台账、现有模块迁移图。
2. **H1 独立纵切**：无 UI／site 依赖的三角→圆，轮廓与真实像素两域、同形状材料过渡、同一 Engine 的播放／采样／打断／倒放，CPU 和实际 GPU 验证。
3. **H2 UI 统一**：UI → HObject，命名部件、默认配方、布局／状态／输入分离，迁移当前 Matter／Morph，关闭已暴露回归。
4. **H3 库与消费者**：不少于 40 个真实公共库条目，至少 20 个创意组合；官网、独立程序和离线输出共同消费。
5. **H4 完整覆盖与质量**：公式／图表／向量场／3D／相机／联动／媒体与插件逐项补齐，完成 Manim 全表及“更好”的对照基准。

各阶段沿用原 R0–R6／C2–C6 的任务映射，本地真实验证优先、CI 异步；同时工作的 subagent 不超过 2。此文是讨论与 review 产物，不宣称上述迁移或能力已经实现。

## 12. 旧 Sunrisepeak/Hanim 的参考取舍

维护者提供 [Sunrisepeak/Hanim](https://github.com/Sunrisepeak/Hanim)，要求选择性参考。2026-11-04 通过 GitHub 读取源码；本节审阅固定版本 **`27f9ebee6ab94d8513e40390b3968a6b9c0ef95a`**，不是旧项目运行／性能验收。README 标注 WIP，仓库同时保留 `core/` 与 `old_project/` 两代实现，不能把不同代际接口拼成一个当前契约。

### 12.1 保留的设计意图

1. **执行器、行为、对象分离**：[原设计说明](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/old_project/docs/design-thinking.md)明确 HEngine／HAnimate／HObject 三者的独立性；这与当前完整产品目标一致。HEffect 加入后同样保持独立可组合。
2. **独立原生场景与对象库**：形状、PixelPanel、Transform、DrawBorder 等直接用于动画程序，说明动画库本来就应能独立于 UI 使用。
3. **组合动画**：HAnimGroup 与 HParallelAnimate 表达复用和对子对象应用的意图。新实现保留组合能力，重做通道冲突、时间映射与执行模型。
4. **跨框架适配的方向**：[旧 ImGui 适配](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/old_project/ImGui.hanim.hpp)展示由外部 UI 消费动画值的方向；新适配须通过公开对象、计划和呈现契约，不沿用宏、全局当前 ID 或逐组件包装动画。

### 12.2 不沿用的实现机制

| 源码证据 | 问题与推导 | 新契约 |
|---|---|---|
| [core/engine.hpp](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/core/engine.hpp) 的静态 Instance／Config、构造时初始化 Render／Recorder | Engine 与全局窗口、录制和设备生命周期绑在一起，不利于宿主嵌入、多实例与无窗口使用 | Engine 显式实例化；时钟／后端／目标由使用方注入；独立 viewer 是普通宿主 |
| [core/scene.hpp](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/core/scene.hpp) 的 play_internal → render，内部循环推进全部帧，并保存每帧对象副本 | play 同时编排、推进和渲染；target_frame 读取已生成帧缓存。不能据此证明未计算帧随机寻址，且场景长度影响保留量 | play 提交计划并返回句柄；宿主驱动 evaluate(t)；离线直接采样所需时间，缓存有明确预算 |
| [animations/animation.hpp](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/animations/animation.hpp) Rotate 使用 mOldValue 计算增量；[creation.hpp](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/animations/creation.hpp) DrawBorder 依据当前 points.size 推进 | 结果依赖此前 update 顺序，不能直接支持任意顺序采样、倒放和并行帧 | 不可变源数据＋时间直接生成当前状态；需要累积的算法明确求解／检查点契约 |
| [core/framework/object.hpp](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/core/framework/object.hpp) 的 ref／clone／move_sema 与复制赋值共同控制 mData | 对象身份、复制状态与共享资源的语义交织；动画 begin／finish 又修改 move flag，难以推导并发动画生命周期 | 分开 HObjectRef、不可变 Snapshot 和资源租约；普通 C++ 值／移动语义不随播放状态改变 |
| 同上 align_components／align_data_style1 复制部件／重复点，并缩放透明度；[interpolate.hpp](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/core/framework/interpolate.hpp) 按数组位置及较短长度处理 | 点数相等不等于轮廓／部件语义对应，也不能保证形状、coverage 或颜色守恒；空输入与不兼容状态需要显式校验 | 准备阶段建立有类型的对应；保形细分、方向／起点、birth／death、像素权重各自定义；失败返回原因 |
| [core/framework/animate.hpp](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/core/framework/animate.hpp) 逐动画虚函数与整对象写回；通用插值构建对象／vector | 热路径模型与当前无虚调用、暖帧零分配目标不一致；同属性并行动画缺少统一解析 | 配方编译为按类型批量执行的计划；明确 replace／add／multiply／blend、空间与优先级 |
| HObject 直接包含 DrawMode，并导入 gl_interface；[core/render.hpp](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/core/render.hpp) 管理 GLFW／GL 和逐对象绘制 | 通用对象契约绑定具体绘制接口，无法自然复用当前共享 renderer／多后端 | 中立对象数据；renderer 为后端，窗口为宿主；沿用共享 RHI、渲染图、纹理池与 CPU 参考 |
| [objects/pixels.hpp](https://github.com/Sunrisepeak/Hanim/blob/27f9ebee6ab94d8513e40390b3968a6b9c0ef95a/objects/pixels.hpp) 的 PixelPanel 是小矩形构成的对象组 | 像素题材对象有参考价值，但该类本身不能证明真实图片／UI 像素采样、跨对象对应、运输与重组 | 从真实内容与 mask 准备像素域，公开采样密度、源坐标、颜色／alpha、面积权重和匹配质量 |

以上是源码静态推导，不把未运行的旧实现写成已复现崩溃或性能数字；本轮不复制旧源码、不修改旧仓库。

## 13. omui 以普通用户身份使用 hanim

### 13.1 接口边界

**独立引擎不导入 UI，omui 不访问引擎私有成员。** UI 适配只使用公开的 Adapter／Snapshot／PreparedPlan／Presentation 接口；其它 UI 框架有相同接入权。hanim 的配对与时间算法不写 button、toggle、Runtime 或无障碍角色的特例。

| 模块 | 所有权与职责 |
|---|---|
| hanim | 对象、域、匹配、动画／效果计划、求值、句柄和资源引用契约 |
| renderer／host | 图形设备、共享渲染图、像素输出；窗口／编码／离屏目标 |
| omui adapter | UI ID ↔ HObjectRef，部件、坐标、资源、状态版本与呈现结果的转换 |
| omui 默认配方／theme | 根据组件角色、形状、用途与风格选择公共动画条目，定义触发与覆盖规则 |
| 应用 | 业务状态、交互意图、可选覆盖与创意组合 |

HObject 的使用接口至少区分：

- **冻结 capture**：导出独立 Snapshot 与资源租约，可在 UI 销毁后按约定继续采样和离线输出。
- **实时 bind**：公开 Binding 关联宿主对象，版本变化触发重新准备；对象销毁／代次失效有明确结果，不借用临时 UI 内存。
- **子域选择**：整组件、命名部件、某段字形／轮廓／像素使用统一 selector，不为每一种控件手写专用引擎动画。

转换优先保留矢量、真实 shaping、图片引用、clip、层次、命名部件和布局 reference；需要像素动画才捕获真实像素。一个复合组件仍是有结构的 HObject，不能强制压成一张平面截图。

### 13.2 一次 UI 状态变化的完整流程

1. UI 业务状态提交为 B，语义及时生效；adapter 取得当前呈现 A 与 B 的目标快照及命名部件。
2. 默认规则选公共库配方，准备器生成同一 hanim Plan；应用显式配置也走相同接口。
3. 框架的帧管线使用宿主时间批量 evaluate；play 不占有事件循环，不内部等待动画结束。
4. 引擎产出当前物质与 Presentation；共享 renderer 执行同一渲染图，adapter 用同一呈现结果维护命中、局部坐标、裁切及 agent 可见几何。
5. 打断从当前采样状态建立新计划；删除后的退出物质保留所需资源，退出物质不成为新的可操作业务对象。

单击、undo、业务 setter 不由可视采样逐帧重复调用。命名部件的状态运动与外部分配／父布局运动分别组合；选中面跟随其选项的 reference，不按旧绝对坐标独立漂移。端点、布局版本、异步字体／资源更新都必须显式处理，不能依赖捕获后永远不变的假设。

### 13.3 独立性必须能被测试证明

1. **纯动画程序**只依赖 hanim 和选定 renderer／host，创建对象、组合动画与效果，交互和离线输出均不构造 omui Runtime。
2. **omui 程序**经公开 adapter 将真实组件转为 HObject，组合相同库条目；禁止直接引用引擎 implementation partition 或私有状态。
3. **第三方／合成宿主 adapter** 使用相同接入 CTS，证明 omui 没有特权接口；不需要为此引入另一个大型 UI 框架依赖。
4. 同一计划在任意采样顺序、暂停／倒放、打断、对象删除、资源变更下有证据；真实像素与真实输入均检查，不能只测数值轨道。
5. 40 个基础库条目与 20 个组合都引用公开 hanim；官网只持有业务模型与场景内容。未迁移的现有 UI 特有能力列入清单，不能用新旧两套长期并存宣称统一完成。

这些条目是后续实施和迁移的验收边界，本节不宣称当前已有该 adapter 或完整 HObject／Engine API。
