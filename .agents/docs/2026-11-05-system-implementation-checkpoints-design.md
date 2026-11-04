# 综合架构实施：公共能力、官网映射与检查点

2026-11-05。执行基线 `7b22704`；承接[顶层×子系统方案](2026-11-05-system-subsystem-architecture-and-api-design.md)、[feature／AI／style](2026-11-05-features-ai-agent-scoped-style-design.md)和[PPet](2026-11-05-pixelpet-generative-capabilities-design.md)。维护者已要求开始拆分、实现、测试、更新方案并提交PR #1。本文是执行台账，不另立缩小范围的总目标；P0–P6、F0–F4、K0–K5、R0–R6、C2–C6与公共40／20要求继续有效。

## 1. 执行规则与全局验收轴

每个检查点沿“公开契约／独立消费者→真实输出→UI／官网普通消费者→本地相关测试→证据／文档→签署commit”推进。官网优先承接已验证能力；不能在site目录另写算法，再倒称框架已支持。能力规模以实际可带走的公共成果统计。

| 评审轴 | 实施不变量 | 验收证据 |
|---|---|---|
| 简洁／优雅 | 简单调用少参数；复杂语义显式展开；同一编译／执行体系 | 普通消费者源码；便利调用与显式准备输出等价 |
| 规范／兼容 | 保留技术身份、既有有效调用和失败语义；路径迁移有说明 | 旧API／新API组合、C ABI／schema生成检查、无重复包身份 |
| 性能 | 准备／热路径／发布／GPU／观测分开；无隐式大复制 | 现有零分配回归、bench预算；新性能声明先有工作负载 |
| 稳定性 | 版本、租约、打断、取消、失败原子性明确 | stale／foreign、重入、容量、乱序、失败保留原状态 |
| 能力与裁剪 | build feature、运行开关、实际后端能力、权限分别表达 | 正反配置闭包、缺失能力诊断、实际构件与资源成本 |
| 跨平台 | 标准契约、平台adapter；一个平台先贯通再扩 | 原生／Web分开报告；不以编译或模拟GPU替代实际后端输出 |
| 自适应 | 平台／能力／设备／DPI／视口／语言／motion偏好共同参与选择 | 桌面／窄屏、中文／英文、reduced/off、能力不足／设备恢复 |
| AI／Agent原生 | 人／Agent双第一优先级；openas原生语义先行、同一领域实现、内置最小skill／SOP；无GUI完整任务，批量控制不进入热循环 | 同一任务的人类交互／语义操作／headless真实输出；权限交集、委派及隐私边界；指南可执行 |
| 单一归属 | 算法、资源、渲染、调度各有owner，领域状态不强行统一 | 包DAG、实际依赖图、移除重复实现及独立消费者 |

## 2. 检查点拆分与依赖

下表是可合入的实施批次；大型项允许连续检查点，不能用一次局部通过勾掉整行。S2/S4/S5尽早交错检验不同领域，不等待hanim全部完成。每轮保持最多2名subagent的上限；当前轮单线程集成。

