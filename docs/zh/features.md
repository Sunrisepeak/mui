# omui 的特色

按优先级排列。每一项写清楚：它是什么、为什么别的框架做不到或做得不完整、现在已经有什么证据（测试、基准、作品）、还差什么。状态以仓库里的代码和 CI 为准，没有实现的不写成已有。

对比对象是目前主流的 UI 框架：Qt（Widgets / QML）、Flutter、SwiftUI、Jetpack Compose、React / React Native / Electron、.NET（WinUI / MAUI / Avalonia）、Slint、Dear ImGui / egui。

---

## 1. 统一语义树：像素、无障碍、agent、测试是同一棵树

**是什么**：界面先是一棵语义树（每个节点有 role、名字、值、状态、可执行的动作），像素只是这棵树的一种渲染。无障碍、agent 操作、测试断言读的都是同一棵树。

**别的框架**：语义（无障碍树）大多是事后从控件树导出的副产品，测试靠截图或选择器，agent 靠截图点坐标。

**证据**：语义树和查询、紧凑快照、H0 到 H3 四级 headless；`omui agent` 命令行；节点 ID 稳定且不依赖显示文字（S-9）。

## 2. Agent 原生，而且安全

**是什么**：每个应用自带 agent 接口（`openmui.tree@0.1`）和 MCP 服务器（`omui mcp`），Claude Code 等 agent 直接读树、找节点、执行动作、等待界面稳定。动作分效果等级：不可逆和对外的动作必须由人确认（S-1）；标为 `person` 的动作只有人在本机能做，任何 agent 都无法冒充（S-10）。开发者给应用定上限（`AppSpec::agent`，构建时定，运行时不能提高）；人在应用自己的界面里按 agent、效果等级和动词范围授权，授权内的动作不必再确认（S-11）。可逆的动作能撤回：组件说明怎么撤回（开关再拨一次，文本框拿回原来的文字），窗口保存历史，`undo` / `redo` 本身也是 agent 看得见、能执行的动作；几个动作可以合成一个事务，一起成功或一起撤回；agent 还可以先问一个动作会做什么（dry-run 不改变任何东西）（S-12、S-13）。这些规则来自开放标准 [openas](https://github.com/openlifa/openas) §5，由 `openas::decide` 判定。

**别的框架**：没有内建的 agent 接口；常见做法是截图加坐标点击，或者基于无障碍 API 的脚本，没有效果等级，也没有防止 agent 自己批准自己的机制。

**证据**：`tools/mcp` 的测试和 CI 里的真实进程端到端；elicitation 确认流程；S-10 的协议测试（伪装成人的请求也会被拒绝）；S-11 的协议测试（超过上限被拒，授权链上的 agent 不用再确认，别的 agent 照常确认）。

## 3. 确定性：三个平台、两种编译器，逐字节相同的像素

**是什么**：同样的输入在 Linux、macOS、Windows，GCC 和 LLVM 下得到逐字节相同的像素；动画是时间的纯函数，任何一帧都能直接算出；会话可以在 headless 下精确回放。

**别的框架**：像素依赖系统字体、系统光栅器和 GPU 驱动，测试只能容忍差异或只在一个平台上做截图测试。

**证据**：CI 六个平台行上固定的像素哈希（示例、组件、展示都有）；CPU 参考光栅器不用浮点收缩；文本、i18n 都不依赖系统库（不用 ICU）。

## 4. 一棵树：C++、TypeScript 和视图文档

**是什么**：C++、TypeScript 和 `*.view.omui.json` 写出同一棵语义树；组件契约优先，TypeScript 绑定由生成器产出。脚本语言只有 TypeScript（D-138）：把一种绑定做完整，而不是把几种都做一部分。

**别的框架**：通常绑定一种语言（Dart、Swift、Kotlin、JS），或者多语言绑定由社区各自维护、能力不一致。

**证据**：CTS 场景在 TypeScript 下记录出固定的轨迹；C++、TypeScript 和视图文档写出的树在测试里逐一比对；`omui-gen` 生成绑定、存根和 schema。

## 5. 独立动画引擎，可组合的播放

**已有能力**：`import hanim;` 提供对象、可复用的严格轮廓形变、按序效果参数、sequence/parallel/stagger与显式时钟Engine。`hanim::anim::morph(source, target).duration(600ms)`这样的叶子可直接交给 `hengine.play(animation)`，或通过 `hengine.prepare(animation)`准备后复用。实时引用在准备时捕获，显式快照保持冻结；同一播放器支持跳转、暂停、倒放和从当前姿态重定向，不替应用修改业务状态。

常见属性配方有 `move_to`、`move_by`、`rotate_to`、`scale_to` 和 `fill_to`。`anim::to(object, PropertySet{...})` 可在同一时序中选择多个部件／属性；不同叶子则可为同一对象的独立通道使用不同时间和曲线。下面的 `card` 是含有稳定部件 ID `icon` 的 HObject：

```cpp
auto animation = hanim::composite(hanim::parallel(
    hanim::anim::move_to(card, {120, 40}).duration(600ms),
    hanim::anim::to(card, {.rotation = std::numbers::pi, .part = "icon"})
        .duration(250ms).easing(hanim::ease::out_quad)))
    .with_effect(hanim::fx::blur(0, 2));
auto playback = hengine.play(animation);
```

同一对象只绘制一次，未选择的局部属性保持，父级变换仍会作用。相同通道的重叠写入会报告冲突；共用图像效果包围并行表达式，不静默合并不同效果作用域。原生工具和 Agent 可以通过 `hengine.plan` 查询保留的原始编排及通道时间。

类型化的 `fx::vertex_path(PointPath::bend(0.25))` 和 `fx::translation_path(...)` 在渲染前改变匹配后的几何或局部位置轨迹，可与模糊／色调组合，保留端点。C++ UI路径adapter复用同一个core算子；重复的路径修饰在同一端点坐标系中叠加位移，不修改匹配规则。

UI动效通过普通配置使用同一算子：

```cpp
auto motion = Value{}.set("use", "morph")
    .set("vertex_path", Value{}.set("bend", 0.22));
drawing.prop("motion", Source(motion));
```

组件只画状态，运行时完成匹配后的填充／描边路径动画。基础动画页以三角形到圆开场，详情中可调整路径、时间和局部风格，并复制可复用的动效JSON。弯曲值是各点位移的比例，不改变文字、图像几何或命中轮廓；reduced过渡不添加此位移。

**证据**：独立公共消费者在dev/release验证几何与控制；另一个消费者通过共享CPU renderer输出真实像素和指定区域。渲染显式选用，基础 `hanim.core` 保持独立。

**待完成**：通道加法／权重混合、世界空间约束、共享几何转换、3D／图像／骨骼域和配置式 UI 接入。嵌套图像作用域及独立的 2D 属性通道已可用；当前 UI 动效主要使用 core 原语，官网场景数量不等于公共引擎配方数量。

## 6. 渲染：CPU 参考 + GPU，效果是一等公民

**是什么**：渲染器只写一次。CPU 参考后端是确定性的基准，GPU 后端基于 `mlibs.rhi`，与 CPU 的差别有明确的容差。自定义效果用 Slang 写，热重载约 50ms；液态玻璃等背景效果是通用能力；风格包切换外观，语义树不变。

**证据**：GPU 测试逐场景对比 CPU 参考；基准报告（`docs/zh/benchmarks/`，2026-10-27）：RTX 4080 上五种风格每帧 p95 0.36–1.38ms（运行时一帧加 GPU 提交并等待完成），1080p 十块液态玻璃 1.61ms。

## 7. 性能可证明：预算进 CI，报告公开

**是什么**：性能声明都有基准和预算，CI 每次修改都检查；报告公开机器、方法和结果。帧需求和帧节奏（ProMotion / VRR）由框架提供分层接口。

**证据**：`tests/bench` 的 48 项预算（`budgets.bench.omui.json`）；`docs/zh/benchmarks/`；S1 一千个窗口缩放扫描在 GPU 上 p99 7.11ms（p95 1.20ms；CPU 路径 p95 0.115ms）。

## 8. 简单的写法，深入的控制（渐进式披露）

**是什么**：每项能力分四层：L0 默认就对（增量帧、缓存、并行光栅、帧节奏都是自动的）；L1 用属性给提示；L2 显式控制（渲染到纹理、管线阶段、并行度）；L3 直接用底层（rhi 设备、自定义 pass、外部纹理、帧需求租约）。性能敏感的部分都是 C++ 模块，开发者需要时可以一路下到硬件。

**别的框架**：要么简单但封闭（底层够不到），要么开放但一开始就复杂。

## 9. 一切可替换，可以单独发布

**是什么**：几何、动画、布局、文本、光栅、渲染、rhi、i18n、录制、trace 都是独立组件，不依赖框架的其他部分；一帧的流水线是可以编排的管线；宿主也是插件（SDL3 是默认的桌面宿主，不是框架的依赖）。

## 10. 国际化：不用 ICU，结果在各平台一致

**是什么**：i18n 引擎和语言数据分开，引擎可以单独发布；数据由 CLDR 生成；MessageFormat 2；en / zh 做到顶级支持；切换语言时文字原地更新，ID 不变。

中文排版按 CSS Text 4：中西文之间自动留白，全角标点相邻时挤压，开括号在行首挤压，闭标点不出现在行首。

**证据**：`modules/content/i18n` 的测试与 CLDR 给出的结果一致；`tr()` 在四种写法里语义相同；`openmui.text` 的中文排版测试。

## 11. 窗口：默认用系统的，框架自绘有完整的深度能力

**是什么**：窗口默认用系统的外框；应用可以要框架自己的（`AppSpec::window = "framework"`，或 `--window framework`；`switchable` 让用户通过动词 `window.frame` 自己切换）。框架的外框（`omui.ui.window`）和其他组件一样：阴影、圆角边、标题栏、两种风格的按钮，跟随窗口状态变化；标题栏拖动窗口、边缘调整大小都交给系统做，所以贴边分屏照常可用。所有窗口操作都是动词（`window.minimize`、`window.toggle_maximize`、`window.fullscreen`、`window.close`），点击、快捷键和 agent 做的是同一件事。关闭是立即的：窗口不再接收输入，输入穿透到下面，溶解只是呈现。

**别的框架**：自定义标题栏要按平台分别做（Electron 的 `-webkit-app-region`、Flutter 的 window_manager 插件、WinUI 的标题栏定制），跨平台很难一致，也没有把窗口操作放进 agent 能读的模型里。

**证据**：`components/window` 的测试（两种风格逐字节固定、各部分的命中区域、最大化和非活动状态、按钮名称有 en / zh-Hans / zh-Hant、外框里应用的路径不变）；`modules/ui/host/sdl3` 的测试（动词、切换、溶解期间不接收输入的关闭）。

---

## 对比总表

| | omui | Qt | Flutter | SwiftUI | Compose | React / Electron | Dear ImGui |
|---|---|---|---|---|---|---|---|
| 语义树是一等公民 | ✓ | 部分（无障碍） | 部分（Semantics） | 部分 | 部分 | 部分（ARIA） | ✗ |
| 内建 agent 接口 / MCP | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| 动作效果等级、人确认 | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| 开发者上限下的 agent 分级授权（开放标准） | ✓（openas） | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| 跨平台逐字节相同的像素 | ✓ | ✗ | 接近（Skia/Impeller，字体随系统） | ✗ | ✗ | ✗ | ✗ |
| 精确录制和回放 | ✓ | ✗ | ✗ | ✗ | ✗ | 部分（工具） | ✗ |
| 多种语言写同一棵树 | ✓（C++、TS + 文档） | C++ / QML / Python | Dart | Swift | Kotlin | JS | C++ |
| 自定义着色器效果 | ✓（Slang，热重载） | ✓（QML ShaderEffect） | ✓（FragmentProgram） | ✓（Metal） | ✓（AGSL） | 部分（WebGL） | 手写 |
| 性能预算进 CI、公开报告 | ✓ | ✗ | 部分 | ✗ | 部分（Macrobenchmark） | ✗ | ✗ |
| 组件可单独发布 | ✓ | 部分 | ✗ | ✗ | 部分 | ✓（npm） | — |

*表中“✗”指没有内建的支持，不是做不到。*
