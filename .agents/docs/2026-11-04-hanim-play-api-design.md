# hanim play API：候选入口、动画表达与统一计划

2026-11-04。状态：**接口 review 稿；严格对象域的嵌套 Program 首批实施中**，区分当前播放器与完整目标API。配套[hanim完整产品](2026-11-04-hanim-animation-library-playback-design.md)。维护者讨论 `play(obj, anim, effect, config)` 与 `play(anim(obj), effect, config)` 的取舍。

## 1. 候选层次与当前讨论状态

2026-11-05补充：[可组合算子／反应／相机IR](2026-11-05-hanim-composable-operators-reactions-ir-design.md)将effect扩展为匹配、路径、属性、条件／触发反应和呈现等阶段的typed算子；相机及其运动／镜头／视图、动画与effect参数也可作为目标。环境场只是刺激来源之一，亦覆盖事件／阈值／接触／邻域和材料规则。主动／被动是绑定方式，与pure／stateful及执行阶段分别建模。主入口语法仍待review，先保证共同契约、作用域和执行计划可表达。

维护者澄清对象中心与动画中心都是供比较的思路，不能直接定案。最新比较见[通用性／风格／GPU review](2026-11-04-hanim-generality-style-gpu-review-design.md)。**前稿“四参数为主、anim(obj)仅语法糖”的推荐不再作为定案。**

当前倾向：创作层可采用动画中心的`hengine.play(animation, config)`，`X(obj)`或明确角色的`X(a,b)`构造可组合动画描述；对象中心四参数入口作为适配／便利形式；编译与执行层采用同一Program／PreparedPlan。倾向仍待review，性能与通用性由下层的表示、匹配、求值、效果及批量执行契约保证，不由函数参数顺序保证。

动画配方、已绑定描述、准备计划与运行实例分开。构造`X(obj)`不修改对象、不启动时钟、不产生渲染或业务副作用；对象组、逐对象应用、A→B转换也各自有明确语义。C++ callable、原生builder和CLI／openas数据描述都可规范化为相同图，不要求所有语言模拟同一种语法。

~~~mermaid
flowchart LR
    Animation[动画中心创作表达] --> IR[绑定／时间／通道／效果IR]
    Object[对象入口与adapter] --> IR
    Graph[高级编排／Agent数据] --> IR
    IR --> Compile[匹配／能力／版本／冲突／资源及预算]
    Compile --> Plan[同一PreparedPlan]
    Plan --> CPU[CPU／SIMD批次]
    Plan --> GPU[可选GPU批次]
    CPU --> Output[共享呈现／离线输出]
    GPU --> Output
~~~

CPU／GPU完整编译仍是目标架构；当前实际严格对象Program和CPU路径见下一节。不为不同入口分别实现播放器、插值器、数学、时钟或renderer。`hengine`可作为示例变量名，当前C++类型为`hanim::Engine`。

## 2. 当前首个单对象API

以下与本轮首个C++播放器接口一致，不表示完整公共动画目录已完成：

```cpp
import hanim.engine;
using namespace hanim;

// triangle、circle是同一Engine中的有效ObjectRef。
auto handle = engine.play(
    triangle,
    anim::morph_to(circle),
    fx::blur(0, 2),
    {.duration = seconds(0.6), .easing = ease::in_out_cubic});

if (handle) {
    auto sought = engine.seek_progress(*handle, 0.5);
    auto paused = engine.pause(*handle);
}
```

不需要效果时可省略；传空链可显式使用 `Effects{}`。当前single EffectSpec与span便利重载对裸`{}`可能歧义，正式统一Effect表达式时应收敛成一个效果值入口，而不是继续增加含糊重载。

当前支持严格命名部件的轮廓／transform／实色过渡，以及有序效果参数。普通ObjectRef入口在play准备时从Engine注册表捕获端点；外部宿主可用两个公开Snapshot进入同一播放器，不要求导入UI Runtime，也不每帧回调宿主setter。当前单source保留一个binding，取消仍占用，显式release后才重新play；同对象替换用retarget。

播放器原生输出与可选renderer绑定分开。当前CPU矢量适配把同一当前几何、材质mix与效果交给唯一renderer；GPU、完整材料历史重定向、UI呈现／命中及其它对象域仍需继续实施。