| 检查点／原计划 | 实现内容与依赖 | 官网／产品映射 | 退出条件 |
|---|---|---|---|
| **S1 / P1：公共入口与authoring** | 唯一hanim伞包迁gfx；动画中心叶子／组合直接进入既有Program；live/frozen输入、错误、旧入口兼容 | 动画文档的真实公共API；后续动画页开场使用同一能力 | 独立dev/release、纯core兼容、无UI/renderer闭包、真实CPU像素、旧回归 |
| **S2 / P1＋P2-A：通道／scope与UI接入** | typed属性通道、一个通用path modifier、显式effect作用阶段；UI adapter发布姿态／命中几何 | 基础动画页开场、按钮／开关／浮层／列表的默认配方 | 4类组件人／Agent触发、打断C0、焦点／点击／中间态、reduced/off；按证据迁移旧motion实现 |
| **S3 / F0＋F3＋R：自适应与局部style** | 复用能力查询和feature选择；theme/effect/motion分工；pixel identity、高DPI、scope/portal及文字失效修复 | 风格页同对象对照，组件舞台按空间调整；局部pixel作品 | 可解释选择／失败；DPI与窄屏、中英、文字可读／命中；未选资源不进闭包 |
| **S4 / P2-B：反应／计算／渲染** | 两材料、状态属性、阈值／邻域、chunk/halo、检查点；一个真实WebGPU算子 | 创意作品中可操作材料画布／反应实验 | CPU参考、边界／顺序／重建、设备资源／数值等级；Noita扩负载单列吞吐与内存预算 |
| **S5 / P2-C：音频／时间映射** | 现有audio/timeline的自动化和视觉同步；math与CLI依赖收敛 | 可交互混音／节奏作品 | CLI同源WAV与画面；30/60/120fps不改音频；seek不重复副作用 |
| **S6 / P3＋F1：公共控制与观测** | 从实际消费者提取中立control/resource/time；hanim和UI通过同一openas adapter | Agent页开发／使用／设计真实任务与可审查过程 | 非UI引擎独立控制、授权／revision／取消、真区域像素、trace；DAG防反向依赖 |
| **S7 / F2＋K0：混合语言／AI全双工底座** | C++核心＋项目TS／所选语言；player编译移worker、插件prepare/migrate/swap；主会话／worker隔离；llmapi复用；Web脚本bridge | 统一接口×多语言、Agent作品，PPet入口 | 饱和仍可输入／取消、旧回调失效、热换失败保留；按选用语言裁剪闭包；模型与Web分别实测；native动态卸载另列可选能力 |
| **S8 / P4＋R：公共库与官网质量** | ≥40公共基础动画、≥20可用创意工程；完整组件默认配方；官网渐进披露与按页交付 | 简洁首页、动态logo、核心特色／Agent／创意作品导航 | 每项公共定义／参数／独立工程／真实交互／视觉／预算；不以主题或速度变体凑数 |
| **S9 / K1–K5＋P5：应用与工具纵切** | PPet新服装／实时UI／技能／绘画；Inspector、动画／UI工具优先，之后素材／音频／关卡／玩法 | 可使用的作品和工具展示 | CLI完整、GUI可选、重启重用、可编辑源、技能版本／权限；3D/rig分别验收 |
| **S10 / P5–P6：多后端与完整质量** | Web/Toy重点、原生／移动／TUI适配；扩3D、模拟与制作链；独立产品交付 | 真实支持矩阵、跨设备布局／能力降档 | 共用CTS＋后端CTS、成熟场景矩阵、性能／稳定性／无障碍及C2–C6逐项证据 |

没有把“所有引擎、算子和工具”串成先全建完再展示的长链。S1后可开始S2，S4/S5用最小真实任务检验共享契约；S3能先修现有style问题。S6提取需至少两个具体消费者证明职责，不为目录完整预建万能Runtime。S7可先无模型fixture，随后接真实内置或外部Agent。

## 3. S1设计修正：一个Program，不复制实验树

之前的接口实验包含消费端Description AST和HEngine façade。正式实现直接扩展既有 `hanim.program`：`anim::Morph`是有值语义的叶子，转为同一个Program节点；sequence/parallel/stagger继续使用原编译器，删除消费者里的第二棵描述树和递归lowering。

`ObjectInput`明确区分ObjectRef与Snapshot；ProgramInputs保存尚未捕获的端点，原ProgramBinding继续承载既有冻结端点、时间、效果和阶段源。prepare先捕获所有引用，再开始匹配；独立编译器可显式取得registry，Engine入口使用自身registry并拒绝重入修改。未提供registry的live引用返回unresolved-object，foreign/stale保留节点与ObjectError。

本批支持 `anim::morph(a,b)`、duration/easing/leaf effect/source选项与嵌套编排；没有以相同名称假装完成任意属性、path modifier、组图像effect或通道混合。`Engine::play(program, ProgramLimits)`的第二参数是**准备预算**；高级播放配置仍属后续契约，不能把它当成完整PlaybackOptions。

伞包迁移保持 `import hanim` 及core符号身份；core-only继续使用原路径。旧本地path依赖需要从base/hanim改为gfx/hanim。mcpp 2026.11.2.2的解析图对原有无namespace的hanim使用默认规范名 `mcpplibs.hanim`；依赖键／模块仍是hanim。审计同时核对实际包源路径，不按显示短名误判缺失，也不借此更改已有技术身份。

