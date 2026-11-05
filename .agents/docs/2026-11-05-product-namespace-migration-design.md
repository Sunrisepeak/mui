# 产品归属、命名空间与兼容迁移

2026-11-05。维护者补充形成 D-100，修订原“所有实现包都使用 openmui”规则；包粒度继续遵循 D-99。**命名规则立即用于新增能力；既有仓库尚未全量完成改名。** 本批实际迁移公共控制的归属，原有 UI API 继续可用。

## 1. 五类归属

| 职责 | 包／主要 C++ module 目标 | C++ 命名空间 | 说明 |
|---|---|---|---|
| UI 协议／规范 | `openlifa.openmui` 包、`openmui.spec` 模块 | `openmui::spec` | 标准由 OpenLifa 维护；当前标准 C++ 类型仍在 `omui::spec`，其迁移须在标准仓库单独提交 |
| UI 框架／组件／宿主 | `omui.core`、`omui.view`、`omui.ui.base`、`omui.host.web`、`omui.sdk` | `omui` 及已有领域子空间 | 产品品牌 omui；当前包／模块大多仍用 `openmui.*` 兼容名 |
| 已独立命名的产品 | `hanim`／`hanim.*`、`gongfu`、`mgame.*` | 对应产品名 | Hanim 的代码标识保持小写 `hanim`；不为统一前缀再套 `xengine.hanim` |
| 通用可复用基础库 | `mlibs.math`、`mlibs.log`、`mlibs.control` 等 | `mlibs::math`、`mlibs::log`、`mlibs::control` 等 | 跨域算法、数据与契约；按需单独依赖 |
| 尚无独立名字的领域引擎 | `xengine.render`、`xengine.audio`、`xengine.simulation` | `xengine::render`、`xengine::audio`、`xengine::simulation` | 围绕本域执行、状态与调度形成内核；以后可采用独立产品名 |

`mlibs` 和 `xengine` 是归类空间，不引入一个必须链接的统一运行时。公共控制属于 `mlibs.control`：共享授权、会话与调用来源，不拥有各引擎的业务操作。领域控制 adapter 则属于相应引擎的 `src/control/`，按需连接公共控制，不再建立第二份权限实现。

归属按职责判断，不按目录层数或当前文件数量。RHI 负责设备／队列契约，归 `mlibs.rhi`；renderer 负责渲染图／执行，归 `xengine.render`；UI theme／motion 负责 UI 领域，归 `omui.theme`／`omui.motion`。可选 AI 产品若尚未命名，可用 `xengine.ai`；其通用模型通信继续复用 `mcpplibs.llmapi`，不能更名吞并第三方库。

有明确独立产品价值且职责名不足时，可以先选易懂的暂名，登记职责、owner、消费者和兼容计划；暂名不同时建立另一套 `xengine.*` 实现。仅为分组或美观不创造新的品牌和独立包。

## 2. 代码树与迁移映射

保留现有按层组织的物理目录；命名归属和依赖方向是两个维度。下面是目标映射，不表示右列已全部存在：

| 现有路径／身份 | 新目标 | 本批状态 |
|---|---|---|
| `spec/`：`openlifa.openmui`／`openmui.spec` | 包与模块不变；类型归 `openmui::spec` | 标准仓库未修改 |
| `modules/ui/*`、`components/*`、UI 语言绑定：`openmui.*` | `omui.*`；UI C++ `omui::` 不变 | 保留旧包／import，按 N3 迁移 |
| 根 `openmui.sdk`／`import openmui` | `omui.sdk`／`import omui` | 保留旧入口，N3 一起迁移 |
| `modules/base/control/` | `mlibs.control`／`mlibs::control` | 本批直接使用新身份；从 UI 提取已有实现 |
| base 的 math／geom／hash／memory／unicode／fuzzy／log／trace／session／pipeline／image／testing | 对应 `mlibs.*` | N1，保留现有实现与兼容层 |
| gfx 的 rhi／shader／raster；content 的 text／layout／markdown／i18n | 对应 `mlibs.*` | N1，子包沿同一 owner |
| `modules/gfx/render/`：`openmui.render.*` | `xengine.render.*` | N2，渲染特性仍记录进同一张图 |
| `modules/base/simulation/` 和 gfx 下模拟 GPU adapter | `xengine.simulation.*` | N2，不重复时钟／重建／规则实现 |
| `modules/media/audio/`：`openmui.audio.*` | `xengine.audio.*` | N2，实时边界不随改名改变 |
| media 的 timeline／video | 通用核心 `mlibs.timeline`／`mlibs.video.*`；音频 adapter 归 `xengine.audio.timeline` | adapter 随 N2 移至 audio 的子包目录并保留兼容；有新的完整执行引擎时再单独评审 |
| hanim 各层及 renderer adapter | `hanim`／`hanim.*` | 保持已有产品身份 |
| 工具、应用、构建插件 | 随所属产品／实际独立名称 | UI 工具可以使用 `omui.*`；跨引擎独立工具有自己的产品名；不因为 GUI 用 omui 就把引擎改成 omui |

