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
| 包粒度 | 内部小能力先归所属包的src目录／分区；独立库或必要隔离才建清单（D-99） | 独立消费者／目标任务、隔离收益、feature替代方案与维护成本；不按文件数机械拆包或合并 |

## 2. 检查点拆分与依赖

下表是可合入的实施批次；大型项允许连续检查点，不能用一次局部通过勾掉整行。S2/S4/S5尽早交错检验不同领域，不等待hanim全部完成。每轮保持最多2名subagent的上限；当前轮单线程集成。

| 检查点／原计划 | 实现内容与依赖 | 官网／产品映射 | 退出条件 |
|---|---|---|---|
| **S1 / P1：公共入口与authoring** | 唯一hanim伞包迁gfx；动画中心叶子／组合直接进入既有Program；live/frozen输入、错误、旧入口兼容 | 动画文档的真实公共API；后续动画页开场使用同一能力 | 独立dev/release、纯core兼容、无UI/renderer闭包、真实CPU像素、旧回归 |
| **S2 / P1＋P2-A：通道／scope与UI接入** | typed属性通道、一个通用path modifier、显式effect作用阶段；UI adapter发布姿态／命中几何 | 基础动画页开场、按钮／开关／浮层／列表的默认配方 | 4类组件人／Agent触发、打断C0、焦点／点击／中间态、reduced/off；按证据迁移旧motion实现 |
| **S3 / F0＋F3＋R：自适应与局部style** | 复用能力查询和feature选择；theme/effect/motion分工；pixel identity、高DPI、scope/portal及文字失效修复 | 风格页同对象对照，组件舞台按空间调整；局部pixel作品 | 可解释选择／失败；DPI与窄屏、中英、文字可读／命中；未选资源不进闭包 |
| **S4 / P2-B：反应／计算／渲染** | 两材料、状态属性、阈值／邻域、chunk/halo、检查点；一个真实WebGPU算子 | 创意作品中可操作材料画布／反应实验 | CPU参考、边界／顺序／重建、设备资源／数值等级；Noita扩负载单列吞吐与内存预算 |
| **S5 / P2-C：音频／时间映射** | 现有audio/timeline的自动化和视觉同步；math与CLI依赖收敛 | 可交互混音／节奏作品 | CLI同源WAV与画面；30/60/120fps不改音频；seek不重复副作用 |
| **S6 / P3＋F1：公共控制与观测** | §15已提取mlibs.control授权／会话／上下文；继续中立provider/resource/time及公共openas adapter | Agent页开发／使用／设计真实任务与可审查过程 | 非UI引擎独立控制、授权／revision／取消、真区域像素、trace；DAG防反向依赖 |
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

## 10. S3首批：局部像素效果与通用参数动画

执行基线 `4245048`。本批先补可复用的pixel效果和参数桥接，再由官网普通消费者使用。**S3仍在实施；完整pixel风格包、部件／portal隔离及feature产物裁剪尚未完成。**

### 10.1 公开能力与单一实现

- 共享renderer的`pixelate`保留`size`首参数，增加`amount`、逻辑／设备单位、层角点相对`origin_x/y`。逻辑网格随输出DPI缩放；设备网格保持设备像素大小；当前均沿输出轴对齐。`amount=0`经通用identity契约精确旁路，保留普通层opacity／blend和嵌套效果，不把逻辑size=1误当作高DPI恒等。
- CPU和GPU共用网格参数推导，块均值与强度采用相同预乘字节算法；强度量化为1/255，满强度保留直接写均值路径。原完整捕获场景的默认效果保持既有pinned像素。
- 裁剪输入需要的邻域与效果输出范围分别声明。`EffectInfo::input_radius`让两后端在视口边缘保留完整网格邻域；不扩大层本身的reach／output_reach。新增回调均可选，无后端效果名特判。
- `hanim::fx::pixelate(from,to,grid)`只提供公开参数配方，通过既有Program／Engine／PreparedEffects执行；纯hanim不引入renderer、UI或窗口。可选`hanim.render.vector`使用同一真实renderer。
- UI的`pixel_effect`新增`animate:{参数:[from,to]}`，由公开`hanim.effect`准备和采样；复用Runtime时钟、seek和动画状态。旧duration-only继续驱动progress；没有progress的效果必须显式选择参数。非法通道、非有限端点／时间、越界参数、错误reverse等拒绝安装，不静默假装动画有效。静态参数的全部诊断并未在本批重写。
- theme读写支持同一嵌套配置；`usage:"appearance"`使disabled保留基础材质，默认feedback保持原抑制规则。disabled不选hover／press／focus反馈。reduced／off选最终静态参数并停止持续调度；这不是完整typed通道混合、冲突策略或自动retarget。

```cpp
auto animation = hanim::anim::morph(source, target).with_effect(
    hanim::fx::pixelate({.size=6, .amount=0}, {.size=12, .amount=1},
                       {.unit=hanim::fx::PixelUnit::device}));
auto playback = hengine.play(animation);
```

UI示例：`{"name":"pixelate","size":8,"duration":0.4,"animate":{"amount":[0,1]}}`。参数单位／作用域／结果验收流程见各公共包README；指南与真实能力一并交付。