## 4. S1执行证据与剩余范围

**S1本地验收通过，作为本轮检查点提交；P1整体及后续阶段仍未完成。** [机器证据](../../tools/ci/hanim-api-probe/2026-11-05-public.report.omui.json)包含源码／fixture哈希、工具链、profile、依赖图、执行命令、断言与输出来源。报告记录实现所基于的提交及未提交源码哈希，不能仅凭base_commit把本轮结果归于旧代码。

| 验证 | 实际结果与边界 |
|---|---|
| 公共入口独立消费者 | Linux GCC16.1，dev/release各84项断言；真实cubic/transform/effect值、live/frozen/mixed输入、旧Program组合、源重置、seek、错误／容量／生命周期。11个库＋消费者，无UI/spec/renderer/RHI |
| 相关包回归 | dev/release各40个用例：兼容3、Program15、Engine20、CPU视觉2；包括既有sample零分配检查、新增standalone capture和准备期间重入拒绝 |
| Wasm实际执行 | emsdk6.0.9、wasm32-emscripten，公共入口3个用例通过；包含新authoring→play→seek的实际结果，不是仅编译。不代表浏览器GPU或完整官网验收 |
| 独立CPU像素消费者 | dev/release各5组输出：起点、中间态、终点、区域裁切、当前姿态retarget；真实PNG／provenance与非法区域不输出检查通过；渲染显式选包 |
| 工程约束 | lint 0问题；lint自测20/20；文档链接／差异格式检查；源锁文件SHA256保持不变 |

本地命令：`mcpp test --locked -p hanim.program -p hanim.engine -p hanim --profile dev`及release；`mcpp test --locked -p hanim --target wasm32-emscripten`；`tools/ci/hanim-api-probe/run.py`与`tools/ci/hanim-play-consumer/run.py`。本轮使用mcpp2026.11.2.2。公共authoring独立消费者已加入CI，现有像素消费者改用公共入口；不等待异平台CI继续推进。

自审修正：①直接扩展Program并删除实验AST；②新增registry重载采用受约束类型推导，保留旧 `prepare(program,{})` 的无歧义含义；③错误保留node／ObjectError，Engine拒绝准备期间重入；④伞包与渲染adapter同表依赖必须引用TOML点分键，实际外部构建发现并修复；⑤mcpp默认规范名按实际源路径核实。

仍需跟进：Wasm测试有依赖ABI声明的构建工具warning，现有库清单没有因此增加线程／异常需求；后续审计mcpp继承／诊断来源。本轮没有GPU性能、完整高级scope、通道混合、浏览器视觉或全平台完成结论。

S1之后的优先项为S2：先落typed通道与过程modifier，再接组件与官网。现有 `openmui.motion` 仍主要消费hanim.core；本批不声称已迁移默认动效。官网本批只更新其开发者文档中的公共调用与能力边界，完整视觉开场在S2验收。

## 5. 资源、验证与提交

相关本地真实测试通过即可进入下一项并提交同一PR #1；异平台CI异步反馈。公开消费者、契约与自动检查加入CI，但不等待其他平台结果才开始下一项。GPU、Web、Agent和无障碍的未测项分别保留，不能用本地CPU回归推导其通过。

允许清理本项目旧构建缓存；清理前检查live进程和目标目录，只删除已确认可再生的历史构建树，保留当前增量及报告／像素证据。用户已有mcpp.lock修改保持字节不变、不会纳入本轮提交。当前约8GB可用，首批独立消费者和复用增量足够，暂不删除缓存。

## 6. S2首批：通用点路径与类型化过程效果

在S1之后先完成“通用过程算子→公共authoring→真实渲染→UI路径adapter”的小纵切。**S2仍在实施；首批未包含typed通道IR、组scope、四类组件完整接入或官网交互开场，后续官网接入见§7。** 首批不增加公共基础动画数量，也不把官网场景当作公共库验收。

```cpp
auto animation = hanim::anim::morph(source, target)
    .duration(600ms)
    .with_effect(hanim::fx::vertex_path(hanim::PointPath::bend(0.25)))
    .with_effect(hanim::fx::translation_path(hanim::PointPath::bend(-0.2)))
    .with_effect(hanim::fx::blur(0, 8));
auto playback = hengine.play(animation); // 与prepare后play使用同一编译／播放器
```

