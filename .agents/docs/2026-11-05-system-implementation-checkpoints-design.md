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

## 16. D-100／N1首组基础库命名迁移

执行基线 `8910702`。本批将 hash、math、log、trace、testing 的包、主要 module、C++ namespace 和产物归属迁至 `mlibs`；**只完成 N1 的首个依赖组，没有完成全仓迁移或增加功能验收数量。** 实际命令、摘要、依赖与画面回归见[验证报告](../../tools/ci/2026-11-05-foundation-namespaces.report.omui.json)，兼容使用方式见[迁移方案 §5](2026-11-05-product-namespace-migration-design.md#5-n1首组五个基础库的实际迁移)。

### 16.1 实现与兼容边界

- 五个原清单保留原目录，新身份为 `mlibs.hash/math/log/trace/testing`，命名空间分别是 `mlibs::hash/math/log/trace/testing`。仓内 120 个清单以及相关外部消费模板更新依赖键；129 个 workspace 成员总数不变，无新增兼容包。hanim.core 与 mlibs.control 直接消费新 API，其余 UI／官网旧 import 继续工作。
- 每库只有一份 `.api` 声明／实现，主要入口及旧 `openmui.*` 转发在同一个包内。旧命名空间用 namespace alias／using 共享类型与函数；日志 sink／filter／history、trace recorder 与测试状态也只保留一份。声明与旧版本经过归属替换后相同，原算法及数值常量没有重写。
- `.api` 是兼容布局的接口单元，不是新 library；主要入口导出 legacy，保证 `mcpp pack` 真正包含旧模块。新旧 import、依赖键混用的实际图中每个库只有一个包，未重复编译实现。包选择器和产物改用新名，旧 CLI 名称不作为别名承诺。
- 规范子模块、schema／协议字段、生成 C ABI、元数据键、数据后缀和默认日志类别未随之批量更名；用户已有 `mcpp.lock` 保持原字节且不提交。源码消费者重新构建，不承诺 C++ 二进制兼容。

### 16.2 本地真实验证

| 范围 | 通过结果 |
|---|---|
| Linux GCC 16.1 dev | 58 个测试程序；344 个具名用例，另有 hash 独立检查程序 |
| Linux GCC 16.1 release | 58 个测试程序；344 个具名用例，另有 hash 独立检查程序 |
| Wasm／Node 实际执行 | 28 个测试程序；138 个具名用例，另有 hash 独立检查程序；覆盖五库的可执行测试、control、hanim.core、UI core |
| 外部 foundation 消费者 | 源码／真实静态安装包 × 新名／旧名／混用，6 种组合；各运行首次构建及未修改后的增量命令，类型／函数／状态与实际依赖闭包检查通过 |
| 独立 hanim 消费者 | core、公开创作 API、完整 playback／共享控制分别在 dev／release 通过，产品图未引入 testing 或 UI |
| 动画过程 | 两个 profile 各两组 61 时刻真实 PNG，合计 244 帧及逐帧反向重放，授权／撤销／跨实例／拒绝导出继续通过 |
| 官网 | 两个原生 profile 各 20 个程序／129 用例；新 Web 打包、原生 prerender 与 Agent 指南生成成功；Chromium 14 项交互检查通过 |
| 画面稳定性 | 浏览器 25 个固定状态和画布 PNG 摘要与前批反应工作台记录逐一一致；检查了中文窄屏与中间态实际图像 |

合计 144 次仓内测试程序执行、826 次具名用例执行；这不是 144 个不同功能。独立消费者和浏览器另列，不混入这个分母。保留确定性 math／hanim 摘要；页面整体截图含实时时序，不声称整体 PNG 逐字节不变。没有重新认定首帧性能、GPU／设备覆盖或人的美术观感达标。

### 16.3 工程问题与发布前缺口

- namespace 改动使宿主工具源码摘要改变，mcpp 新建的工具 scratch 在 `--locked` 下缺失 json／tomlplusplus 锁项，首次大回归有四个成员在构建阶段失败。通过仓库锁实际重建 gen／shader，再用 mcpp 支持的 `MCPP_TOOL_*` 指向这些工具完成回归；未修改锁、未使用旧二进制、未关闭锁验证。报告保留初次失败及工具摘要。CI 正常构建仍异步执行。
- 当前 mcpp 的 testing 安装包清单遗漏传递 hash 依赖。安装包消费者显式列出五个包，证明同一实现的实际链接与兼容；**testing 单依赖安装尚未验收**，正式发布前需修复上游打包清单并补对应独立消费验证。没有用复制算法或新增空壳包掩盖缺口，也未发布 registry 名称别名。
- 新增 `tools/ci/foundation-migration` 为现有 CI 内的外部消费 fixture，不是 workspace 库；已接入 Linux release CI 并保留日志／图／产物信息。记录首次工程构建、增量命令时间和静态包大小，仅作为本机观察，不宣称全冷构建或性能提升。
- 后续继续迁移其余基础库，再处理 renderer／simulation／audio 和 UI／标准连接；正式发布和兼容退役各自验收。S1–S10、40／20、完整默认动效、PPet 与后端覆盖的剩余工作保持开放。

最终检查：全仓 lint 为零、变更清单 TOML／Python 语法、本地文档链接、`git diff --check`；本批以本地证据提交 PR #1，不等待其他平台 CI。

## 17. D-100／N1第二组：共享几何、内存和文本基础

执行基线 `f0bc3ef`。本批迁移 geom、memory、unicode、fuzzy，继续真实消费者与兼容验证；**N1 尚未整体完成**。公开入口和后续边界见[命名方案 §6](2026-11-05-product-namespace-migration-design.md#6-n1第二组几何内存unicode与搜索)，源码、命令及检查点证据见[本轮报告](../../tools/ci/2026-11-05-geometry-memory-unicode-namespaces.report.omui.json)。

### 17.1 唯一实现与实际消费者

四个原包直接成为 `mlibs.geom`、`mlibs.memory`、`mlibs.unicode`、`mlibs.fuzzy`，目录与内部职责划分保留，新旧 import 在同包导出。几何及颜色仍是同一类型，内存模块通过 using 保留旧 `omui::Arena/Handle/SlotStorage/SlotMap/FunctionRef` 等 API；没有新建 library 或复制算法。hanim 对象／播放注册表和 vector adapter 已直接使用新归属，UI／renderer／文字和脚本 adapter 继续正常消费旧入口。

Unicode 生成器一起修改，并从官方 17.0.0 UCD 实际重新生成。对照基线：除 module／namespace 外，5760 行生成文件相同，测试输入原字节不变；`omui-gen unicode ... --check` 通过。字素 766、词边界 1944、断行 4835 个已提交参考输入在原生和 Wasm 都通过；断行是现有四分之一子集，不能写成重跑了全部 19338 条。

现有外部消费 gate 扩到九个基础库，在新名／旧名／混用下分别从源码和实际静态包运行；新增同一类型跨入口传递、arena 复用、slot 撤销与 generation、UTF-8／grapheme／大小写及 fuzzy→Unicode 真实调用。核对图中每库只有一个身份及源码实现集合；geom 没有 `.cpp` 仍保持轻量，Unicode 多实现单元也未被强制拆包。

### 17.2 验证范围与 Web 测试修复

- 原生 dev 与 release 各 62 个不同测试程序通过，报告 421 个套件用例；覆盖四库、hanim.object／engine、text／layout、UI core／motion、C ABI、native fuzzy、Agent、Inspector、官网。C ABI 另运行真实 C 源码消费者。三个 motion 测试在修正后，两个原生 profile 均再次实际运行所在包，继续保留并发验证。
- Web／Node 共 33 个不同测试程序完成验证，覆盖四库以及 hanim 对象／播放、文字／布局、core、native fuzzy、motion。原始大回归有三个 motion 程序失败，不能把初次执行记成全绿；修正后整个 motion 包七个程序通过。
- 失败位置均为既有测试无条件调用 `std::jthread`／`std::async(launch::async)`，而当前 Web target 的 `testing::threads` 为 false。用现有 constexpr 能力常量分支：原生保持真实并发，Web 执行独立输出缓冲、重入及重复采样；名称也标明 threadless。未删除数值、像素或分配断言，未宣称 Web 已支持共享内存多线程。
- 独立 hanim 创作 API 与实际 playback／Agent 控制继续在两个 profile 验证；两组 61 时刻分别采样并反向重放，共 244 个 PNG。产品依赖闭包未混入 UI／testing。

官网 Web 重新打包、原生生成静态页面与 Agent 指南，Chromium 14 项交互检查通过；25 个固定状态／PNG 摘要与此前反应工作台记录完全一致，实际查看中文窄屏画面。实速性能、跨设备 GPU 与人的美术验收不由这些命名回归推导。

### 17.3 仍需完成

共享基础库累计九个完成迁移，另有此前直接新建的 `mlibs.control`；workspace 成员仍为 129。剩余 session／pipeline／image、gfx 与 content 基础库，之后引擎与 UI／标准按依赖顺序迁移。mcpp 打包遗漏传递依赖的限制也存在于 fuzzy→unicode，本轮明确依赖全部安装包，正式索引发布前还需修复并验证只声明顶层库的安装方式。未声称 C++ ABI 稳定、旧 CLI 选择器别名或新平台交付完成。

保留用户已有 lock，规范子模块不变；实际算法／UCD 数据未重写。按本地相关证据提交同一 PR #1，跨平台 CI 异步，不阻塞下一检查点。S1–S10、40／20、PPet 和完整多后端目标继续开放。

## 18. D-100／N1第三组：录制、管线和图像

执行基线 `a4ee3ad`。`session`、`pipeline`、`image` 的真实包／模块／实现归属迁至 `mlibs.*`，累计十二个基础库完成此步；另有此前交付的 `mlibs.control`。兼容与使用说明见[命名方案 §7](2026-11-05-product-namespace-migration-design.md)。机器证据在 [session／pipeline／image 报告](../../tools/ci/2026-11-05-session-pipeline-image-namespaces.report.omui.json)。

### 18.1 交付与兼容边界

- 三个原包内保留主入口、`.api` 与旧 import；没有新增 `mcpp.toml` 或 workspace 成员，成员仍为 129。九个实际接口／实现文件在还原 module／namespace 名称后与基线一致，未重写录制、管线或编解码算法。
- 新 API 分别为 `mlibs::session`、`mlibs::pipeline`、`mlibs::image`；旧 `omui::session`、`omui::pipeline`、`omui::imageio` 指向同一类型和实现。session 的 `openmui.session@0.1`、JSON 字段和文件后缀不变；pipeline 的插件／资源 ID 不变。
- pipeline 的旧限定名 `resource_type<T>` 特化继续可用。旧 namespace alias 不能重新打开，UI 的资源注册块已改为 `namespace mlibs::pipeline`；外部同类扩展也需此迁移。实际 fixture 检查旧限定名注册、新 API 使用、替换失败保持原管线、成功重配及共享 trace。
- 17 处消费者清单／模板的依赖键同步新身份；独立 hanim playback 的 PNG 采样直接使用 `mlibs.image`。UI／官网保留旧 import 验证渐进迁移。C ABI／规范子模块未改，用户已有锁文件原样保留。

### 18.2 本地实际验证

| 验证面 | 结果与范围 |
|---|---|
| Linux GCC 16.1 dev | 50 个不同测试程序，388 个套件用例；三库、hanim.engine、semantic、view、headless、Agent、Inspector、C ABI、官网；修改测试平台边界后另重跑 headless 两个程序 |
| Linux GCC 16.1 release | 同样 50 个程序、388 个套件用例；真实本机 IPC 的授权／来源检查保持通过 |
| Wasm／Node | 20 个含实际测试体的不同程序完成，另一个本机 IPC 程序明确跳过；套件报告 204 项，其中既有 H3 本机 IPC 分支也跳过，实际执行 203 项；没有声称 Web 支持本机 IPC |
| 源码／安装包 | 十二库分别真实打包；源码／静态安装包 × 新名／旧名／混用，共 6 个外部工程，每个首次及增量运行 12 项；每个依赖图为 12 库＋9 个外部依赖＋消费者，无重复 owner |
| 编译闭包 | 每个源码工程的编译闭包包含 17 个本仓实现单元；安装包工程不编译这些 `.cpp`。244 个第三方编译单元单列，版本、来源及合法身份摘要对照锁检查 |
| 独立 hanim | dev／release 各两组 61 时刻采样，共 244 张真实 PNG，反向回放、授权／撤销、拒绝导出检查通过；像素与 PNG 摘要全部等于前批样本，产品图未混入 UI／testing |
| 官网浏览器 | 新 Wasm 打包、实际原生 prerender／Agent 指南生成；Chromium 14 项交互检查通过，25 个固定状态与 PNG 摘要和之前反应工作台记录一致；实际查看中文窄屏与动画中间帧 |

原生用例数不包含 C ABI 的 C 程序内部原始断言数。报告同时保留重复运行、首次失败和明确跳过，不能把程序执行次数当成不同功能数。实速性能、跨设备 GPU、所有平台或整体美术达标不由这次改名回归推导。

### 18.3 发现的问题与下一步

- 首次扩大 Web 回归时，`headless/agent_policy_test` 要求真实本机 endpoint，当前 Web 的 IPC 返回不支持，因此失败。修正为按现有 `testing::threads` 能力明确跳过整个本机 transport 测试，不制造一项通过用例；原生仍必须启动真实 endpoint。Web 的 in-process 授权、离屏图像／录制、浏览器 Agent 入口仍实际验证。首次失败记录保留在报告中。
- 外部依赖审计初版误将默认命名空间的短锁键与限定锁键摘要直接比较。核查固定 mcpp `v2026.11.2.2` 源码后，确认摘要基于 `<namespace>:<lock-key>@<version>`，不是产物内容 hash。现在解析到同一身份后对照版本／来源，并复用 `mlibs.hash` 验证两种合法摘要，没有另写算法或改仓库锁。
- mcpp 安装包缺少传递依赖元数据仍未解决，本轮扩大到了 pipeline→trace、session→os／json、image→mwrap／codec。fixture 显式声明实际依赖，证明可链接与使用；正式发布前仍须修复并验证只声明顶层库的安装。尚未发布索引别名，不承诺 C++ ABI 或旧 CLI 选择器。
- N1 仍需 gfx／content 基础库，之后 N2 的 render／simulation／audio 与 N3 的 UI／标准连接按依赖推进。S1–S10、40 个公共动画／20 个完整作品、完整默认动效、PPet、多后端与性能预算继续开放。

最终全仓 lint 为零；变更 Python／TOML、本地文档链接与 diff 检查通过。以本地真实验证提交到同一 PR #1，其他平台 CI 异步反馈。

## 19. D-100／N1第四组：gfx 基础库与实际后端

执行基线 `33e1f73`。RHI、三个设备后端、CTS、raster、shader 及其构建规则共八个原包迁入 `mlibs.*`。**N1 尚未整体完成**，本批没有增加动画／效果或作品的功能计数。接口与生成链说明见[命名方案 §8](2026-11-05-product-namespace-migration-design.md#8-n1第四组gpu-契约光栅器和着色器工具)，机器记录见[本轮报告](../../tools/ci/2026-11-05-gfx-namespaces.report.omui.json)。

### 19.1 归属和兼容

RHI 父包、后端与 CTS 使用 `mlibs::rhi` 及其子空间；raster、shader 分别使用 `mlibs::raster`、`mlibs::shader`。同包 `.api` 保持唯一声明与实现，旧 module 导出原 API 别名。宿主 rules 独立于运行时 shader，旧 `openmui::shader::rules` 仍转发到同一构建规则。没有新增清单，workspace 仍为 129 个成员。

仓内依赖键、构建插件及生成 C++ 同步到新身份；`omui-shader` 命令、既有 `OMUI_*` 选项、JS／C 桥符号、wire 编码、规范子模块和元数据键保留。41 个实现／接口单元在显式还原 module、namespace、内存类型限定和生成路径后与基线一致；新转发单元和清单另行审查，不将此检查当作功能测试。RHI 复用同一 memory、hash，raster 继续复用 geom 与 hanim.core。

### 19.2 本地验证

| 范围 | 结果与边界 |
|---|---|
| Linux GCC 16.1 dev／release | 每个配置最终选集 65 次测试程序执行、460 个具名用例通过；覆盖三个 gfx 库、SDL 后端及 hanim.engine、render、vector、view、headless、agent、inspector、C ABI、官网。不同包可有同名测试文件，不以文件名去重或当作功能数量 |
| Wasm／Node | RHI、raster 的 4 个程序、32 个用例实际执行通过；shader 编译器本身在原生宿主验证，未声称可在 Wasm 内运行 |
| 外部源码／静态包 | 十五个 mlibs 库和未改名的 hanim.core 共 16 个真实 pack；6 种消费组合，各 15 个用例，首次和增量运行均通过。每个实际 graph 为 26 个身份，没有 UI 或重复包；安装包消费没有编译本仓库的实现 cpp |
| 外部构建插件 | 新名／旧名／混用 3 个仓外 build.mcpp，实际生成、编译、读取两个 shader entry 和三种格式；三份生成分区摘要相同 |
| 原生 Vulkan | 无窗口的 SDL offscreen＋`OMUI_REQUIRE_GPU=1`，lavapipe／llvmpipe 软件设备实际执行 19 项 RHI CTS；元胞 16 场景、266 步、168 次比较／61320 个值、129 个校验通过，过程摘要 `0769cee00d2cbf3a` |
| Chromium WebGPU／WebGL2 | 各跑 19 项 CTS；WebGL 的 compute 项验证明确 unsupported，不算计算执行。WebGPU 元胞过程与 CPU 对齐；真实宿主页面与 CPU 比较均为最大差 4、76800 像素中 64 个差值超过 1。25 个生成分区的 33 个 WGSL、30 个 ESSL entry 完成 70 项 shader 检查 |
| 独立 hanim | 无 renderer 的创作 API：dev／release 各 104 项检查；带公开 renderer adapter 的播放消费者：244 张过程 PNG、逐帧反向和授权验证通过，采样时间／像素／PNG 与上批逐一一致 |
| 官网真实交互 | 重新生成 Web 包、原生 prerender 与 Agent 指南；Chromium 14 项交互通过，25 个固定画布状态和 PNG 与既有反应工作台记录相同。实际查看中文窄屏、反应中间态和 hanim 中间帧 |

所有构建、测试和打包使用 mcpp；本机生成工具以仓库锁重新构建，再通过 mcpp 支持的工具路径配置供消费者使用。原有用户 `mcpp.lock` 修改保持原样，没有进入此提交。未等待其他平台 CI；没有给出硬件 GPU、macOS／Windows／手机的新验收或性能提升结论。

### 19.3 保留的问题与边界

- 初次 shader 测试有一条字符串断言仍期待旧 `omui::rhi` 生成文本，更新为真实的新 API 后 11 项全部通过。初次 SDL 运行未选择离屏 video driver，在无显示服务的本机初始化失败；显式使用 offscreen 后仍要求设备成功，没有跳过设备测试。
- 浏览器第一次 WebGPU canvas 呈现报 `Device was destroyed`；既有脚本以 Vulkan／SwiftShader 参数重试同一测试后通过，未使用 offscreen 降级分支。该环境依赖保留在报告中，不写成首次运行全绿，也不将软件设备结果描述为显卡性能。
- 外部消费者的运行断言先已通过，但编译源审计遇到 shader 生成的 provisioned 分区和同包 CLI 入口。审计现仅接受准确生成路径，单列工具源并检查不进入消费程序 link；同时修正 Ninja 空白解析。没有放开任意生成目录或把 CLI main 算入库实现。
- mcpp pack 的依赖元数据缺口继续存在，二进制 fixture 显式列出依赖；shader 的 provisioned Slang 路径尚未验证跨机器重定位。这些结果不能宣称单个顶层包安装或正式索引发布完成。namespace alias 不能重新打开，旧扩展块需使用新的实际命名空间；C++ 使用方需要重建。
- 官网整体截图含实时时序，只对固定画布与动画采样做逐字节比较；没有因此认定实时节奏、美术、全部默认动效或 40／20 已达标。下一依赖组为 content，随后是引擎和 UI／标准连接。

本批收尾检查为全仓 lint 零问题、TOML／Python／JS／shell 语法、本地文档链接与 `git diff --check`；以同一 PR #1 的 Signed-off-by 检查点保存。

## 20. D-100／N1第五组：content 与字体交付

执行基线 `8005848`，与下面的 Hanim 包收敛组成同一检查点。text／layout／markdown／i18n／i18n.cldr 五个既有包迁至 `mlibs.*`，同包旧入口仍导出同一声明／类型／状态。39 个原接口与实现单元仅有明确的 module／namespace／依赖限定调整，字体文件与 CLDR 数据未改变。没有为 api／legacy 新建清单。证据见[content 与 Hanim 检查点报告](../../tools/ci/2026-11-05-content-hanim-consolidation.report.omui.json)。

### 20.1 实际交付和测试

- 五库在 Linux GCC 16 dev／release、Wasm／Node 各通过 8 个程序、77 项套件用例。
- 字体打包器同时接受 `mlibs.text` 与旧 `openmui.text`，拒绝其他 owner 的同名包。真实 graph → 打包 → 重新载入 Inter 字体并检查字形的新增回归通过；gen 测试为 5 个程序、33 项。首次官网打包仍指向旧工具，改用 mcpp `--print-launch` 返回的新产物路径后成功，未用 `--fonts-dir` 绕过真实 graph。
- content 迁移后的 native 消费者选择在 dev 为 43 个程序／315 项，release 含 gen 为 48 个程序／348 项；覆盖 view、headless、Agent、Inspector、C ABI、官网。Hanim 收敛后，另运行 view＋canvas／markdown／timeline／window＋expr，在两个 profile 各通过 17 个程序／179 项。选择之间有重复，不相加当作独立功能计数。
- Wasm 消费者报告 24 个程序正常退出、220 项套件用例；本机 IPC 程序和相关用例明确跳过，不算 Web 本机 IPC 验收。Hanim 自身另见 §21。
- 官网重新构建 Wasm、原生 prerender 和 Agent 指南，Chromium 14 项交互检查通过，25 个固定状态及真实 PNG 摘要与之前反应工作台基线一致。实际查看窄屏中文、三语文字和弯曲变形中间帧；这不是新增美术或实时性能达标声明。

### 20.2 独立包与宿主／目标边界

namespace consumer 真实打包 20 个 mlibs 库，加上 hanim.core、render、render.vector，共 23 个库。源码／静态安装包 × 新名／旧名／混用六个外部工程，各实际执行 20 项并重复运行。新增视口重排、Markdown stream／HTML、Locale／CLDR 共享格式化、拉丁／中文／阿拉伯文字 PNG；六种方式及其重复运行的像素完全一致。三个独立 shader rules 构建工程也通过。

target 图要求精确的 23 个本仓库包＋17 个第三方包＋消费者。embed／plan／rules／gen 只允许作为四个已知 host build-program 实例出现，不能把宿主工具误算为 target 依赖，也不允许其源文件进入 target 编译。生成接口限定为实际 owner 的 provisioning、绘图／effect shader 与四个 locale 分区；每个检查 module 声明并记录摘要。此前只枚举绘图 shader 的审计遗漏已修正。

真实安装包暴露 GNU ld 的 CLDR → i18n 链接顺序失败。原因是当前 mcpp pack 没有保留本地 path 依赖边。fixture 现在显式声明实际依赖与归档 rescan group，没有修改包／接口摘要、没有强制链接全部对象或重新编译本仓实现。这证明**显式集成的安装包可用**；自动传递依赖、正确自动排序、只声明顶层库的安装与 shader 工具重定位仍属于 N4 发布缺口，不能由本轮结果抵消。

## 21. D-99：Hanim 产品收敛与真实按需选择

维护者明确：内部独立子项目必须有具体库／引擎价值和理由。多模块互用本身不是拆包理由；局部职责与全局消费者、可选依赖边界交叉评估。该要求已补入 AGENTS.md 和局部×全局设计准则。

### 21.1 代码树与 API

```text
modules/base/hanim/core/     # 独立 std＋mlibs.math 库；motion/raster/timeline 共用
modules/gfx/hanim/
  mcpp.toml                 # 一个动画产品清单
  build.mcpp                # 从已解析 features 生成 hanim:selected 导出分区
  src/
    hanim.cppm
    object/ effect/ library/ program/ engine/
    render/vector/          # 可选 renderer adapter
  tests/
    compatibility_test.cpp
    object/ effect/ library/ program/ engine/
tools/ci/hanim-play-consumer/tests/  # 三个真实渲染集成测试
```

移除六个微型清单，workspace **129 → 123**。12 个搬迁的实现／接口文件逐字节不变；公开 `hanim.object/effect/library/program/engine/render.vector` 仍是原 C++ Modules，由一个包管理。旧子包依赖键和 `-p hanim.engine` 等选择器迁为 `hanim`＋feature／测试路径；不保留空壳 manifest。产品默认仍支持 `import hanim;`、简单动画创作和高级组合编排，共用原有 compiler/player。

`hanim.core` 保留独立清单，README 写明三个真实共享消费者与 std＋math 依赖收益。renderer adapter 的可选性由同包 feature 实现，因此无需额外清单。UI view 明确关闭默认 feature、仅选择 effect；bench 和独立用户选择产品入口。

### 21.2 真实 feature 闭包

| 选择 | 选中内部模块 | 产品源码数，不含生成入口／core | target 包数，含消费者 |
|---|---|---:|---:|
| 关闭 defaults，无额外 feature | core 公共导出 | 1 | 4 |
| effect | effect | 3 | 6 |
| object | object | 3 | 7 |
| library | object＋library | 5 | 7 |
| program | object＋library＋effect＋program | 9 | 7 |
| 默认 engine | 上述＋engine | 11 | 7 |
| 关闭 defaults，render-vector | object＋library＋effect＋program＋render/vector | 11 | 13 |

七个独立工程均经公开 `import hanim` 实际执行，检查精确源码集合、生成导出和依赖闭包，重复 locked 运行。effect-only 无 object／player／renderer；默认完整动画无 renderer／RHI；vector adapter 可不带 player。跨依赖边的 feature 是并集，应用中另一消费者显式启用 engine 后，不能仍把整应用描述成 effect-only。

源码按 feature 裁剪，安装包则固定打包时选择。默认产品真实打包为七个接口和预编译库，外部工程运行全部 104 项公共 API 检查，未编译原产品 `.cpp`。当前 pack 的本地依赖边限制仍要求显式 foundation 依赖；不承诺安装同一二进制包后重新裁剪 features 或单顶层依赖发布完成。

### 21.3 验证与边界

- renderer-free 的八个程序：native dev／release 各 78 项，Wasm／Node 76 项；并发能力不同按原契约处理，没有放宽数值或像素断言。
- 独立创作 API：dev／release 各 104 项；默认产品不依赖 UI、spec、testing、renderer 或窗口。
- 外部 renderer 集成：两个 profile 各三个测试程序／18 项，覆盖矢量效果、中间态、组合 scope、无 GUI 采样与失败原子性。测试另建临时消费者，testing 不进入生产消费者图。
- 播放／Agent 消费者：两个 profile，各两组 61 时刻、反向重放及授权／撤销／拒绝输出，总计 244 帧；pixel 与 PNG 摘要全部匹配前检查点。没有增加第二套渲染器、对象插值器或 Agent 操作逻辑。
- 全仓 lint、变更 TOML／Python 语法、Markdown 本地链接及 diff 检查通过；新增 feature／安装包门禁接入 CI。本地结果作为检查点依据，不等待其他平台 CI。

这些证据证明包收敛、选择边界与既有行为保持，不证明冷构建更快、体积优化比例、新动画功能、硬件 GPU 性能或整体视觉质量。其余微型清单继续逐项评估，不机械全并；N2 引擎与 N3 UI／工具／规范命名、N4 发布、S1–S10、高级 hanim IR／GPU、40／20 的公共可用性及质量、PPet 与多后端完整目标仍开放。

## 22. D-100／N2首组：渲染引擎、效果加载和实际输出

基线 `5947869`。三个运行时包及桌面 CTS 迁至 `xengine.render`／`.vector`／`.effect`／`.cts`。公开 C++ 类型归 `xengine::render`，动态效果加载 API 归 `xengine::render::effect`；同包保留旧 import 和 `omui::render`／`omui::effect` alias。细节见[命名方案 §10](2026-11-05-product-namespace-migration-design.md#10-n2首组渲染引擎及可选效果加载)，机器证据见[本轮报告](../../tools/ci/2026-11-05-render-engine-namespace.report.omui.json)。

### 22.1 边界与兼容

25 个原接口／实现单元仅有身份和底层类型限定变化，渲染、混色、damage、效果与 shader 算法未重写。原始 Slang 文件、模板名 `openmui_effect.slang`、effect ID／参数、数据后缀、RHI wire、UI C ABI 和标准子模块未变。模板经当前 omui-gen 真实重生成，字节与期望一致；生成的 shader 分区归 `xengine.render.vector.api`。Hanim adapter 与 text 直接用新 renderer 入口；UI／官网用旧入口验证渐进兼容。

本轮保留四个清单，workspace 仍 123；README 写明理由，并在三种真实源码消费者的 resolver 图中审计 target 边，排除 host-build 边：

| 边界 | 本机 target 依赖闭包，含自身 | 保留理由 |
|---|---:|---|
| render graph／pool | 5 | 自身＋RHI／memory／hash／trace；完整的设备 pass／纹理池任务，不带 vector、hanim、raster、shader。实际设备 CTS 直接记录图并读回纹理 |
| vector | 10 | 完整的显示列表、资源、damage、CPU／GPU 渲染；Hanim、文字、UI、工具均直接消费；运行时没有 effect loader／Slang 编译／TOML |
| dynamic effect loader | 17 | 原生宿主选用文件轮询、TOML 与运行时 shader 编译；与内置效果分开，复用同一个 registry 和 renderer |
| CTS | 不进入普通运行时 | SDL／桌面设备验证；portable CPU 测试继续支持 Web，未造空运行时库 |

内部 pass、paint、效果实现与解析辅助不各建包。旧 C++ 名字在重建后可用，namespace alias 不可重新打开；旧 `-p openmui.render.*` 选择器不属于兼容保证。没有承诺 BMI／C++ 二进制 ABI 不变。

### 22.2 本地真实验证

| 范围 | 结果与限制 |
|---|---|
| Linux GCC 16 dev／release | 每个 profile 77 个选中程序、551 项套件用例；包含 graph／vector、实际设备 CTS、动态效果、text、hanim、gen、view／motion／theme、headless、C ABI、Inspector、官网 |
| Wasm／Node | 40 个程序、332 项套件用例；graph／vector、text、hanim、view／motion。原有线程能力分支保留，不据此宣称 Web 已支持共享内存多线程 |
| 本机设备 | `OMUI_REQUIRE_GPU=1`＋SDL offscreen，缺设备必须失败；4 个设备程序共 11 项，另动态效果 3 项，包含图剔除／纹理读回、paint mix、效果输出边界、实际 shader 热更新。保留原 500ms 断言；不作物理 GPU 性能声明 |
| 浏览器 GPU | WebGPU／WebGL2 各 19 项 RHI CTS、host.web 实际画面读回；WebGL compute 检查明确拒绝 unsupported。33 个 WGSL、30 个 ESSL、25 个生成分区共 70 项 shader 编译／链接检查 |
| 24 个静态包 | 20 个 mlibs 库＋hanim.core＋三个 renderer 库。源码／安装包 × 新名／旧名／混用六个真实工程，各 23 项、重复运行；类型和 registry 唯一、CPU 效果中间态一致、真实 Slang 资产加载与注册 |
| 独立 Hanim | 七种 feature 组合及默认安装包的 104 项创作检查；播放／Agent 授权、拒绝输出、反向重放和三个渲染集成测试继续通过。244 个过程 PNG／pixel 摘要与上一检查点一致 |
| 官网 | 重新构建 Web、原生 release prerender 与 Agent 指南；Chromium 14 项交互通过，25 个固定状态／PNG 与反应工作台原基线一致；实际查看中文窄屏与中间帧 |

总数按选中程序／套件报告计，不把 C ABI C 程序的内部原始断言另算套件用例，也不把外部重复运行当作更多功能。每帧采样不自动证明实速节奏、美术或所有平台已达标。

### 22.3 发现的问题与剩余工作

- WebGPU 默认 headless 呈现首次仍报 `Device was destroyed`；现有 runner 用 Vulkan／SwiftShader 参数重试后同一真实呈现检查通过，未改为跳过或关闭 GPU。报告同时记录首次失败与最终路径。WebGL2 呈现通过；物理设备／其他平台继续由相应门禁验收。
- 浏览器 shader 收集器原本会捞到旧命名留下的生成缓存；现按当前 workspace owner 过滤，再对该 owner 最新的 Web 分区检查，避免过期 shader 覆盖当前结果。
- 扩大 namespace fixture 时，初版误在 shader rules 段复制了不需要的效果文件步骤，并留下旧的 20 项结果计数。修正脚本语法与计数为 23；实际断言未删减。后续 source／archive 六种工程及三个 rules 工程全部通过。
- mcpp pack 本地 path 依赖边和归档排序缺口仍按 §20.2 处理；显式依赖及 GNU archive group 的消费证明不等于顶层单依赖发布完成。源码 closure 审计与安装包兼容是不同证据。

最终 lint、TOML／Python／shell 语法、文档本地链接、生成器一致性和 diff 检查通过；用户既有锁文件不提交。该检查点完成 N2 的 renderer 组，simulation／audio／timeline／video、N3 UI／工具／标准连接及 N4 发布仍继续。没有增加 3D 能力、动画数量、40／20 质量或 S1–S10 的完成计数。

## 23. 模拟的唯一归属与下一条功能主线

基线 `9893ab9`。模拟时钟、CPU 元胞规则、可选 RHI 执行和跨设备 CTS 迁到 `xengine.simulation*`；共享同一实现，保留同包旧 import。`mgame.sim` 只剩约束与转发，已并入时钟包的 `src/legacy_mgame.cppm`，删除其独立清单，workspace 为 122。边界与清单迁移要求见[命名方案 §11](2026-11-05-product-namespace-migration-design.md#11-n2第二组模拟归属与游戏时钟空壳收敛)，证据见[机器报告](../../tools/ci/2026-11-05-simulation-namespace.report.omui.json)。

### 23.1 实际可用性与本地验收

- Clock／Timeline 与 CPU 规则的实际 target 闭包各为三个包（自身、log、trace），两者互不强制依赖。GPU adapter 为七包，CTS 为八包，均不带 Clock、UI、renderer 或游戏运行时。
- 原生 dev／release 各五个程序、38 项，覆盖时钟、原 mgame 三项兼容测试、规则、游戏 UI 和 breakout；Wasm／Node 四个程序、36 项。Inspector 与官网 dev 共 22 个程序、138 项。
- 源码／静态包 × 新名／旧名／混用六种仓外工程，各 26 项；共 28 个归档，三个真实 shader 构建插件消费者。直接 `import mgame.sim` 在安装包中也通过，Game 对 State 的原约束未放宽。旧包依赖键不兼容，必须改为 `xengine.simulation`，不额外造兼容空包。
- 必须实际创建设备的 Vulkan dev／release 各两个程序、10 项；CPU／Vulkan／WebGPU 比较 16 场景、266 步、61,320 个值，过程摘要均为 `0769cee00d2cbf3a`。WebGL 的 compute 仍按实际能力报告 unsupported，不计作 compute 实现。
- 当前 release Inspector 在移除 DISPLAY／WAYLAND_DISPLAY 后导出 61 张真实 PNG、时间化 WebP、工程与过程报告；PNG 全部匹配原工作台基线。新进程重载后演化到 tick 62，与从头运行一致；缺参数及不存在文件返回结构化失败与非零退出码。
- 重新构建官网 Web，当前 native dev 产物执行 prerender 和 Agent 指南导出；Chromium 的 14 项交互检查通过，25 个中间态状态与 PNG 匹配原基线。没有将此次检查称作官网美术或整体动效已经验收。

### 23.2 问题、限制与优先级调整

SDL 首次测试发生 SIGSEGV；GDB 显示同进程混入 LLVM 20 与 LLVM 22。沿用 `tools/ci/gpu.sh` 的 Vulkan loader／lavapipe ICD 选择后，GDB 只见 LLVM 22，同一测试正常退出。设备为软件 llvmpipe，不作物理 GPU 性能声明，未删弱断言。

simulation 预算测试本轮未执行：root locked 解析之后，fighting 的宿主构建临时工程缺六个图像／TOML 锁项，直接重建该临时工程仍在下一次规划时复现。用户的根锁文件没有修改或提交，原预算没有放宽；此项是构建工具门禁待处理，不能据此报告性能改善，也不阻塞已验证的功能推进。

**后续优先级改为真实功能交付。** 命名迁移继续作为配套工作，下一条主线为独立 Hanim 的公开位移／属性动画、组合与 effect、播放控制、过程画面以及官网真实消费。现有 41 个官网基础场景不等于 41 个独立公共 API，20 个组合也仍需按复用性和视觉质量验收。完整属性通道／GPU 编排、UI 默认动画迁移、AI／PPet、后端、发布与工具仍未完成；本轮不增加这些目标的完成计数。

## 24. 公开属性动画：五个简接口与同一条执行链

基线 `6d3a64f`。本轮新增可由框架外程序实际调用的能力，代码仍在 `modules/gfx/hanim/src/{library,program,engine}`，没有新建清单。公共用法见 [Hanim README](../../modules/gfx/hanim/README.md)，可复现的本地证据见[属性动画报告](../../tools/ci/2026-11-05-hanim-property-recipes.report.omui.json)。

### 24.1 简接口、组合与高级入口

新增五个具名配方：`anim::move_to(object, position)`、`move_by(object, offset)`、`rotate_to(object, radians)`、`scale_to(object, scale)`、`fill_to(object, color)`。ObjectRef 在 prepare 时捕获，Snapshot 保持冻结；均可直接交给 `hengine.play`，支持 duration、easing、过程路径 effect 和图像 effect。

```cpp
auto playback = hengine.play(
    hanim::anim::move_to(card, {120, 40}).duration(300ms)
        .with_effect(hanim::fx::translation_path(hanim::PointPath::bend(.25))));

auto animation = hanim::sequence(
    hanim::anim::move_to(card, {120, 40}),
    hanim::anim::move_by(card, {20, -10}),
    hanim::anim::rotate_to(card, std::numbers::pi));
auto prepared = hengine.prepare(animation);
```

`anim::to(object, PropertyTargets)` 是多属性入口，可同时指定 position／rotation／scale／fill，或用 `part` 指定稳定部件 ID。默认操作 HObject 的唯一根部件，没有硬编码 `root` 名字。位置是父坐标系中的局部位置，旋转／缩放围绕局部原点；角度保留显式圈数，不擅自改成最短方向。高级入口不另计为一种新动画。

顺序编排从上一段**实际结束姿态**派生目标：先移动，再旋转时不会重置位置；`move_by` 从实际姿态累加；形变与属性动画可以前后组合。未指定的字段、子部件局部变换和几何保持。显式 snapshot 模式仍可选择重置起点。

`retarget(playback, PropertyTargets)` 从当前画面生成新目标，保留暂停意图、速度幅值及当前 effect 参数；非法目标不替换旧计划。与 morph retarget 共用一条替换实现。这里承诺现有 C0 姿态连续，不据此承诺速度／加速度连续。

### 24.2 复用与边界

- `anim::BoundAnimation` 统一形变和属性叶子，旧 `anim::Morph` 为同类型别名；Program contract 升到 v3。同一 compiler／player、时钟和缓冲处理所有配方，未另造一个动画调度器。
- 变换复用 hanim.core 的分解插值，颜色复用现有 OKLab 路径；纯属性且没有顶点路径效果时，几何准备一次，热路径复制固定 cubics。显式顶点 effect 仍走原几何采样器。准备阶段继续使用既有严格轮廓 compiler，不宣称冷准备已达到最优。
- 不修改宿主对象，不为属性终点创建虚拟 ObjectRef；对应 PreparedObjectMorph 的 `to()` 为空，source identity／revision 保留。输出供 renderer／adapter 消费，业务状态的写回仍由宿主契约决定。
- 当前对象域仍为已有闭合矢量 HObject，开放路径、任意应用通道、相机、骨骼没有在本轮完成。同一对象的并行叶子仍按整对象拒绝冲突；同时修改多个字段用一个 `anim::to`，不把这一点宣传成逐通道混合已实现。
- renderer 继续显式 feature 可选；纯动画库不拉入 UI、窗口或模型。Agent 检查通过既有 mlibs.control／openas adapter 使用同一 Engine 和离屏 renderer，未新增另一个权限系统或宣称通用 provider 注册已经完成。

### 24.3 本地证据与下一步

原生 dev／release 各八个测试程序、83 项，Wasm／Node 81 项；原有线程能力分支保留。新增检查覆盖真实顺序端点、指定部件、形变／属性混排、非法输入和开启 trace 下的过程采样零额外分配。独立 `import hanim` 程序 dev／release 各 119 项，包含五段动画的 301 时刻、当前姿态重定向及失败不变；实际静态安装包同样通过 119 项，七种 feature 闭包继续通过。

renderer 的两个 profile 各 18 项集成用例继续通过。新增属性组合由同源 CPU renderer 各采样 61 帧，并反向重放 61 帧，dev／release PNG 一致；另有 Agent 授权、只读、dry-run、撤销与拒绝像素输出检查。实际查看 0／15／30／45／60 帧，平移、旋转、缩放与所选 tone／blur 连续呈现，参考视口未见裁剪。既有形变的 244 个过程 PNG／pixel 摘要均匹配前基线。采样不替代实速节奏、美术或硬件性能验收。

**下一检查点必须补真实消费者接入。** UI 默认动效仍主要使用 hanim.core；本次五个新配方尚未进入组件默认动画和官网主舞台。先做可复用 UI／HObject adapter，并接一组组件状态与官网交互，再按公共库补足 ≥40 基础动画和 ≥20 创意组合的真实使用与视觉质量。不能用五个函数的参数排列凑数量，也不将已有官网场景数量等同本轮新增公共能力。属性通道仲裁／混合、GPU 资源编排、后续引擎／AI／PPet／工具与发布目标继续保留。

## 25. 配置式局部控制的边界修正与多部件属性目标

基线 `033cf2e`。维护者进一步明确：整体／局部／文字／子组件的动画选择与简化必须可配置；2D／3D 可视源应按需提供属性、部件、几何／顶点和采样表示；通用几何转换属于共享基础设施，Hanim、renderer、模拟与 UI 共同消费；分级同时包含性能预算、平台适配和许可降级。详见[新增方案](2026-11-05-hanim-representations-selection-policy-design.md)，核心约束已加入 AGENTS.md。

原先准备的整节点独占变换原型固定了 UI／Hanim 的处理边界，无法表达这些组合要求，已从工作树撤回。其临时构建也未完成验收，不计作 UI 接入。§24 的消费者目标继续有效，按新方案的 A1–A5 补齐必要基础，不将错误边界包装成完成的 adapter。

### 25.1 A1 实际实现

公开 `PropertySet` 允许**同一个动画、同一时序**配置多个部件及独立属性：

```cpp
auto animation = hanim::anim::to(object, hanim::PropertySet{
    {.position = hanim::Vec2{120, 40}, .part = "body"},
    {.rotation = std::numbers::pi, .part = "icon"},
    {.fill = hanim::Rgba{0, 0, 1, 1}, .part = "icon"}
}).duration(600ms);
auto playback = hengine.play(animation);
```

未选择的局部字段保持源值，仍遵守父子变换继承；这不等于世界空间固定。不同属性可以组合，指向同一实际部件同一通道的重复写入返回 `property-conflict`，不按声明顺序静默覆盖；空根选择和根部件 ID 的别名也不能绕过冲突。位置与相对位移属于同一通道。

`retarget(playback, PropertySet)` 将所有目标从当前完整姿态派生，再一次性替换；未写入的属性保留，失败不部分发布。旧单目标是同一编译／采样实现的一元素形式，旧简接口保持。Program contract 更新为 v4；没有新增包、UI／renderer 依赖或第二套插值器。

### 25.2 验证

机器证据见[本轮报告](../../tools/ci/2026-11-05-hanim-selection.report.omui.json)。Linux dev／release 各八个程序、85 项；Wasm／Node 83 项。独立 `import hanim` 源码消费者 dev／release 各 128 项，实际默认安装包也通过 128 项，七种 feature 闭包保持通过。

同源 CPU renderer 的多部件场景在两个 profile 各生成 61 张 PNG，左侧部件移动、右侧部件旋转／换色、未选徽标不变；徽标实际像素区域的摘要在整个过程中严格一致。61 帧反向重放一致，dev／release 图像一致，实际查看首帧／中点／终帧。既有形变与属性动画共 366 帧保持原基线；两个 profile 的 renderer 集成各 18 项通过。Agent 的只读、dry-run、委派／撤销边界及拒绝视觉输出检查仍走共享控制层。

### 25.3 后续范围

本轮 A1 不等于独立通道的并行时间／混合，不等于 hold-world，不是 UI、3D mesh／骨骼或像素场已经实现。下阶段必须把选择／读写集、继承／保持与多表示契约用于真实组合组件／官网场景。组件默认动效也是可覆盖的库配方；内容、排版与输入的 owner 不成为动画处理的固定分区。

共享几何数据／转换、渲染设备表示、Hanim 的时间控制和模拟状态演化按新方案分工。性能分级的契约已明确，但没有新增硬件吞吐、自动降级或成熟 3D 支持的完成结论。其余 S1–S10、40／20、PPet、工具、Web／Toy／TUI、多平台与发布范围保持开放。

## 26. 独立属性通道：保留编排与单一对象输出

基线 `6b24f56`。A2 第一批将同一个 HObject 的位移／旋转／缩放／填充通道接入独立时序，代码仍在 Hanim 所属的 `src/{library,program,engine}`，没有新清单或 UI 依赖。公共配方、低层 Program 和高级离线采样共用一条编译／求值路径。可复现证据见[本轮报告](../../tools/ci/2026-11-05-hanim-channels.report.omui.json)。

### 26.1 实际接口与执行语义

```cpp
auto animation = hanim::composite(hanim::parallel(
    hanim::anim::move_to(card, {120, 40}).duration(600ms),
    hanim::anim::to(card, {.rotation = std::numbers::pi, .part = "icon"})
        .duration(250ms).easing(hanim::ease::out_quad)))
    .with_effect(hanim::fx::blur(0, 2));
auto playback = hengine.play(animation);
// 成功后：hengine.plan(*playback) 返回可保留的不可变计划。
```

这里 `card` 是含稳定 `icon` 部件的 HObject，不表示任意 UI Element 的转换已经交付。同一对象的不同通道可以并行；同一通道的多段可以串行，并从前段实际终值继续；其他通道按自身时间继续。父级变换仍作用于子部件，未写入局部字段保持。位置／缩放当前各是一条 Vec2 通道，尚不能分别仲裁 x／y。

编译器把真正同时作用的属性叶子合成一个对象输出。原始 `nodes()` 层级、父子关系、时间及作用域保留；贡献记录关联原节点／路径，通道记录包含部件、属性、起止、曲线、位置路径、来源与边界锚定。`hengine.plan` 在简接口播放后也可查询，已取得的计划副本在重定向／释放后仍有效，失效播放句柄拒绝新查询。多通道没有单一 eased progress，`PlaybackInfo.sample_progress` 对此保持空值。

高级 `PreparedObjectAnimation::property_timeline` 提供独立于播放器的可复用计划；原 `PreparedObjectMorph` 名称保持同类型兼容。显式 clip 源快照只提供选中局部字段，不替换几何；Program 的 `source(snapshot)` 则继续表示整对象重置，两种语义分开。普通零时刻执行作者的即时变化／阶跃；`sample_source`、cancel/restore 恢复原始来源。

### 26.2 冲突、效果与性能边界

- 相同部件／通道的重叠写入拒绝；并行的零时长冲突不能因前面存在串行前缀而被掩盖。作者声明的串行屏障保留，包括并行组之间的即时形变。
- 并发属性叶子共用同一 image scope，使用 `composite(parallel(...)).with_effect(...)`。不同 scope 或叶子独立图像效果返回 `scope-conflict`，不会通过重复绘制整个对象伪造局部效果；共同作用域可嵌套。位置路径仍属于选中的位移通道；并发属性叶子的顶点路径当前明确拒绝。
- 静态几何只准备一份；按部件／通道的分段有序存储，采样二分查找当前段，复用 core 插值。所有活动字段预检通过才发布，失败不部分修改调用方缓冲。通道预算与节点／几何／效果预算分别约束。普通单段继续使用已有轻量路径。
- 普通正时长编排在排序后线性分组；混合零时长组仍有冷准备的两两依赖检查成本，由输入上限约束。未将其宣称为大规模编排已经优化完成。分级优化、平台适配、许可降级和各阶段 profiling 继续遵循[性能契约 §1.3](2026-11-05-hanim-representations-selection-policy-design.md#13-分级天然包含性能契约)。
- `retarget` 从完整当前合成姿态替换单对象计划，未选字段保持此刻值；旧计划中未选通道的后续调度不会继续。选择性替换、add／blend、hold-world、跨播放通道仲裁仍是后续工作，不能将当前姿态连续等同这些能力。

### 26.3 本地验证与官网映射

原生 dev／release 各八个程序、96 项；Wasm／Node 八个程序、94 项。独立公共源码消费者 dev／release 各 143 项；真实默认静态安装包同样通过 143 项，七种 feature 闭包通过。新增检查覆盖独立曲线、串行子分支、形变前后衔接、即时边界、预算／作用域失败、当前姿态重定向、不可变计划生命周期，以及开启 trace 后 1,000 次预热采样无额外分配。Program contract 为 v5，旧公共 import 保留。

同源 CPU renderer 在两个 profile 各通过 20 项集成检查；同一对象的并发动画加共用 blur 与独立构造的参考姿态逐时刻像素一致，透明填充也只画一次；预热求值与作用域记录没有额外分配。新场景各生成 61 张真实 PNG，并按逆序重新采样 61 帧。左部件移动、右部件两段旋转并换色，未选徽标的像素区域摘要全程一致。dev／release PNG 相同，已查看 0／15／30／45／60 帧；既有四种场景共 488 帧的像素／PNG 摘要保持前基线。Agent 授权、只读、dry-run、委派／撤销及拒绝视觉输出门禁仍使用共享控制层。

官网中英核心概念文档已提供公共属性、独立通道和共用效果用法，标明 UI 接入边界。文档高度变化影响回放进入文档后的 39 帧，前 20 帧保持原摘要；查看真实中间画面后更新像素基准。没有把文档示例当作官网主舞台或组件默认动效迁移完成。

官网原生共 20 个测试程序、129 项：其余 19 个程序先通过，更新文档回放基准后单独复验 `site_test` 的 37 项全部通过。Wasm／Node 的同一 59 帧回放也与原生基准一致。第一次 Web 命令误用默认 profile，内存文件系统读不到仓库字体而失败；切换项目已有 `wasm-test`／NODERAWFS 后通过，没有修改断言或另设 Web 像素基准。最终 lint、Python／JSON／文档链接与 diff 检查通过；用户已有锁文件保持原样且不提交。

下一条主线仍是共享表示／选择策略与可配置 UI adapter 的真实纵向接入，让同一组合组件支持整体、部件、文字、保留和简化，并映射到官网的可交互舞台。通用几何／顶点转换、3D／骨骼／采样状态、GPU 资源编排、低端设备实测、40／20 质量与其他系统目标均保持开放。本轮没有新的硬件吞吐、实时节奏或整体美术验收结论。

## 27. 共享路径基础与公开几何捕获

基线 `bfe8754`。本轮推进 A3 的共享 2D 数据边界，同时让既有 UI／官网路径形变实际消费公开入口。没有把某类 UI 内容固定交给不同播放器，也没有为内部职责增加清单。证据见[共享路径报告](../../tools/ci/2026-11-05-shared-path.report.omui.json)。

### 27.1 单一归属与真实消费者

`mlibs.geom` 现在拥有 `PathVerb`、`PathView`／可写视图、`FillRule`、路径校验与读取，以及从 raster 移入的 `PathPolylines`／曲线离散化。现有值类型放到包内 `primitives` 分区，路径放到 `path` 分区／实现文件，公开 import 不变。geom 仍只依赖标准库；Hanim core 仍只依赖 std＋math。

renderer／raster 的旧名字为同一类型的别名，CPU/GPU 渲染器不再将路径命令复制到另一种枚举数组。光栅器的 `flatten` 继续转发同一几何实现，其职责保留 coverage／stroke／compositing。离散化的浮点运算顺序及既有 256 段上限保持，不借重构改变画面或提高精度承诺。源代码需重新构建，不承诺旧 C++ 二进制 ABI。

共享 `inspect_path`／`PathReader` 在暴露命令前校验完整编码、有限坐标、操作数数量、轮廓起点与容量，错误指向源命令／点；读取器不分配，借用期要求输入存活且不变。`close` 后继续绘制会从此前轮廓起点开始新轮廓，空移动轮廓及重复 close 的原有语义保留。结构合法不等于拓扑合法。

公开 `hanim::import_path` 接受上述路径视图，拥有实际 line／quadratic／cubic 控制点与开闭信息；float 坐标精确扩展为现有 double 表示，不先变成采样多边形。结果的每个 Contour 附带源命令／操作数范围及分段对应命令，宿主再关联自己的对象 ID／版本。原数组修改或销毁不会影响捕获。导入预算在转换前校验，仍保留 Contour 每轮廓 65,536 段的上限；没有创建临时业务对象或窗口。

两个实际入口已使用它：UI 原有路径 morph 删除私有 path→ShapeContour 解析器，通过公共捕获后交给现有 core shape plan；独立 CLI 消费者直接将 renderer 的真实三角形 Path 捕获为 HObject，继续使用公共编排、player、共享 renderer 与授权像素导出。UI 只选择 Hanim 的 object feature，不强制开启其 player／render features；core/effect-only 的消费者不引入 geom。该数据 adapter 不等于整个 UI 默认动画已经迁到 Engine。

### 27.2 验证与局限

geom／raster／hanim／vector／motion 五个包的原生 dev／release 各 32 个程序、249 项；Wasm／Node 同样 32 个程序、244 项，保留线程能力分支。新增检查覆盖所有路径命令、缺失／多余操作数、非法坐标、预算、隐式轮廓边界、来源映射、拥有生命周期，以及导入后 61 个时刻的公开动画几何。既有渲染像素、文字／路径过程、打断与预热零分配门禁继续通过。

仓外 `import hanim` 的源码 dev／release 各 150 项，真实静态安装包 150 项，七种 feature 实际闭包通过。object 按需增加 geom；core 与 effect 的最小闭包保持。五类离屏动画各 61 帧、两个 profile 共 610 张 PNG 的像素／文件摘要与前检查点一致，全部逆向重放一致；Agent 只读、dry-run、委派／撤销和拒绝视觉输出检查保留。Vulkan 强制设备 CTS 四个程序、11 项通过，设备为软件 lavapipe，不报告物理 GPU 吞吐改善。

官网原生全部 20 个程序、129 项及 Wasm／Node 动画馆 12 项通过，原有参考像素表未修改。实际查看了路径形变中间态、窄宽度单舞台和独立多部件动画中间帧。UI motion 的最终有效清单另做七个程序复验，完整 lint、TOML／Python／JSON、文档本地链接及 diff 检查通过。用户已有 `mcpp.lock` 保持原样且不纳入提交；只清理旧消费者可重建的 target 目录，保留最新增量与过程证据。

本轮只完成共享 2D 路径及已有离散化的归属、公共捕获与真实消费。现有 flattener 是要求合法输入的低层 append API，保留固定细分上限；严格误差证书、许可降级诊断、生成顶点来源、变体缓存、3D mesh／rig／采样场、文字排版／材质／交互映射仍需后续实现。路径命令索引也不等于跨版本稳定顶点身份。没有把曲线捕获、610 帧一致或 GPU CTS 通过当作这些目标完成。

下一步继续把表示、选择集与局部保持／简化用于同一个真实组合组件，并统一呈现、裁剪与命中结果，再接到官网的可交互舞台。A2 的混合／约束、A3 其余表示、A4 完整 UI policy、A5 资源编排以及 40／20、其他引擎／AI／工具／后端／发布范围保持开放。

## 28. 可选 Hanim 接入与组件属性呈现

基线 `0187eed`，推进 A4 的首条真实属性链路。UI 是公共 Hanim Engine 的消费者；组、文字和控件均可按通道选择，不固定划分“外壳交给 Hanim、文字交给另一套动画”。本轮是可运行子集，以下边界不作为完整 A4 验收。

### 28.1 接口、归属与生命周期

`modules/ui/view` 保留一个清单，新增包内 `src/hanim/`。可选 `hanim` feature 导出 `import omui.hanim;`，为现有 view 依赖增加 Hanim 的 engine feature；正常 view 使用仍可不带完整 player。公开 `omui::animation::Binding::attach(runtime, targets, hengine)` 为选中的 UI 节点建立 HObject；第一项是渲染子树根，其余为该子树内的后代，保留最近选中祖先关系与稳定部件名。对象描述的是初始 identity 的**局部呈现 offset**，没有伪装成包含布局、字形、材质与业务状态的完整 UI 捕获。

`PresentationChannel` 允许位移、旋转、缩放和剪切分别选择；共享节点的不同通道可由不同 lease 持有，anchor 必须相同，重叠写入明确拒绝。多个目标先校验再发布，非法值不会部分改写；未选后代继承组变换，选中后代追加自己的局部变换。`Binding::present(frame)` 只消费公共 Engine 的已采样结果，不保留借用 span，不另设 player／clock。对象由 Engine 持有，先释放 playback、detach，再销毁对象；lease 用运行时寿命 token 与 NodeId generation 避免悬空访问。

首次绑定正在进行的旧扁平 morph 会返回 `busy`，等待自然结束后重试；不会通过取消已有反馈来取得对象。跨 portal 选取、未知 frame／额外几何／图像效果 scope 明确拒绝。原生输入必须在所属线程发布，随后完成 paint frame；`has_frame`／`applied`／`error` 区分已接受的属性与实际绘制结果。`Runtime::inspect` 报告表示、通道、姿态、提交状态和浮层跟随。没有把这条原生 adapter 宣称为新的远端控制协议；应用仍从已授权 openas 入口调用共同领域 API。

### 28.2 一份呈现结果，完整节点边界

选中节点成为明确的呈现边界，变换包住完整绘制范围，包括文字、内部裁剪、后代和 NodeFrame；像素、screen bounds、输入逆变换及语义几何沿同一链路更新。内部默认状态反馈继续运行。上一帧作为自动 morph 来源时恢复未投影的绘制，避免同一外部位移再被 UI 插值一次；显示列表和采样缓冲区复用，该属性 adapter 不进行 mesh／逐字形轮廓转换，renderer 仍按实际绘制需要选择自己的字形表示。

浮层以投影后的目标分配定位，边界／祖先裁剪使用对应变换。自动跟随的位置不再另建一条缓动，显式 `placement: animate` 仍保留作者指定行为。renderer 的非绘制 NodeFrame 增加可选 sampled allocation origin；UI motion 在采样、打断快照和 follow-layout 中传递它，使浮层替换外部分配位置时仍保留自身的出现／状态变换。该元数据不进入绘制 hash。IME 保存局部 caret，宿主查询时映射到已提交呈现，外部动画不需要伪造文字编辑来移动候选框。

跨平台过程验证使用仓外真实应用 `tools/ci/omui-hanim-consumer`：标题旋转、卡片位移和标准按钮缩放由同一 Hanim 编排执行；鼠标和 Agent 都操作原按钮，弹出真实 popover，重定向后检查同帧跟随，Agent 再关闭它。过程图来自相同 CPU renderer，字体、文字像素与每时刻命中结果均显式验证。

### 28.3 性能与仍开放的范围

分级性能设计进一步拆为构建 feature、prepare 表示／预算选择、运行期调节三个阶段。纯属性不为顶点动画付几何转换成本；低性能设备使用许可的近似表示／配方与更新策略，不能以省成本为由破坏命中、文字、局部保持或状态模拟语义。新增 `ui/presentation/publish-100` 与 `frame-100` 基准，分开测属性发布和真实 Runtime paint；只给发布路径设零分配预算，整帧、raster、GPU 与低端设备吞吐不混作同一结论。

仍未完成：完整 UI 内容／几何 HObject adapter、按顶点控制与 3D、通道混合／hold-world、选择式样式／效果、运行中旧计划的边界重分区、任意活动布局 morph 的采样锚点约束、自动 clock／reduced-motion／输入保护接入，以及官网可交互配置舞台。历史 crossfade 继续表示它捕获时的实际画面。当前显式时间采样可以审查关闭自动动画时的姿态，不等于 live driver 已有完整自动策略。A2–A5 其余任务、40／20、完整默认配方、其他引擎／AI／工具／后端和发布门槛保持开放。

### 28.4 本地验证与自审

可复查的数值、源码摘要和过程记录见[检查点报告](../../tools/ci/2026-11-05-ui-presentation.report.omui.json)。五个相关包的 native dev／release 各 39 个测试程序、328 项通过；Wasm／Node 同样运行 39 个程序，324 项通过，线程式本地 IPC 的不支持项保留明确跳过。官网原生 20 个程序、129 项通过，未修改参考像素表。

仓外消费者三个 profile 各 22 项，分别保留 61 张实际 PNG，共 183 张；相同时刻的原始像素／PNG 摘要一致，三个 profile 各自的全部逆向重放也一致。根／子节点通道、默认反馈、输入逆变换、裁剪、浮层、IME、打断来源、失败保持、lease 寿命和资源释放由相应用例覆盖；这不等于任意视觉效果或 live pacing 已验收。已实际查看 release 中间态及移动边界中的真实浮层图片。

自审抓到测试资源缺口：仓外 release 去掉调试源码路径后无法偶然找到字体，原测试忽略 `add_defaults` 的错误，仅检查字形命令／缺字计数仍可能让空文字通过。消费者现在显式供应字体，并检查加载结果、每帧标题区域的真实文字像素；另有缺字体必须失败的反向检查。该修复不表示任意发布包已自动携带所需素材。

关闭 feature 的第二个真实应用已构建和运行；实际构建图中没有 `omui.hanim` adapter 与 Hanim engine 单元。清单明确 `default = []`。验证按 target／profile 和实际构建输出选择单元；IDE 编译数据库包含未激活 feature 的记录，不能据此宣称依赖进入产物，也不能仅凭清单文字声称裁剪成功。

本机 i9-13900K、Linux release 的最终基准：100 组属性发布平均 **2.59 μs**、p95 **2.75 μs**；包含 302 个节点的 Runtime paint 平均 **0.149 ms**、p95 **0.153 ms**。两段预热后记录的普通 C++ `new`／`new[]` 分配均为 0；发布路径零分配预算通过。前者不含 Hanim 求值／paint，后者不含 raster／GPU；这些数值不能当作完整屏幕帧率、全部分配器统计或低性能硬件保证。

完整 lint（含文档 schema）、清单／Python／JSON、文档链接及 diff 检查通过。源仓库用户已有 `mcpp.lock` 原样保留、不纳入提交；宿主构建缓存的临时锁按本地所需构建程序重建。当前检查点以本地真实验证推进，后续继续把公开绑定和选择策略用于官网配置舞台，再补完整几何、自动策略及其他架构任务。

维护者追加空间清理后，移除了 `/tmp` 与项目内 87 个已过期构建缓存目录、530 份旧日志，文件系统实际可用空间增加约 17.25 GiB，清理后约 20.18 GiB 可用。保留各工作树源代码／git 元数据、正在运行的应用、最新 native／Wasm 测试增量与仓外消费者，以及本轮图像／测试／基准证据。详情在 `target/omui-presentation/cleanup.report.omui.json`，摘要写入本轮检查点报告。


## 29. 原生播放宿主与官网组件编排

2026-11-06，基线 `97db51c`。继续 A4 的实际接入：可选原生宿主把公共 Hanim 编排用于活的 UI，官网用同一接口提供可编辑便笺。仍不把这条属性链路记为完整 A4 或 40／20 验收。

### 29.1 同一个 player，分层暴露控制

现有 `openmui.view` 的 `hanim` feature 内增加 `omui::animation::Playback`，无新增包。`Binding` 保留给已有共享 Engine 的应用；`Playback::attach(runtime, targets)` 拥有自己的 Engine、Binding 和帧请求 lease，简单使用为 `play(animation, options)`。对象、公开属性配方、parallel／sequence、prepared plan 及实际求值仍来自 Hanim；UI 宿主没有重新实现插值或新设 timer／线程。

宿主显式安装原生 stage，在 input／regions／layout 后、bounds／paint 前调用 `update(gate)`。必须声明 tree／layout 读取、tree 写入、成本、trace 与进入 bounds 的顺序边；不能在 Runtime 尚未锁定本帧时间前采样上一帧时间。应用决定可见／前台／交互 gate，驱动读取 Runtime 减少动态效果及 motion level，自动接到帧请求；没有暗中安装全局 registry，也没有让脚本进入每帧路径。

`pause`／`resume`／`reverse`／非零有符号 `rate`／`seek`／`seek_progress`／`stop(restore|hold)` 控制同一个原生 playback。第二次 play 要先显式 stop；关闭自动动态仍可 seek 审查画面。暂停、不可见、后台、交互或动态偏好抑制时保留可见姿态并释放持续帧请求，恢复不追赶隐藏时间，显式暂停不被门控恢复撤销。Runtime 时钟暂停后不忙等，显式 seek_time 仍可取样。stop 后管线保持可用，下一帧提交恢复／保留的姿态。

绝对时间、倒退查询和往返循环复用 `hanim.core::TimeMap`；新增 `sample_time` 仅把既有映射的位置、迭代、方向与边界状态公开，旧 `map_time` 委托同一实现。宿主使用相对时间差再加入相位，避免将小时间偏移先叠加到很大的绝对时钟。原生 status／plan 与 Runtime 已提交的 presentation 分别可观察；无新增远端权限入口，应用通过已有 openas 操作映射到同一领域方法。

### 29.2 官网：真实便笺与渐进披露

基础动画目录新增 `component-motion`，使用同一真实便笺的三种配方：**整体**移动完整卡片；**部件**分别移动／轻转标题并缩放按钮；**标题**只改变选中的标题。文字与标准输入框、checkbox、button、popover 都保持活的业务状态；首次鼠标进入／按下即接管正在播放的预览。复制按钮将当前标题／置顶状态输出为可用 JSON，弹出真实回执，不用计时器伪造保存成功。

仍只展示一个舞台；初始只有播放、交互、详情，选择集／速度／往返／局部风格／源码在详情中展开。C++ 内容明确标注为依赖已绑定对象的配方片段，不冒充完整工程；显示代码与实际 program 共享同一 Recipe 数据。plain／glass／neon／retro／ink／liquid 继续通过现有局部 UI scope 组合，未宣称此 adapter 已接通 Hanim 图像效果 scope。目录计数为 42 个基础场景，含独立编排实例；不能据此宣布 40 个公共基础动画库验收完成。

### 29.3 验证方法与自审发现

仓外消费者增加原生宿主 stage，采样 61 个 host-clock 时刻，并倒退 Runtime 时间逐帧复现；加上原有 61 帧显式采样，检查 shaped text、真实按钮、同帧 bounds、暂停／反向／速率、循环跨界、门控、减少动态效果、时钟暂停、非法输入、释放和 Runtime 寿命。三个构建各自产生真实图像，既对比原始像素也对比 PNG 摘要。关闭 feature 的真实第二个应用仍检查实际构建 DAG；缺字体必须失败。

官网另测三种选择各 61 帧及逆向 replay、真实鼠标／Agent 操作、首次点击接管、偏好／路由释放、360px 中英文布局；同一公开配方还绑定到另一套工具栏层级，验证不是只能用于该便笺。浏览器验证通过真实导航／选择／键盘输入和剪贴板检验实际发布路径。新增 `ui/presentation/hanim-frame-100` 基准，测 100 个独立卡片部件的公共 Engine 求值、宿主时间映射、属性发布及 302 节点 Runtime paint；raster／GPU／准备阶段另计，不推导低端设备吞吐。

本轮自审修复 stop 后 update 错把空闲宿主当作管线故障的问题；仓外 stage 的验证还发现漏声明 cost／tree read，已按真实管线契约修正。通用 TreeWatch 的时序调整影响了既有固定过程图，恢复其原有阶段后参考像素重新一致，未以重写 golden 掩盖变化。旧 JSON 动效复用测试与新 Program 配方分别验证实际复用，不把无动作 JSON 当作 Hanim 配方。

本轮 native dev／release 的六个相关包各 **47 个测试程序、395 项**通过；Wasm／Node 的 47 个程序中 **391 项**通过，线程式本地 IPC 的不支持项明确跳过。官网原有 20 个程序／129 项通过；新增程序修复测试声明顺序的编译错误后单独重跑 **6 项**通过，合计覆盖 21 个程序／135 项；同一新增程序另做 release 构建并通过 6 项。参考像素总摘要仍为 `3027fb6720e6078d`，59 帧参考表未改。

仓外 native dev／release／Wasm 三种 profile 各 **51 项检查、122 张过程 PNG**通过：显式采样与宿主驱动各 61 帧，全部逆放可重现，相同时刻跨 profile 的像素和 PNG 摘要一致；共 366 张。另保留官网三种选择各 61 帧及移动端／实际便笺回执图。关闭 feature 的独立应用与缺字体拒绝检查也通过。

本机 i9-13900K、Linux release 的 `hanim-frame-100` 平均 **0.154 ms**、p95 **0.159 ms**，包含上述 100 组动画和 302 节点 Runtime paint；普通 C++ `new`／`new[]` 记录为 **0**。对应直接发布平均 2.60 μs，发布＋paint 平均 0.146 ms；都是特定预热工作负载，未计 raster／GPU／prepare，不推广成整屏帧或移动设备性能保证。最终源码摘要、命令与浏览器交付结果见[本轮报告](../../tools/ci/2026-11-06-ui-playback.report.omui.json)。

新生成的 site-web（LLVM -Oz）包在 Chromium 中通过 **151 项**实际浏览器检查，保留 8 张截图。新场景验证实际自动推进到中间态、整组／部件变化、渐进详情、键盘选项、便笺编辑、真实系统剪贴板与焦点交互；既有基础／创意／Agent／统一接口／局部风格流程同时通过。

停止构建后，以独立冷启动、20 Mbit/s／40 ms、1280×800 测量：总传输 **4,858,067 B**，首帧前 **2,955,838 B**，首帧代理 **1,759.4 ms**、ready **1,722.6 ms**，rAF p95 **16.8 ms**、长帧 **1**，页面异常 **0**，原有 **7／7** 预算通过。5 MB 总量门禁余 **141,933 B**，未调整预算；这是本机单份 cache-enabled 样本，首帧代理不是 FCP／LCP，rAF 不是实际 GPU 绘制成本，不与旧源版本做单因果性能提升结论。

### 29.4 仍开放的范围

完整 UI 内容／几何 HObject、按顶点和 3D 控制、通道混合／hold-world、通用状态与约束、image-effect scope 到 UI 的映射、任意布局 morph 的采样锚点和动态层级再分区、预算／降级控制、自动宿主安装等仍未完成。默认组件配方批量迁移、40／20 的库与完整工程、其他引擎、AI／PPet、工具、后端和发布门槛继续保留。当前过程图与本机耗时不替代实机跨平台节奏、GPU 或整体产品视觉验收。