### 10.2 官网与双视角验收

风格页仍先展示一个可交互样张；进入详情后才挂载图表像素化强度、块大小、设备像素单位与实际配置。只对chart painter应用效果，文字和真实控件保持原绘制顺序中的兄弟节点，可与既有材质组合；默认强度0。此结构证明普通组合能力，不冒充通用组件paint-part隔离。官网没有新增像素算法、时钟或插值器。

人通过真实指针调整；Agent通过同一控件的语义action调整，并由openas协议取得同源renderer的区域PNG。无窗口测试同时比较强度0／中间／终点／回放、外部区域、对象身份、命中和焦点；浏览器验证手机／桌面和中英界面。纯库与官网都提供最小操作流程。

浏览器人工审图发现Slider的语义label并不自动成为画面标题，三条参数轨道缺少可见区分。详情现同时显示每项名称、当前数值与单位，再保留原语义label和action。Agent可操作的自动检查通过不能替代人的可读性验收。

### 10.3 本地结果与自审

[机器证据](../../tools/ci/hanim-api-probe/2026-11-05-pixel-parameters.report.omui.json)记录实际源码hash、命令、日志摘要、锁文件、像素与采样时刻。以下整套回归与后续定向复验分开记录，不将较早运行冒充最后一次修改后的完整重跑。

| 验证 | 本地结果与范围 |
|---|---|
| 相关包整套回归 | Linux GCC16.1：dev 45个程序／329例；release另含真实GPU CTS，49个程序／340例。renderer、hanim effect／adapter、view、theme和site全部通过 |
| Web真实执行 | Emscripten6.0.9的`wasm-test`，Node实际执行25个程序／191例，通过。未将其当作浏览器GPU验证 |
| 最终定向复验 | UI恒等层修正后，dev和Wasm各12个程序／122例；release UI＋site 31个程序／249例。可见参数标题修正后，两native配置各7例。最后增加过程采样测试，dev／release／Wasm各6例通过 |
| 独立公共API | 临时mcpp工程只依赖`hanim`；dev／release各104项断言，12包含消费者，无UI／spec／renderer／RHI依赖 |
| 真实GPU读回 | SDL offscreen＋Vulkan lavapipe，3种DPI×2种单位×3种强度共18组；DPI 1／2与CPU完全相同，1.25最大差1/255，无超差像素。软件GPU验证，不是硬件性能结果 |
| 正式官网包与浏览器 | mcpp正式打包，56个开场PNG及8个完整参考；真实Chromium展示159项、加载88项、交付预算7项全部通过，无浏览器异常。中英手机／桌面详情已审图 |
| 交付测量 | 新profile、访问内HTTP缓存开启、1280×800、20Mbit/s＋40ms：总传输4,740,514 bytes，首帧代理前2,838,285 bytes；首帧代理1584.5ms，ready 1548.7ms，rAF p95 16.8ms。单次记录，不据此宣称稳定提速或真实呈现帧率 |
| 工程与参考图 | lint、diff、文档链接及官网media gate通过；原glass参考图保持字节不变；新增局部图表PNG由真实Agent捕获。现有`mcpp.lock`保持字节不变、不纳入提交 |

### 10.4 动画按全过程验收的首个补充实例