### 6.1 单一实现与明确阶段

- `hanim.core:path`拥有`PointPath`：两个三次控制点的偏移量，x沿端点弦、y沿左法向`(-dy,dx)`；无需归一化／三角函数。`bend(f)`是方便构造，中点偏移为弦长的f倍；自定义两对控制量可产生不对称路径。它也可直接用于任意Vec2属性，不依赖HObject／UI／renderer。
- `PreparedShapeMorph`在对应关系确定后对各顶点／控制柄应用该算子；不改变匹配，不重新采样端点为多边形，保持闭合连接。零控制量继续原直接循环；形变准备／热采样仍分开。
- `fx::VertexPath`与`fx::TranslationPath`是类型化过程目标。前者选择绑定morph的几何点，后者选择各part的局部tx/ty；不是world-space路径，也不是默认只移动root。父子transform按原规则组合。本批没有任意部件选择器。
- 过程效果降入同一个MorphOptions／Program；相同目标反复添加采用**同一端点坐标系内位移相加**。它不是有顺序的任意变形函数复合。图像blur/tone继续自己的顺序参数链。组scope和通道冲突分析没有被平铺参数替代。
- `openmui.motion::Options::vertex_path`直接复用算子，兼容路径、准备后的闭合路径、fill／stroke共用；不让组件编写动画。首批仅交付C++ adapter；后续MotionSpec配置和官网接入见§7。

### 6.2 边界、稳定性和自审

控制量在prepare验证为有限且±1e6内；core几何保留既有数值域。对象translation可能更大，采样先检查所有修改后的transform，再一次性写frame，失败不污染任何输出。UI将路径写入float几何，提前验证整个[0,1]区间的保守界；不支持时记录PathMorphStatus和既有fallback，不能报告成成功连续形变。

端点和静止点不受过程位移影响，任意顺序seek结果不变。retarget捕获真实当前几何／位置，并以相同prepare重新匹配，保证C0而不声称C1／速度继承。路径不是等弧长参数化、不保证不自交，不覆盖像素物质迁移／反应／GPU计算。

自审调整：typed效果重载采用约束模板，保留原`with_effect({})`的EffectSpec解释；默认形变保持原轻路径；公共库和UI都检查数值范围；PPet命名维持**应用pixelpet，简称PPet**，这些算子属于公共hanim，未来由其项目TS绑定消费，未提前建立宠物专用动画实现。

### 6.3 本地验证记录

[本轮机器证据](../../tools/ci/hanim-api-probe/2026-11-05-path.report.omui.json)记录基线commit、实际编译源码哈希、用例、工具链、锁文件与真像素结果。S2首批本地通过；检查点之外的能力不因这次通过而变成完成。

| 验证 | 实际结果 |
|---|---|
| 独立公共API | Linux GCC16.1、mcpp2026.11.2.2；dev/release各94项断言，包含新的类型目标、实际几何／位置、端点及直接play与prepare后play等价；11个库＋消费者，没有UI／renderer依赖 |
| 相关包回归 | dev/release各178个用例；core、library、Program、Engine、公共入口与UI motion。保留原形变fingerprint；新增过程采样／几何记录零分配、非法控制、溢出失败原子性、UI真像素与C0打断检查 |
| Web实际执行 | emsdk6.0.9，node实际运行Wasm，`wasm-test` profile，共68例；包含公共入口的translation path与blur参数。未覆盖浏览器／GPU／完整官网 |
| 独立CPU真像素 | dev/release各9组PNG：普通与过程效果起点／中间态／终点、普通crop，以及两种retarget；blur/tone同时执行。两组端点像素相等、中间像素不同；320×240 fixture重定向最大差异普通3/255、弯曲4/255，均只在既有边缘容差内，边缘以外无差异 |
| 工程约束 | lint 0；现有mcpp.lock字节不变。两个独立消费者沿既有CI gate执行，不等待异平台CI |

最初用默认Web profile运行全core时，数学参考数据未挂载、读取0行；改用仓库声明的`--profile wasm-test`后68例全部通过。该运行配置问题和有效执行记录分开保留，没有删测试绕过失败。