### 2.1 当前首批可运行的 Program

`import hanim.engine;` 导出同一 `hanim.program`，可直接编译嵌套sequence／parallel／stagger；PreparedProgram不拥有时钟，Engine只有一条执行与发布路径。下例a／b／c是公开的owned Snapshot：

```cpp
auto description = sequence({hanim::bind(a, b), hanim::bind(a, c)});
auto plan = PreparedProgram::prepare(description);
if (plan) {
    auto instance = engine.play(*plan);
    if (instance) {
        auto selected = engine.seek(*instance, seconds(1.5));
        auto frame = engine.frame(*instance); // 此例只有一个逻辑对象轨道。
    }
}
```

第二段从前段真正边界开始，逻辑身份仍为a；显式`bind(b,c)`会创建b轨道，不隐式合并身份。多轨使用`frame(ref, track_index)`，单参数frame明确拒绝含糊结果。准备保留节点路径、版本、时间和分段计划；renderer可预先逐段绑定，用frame的segment_index选择，不在每帧匹配。

首批采用严格整对象通道，重叠写入报binding_conflict；未交付通道混合／组foreach／隔离效果作用域／repeat表达式。每段效果端点显式编写，不把几何连续误称材料／效果历史自动连续。单轨序列可从实际当前阶段retarget成新段；多轨替换到单目标明确报unsupported_operation。完整Program仍按下文扩展。

## 3. 完整目标的类型分工

| 类型／职责 | 应包含 | 不应混入 |
|---|---|---|
| HObject／Snapshot | 对象身份、部件、表示、能力与资源版本；显式快照固定端点 | UI Runtime、业务setter或编辑器面板状态 |
| Animation | 可复用运动／轨道／几何意图、自然时长、曲线及作用通道；尚未绑定或可复合 | 启动播放、网络请求、直接修改目标 |
| Effect／过程修饰表达式 | 匹配策略、路径、属性／响应、场、材料／呈现等typed算子及作用域；参数变化进入同一时间计划 | 第二套动画时钟、重复solver／renderer、无类型跨域连接或逐元素脚本 |
| Program | 对象与动画／效果的绑定、sequence／parallel／stagger、嵌套作用域、冲突规则 | 每帧脚本循环或隐式业务操作 |
| PreparedPlan | 已解析资源、对应关系、编排、能力结果、容量／scratch要求和确定性输入 | 任意时间偷偷重匹配／分配或读取实时随机状态 |
| PlaybackRef／控制 | 播放实例身份、位置／速率、暂停／seek／倒放／取消／重定向、状态与结果 | 把控制句柄本身当成可持久化工程资源 |

`bind(ObjectRef, ...)`在prepare时解析身份与版本；对象已删除／代次不匹配则失败。`bind(Snapshot, ...)`使用明确冻结的内容。准备后的运行保留实际输入版本，源对象变更不会暗中改写旧计划；需要显式retarget或重新准备。

## 4. 多对象与效果作用域

以下为**完整作用域目标API示意；首批Program组合已实现，with_effect等嵌套作用域尚未交付**：

```cpp
auto program = parallel(
    bind(title, anim::move_to(...)).with_effect(fx::blur(...)),
    sequence(
        bind(card, anim::morph_to(panel)),
        bind(label, anim::reveal(...))))
    .with_effect(fx::tone(...));

auto prepared = engine.prepare(program, prepare_config);
auto playback = engine.play(prepared, playback_config);
```

对象绑定处的效果只作用于该对象／域；组合节点效果作用于该组合的当前合成结果。不同位置的blur或tone不可互换。链是有序处理；材料mix是同一当前几何上的材料结果混合；风格还可能展开字体／布局、部件和降档规则。需要分清作用域，不能把任意effect作为无作用域的最后一个参数附到所有并行动画。

同一对象不同通道可按规则并行；同通道冲突必须有replace／add／multiply／blend等显式策略及优先级，不能由函数调用顺序碰巧决定。当前编译器支持严格整对象轨道及非重叠分段，冲突明确拒绝；完整通道混合图仍未交付。

## 5. config分准备与播放两部分

