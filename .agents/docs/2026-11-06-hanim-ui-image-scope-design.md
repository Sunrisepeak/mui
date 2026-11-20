# Hanim × UI：原生图像作用域与共享内容边界

2026-11-06，实施基线 `ab4d823`。这是进行中的 A4／S2 实施细化；设计条目不等于已验证能力。完整总体目标保持，入口仍为[综合方案](2026-11-05-system-subsystem-architecture-and-api-design.md)。

## 1. 当前缺口

检查点开始时，可选 `omui.hanim` 已将组／文字／组件的属性姿态发布到同一呈现与命中链路，原生 Playback 可由宿主时钟驱动，但 Binding 仍拒绝 image-effect scopes。独立 Hanim 的 VectorProgram 已能消费叶子和嵌套 scope，参数绑定／代际校验却是私有辅助逻辑，不能由 UI 普通消费者复用。以下记录本轮补齐通路的设计，实际验收另见实施台账。

UI 的像素效果／局部样式可以组合，但这不等于 Hanim 编排的 scope 已进入 UI。新的通路不得每帧构造 JSON、重写业务属性、烘焙文字或重新采样动画；也不能复制 renderer 的效果实现。

## 2. 语义与边界

- Hanim 负责动画／效果参数的时间求值与作用域，renderer 负责源像素捕获、过滤和资源。UI 提供真实内容与稳定节点边界，保持业务和输入语义。
- 效果包住已经选定的真实绘制内容，含文字、子控件、当前属性变换与内部状态反馈；嵌套顺序与原 Program 一致，不凭效果名称另写分支。
- 普通局部风格继续与编排效果分开配置。身份效果跳过隔离层，防止重复抗锯齿裁剪；未知、过期、超预算、不支持的效果明确失败。
- 现有单 HObject 的属性 Binding 不冒充任意多对象 UI 合成器。Program 的合法 scope 路径按原样消费；现有通道 scope 冲突不靠重复绘制整棵 UI 绕开。按部件图像 scope、任意跨树组合与空间变形命中，需各自扩展契约和验收。
- blur／tone／pixelate 等显示效果不会把扩散出的光晕当成新交互区域。renderer 的 `preserves_hit_geometry` 元数据默认未知，仅明确声明保留交互几何的 provider 可进入当前 UI 通路；这三个内置效果已声明，consumer 不按名称判断。未知／空间扭曲（包括已有 `source_of` 的效果）、backdrop 和 GPU-only 能力明确拒绝，需各自补完整映射／后端契约，不能只画对而保留错误命中。

## 3. 唯一的参数绑定

在既有 `hanim.render.vector` 入口公开 `PreparedImageEffects`：把 `PreparedEffects` 的命名输入映射到 renderer 的顺序参数、默认值、范围和 registry generation。Vector 与 UI 使用同一实现。准备持有不可变元数据；热路径 `resolve(sampled, output)` 只验证和拷贝已有样本，无插值、时钟、分配或 GUI。

输出空间不足、非法值、alias、registry 代际改变必须在任何输出写入前返回。作用域参数和几何源身份分别校验，不能因数目相同就接受 foreign frame。

## 4. renderer 按实际内容确定边界

当前 PushLayer 的矩形由消费者给出。UI 的布局矩形不能代表实际绘制范围：子对象移动、字形 overhang、描边、阴影及嵌套效果都可能超出它。新增可选的内容边界模式，普通显式矩形路径保持兼容。

边界计算归 renderer，复用其 `device_bounds`、GlyphSource 和 effect reach 契约；不在 UI／Hanim 再写另一份字形或路径测量。一次遍历构建层匹配、变换和嵌套输出范围，复用缓冲区；按层调用，避免逐像素控制。CPU、GPU 和 damage 消费同一测量。外部裁剪在原有合成位置生效，内容捕获不因错误使用布局盒而漏掉可见像素。

默认模式的命令 hash／实际画面应保持；新模式参与 hash、拷贝、快照／重放与增量 damage。嵌套后的源范围、输出范围和 viewport capture 分开计算；透明／空内容、奇异变换、DPI 和重复 scope 需要测试。

效果参数的逻辑像素尺度与递归 offscreen 的几何变换分开传递。检查发现 CPU 递归捕获的初始 transform 可能含父节点缩放，而 GPU 保留输出尺度；先用真实 CPU/GPU 比对确认，再修正共用语义，不用对某个 effect 的特殊补偿掩盖问题。

## 5. 原生 UI 发布

在现有 presentation 生命周期中加入按节点、具预算的原生图像层发布；独立 writer／lease、代际、容量、取消和 detach 可观察。UI 层只持有 renderer 原生 refs 和参数，不依赖完整 Hanim 编排器。姿态与效果批次一起预检、一起提交，失败保留已接受的值。