首批结束时，MotionSpec／脚本prop和官网舞台尚未使用新的路径参数；后续进展见§7。typed过程目标不等于通用属性通道IR／混合，也不等于完整scope或GPU资源编排。

## 7. S2第二批：公共动效配置到真实官网舞台

承接 `fd606af`，本批将同一个点路径算子贯穿 **MotionSpec → Runtime → 官网共享播放器**，没有在官网新增插值器／时钟。S2仍未整体验收；这里不把官网场景数量转换为公共动画库数量。

### 7.1 使用入口与职责

```json
{"use":"morph","curve":"motion.slow","vertex_path":{"bend":0.22}}
```

此配置可以用于普通节点的`motion`属性或theme动效库；C++直接设置`MotionSpec::vertex_path`使用同一字段。更细的输入为`{"control1":[along,normal],"control2":[along,normal]}`，以每个点的端点弦为坐标系。`null`／`"linear"`清除继承的路径修饰。解析拒绝非数值、非有限值、错误维数、未知嵌套键和超过控制量范围的输入，不把非法数据悄悄解释为零。

配置作用于匹配后的填充／描边路径，包括不同拓扑准备后的轮廓；不改变匹配、时间曲线、文字或图像几何。命中仍使用节点明确的边框策略和现有呈现变换，本批不声称具有逐顶点或像素级命中。新增reduced过渡保留短时默认空间插值，off／系统reduce直接呈现业务端点；关闭动效能释放已经暂停持有的形变。full→reduced过程中重新规划已有形变不在本批验收范围。

### 7.2 官网设计与真实交互

- 基础动画开场改为三角形→圆，端点由painter描述；组件／场景内没有逐帧动画逻辑。默认画布高300px，形状绘制区为240px，按可调弯曲范围为上下轨迹留出空间；手机和桌面复用相同场景。
- 继续一个舞台、关闭的有界目录、详情按需挂载。指针／键盘焦点进入后释放预览并交给真实控件；公共播放器负责暂停、seek、倒放和重新播放。
- 详情提供路径弯曲、时间曲线、播放速度、stagger和场景局部风格。plain／glass／neon／retro／ink／liquid复用既有风格包，不新增场景专用材质；retro不冒充完整pixel风格交付。
- 显示及复制的JSON跟随实际路径参数。风格使用独立theme scope，界面明确说明其不包含在复制的motion配置内；复制结果也不是完整多语言工程。中英文开发文档补充普通builder的使用方式。
- 本批官网目录为41个基础场景、20个创意组合。40／20公共可复用库与作品质量的总要求继续开放；新增场景以可调试、可复制的动效配置工具接受验证。

### 7.3 自审发现与验证边界

1. 实际Runtime回归发现：关闭动效时，若业务端点未变，绘制hash快路径会保留一个已暂停持有的旧形变。清理阶段现在独立执行off／系统reduce策略，保留工具正常暂停、seek的语义。
2. GCC16在导入端合成带跨模块PointPath成员的默认比较时崩溃。保留逐成员默认比较的语义，将MotionSpec比较函数定义移回其实现模块；不手写第二份字段列表。
3. 真像素测试区分起点／中间态／终点，任意seek与当前姿态打断；显式检查节点身份、矩形命中和焦点。官网补充真实鼠标、键盘、Agent action、复制参数与局部风格验证，不以目录数量代替效果验证。

4. 滑块存在正常浮点量化，复制配置使用数值容差验证；不要求经过组件量化的值与字面量逐位相等。视觉复查选择时间25%的真实中间态，并将按钮保持为“切换形状”，避免文案变化分散注意力。
5. 中英文开发文档新增使用说明，参考回放从进入文档的第20帧起变化；前20帧保持原像素hash。检查画面后更新39帧基准，并重新运行完整参考回放。

[本批机器证据](../../tools/ci/hanim-api-probe/2026-11-05-motion-policy.report.omui.json)记录源码hash、原始失败及修正后的有效结果、包与图片hash。验证未覆盖的维度不会由通过项推导。