动画自身的自然时长、关键帧、曲线和通道属于Animation；全局播放速率、暂停意图、循环／方向、取消／冲突策略属于播放控制。便捷入口可以提供时间覆盖，但覆盖应是显式optional，不能用默认config覆盖库配方自己的时间。

准备配置负责域、严格／自适应／显式替代、预算、精度、种子、后端能力和资源要求；播放配置负责已准备计划的时间与生命周期。改变要求重新准备的配置必须明确触发cold工作；暂停／seek／rate不得偷偷编译shader、匹配几何或分配全场缓冲。

首个实现的PlayOptions暂提供duration／easing，用于整段对象过渡。完整公共Animation／Program落地时，统一时间归属与显式覆盖语义；此文不把这些未来字段塞进当前尚未支持的config或伪造已可调用API。

## 6. 返回、诊断、性能与双视角

play／prepare返回expected，错误定位对象／部件／动画阶段或效果参数。成功绑定采样计划与实际renderer准备／呈现的状态分别可观察；不能未知效果未绑定却宣称已在屏幕应用。选择无输出的纯采样模式与要求视觉输出的模式由实际管线能力区分。

默认便利play可承担准备工作；显式prepare便于预热、缓存和离线并行。采样写预分配缓冲，引用和资源代次校验使用共享memory机制。新段retarget固定当前物质为源；恢复源姿态绕过跳步曲线，retarget时间零的锚点在任意seek时保持一致，合法离散曲线随后仍可跳变。

CLI／openas表示使用同样的object、animation、effect和config契约，复杂场景传绑定Program；人用GUI／TUI编辑同一描述。所有工具均可不开GUI执行和控制；视觉审查经相同renderer／输出层获取指定版本／时间／区域的像素及元数据。业务撤销、视觉seek和模拟恢复各自定义，不把构造Animation表达式等同真实动作执行。

本次推荐先确认两层入口及统一计划语义；后续Program通道／效果作用域、完整40项库、语言绑定及UI迁移按完整方案推进，不能用单对象播放器替代这些交付。

## 7. 四个组合维度与交叉播放的完整架构

维护者进一步明确：**对象×动画×效果×配置的每项本身都可组合；对象A到B、复杂编排与交叉播放必须从底层支持。** 因此完整编译入口是Program，单对象四参数调用是规范化便利形式。当前编译器的严格整对象域是首阶段，不能将它作为最终通道冲突模型。

### 7.1 四类表达式与作用域

| 维度 | 组合结构 | 必须保留的语义 |
|---|---|---|
| ObjectExpr | 单对象、命名部件／域选择、group、实例、层级与快照 | 稳定身份、父子空间和绘制顺序；同一资源可被多个实例复用，但实例状态独立；UI／模拟／渲染树各自类型不被强行统一 |
| AnimationExpr | 原语、sequence、parallel、stagger、delay、repeat、reverse、嵌套及时间映射 | 自然时长、曲线、读写通道、前后依赖、循环与结束语义；复用core编排和数学，不在每个组合节点加一套时钟 |
| EffectExpr | 匹配／路径／属性／场响应算子，有序chain、material mix及对象／子组／视图／组合结果作用域 | 类型、单位、空间、pure／stateful、顺序及代价；准备阶段与tick／呈现阶段分开，局部与组合效果保留区别 |
| ConfigScope | 引擎／项目默认、程序、子组合、对象／部件及具体阶段的typed配置 | 按字段和明确规则继承／覆盖；时间、匹配、空间、冲突、能力、质量、资源预算、观测分别定义，不用无类型字典盲合并 |

四个组合树通过稳定引用与绑定形成Program，编译时展开实际作用域。对象组合是内容结构，动画组合是时间／通道结构，效果组合是处理结构，配置组合是策略结构；它们不必同形或同粒度。

### 7.2 A到B与多对象目标

`morph_to(target)`描述显式目标；完整形式可指定源／目标快照、部件映射、对象域和能力策略。源／目标都可以是组或不同层级的对象，匹配先使用稳定部件身份，再使用明确选择的几何／字形／像素对应算法。增删、重排、重父级、孔洞和分合有各自策略及能力结果，严格模式不能静默退成淡化。