按维护者补充，根[AGENTS.md](../../AGENTS.md)与[核心准则§9.6](2026-11-05-local-global-composable-architecture-design.md#96-动画以过程采样和实际播放验收)明确：时间轴采样、关键事件前后加密、真实语义与像素联合检查、可回放证据，以及实速节奏／性能与人的观感分别验收。既有端点测试不会因此自动获得“过程已验收”状态。

本批`view.effect-animation`以公开Runtime时间控制，在0–1秒间按60Hz采样61个时刻，DPI 2、240×160、CPU同源renderer、合成背景`#f8fafc`。像素强度0→1与网格origin 0→3同时变化；每帧验证实际参数与图像变化、作用域外像素、对象身份／命中／焦点，再反向seek全部61帧逐帧复核。dev／release／Wasm的61张PNG、关键帧图和无损WebP均字节相同。它是参数效果的功能／回归fixture，不作为官网美术作品评分。

提交内保留[8帧审查图](media/2026-11-05-effect-parameter-process.png)和[按时间回放的WebP](media/2026-11-05-effect-parameter-process.webp)，均直接来自上述真实测试输出；从左到右、从上到下的时间为0、0.05、0.15、0.30、0.50、0.70、0.90、1.00秒。WebP包含最后一帧的一个显示间隔，时间戳以结构化采样记录为准。全部61帧保存在本地测试scratch目录；CI既有流程仅在失败时收集其中PNG到`test-pictures`产物，不自动上传成功运行的全部回放。可通过`mcpp test effect_animation --locked -p openmui.view --profile dev`复现；图像哈希与完整时间序列见机器证据。

这批离线采样没有测量该动画的实时呈现帧间隔，官网交付rAF测量也不能替代这个场景的实速手感验收。网格位置取整与强度1/255量化是明确的离散变化；帧hash变化不单独证明流畅或美观。其他动画的事件边界、文字与交互仍按§9.6逐项补证。

### 10.5 自审发现与保留范围

自审发现并修正：①原像素化缺少独立强度，size=1高DPI非恒等；②UI只驱动progress，给其他参数duration没有实际动画；③disabled混淆基础材质与反馈；④TOML库的bool读取接受数值，需要先检查真实类型；⑤区域裁剪缺输入邻域，导致1／1.25／2倍DPI的边缘像素与全图不一致。新增回归先复现后修复，不通过修改golden掩盖问题。

扩展Web真实执行发现旧display-list hash直接编码`size_t`：Wasm32和native64在画面相同的情况下得到不同hash。命令index、path长度和material-mix计数改为固定64位，并经公共hash模块编码明确小端数值；保留原native hash和像素基准。修正后Wasm整套相关测试通过，未更换golden或跳过该测试。

图片比对还发现：UI的零强度效果即使跳过滤镜，保留额外普通合成层也会重复应用祖先抗锯齿clip。UI现在按通用identity声明省去自身无opacity／blend的效果wrapper，节点真正的透明度层保留；显式renderer组合层仍保持调用方要求的隔离语义。加入圆角祖先的零强度回归，并复核原官网图片，不以换参考图代替修正。

剩余：palette／dither与像素字体、按角色组织的完整pixel风格／默认motion、通用paint-domain／parts及portal、完整链命中逆映射、旋转／剪切网格策略、typed通道／混合／retarget、feature实际闭包裁剪。没有新增硬件GPU性能、全平台支持或S1–S10整体完成结论。

## 11. S5首批：样本时钟自动化与无窗口审查

执行基线 `458fa32`。按S2／S4／S5交错推进的规则，本批验证audio与timeline的真实组合，交付可独立使用的适配包和现有CLI入口。**S5仍在实施，官网混音作品与实时设备同步尚未完成。**

### 11.1 局部契约与单一归属

- audio仅定义`GainSource`：保留共享不可变owner，要求`rate()`与`sample(first, values)`，每个bus／block一次类型擦除函数调用，无继承要求、新增虚函数或逐sample接口派发。新源先准备，`Mixer::automate_gain`在静默边界绑定／清除；未知总线、采样率不符均保留旧源。没有把控制消息放进逐sample循环。
- 独立可选`openmui.timeline.audio`冻结选定property与clip位置／level／fade。曲线沿用`hanim::Track<double>`，时间映射沿用timeline的整数Flicks；不另造播放器、数学库或插值器。key是clip-local呈现时间，source trim／speed不改写它；片段外值显式配置，end为开区间。
- adapter准备验证键值、时间／fade范围、溢出、property身份和缓动。`Ease::valid()`收敛原Program的验证，两个消费者共用一处规则。采样率须能精确换算；乱序／反向读取和超出整数时间范围的查询有明确结果。
- 增益在效果／ducking之后、路由和master limiter之前乘总线静态gain；sidechain继续读取pre-gain信号。有限值限于0–8，非有限插件值转为静音并计入trace。控制源的准备／替换与热循环分开，共享owner不在采样时增减引用计数。
- audio原来只因确定性数学间接依赖hanim；本批改为直接`openmui.math`，既有音频和音乐hash保留。JSON因包内音乐CLI仍在实际依赖中，不能宣称纯DSP依赖已经完全裁剪。

```cpp
auto gain = omui::timeline::GainAutomation::prepare(
    clip, {.sample_rate = mixer.rate(), .property = "gain", .outside = 0});
if (!gain) { /* handle typed preparation error */ return; }
auto bound = mixer.automate_gain("music", *gain); // 静默边界
if (!bound) { /* old source remains installed */ return; }
float shown = (*gain)->at_sample(24000); // 只读，不推进音频或触发事件
```

### 11.2 无窗口真实任务与过程审查

现有`omui` CLI新增`audio automation FILE --track NAME --clip NAME --out PREFIX [--property gain] [--seconds 2] [--fps 60]`。它将选中曲线应用于真实220Hz校准音，输出float WAV、omui自身绘制的8帧PNG、无损WebP回放和结构化报告。无需创建窗口、启动显示服务或打开音频设备。报告包含源timeline快照、property、精确Flicks／秒时间戳、采样率、增益、PCM／像素hash、后端／DPI／区域等。

这是有明确用途的自动化曲线试听与审查入口；不会假装播放`clip.source`里的任意素材。现阶段只导出校准音、曲线图和增益条，完整媒体播放、实时GUI编辑及外部openas provider分别实施。各公共包README随附最小SOP，纯库消费者无需CLI或UI。

30／60／120fps分别导出61／121／241个真实过程帧，WAV字节相同；视觉读取不改变audio时钟／voice计数。算法CTS另覆盖hold转折前后1个Flick、片段排他终点、不同render block划分和反向查询，避免仅以A／B端点或少量截图判定通过。8帧图仅为审查索引，完整时间序列保留在报告／WebP中。

### 11.3 本地验证与证据

本批以本地native dev／release、Wasm实际执行、独立mcpp消费者、现有音频benchmark和真实导出文件验证，不等待异平台CI。最终源码、命令、测试结果、产物hash及运行范围记录在[机器证据](../../tools/ci/2026-11-05-audio-automation.report.omui.json)。

| 验证 | 结果与范围 |
|---|---|
| 最终native回归 | GCC16.1 dev／release各5个程序、43例通过，含既有CLI操作／视频／配对回归；不是全仓库重跑 |
| 最终Web计算回归 | Emscripten6.0.9、Node实际执行3个程序、32例通过；不代表浏览器音频设备测试 |
| 确定性 | adapter测试的PCM为`9a6e430560827945`，三配置和三种观察帧率相同；既有音乐固定hash`ab0350d3813bbcc8`保留 |
| 真实导出 | 校准音PCM为`ac9eeb86d84d2cad`；30／60／120fps各自的PNG、WebP与WAV在dev／release之间字节一致，三种fps的WAV也字节一致 |
| 公共独立使用 | 两个临时mcpp工程只声明audio或timeline.audio；release真实运行并检查实际resolution。audio闭包无hanim／timeline／UI／renderer；adapter只增加所需timeline和hanim.core，仍无UI／renderer。JSON依赖尚未拆掉 |
| 热路径预算 | i9-13900K、Linux release：48声部＋echo＋64键控制曲线，128个立体声帧，8,651次测量p95为0.052994ms、最大分配0，满足0.5ms／零分配预算。既有mix-48同次运行p95为0.050961ms；两负载并非严格只差一个变量，不据此宣称精确额外开销。排除准备、voice补充、文件、设备和GUI |
| 工程 | lint、diff格式、链接检查；原`mcpp.lock`字节不变，不纳入提交；相关新增用例随workspace测试进入CI |

审查产物：[8帧图](media/2026-11-05-audio-automation.png)、[121帧回放](media/2026-11-05-audio-automation.webp)、[真实校准音WAV](media/2026-11-05-audio-automation.wav)。图像来自384×256、DPI 1、CPU renderer、背景`#101827`，PNG按从左到右、从上到下排列；时刻约为0、0.283、0.567、0.850、1.133、1.417、1.700、2.000秒。首尾增益均为0，中间包含渐变、保持和声明的跳变；仅看端点会漏掉整个过程。已查看可读性和曲线／游标／数值对应，没有把截图审查记为主观听音或实时音画同步验收。

离线CLI保留帧用于编码；60fps／2秒的RGBA帧本身约47.6MB，另有编码和渲染scratch，不能宣称该工具逐帧零分配。WebP含末帧一个显示间隔，WAV独立导出，未封装为同步音视频；精确时刻以报告为准。既有CI只在失败时收集PNG，不自动上传成功运行的全部回放。

本地构建恢复沿用§8.4记录的工具缓存边界：分别补齐生成的host-tool／build-program锁记录，版本全部取自当前仓库已有依赖；最终父命令保持`--locked`，仓库lock未写入。公共benchmark仍连带作品／工具构建，`--filter`只筛运行用例；后续拆出单一共享harness再按域选择目标，不复制多套测试基础。该构建成本不属于独立audio消费者的依赖闭包。

### 11.4 自审与保留范围

自审修正：①保持sidechain pre-gain语义；②插件非有限／过量增益明确处理；③用小型值契约替代最初的虚接口，保持批量调用与owner寿命；④CLI拒绝不能精确换算的帧率；⑤后续文件写入失败时返回`ok:false`，列出此前已写文件。输出不是多文件原子事务。静默换绑、无窗口、只读观察和独立依赖均写入公开契约，不能由接口名称推断尚不存在的能力。

尚未完成：跨线程有界参数／结构发布、sample-accurate transport事件、设备时钟及延迟校准、带状态DSP的seek／checkpoint、完整调制图、通用媒体播放、音频CLI依赖拆分、GUI编辑与官网混音作品。视觉采样、离线PCM一致和block预算不能证明声卡低延迟、无爆音的实时换绑或人的听觉／动画节奏体验。S1–S10与公共40／20、Web／Toy／TUI、PPet等目标继续保留。

## 12. S4首批：独立模拟与整数反应的CPU／GPU过程一致性

执行基线`5d7dc66`。按S2／S4／S5交错推进，本批交付可独立选择的固定步长／状态重建、通用整数格点规则与可选RHI kernel。**S4仍在实施；材料画布、真实视觉过程、物理halo交换与Noita规模验收尚未完成。** 没有在官网写另一套模拟、compute设备或渲染器。

### 12.1 局部核心与共享归属

| 所属包 | 实际职责／依赖 |
|---|---|
| `modules/base/simulation`：`openmui.simulation` | 从原`mgame.sim`下沉唯一Clock／Timeline实现，依赖std、trace、log；owned State可深拷贝，Input仍为plain data。旧游戏包保留source-compatible aliases与Game约束，删除原clock实现 |
| `modules/base/simulation/cellular` | 类型化channel、range、邻域offset、条件与write；冷prepare生成一份只读整数描述；CPU完整／部分tile执行。只依赖std、trace、log，不自动引入clock／UI／game／GPU |
| `modules/gfx/simulation/cellular/rhi` | 直接复用RHI的device／buffer／command list／queue与现有Slang规则；同一描述交给整数compute kernel。拥有规则buffer／pipeline，不拥有世界、时钟或场状态 |
| `…/rhi/cts` | 同一组域用例由既有SDL_GPU／WebGPU测试调用。测试依赖不进入普通后端的运行时闭包，不再建立测试后端／线程池／设备接口 |

类型化规则围绕本域能力组合：每个cell可以具有材料、状态、年龄等多个uint32通道；条件支持自身区间和显式邻域计数。按作者顺序首个匹配规则生效，所有读取来自同一旧状态；set、饱和加／减为通用写算子，未写通道原样复制。材料反应只是调用方配置，不是内核枚举或场景分支。空writes阻止后续规则，空规则集为identity；±32范围、offset重复、形状／容量、通道与写冲突在prepare／执行边界检查。

```cpp
namespace ca = omui::simulation::cellular;
auto program = ca::Program::prepare(channels, rules);
if (!program) { /* code + rule + item */ return; }
auto result = program->step(shape, before, after, {.tile = {32, 32}});
if (result) before.swap(after); // 发布由宿主掌握

auto kernel = ca::gpu::Kernel::prepare(device, *program); // 显式选择GPU
if (!kernel) { /* RHI error */ return; }
auto recorded = (*kernel)->record(commands, input_field, output_field, boundary);
if (recorded) { /* 宿主submit、检查fence／错误，再发布 */ }
```

CPU tile从完整旧field读取逻辑halo，分块大小和遍历方向不影响结果；可把互不相交的输出tile交给既有任务系统。GPU workgroup为8×8，与CPU tile／未来render tile独立；连续12步可只在设备中交换两个buffer，期间不读回。kernel不在record中上传、submit、等待或推进时钟。输入／输出owner、有效handle、shape、容量和usage在追加命令前验证；失败保留调用方原命令及payload。异步设备执行仍可能失败，不能把record原子性说成GPU事务。

共享Timeline预填快照bank，固定形状的vector State复用容量；seek通过快照＋输入重放恢复，观察不触发外部副作用。规则／拓扑修改需要宿主重建，不偷偷改变旧历史。自审还发现原Timeline在ring淘汰旧历史后回退开分支，会用降低后的latest重新计算earliest，使已覆盖的过去再次变为“可seek”。现在在显式branch与隐式step分支前保留已淘汰边界，真实回归覆盖两种入口且错误seek保留状态。Clock还修复了原来先float→int、后截断大stall的风险，支持有限负时间起点并明确非有限时间／scale处理。大型history容量仍是可信调用方前置条件，尚无不可信文件解析／存储预算工厂。旧C++消费者需重编译，不保证旧预编译ABI。

设计随证据收敛：RHI已经提供设备计算所需基本操作，本轮不增加一个转调RHI的空`compute`包装层。跨算子计划／调度有多个实际消费者后再提取，继续保留HPC独立产品方向；mcpp仍只负责构建。各公共包有README、契约、最小Agent／人SOP、trace span与`simulation`日志类别。

### 12.2 验证完整演变，不只检查首尾

CPU CTS使用独立手写标量参考，不调用被测encoding／tile循环。19×11、三个通道、可反应／惰性材料与状态／年龄规则，四种tile大小×正反顺序连续24步比较完整状态；另验constant／clamp／wrap、partial tile、饱和写、错误保留、owned快照／输入分支。真实breakout消费者保留原machine与game hash，验证原接口迁移。

GPU域CTS的16组场景覆盖逐tick读回、12步驻留批次、checkpoint上传后重放、1×1／奇数尺寸、±32 halo、wrap重复映射、barrier／identity与饱和写。每个后端实际执行**266步、168次完整比较、61,320个uint32值、129项拒绝／命令保留检查**，过程hash均为`0769cee00d2cbf3a`。离散分支用完整整数状态等价验收，不用画面epsilon掩盖差异。

这属于模拟过程的状态验收，**尚不能代替材料动画画面的视觉验收**。动画总规范继续要求同源renderer时间采样、关键事件前后帧、回放和实时节奏；核心设计准则§9.5新增显式过程门槛，§9.6保留完整SOP。后续画布需在这些状态上做真实像素／交互审查，不能直接把本批CPU／GPU hash计为视觉通过。

### 12.3 本地证据

命令、最终源码hash、执行日志、独立消费者源码与真实resolution、benchmark结果记录在[机器证据](../../tools/ci/2026-11-05-simulation-cellular.report.omui.json)。本轮不等待其他平台CI。

| 验证 | 实际结果与范围 |
|---|---|
| native dev／release | 各5个成员、6个测试程序、46例通过；含共享simulation、cellular、原mgame.sim、真实breakout及SDL_GPU原有／新增用例 |
| Wasm计算 | Emscripten6.0.9、wasm-test／Node实际运行4个程序、36例；不是仅编译 |
| 本地GPU | SDL offscreen＋Vulkan llvmpipe，完整域CTS通过；这是软件GPU执行，不是物理GPU性能 |
| 浏览器GPU | mcpp wasm-browser构建，Chromium真实WebGPU／SwiftShader，7例通过，域过程结果与native一致；独立Node无WebGPU不计为此项 |
| 独立CPU消费者 | 外部临时mcpp工程dev／release实际运行；闭包仅消费者、simulation、cellular、trace、log，无UI／game／renderer／RHI／shader；报告保留准确工程内容 |
| 参考步进成本 | Linux i9-13900K，release；256² cells、3通道、3规则、32² tiles，591次测量p95 **0.716792ms**，最大分配0。排除初始场重置、准备、传输、渲染与GPU |
| 检查点成本 | 64² cells的反应步进＋每步深快照，8,917次测量p95 **0.047030ms**，最大分配0；排除bank准备和回退。沿用现有benchmark harness及预算 |
| 工程 | lint／链接／diff检查；现有后端测试与workspace测试进入原CI路径；源`mcpp.lock`字节保持不变，不纳入提交 |

本机最初GPU运行因同时加载LLVM20和lavapipe的LLVM22而SIGSEGV，gdb显示LLVM22调用进入LLVM20的内部符号。显式预加载本机lavapipe配套LLVM22后原有RHI与新增CTS全部通过；只修正本地验证环境，没有在产品中写死系统路径、降低断言或跳过GPU。该环境边界随报告记录；浏览器走独立SwiftShader实现，也实际执行。

新增GPU包首次触发旧生成host build-program锁的resolution差异：只在错误指出的`target/.build-mcpp/host/…`内用offline mcpp重建，父命令继续`--locked`，不改变仓库源锁。没有清理任何用户文件或等待CI。

### 12.4 自审、剩余范围与下一步

自审重点：未复制已有Clock／Timeline／RHI；状态深拷贝和冷容量准备有可用模型与分配证据；GPU只执行已验证的整数描述，buffer／状态发布归宿主；失败追加与设备执行失败区分；逐步状态检查不能假冒像素审查。规则准备支持的预算和Timeline仍要求可信容量分别写清。

继续开放：稀疏／活动区域、物理halo交换、多cell移动／守恒与冲突仲裁、通用有状态反应图、浮点科学solver、GPU帧图资源自动编排／device-loss恢复／时间查询、真实硬件吞吐、Noita规模压力矩阵、材料画布与规则编辑／openas provider。下一条纵切优先以公共cellular能力做可操作材料作品，沿用共享renderer／headless采样；不在site复制规则或播放器。S4、全局S1–S10与40／20等目标仍未整体验收。

## 13. 包粒度修订：内部目录优先，按独立价值建库

执行基线`4ab416e`。按维护者补充，将“每个目录都是workspace成员”改为D-99：少量内部实现先放所属包的`src/<职责>/`和模块分区，共用清单；有足够内聚实现的复用库、明确独立产品计划或必要隔离才建立成员。小型必要库保留例外依据，不设机械行数／文件数阈值。已同步AGENTS、核心准则§5.1、综合方案§5、早期架构／实施计划和索引，避免旧表述继续驱动过度拆包。

源码初筛遍历根清单的128个登记成员，排除嵌套成员后统计自身`src/`下`.cppm`／`.cpp`：54个成员不超过两份C++文件；该数不计测试、shader、脚本和资产，不是应删除的包数。结合实际清单，综合方案区分共享基础、可选设备／GPU／跨域adapter、host构建插件、兼容入口与可收敛的hanim内部职责。UI view当前直接依赖`hanim.effect`，后续合并需保留轻量选择，不能仅减少清单数量却扩大依赖闭包。

下一阶段的新IR、AI内部职责、领域控制和工具命令，默认进入已有owner的源码目录；独立材料工具仍以完整制作任务与复用边界决定是否提取。既有小包的合并需要单独记录API兼容、feature／平台裁剪、独立消费者和构建成本验证。本轮未迁移源码或更改运行时依赖，不将规范修订计为S4工具或包收敛实现完成；动画过程继续遵循[核心准则§9.6](2026-11-05-local-global-composable-architecture-design.md#96-动画以过程采样和实际播放验收)的真实帧采样与实际播放验收。

验证：全仓lint为0问题；新增／修改的本地文档链接与锚点、`git diff --check`通过。现有lint的members／layout已经只约束实际清单和成员，并不要求普通源码目录带清单，无需添加按行数拆包的lint。仅文档变化，没有重复运行无关的引擎测试；源`mcpp.lock`保持原字节且不纳入提交。

## 14. S4反应工作台：共享库、Inspector、官网与无窗口过程验收

执行基线`ed87ec7`。本批把§12的公共模拟能力接到可操作工具和官网；没有新增workspace成员。**S4仍在实施，不能据此宣布通用模拟、材料编辑器或Noita级规模完成。** 可复现命令、源码／产物摘要和逐项结果见[验证报告](../../tools/ci/2026-11-05-cellular-workbench.report.omui.json)，公共操作与最小SOP见[工具文档](../../tools/inspector/docs/cellular.md)。

### 14.1 归属与真实功能

- `tools/inspector/src/cellular/`在现有Inspector清单内组织模型、示例配方、视图和批处理，公开`openmui.inspector:cellular`分区；按D-99用源码目录表达职责，不建立一文件一包。
- 整数规则执行、时钟和检查点分别复用`openmui.simulation.cellular`与`openmui.simulation`；视图用现有Painter／组件、headless和renderer，撤销走现有actor history。工具层只负责工程数据、调色板、操作和呈现。默认火种／路径／屏障是可编辑配方，未引入内核材料类型或另一份模拟器。
- 官网在`#/explore/reactions`直接嵌入同一工作台，从Creative demos进入；首页不增加一屏。先呈现画布、播放／单步／回退和画笔，规则JSON及工程操作经“规则与工程”展开。支持中英文、窄屏、键盘光标和实际指针定位；离开路由／窗口失焦会停播。
- 工程包括大小、命名通道、整数field、边界、规则、调色板、画笔和rate／tick。载入／配置先完整验证再发布；浮点冒充整数、非法偏移／通道、容量超限及过期revision拒绝。导出当前field和规则，重新载入可继续相同演化；历史日志未序列化。

### 14.2 人与Agent共享操作、时间和权限

十二个`cellular.*` verb：`describe`、`snapshot`、`project`、`config`、`inspect`；`load`、`configure`、`paint`、`advance`、`seek`、`reset`、`play`。沿用openas的effect、actor、session和回执。默认批处理显式绑定可信本地开发实例，不改全局fallback策略；常规`--cellular --headless --serve`继续使用SDK的发布／开发策略。

`Runtime::current_invocation()`只给可信C++延续代码复制当前调用上下文；定时播放每次仍通过`Runtime::invoke`重新判定权限，不能把Agent发起的播放换成无session的人类调用。已验证启动后撤销session会停止推进、只读层允许查询而拒绝写入；人的本地操作继续按人的身份处理。

paint是记录输入，不消耗模拟tick；seek使用history frame，二者不能混淆。编辑的undo／redo恢复拥有的前后状态并开始新的本地重放历史；时间推进使用240输入容量、每30输入的检查点，不为每个tick建立UI逆操作。换工程和undo后同步合法画笔、光标边界和规则文本，避免旧工程的选择越界。

### 14.3 本地验证与真实过程证据

| 范围 | 实际结果 |
|---|---|
| Inspector原生dev | 2程序／9用例通过；含已有连接Inspector的回归和8项新工作台用例 |
| Inspector原生release | 工作台8用例通过；独立release CLI另实际运行 |
| Wasm／Node | `wasm-test`实际执行8用例通过，不仅编译 |
| 官网原生回归 | 20程序／129用例通过；含新工作台6种宽度／语言组合、路由停播及现有首页／组件／动效／Agent测试 |
| 状态与像素过程 | 71个前向状态逐个反向重建；960英文／260中文／640中文2×DPI各25幅实际画布状态逐个反向像素核对，含窗口缩放后真实点击 |
| Chromium官网 | 14项检查通过；真实鼠标绘制与Agent同状态，25个连续实际画布PNG及17／8／0帧反向像素一致，真实键盘改规则、播放、离页停播和390中文布局 |
| 无显示环境CLI | 移除DISPLAY／WAYLAND_DISPLAY后，release导出61个PNG、WebP、工程和报告；新进程载入后演化到tick62，与从原输入推进的参考一致；缺参数／缺文件为非零退出及结构化失败 |

浏览器脚本`tools/ci/site-reactions.mjs`已接到现有site browser检查，CI保留画面与报告。本轮不等待跨平台CI；没有宣称Windows／macOS／手机真机已验证。原生与浏览器画面分别按自身后端核对，不把它们误报为跨后端逐像素相等。

批处理逐帧保存合理时间的有理数分子／分母、revision／state hash、像素hash、viewport／DPI、renderer、输入工程和操作回执。WebP把累计时间舍入到毫秒，避免逐帧舍入累计漂移；离线采样证明过程可审查，实播按钮验证功能，**均不等于实时帧时延或人的观感预算已经验收**。已实际查看窄屏中文和中间态，确认画布有过程变化、文字与控制可读。

### 14.4 自审与后续边界

- 本工作台目前执行CPU规则程序；独立GPU kernel仍是§12的能力，尚未形成GPU驻留模拟→共享帧图→画布的完整通路。稀疏／活动区域、物理halo、多cell移动与守恒、通用反应图、科学solver、设备丢失与Noita规模预算继续开放。
- 本轮没有新增包，但Inspector尚未提供关闭cellular源码和依赖的内部feature；该项成本裁剪不能标为完成。公共模拟库仍可不依赖UI／Inspector独立使用。当前site-web构建的Wasm及gzip体积已记录在验证报告；这是本地产物观察，不是新的首帧性能验收。
- `state_hash`只覆盖维度／通道数／tick／cells，规则与风格改动应结合revision和导出工程核对。保存当前状态、回放历史、actor撤销与外部文件输出分别定义；多个导出文件不是一个原子事务，失败会返回已写路径。
- 工具仍是有限整数场工作台：人通过展开的JSON修改规则，尚非成熟节点编辑器；没有刷拖、多工程资产管理或远程协作事务。不把一个新增作品计为40种动画或20个创意作品的整体验收。

最终静态检查：全仓lint、JS／shell语法、变更链接与`git diff --check`；用户已有的`mcpp.lock`保持原字节并排除提交。下一步仍按全局S1–S10的实际缺口推进，复用既有owner和公开契约。

## 15. S6首批共享控制与D-100命名归属

执行基线`4833c1a`。本批交付公共授权、会话与调用上下文，并将维护者的命名补充形成D-100。**S6完整provider／控制与观测链、N1–N4全仓命名迁移均未完成。** 源码、命令和本地证据见[验证报告](../../tools/ci/2026-11-05-shared-control.report.omui.json)。

### 15.1 代码归属与兼容

- `modules/base/control/`以`mlibs.control`包／模块、`mlibs::control`命名空间公开Policy、Restrictions、Access、Session和Invocation／Context／Scope。授权判定继续调用openas 0.2；控制库只依赖openas与已有log／trace，无UI规范Value、renderer、网络或模型依赖。
- 从`openmui.core`移动实际授权和上下文算法，删除UI原实现；UI保留AgentAccess／AgentSession、聚合PolicyState／Invocation、Value序列化及操作回执。可信宿主可以共享同一个control Access和Session，UI和独立引擎由同一issuer管理；兼容入口不再维护第二份权限状态。
- 这次建立独立清单的依据是UI与独立hanim两类真实消费者，以及从UI依赖闭包中隔离控制能力。policy／session／context仍在同一包，不逐职责拆清单；默认hanim仍不依赖控制库，由宿主显式选择。遵循D-99，不能将本次提取当作继续创建微型包的理由。
- 保留开发可信会话的预授权、发布模式权限交集、输入／操作限制、grant epoch、撤销和跨实例拒绝；新增`control` trace类别，现有`agent`日志类别及可重入sink行为不变。低层调用省略operation时，现在按verb名过滤；该行为收紧有单独回归，不再让空参数跳过操作限制。
- Context属于实例和线程；异步任务复制上下文、执行前重新校验。`Session::attribute`是归因，不能代替live授权；同步决定不承诺取消已经在运行的任务。UI操作过滤沿用其transport类别，独立hanim宿主按当前领域操作过滤，嵌套进入不能把capture权限当成seek权限。

### 15.2 实际独立消费者与过程验收

扩展既有`tools/ci/hanim-play-consumer`，没有另建播放器或协议。外部临时工程明确选择公共hanim、vector renderer与`mlibs.control`，用真正的Engine seek和共享CPU renderer执行Agent审查；实际resolution／编译源闭包不含UI、UI规范、SDK、site、host或testing。

dev／release各执行直线路径与扰动路径两组动画，每组61时刻，输出真实PNG，并按反向61时刻逐帧比对。共244张过程图和244次反向重放核对；输出包含有理采样时间、实际播放位置、计划／目标revision、viewport、像素与PNG摘要。调用前移除DISPLAY／WAYLAND_DISPLAY；无窗口／显示服务器要求。

还验证只读层拒绝seek但允许capture、dry-run不改状态、capture-only会话的嵌套seek被拒绝、跨实例会话失败、已排队调用在session撤销后不能继续改状态或读像素，以及显式禁止capture时非零退出且不写图像／元数据。fixture显式创建可信开发会话，即使release二进制也如此；这不改变生产应用的profile默认策略。成功审查后的普通PNG是本地宿主导出，逐帧Agent审查自身均经过授权。

已实际查看多个中间态，形状、颜色与曲线路径随过程变化。离线采样验证可观察、可重放的过程，**不等于实速播放／帧时延或人的美术观感验收**；没有宣称GPU、物理设备或通用远程provider已完成。

### 15.3 本地验证与修复

| 范围 | 实际通过 |
|---|---|
| Linux dev | control 2程序／11用例；UI core 11／41；Agent 5／33；Inspector 2／9 |
| Linux release | control 2／11；UI core 11／41；Agent 5／33 |
| Wasm／Node实际执行 | control 2／11；UI core 11／41；Agent session 1／6 |
| 独立消费者dev／release | 原有Program／region／retarget／路径检查及新增两组61帧过程、权限／嵌套／撤销／拒绝导出；依赖闭包审计 |

过程中修正两个集成问题：初稿`omui::control`与UI内部`control()`重名，D-100要求的新owner `mlibs::control`消除该冲突；既有task测试假定spawn异步，在单线程Web后端形成永久等待。测试现按Tasks报告的能力验证同步取消及后续token状态，队列经可用worker验证；生产Web调度器未因此获得后台线程能力。

全仓lint、变更文档本地链接、Python语法与`git diff --check`纳入收口；用户已有`mcpp.lock`保持原字节且不提交。本轮不等待其他平台CI，没有把Windows／macOS／浏览器实播列为本轮验证。

### 15.4 命名落地与剩余工作

[D-100方案](2026-11-05-product-namespace-migration-design.md)明确openmui规范、omui UI、具名产品、mlibs通用库与xengine暂名引擎的代码树归属；AGENTS、主架构与中英文仓库入口同步。当前实际新入口是`mlibs.control`；其他`openmui.*`实现包仍在兼容迁移期，标准子模块、schema、协议字段、C ABI、第三方包及用户lock没有随之更名。

后续按N1基础库→N2引擎→N3 UI与标准连接→N4兼容收口推进，包别名能力须先实际验证，不能用源码转发冒充包解析兼容或成倍预建空壳包。S6的通用provider注册、资源范围／revision／取消、IR编辑、完整观测与传输接入仍开放；S1–S10的其他功能与40／20、PPet目标不因命名或这批共享控制而计为完成。