| 验证 | 本地结果 |
|---|---|
| view运行时 | dev／release各11个程序、117例通过；包含非法参数、真实中间像素、精确端点、任意seek、当前姿态打断、节点身份／矩形命中／焦点、reduced及运行中off |
| 官网 | 19个程序、126例覆盖。release整套通过；dev首次两项失败修正并重验后全部有效结果通过。最终固定按钮文案后，两配置再运行4个相关程序、24例，全部通过 |
| 组件默认动效 | release的6例通过：37类默认配方引用、六类真实控件中间态、形状相关组合、off／reduced、文本输入打断和局部风格优先级；不等于全部组件完成新版hanim迁移 |
| 实际Web发布包 | site-web正式pack，native重新生成56张开场及8张全图参考，预渲染与Agent指南更新；最终Chromium动效130项、展示129项、加载／首帧交接88项，共347项通过，动效／展示无运行时异常 |
| 工程与审查材料 | lint、文档链接、格式及README媒体检查；真实中间态图片见[官网README](../../site/README.md)。现有mcpp.lock字节保持不变，不纳入提交 |

异平台CI继续异步反馈。完整通道IR／混合、组效果scope、GPU资源编排、统一呈现几何及更广组件迁移仍是S2后续范围；本轮没有新增GPU性能或全平台完成结论。

## 8. S2第三批：显式组合图像scope与混合语言应用补充

执行基线 `e47b624`。**本批交付嵌套图像scope的公共闭环，S2整体仍未完成。** 编译层级／时间视图是完整IR演进的一部分；通道读写、状态跨tick边与GPU资源／批次编排继续开放。

### 8.1 从公共表达式到同一个播放器

```cpp
auto animation = hanim::composite(hanim::parallel(
    hanim::anim::morph(card, panel).with_effect(hanim::fx::vertex_path(hanim::PointPath::bend(.2))),
    hanim::anim::morph(icon, final_icon)))
    .with_effect(hanim::fx::blur(0, 4));
auto prepared = hengine.prepare(animation);
// 检查expected后：hengine.play(*prepared)；简单调用仍可play(animation)。
```

- `composite(...)`明确作用于合成后的子树输出；叶子image effect先于外层scope。sequence／parallel／stagger本身不隐式广播效果或创建离屏层。顶点／位移路径继续是typed叶子过程效果。
- Program v2保留作者preorder节点、父节点、子树边界、绝对起点／时长和最近scope；每段保留node／scope；scope保留父级／深度、时长、效果缓动、顺序与参数范围。没有第二套AST、播放器或渲染器。
- `.effect_easing(...)`只改变scope参数，子动画使用自己的时间／缓动；scope时长来自子树。before-start、hold、零时长、restore与现有时间语义对齐。
- `program_frame(ref)`借用同一次发布的所有track帧、scope参数、时刻和prepared identity；seek／倒放／暂停／取消共用既有时钟。几何或参数失败时全批保留原发布帧。scope参数计入准备／播放预算，scope数量有独立上限。

### 8.2 可选共享renderer与真实像素

`hanim.render.vector::PreparedVectorProgram`作为普通消费者绑定整个准备计划。它复用既有几何校验／写入、effect参数映射、DisplayList、CPU renderer与raster；单对象`PreparedVector`和程序入口使用同一实现。没有私有纹理池或另一个像素循环。程序入口首批使用当前solid fill；显式gradient material仍经单对象接口，不能宣称程序material编排全部完成。

每帧先验证并测量叶子，按内→外计算scope范围（含blur等声明的output reach），再按作者顺序记录嵌套层。活动segment决定scope归属，包含held source／endpoint；同一track不会因scope出现多份。普通单段tracks保持直接顺序，跨段归属变化才需要排序；空效果链不增加层，无轮廓的子树不创建空图像层。预备后端参数／层数有总预算，包含未显式提供而由backend补齐的默认参数。

真实CPU测试把两个半透明重叠对象作为一组模糊，与手工使用共享DisplayList离屏层得到相同像素；分别对两个叶子模糊得到不同像素。组外对象保持锐利，halo不被截断。另覆盖嵌套顺序、空scope恒等、跨阶段排序和任意seek、容量、错误参数、stale effect generation及失败不追加半帧。预热后seek＋Program查询＋record在trace开启时512次零分配；CPU实际滤镜执行的分配不属于该结论。