绑定实时对象引用时，prepare固定源／目标版本；绑定Snapshot时使用指定内容。打断后必须从当前实际合成物质建立新源，不能从旧业务A或某个单独clip的未混合结果重新开始。新计划深拥有所需数据并释放不再需要的历史，外部仍持有的租约单独计量。

### 7.3 交叉播放与通道仲裁

时间区间可重叠，同对象也可有多个活动clip。编译／绑定时展开类型化读写集，例如position、scale、rotation、opacity、contour、material参数或某个命名部件。不同通道并行；相同通道依策略replace／add／multiply／weighted blend，带明确优先级与确定性次序。含糊冲突应报告，不由调用顺序或线程调度碰巧决定。

几何处理链中的“形变后再扰动”与两个clip同时覆盖同一轮廓不是同一种合成；组transform、局部运动与部件运动也有明确矩阵次序和空间。UI布局基准、父级过渡、子组件hover／press应可分层组合，不能吞掉彼此；物理权威状态与视觉偏移分别拥有通道。

replace／retarget可只影响选定通道。C0要求保持当前姿态／材料连续；要求C1时还需传递已定义的速度／导数。解析弹簧可复用core速度，模拟重放使用相应状态／检查点；不能给任意离散效果承诺无条件速度连续。取消、恢复、持有、排队及完成后的fill策略必须明确，同时说明业务模型是否改变。

### 7.4 配置继承与时间

推荐基本解析顺序是引擎默认→程序→嵌套节点→具体绑定／阶段，子级仅覆盖显式字段。资源上限和权限约束取有效限制，不能由内部配方扩大；reduced/off及设备能力策略按公开规则影响实际选用计划，并报告替代结果。

父级时间映射驱动子级采样，子级可有自己的offset／rate／repeat，但不会偷偷读取第二个墙钟。组暂停、局部seek和重定向的影响范围由句柄／选择器指定。无限循环可以是有界描述的repeat，不允许无限递归表达式；sequence中无限子段之后的节点须有显式截断／结束条件。反应图／模拟反馈需声明求解阶段，不能当作没有规则的普通DAG环。

### 7.5 统一编译与批量执行

完整路径：表达式规范化→解析对象／版本与选择器→匹配和能力校验→时间／通道读写与冲突分析→效果／材料阶段和资源依赖→预算／档位计划→准备缓冲和插件批次→PreparedPlan。

PreparedPlan以可批量执行的数据和阶段表示，不逐帧递归解释builder、不逐像素虚调用、不执行脚本。CPU参考与可选GPU阶段消费相同契约和版本，接同一渲染图／资源池。prepare、重新匹配、shader编译与资源准备为显式cold工作；step只采样并提交预分配输出。

播放控制作用于一个Program实例，实例可包含多对象、多clip和多个效果作用域；查询还可选择子组、对象和通道。完整结构／效果程序替换不能只换动画时间而保留不匹配的旧renderer绑定。输出帧发布、资源切换与采样失败原子性单列，不能把CPU整批提交说成整个GPU／音频／业务世界同时可回滚。

### 7.6 简单与复杂入口的目标关系

以下为尚待实现的组合API示意：

```cpp
// 现有对象入口：规范化为一个绑定节点。
engine.play(object, animation, effects, config);

// 同一对象组内含子对象；动画、效果与配置也各自嵌套。
auto program = sequence(
    bind(objects::group({card, label}),
         anim::parallel({anim::move_to(...), anim::fade_to(...)}))
        .with_effects(fx::chain({...}))
        .with_config(group_config),
    bind(card, anim::morph_to(target_group))
        .with_config(transition_config));

auto plan = engine.prepare(program, prepare_config);
auto instance = engine.play(plan, playback_config);
```

`play(animation(object),...)`若未来提供，仅是bind／Program构造的语法糖；效果应绑定在明确节点，统一prepare和play。C++、数据文件、CLI／openas和GUI编辑器描述同一Program契约，不各自增加一套不兼容的组合语义。

### 7.7 必需的组合验收