例如目录 `modules/base/control/src/` 下的接口、授权实现和上下文共用一个清单，不为 `policy`、`session`、`context` 各建包。目录 `modules/gfx/hanim` 继续是 hanim 产品，`base/gfx/content/media` 继续只描述依赖层级。D-80 的叶目录与包名末段对应继续适用。

## 3. 兼容必须分清四类标识

1. **mcpp 包身份与依赖键**：只有一个真实实现 owner。更换身份时同步 workspace、path 依赖、feature、构建插件、生成器和 CI；不能假定 C++ 转发模块能自动解决包解析。先验证 mcpp 是否具备所需包别名，再选择兼容发布策略；没有依据时不预建成倍的空壳包。
2. **C++ module 与 namespace**：旧 import 可由同包的薄转发模块保留，旧 namespace 用类型／函数转发指向同一类型身份。只验证源码兼容，不声称 BMI、mangled symbol 或 C++ 二进制稳定；消费者重新构建。转发不得生成第二份状态、注册表、数学函数或渲染器。
3. **协议与数据身份**：openmui 规范、openas、schema、总线操作名、数据格式版本与 `*.omui.*` 后缀保持其标准归属。`[package.metadata.openmui]` 是现有规范化元数据键，不因实现改名而替换。协议演进独立版本化，不做全仓文本替换。
4. **外部产品／C ABI／品牌**：`mcpp`、`mcpplibs.*`、`compat.*`、`openlifa.openas` 等保持上游身份。UI 的 `omui*`／`Omui*`／`OMUI_*` C ABI 与 `omui` CLI 已符合新规则；生成头文件路径 `spec/include/openmui.h` 暂保留。仓库重命名、域名和资产 URL 属于单独迁移。

标准类型的命名迁移在 `openlifa/openmui` 的 `openmui-design` 分支按原签名／许可证流程实施，再更新本仓 `spec` 指针；不在本仓私造另一份规范类型。普通实现改名不能更改标准所有权。

## 4. 实施顺序与验收

| 阶段 | 工作 | 必要证据 |
|---|---|---|
| N0：当前 | 规则、映射与新增 `mlibs.control`；UI 使用兼容 adapter，独立 hanim 宿主显式依赖控制库 | 同一授权实现、两类实际消费者、控制层无 UI／spec 依赖；本地 dev／release 与 Wasm |
| N1：基础库 | 从叶依赖开始迁移 math／hash 等，再到 log／trace／RHI／内容库；以完整依赖组为单位提交 | 新入口可独立构建；旧入口按承诺可用；无双份符号／状态；冷／增量与体积记录 |
| N2：引擎 | render／simulation／audio 的包、模块、命名空间与 adapter 同步迁移 | hanim、UI、CLI 独立任务；实际图像／音频／状态过程不变；CPU／Wasm 与所选 GPU 后端 |
| N3：UI 与标准连接 | UI 包改 `omui.*`，语言绑定和工具同步；标准命名另走标准仓库 | 生成绑定、SDK／模板／官网、Agent 与 headless 任务；新旧源码消费者；协议／C ABI 兼容 |
| N4：收口 | 清理已到期兼容入口、文档与发布元数据，冻结新的规范检查 | 实际依赖闭包、重复实现扫描、旧消费者迁移说明、独立发布验证 |

每步仍提交到 PR #1，优先本地真实验证，其他平台 CI 异步反馈。N1–N4不替代 S1–S10 的功能验收，单纯改名不增加“40 动画／20 创意工程”的完成数量。涉及高扇出的基础包时，先做一个有新旧消费者的最小迁移验证，再扩展；不得只替换源码字符串后将整仓改名记为完成。