外部效果在完成组件默认状态运动后包住所选 BeginNode／EndNode 范围；未投影的原内容仍是自动 morph 的来源，避免把同一外部效果烘焙回下一帧再次处理。初次挂载正在扁平 morph 的边界仍按实际能力诊断，不取消原反馈夺取绑定。

Hanim adapter 冷准备各叶子／scope 的参数映射及最大 buffer，热路径消费 EngineProgramFrame 并发布当前活动路径。owning Playback 在 play 前准备，stop／hold／restore 与显式 seek 保持可解释语义；纯属性轻量路径不为未选效果做转换或逐帧扫描。

插件换代恢复：低层 Binding 可重新 `prepare` 同一计划；owning `Playback::rebind()` 在静止边界重新绑定当前计划并发布当前样本，不重新 `play`，保留进度、方向／速率、宿主时间映射和暂停／门控意图。成功只请求一次绘制，不自动恢复连续播放；失败返回原有类型化错误，保留已接受的 UI 值。调用者恢复兼容 provider 后可再次尝试。此为可分配的冷操作；registry 的并发替换、二进制加载和旧代码资源租约不由它实现。已接受的参数不保证被替换的 provider 仍能重画原像素，须实际 render／inspect 验收。局部检查点见台账 §31。

## 6. 真实消费者与官网

先用仓外 headless 应用验证同一公开 Program 作用于真实文字、button 和 popover，再把可用路径接到官网便笺：整体／部件选择保留，图像效果和局部风格独立选择；复制内容仍是实际公共配方。输入接管、暂停、反向、reduced/off 和无窗口操作保持。

## 7. 验证与交付顺序

1. 公共参数绑定与原有 vector renderer 共用：顺序、默认值、重复效果、非法／过期／alias 原子性和预热零分配。
2. 内容边界与尺度：文字／溢出子对象／阴影／多层效果，变换、DPI、identity、裁剪与增量结果；CPU 参考、真实 GPU 路径及 Web 分开报告。
3. 原生 UI 批次：通道所有权、whole/partial scope、拒绝与恢复、销毁安全、计划／呈现状态、真实人／Agent 操作。
4. 全过程采样与逆放、公共独立消费者、官网过程图及实际浏览器；性能与包体积分别记录，不能仅凭编译通过验收。
5. 更新检查点、单一签署 commit 追加 PR #1。保持用户已有 mcpp.lock 差异；异平台 CI 异步反馈。

实现已覆盖公共参数绑定、共享内容边界、原生原子发布和官网可选效果；局部验收与明确限制见[实施台账 §30](2026-11-05-system-implementation-checkpoints-design.md#30-原生-ui-图像作用域与共享内容边界)。新增 renderer 文件仍在原包的 `src/`；没有另建微型包。公开 UI 的 `hanim` feature 按需引入已有 `render-vector` feature，普通 view 不应包含完整播放器／vector adapter。

复查修正：blur 半径零、tone 强度零声明 filter identity。底层 renderer 的 identity 保留普通组 opacity／blend；UI 在整组 opacity 已由原内容管理时省略其外部零强度层，保证初始画面精确恢复。不能把“普通层”和“没有层”混为一个测试基线。Matter 对内容尺寸的嵌套效果组也消费共享边界测量，避免空声明矩形变成空动画包围盒；实际字形捕获仍由带 GlyphSource 的 renderer 完成。

完整 3D、hold-world、通道混合、任意多对象／逐部件 image-scope IR、动态布局约束、GPU-only／backdrop／空间逆映射、40／20 公共库及其他子系统目标继续开放。CPU 滤镜执行本身的分配和资源策略未在本轮宣称为零成本。

稳定性能与冷资源分开记录：固定字形／字号、DPI 1 的所有公开子像素 mask 预先准备后，100 个平移部件、两层效果、UI paint 与内容边界测量为零 C++ new；未预热时首次新字形相位曾有最多 72 次分配。公开参数绑定另有独立零分配预算。新字形／字号、滤镜像素执行和 GPU 成本仍需各自评估，不能由该基准推出全场景每帧零分配。

## 组透明度的呈现边界

公共 `fx::opacity(from,to)` 使用同一图像 scope，捕获根对象内容后统一缩放预乘 RGBA，包含真实文字与子控件，不逐个改 fill alpha。CPU／GPU 都归共享 renderer，既有 UI adapter 不新增透明度专用采样器。amount=1 是 filter identity，保留原层 opacity／blend；amount=0 是透明输出，不能当作 identity。

透明度是呈现参数，不自动改变 hidden／display、布局、焦点、命中或授权。业务需要不可交互时由其已有策略表达；也不能用某个属性动画的 part 名暗示整组图像 effect 已变成部件 mask。当前 UI image scope 仍遵循绑定根的内容边界，局部根可独立选择。没有将 group opacity 与每个 paint 的 alpha 混为同一行为，也没有把 UI 全部渐隐行为锁死成一套默认策略。