至少覆盖：多层对象组与动画／效果／配置交叉嵌套；同对象不同通道并行与同通道混合；A／组A到B／组B及明确增删匹配；中途重定向与任意seek；组与局部暂停／取消；父布局与默认子动效；材料和效果作用域；配置限制与显式降档；多个实例资源复用但状态独立；headless与交互使用同计划；CLI／Agent查询和离屏区域审查。

每项包含端点、中间态、失败语义、生命周期及成本证据。当前严格对象Program的通过不替代全部域与作用域组合场景，也不关闭40项库、20项创意组合或完整Manim／Noita范围。

### 7.8 对象组不是隐式广播，通道也不是任意可相加

组本身的transform／opacity，逐个子对象执行同一配方，以及对子对象按序stagger，是三种操作。API应显式区分“作用于组”“for_each选择器”和“按部件绑定”，不能见到group就猜测递归广播。源组和目标组通过稳定映射对应；一对多／多对一、不同绘制域及拓扑变化要求对应插件明确支持，否则返回带部件路径的错误。复用同一Animation值不共享可变播放游标。

通道描述符声明值类型、单位、坐标空间、读写范围、支持的合成算子和对应算法。位置可加、缩放可按定义相乘，但轮廓／旋转／材料／离散资源不能全部套标量加法；旋转采用明确的组合次序，几何blend要求共同对应域，材料mix走渲染材料契约。请求不支持的blend应在准备时失败。Effect链按数据依赖处理，只有隔离／滤波需要时分配中间层，不为每个组合节点强制生成离屏纹理。

### 7.9 分清计划准备与阶段起点取值

`sequence(A→B, B→C)`的第二段不能误读整段prepare时的原始A。完整Program需要三类显式源：固定Snapshot、前序计划在阶段边界的输出、交互打断时的当前合成输出。静态编排在prepare时建立依赖和对应计划，任意seek按依赖求边界状态，不依赖此前逐帧播放过。默认静态sequence采用前序边界输出；固定快照需显式声明。

交互式current源是一次有版本的捕获／绑定事务：在命令生效的逻辑tick取当前合成结果、准备或选用已准备的替代计划，再原子发布新revision。需要重匹配或分配时须报告cold工作／未就绪状态，不能声称所有动态retarget都零成本；旧计划在准备失败时继续有效。prepare与play间对象变化也依选择的冻结／重新验证策略处理，不悄悄混用版本。

### 7.10 控制、事件与确定性边界

顶层PlaybackRef控制一个实例；子段／组／通道通过稳定路径和选择器定位。暂停父段影响其时间域，局部暂停的持有与恢复相位必须显式定义。运行时增删段、换效果或改变匹配策略形成新计划revision；普通seek／rate只改变执行状态。相同逻辑tick内命令采用可记录的确定次序，外部多线程先进入命令批次，不用数据竞争决定结果。

求值本身不触发业务副作用。越过标记、seek、倒放及循环时的事件投递另有策略和幂等标识；同一时刻重复采样不能重复提交动作。模拟／音频有各自状态和时钟，Program通过明确同步／检查点契约接入；不能把随机物理推进伪装成任意seek的纯插值函数。

### 7.11 实施顺序与可审查交付

1. **统一描述与编译契约**：ObjectExpr／AnimationExpr／EffectExpr／ConfigScope、稳定节点路径、typed通道、源模式及错误路径；数据与C++ builder同语义，不先堆大量play重载。
2. **最小Program编译器**：bind／sequence／parallel／stagger与时间映射；复用core和现有morph／effects，准备阶段解析对应、版本、作用域、读写集和容量。通过静态A→B→C乱序seek与组／子对象用例。
3. **通道组合与交互替换**：先实现有数学定义的算子，验证优先级、空间、组合后的current retarget、失败保留、取消／fill及资源释放，再扩展复杂几何／材料域。
4. **普通消费者迁移**：UI的状态与命中适配、组件默认配方、40项公开库、20项真实创意任务消费同一播放器；按当前对象／后端能力声明边界。
5. **性能与双视角验收**：嵌套图预热后批量求值零新增分配、预算／trace／profiling、CLI计划查询与无GUI区域输出；CPU／Web参考结果及能力降档一致。GPU／骨骼／模拟插件随后按同契约加入，不另写播放器。

该次序保留全部目标，但不把第一条严格形变链路误报为完整编排内核已经交付。