### 8.3 本地验收与可复核边界

[机器证据](../../tools/ci/hanim-api-probe/2026-11-05-scopes.report.omui.json)记录源码hash、实际命令、工具链、断言、像素hash及初次失败和修正；基线commit不是新实现的唯一身份。

| 验证 | 结果 |
|---|---|
| Linux GCC16.1 dev／release | 各6个程序、63例通过；最终补充scope stale-generation检查后，相关7例在两配置复验通过 |
| Emscripten wasm-test实际执行 | 6个程序、62例通过；默认无threads故不运行1例并发采样测试；最终7例scope消费者复验通过。不是浏览器GPU验证 |
| 独立公共入口 | 临时mcpp工程只依赖`hanim`，dev／release各100项断言通过；实际依赖图无UI、spec、renderer或RHI |
| 像素与分配 | 三配置组合输出hash一致；相同CPU层的独立oracle、组外对象和不同leaf结果均通过；新增scope基准及零分配预算。100个双track实例的本机release采样／发布p95约0.030ms，加入各2层scope／5参数和Program查询后约0.036ms，两项最大分配均为0；不含record、滤镜执行或整帧成本 |
| 工程 | lint、diff格式和文档链接检查；现有`mcpp.lock`字节保持不变、不纳入提交；不等待异平台CI |

### 8.4 自我review、PPet补充与下一步

1. 作用域不能简化为给每条track复制effect。此次保留层级并验证重叠alpha的真实像素差异；GPU计划将消费同一语义，但还没有GPU资源调度交付。
2. 时间段选择与对象identity分开。作用域变化可改变叠放次序，这是显式行为；没有隐式crossfade。单对象旧入口不改变调用方自定绘制顺序。
3. 旧单track `retarget`会规整成一段；本批对带scope程序明确返回`unsupported_operation`，禁止丢失层级／效果。保持scope的中断重编排后续单列。
4. 全scope／track更新先staging再发布；计数预算包含scope。record错误只改scratch，不改目标列表。仍要求单owner或外部同步、effect注册表在静默边界更新；借用视图不成为永久资源句柄。
5. [PPet §8.1](2026-11-05-pixelpet-generative-capabilities-design.md)按维护者补充改为C++核心＋项目TS及所选语言混合。语言与reload能力分别声明，数据／脚本热替换、静态核心升级和可选native动态插件分别验收；跨语言共用生成契约、稳定handle、资源owner、状态schema和generation。主交互／worker全双工目标不变，此项是设计补充，尚未交付运行时。
6. 下一步需把通道／scope通过公共UI adapter映射到实际交互，不在官网复制编排实现；同时按台账穿插S4反应／分块与S5音频时钟纵切。S1–S10、公共40／20、PPet K0–K5、Web／Toy／TUI及其他平台范围全部保留。

构建复查：共用benchmark工具的依赖包含既有作品构建器，`--filter`只筛运行用例，不裁剪其构建闭包。首次运行遇到生成的host-tool／build-program缓存lock缺项；在生成工作区完成bootstrap，host-tool只采用现有仓库依赖版本并剔除无关项后，最终父命令保持`--locked`通过。仓库lock始终未变。公共hanim独立消费者不含这些作品依赖；后续拆分共用harness需保留单一实现，不能为每个引擎复制测试框架。

## 9. 双第一优先级开发规范补充

执行基线 `98d14be`。按维护者要求在根[AGENTS.md](../../AGENTS.md)加入人／Agent双第一优先级规范，并在[核心准则§9](2026-11-05-local-global-composable-architecture-design.md#9-人与-agent-双第一优先级的设计与验收)展开评审门槛；综合方案§8和本台账验收轴同步引用。

本批明确语义契约先行、完整headless任务与真实离屏视觉、随能力交付最小skill／SOP、开发会话默认full access及发布权限交集、Agent委派和观察数据隐私、纯库的轻量依赖边界。它约束后续各检查点，不将这些要求留到S6才开始考虑；完整公共控制、独立provider及权限链仍在相应检查点实现。

本批只修改开发规范与设计文档；本地检查文档链接、diff格式与仓库lint，不据此宣称新增运行时能力或完成S1–S10。现有`mcpp.lock`保持不变，不纳入提交。
