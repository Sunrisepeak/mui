# 产品归属、命名空间与兼容迁移

2026-11-05。维护者补充形成 D-100，修订原“所有实现包都使用 openmui”规则；包粒度继续遵循 D-99。**命名规则立即用于新增能力；既有仓库尚未全量完成改名。** N0 已交付共享控制；N1 已迁移十二个基础库，实际入口和兼容范围见 §5–§7；UI 实现包的后续迁移另行验收。

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
| base 的 math／geom／hash／memory／unicode／fuzzy／log／trace／session／pipeline／image／testing | 对应 `mlibs.*` | N1 十二库已迁移，保留同包旧源码入口，见 §5–§7 |
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

## 5. N1首组五个基础库的实际迁移

| 现有目录 | 当前包／推荐 import | 当前 C++ API | 保留的旧 import／API |
|---|---|---|---|
| `modules/base/hash` | `mlibs.hash` | `mlibs::hash` | `openmui.hash`／`omui::hash` |
| `modules/base/math` | `mlibs.math` | `mlibs::math` | `openmui.math`／`omui::math` |
| `modules/base/log` | `mlibs.log` | `mlibs::log` | `openmui.log`／`omui` 内原日志声明 |
| `modules/base/trace` | `mlibs.trace` | `mlibs::trace` | `openmui.trace`／`omui::trace` |
| `modules/base/testing` | `mlibs.testing` | `mlibs::testing` | `openmui.testing`／`omui::testing` |

仓内清单和模板的依赖键已同步为实际新身份；hanim.core 与 mlibs.control 直接使用新源码入口。UI、官网等现有消费者仍可使用旧 import，验证渐进迁移。产物使用 `mlibs-<name>`。testing 仍是测试依赖，不进入产品运行时；没有新增 workspace 成员，也没有改变算法、默认日志类别、协议字段、规范子模块或 C ABI。

### 5.1 一个包与实现，两种源码入口

每个现有清单内按以下方式组织：

```text
src/api.cppm       mlibs.<name>.api：唯一的声明和实际类型归属
src/<name>.cppm    mlibs.<name>：主要入口，export import api 与 legacy
src/legacy.cppm    openmui.<name>：export import api，导出旧 namespace alias／using
src/*.cpp         module mlibs.<name>.api：原实现
```

`.api` 是同包接口单元，未成为另一个库或清单。math 的 det／random／color、testing 的平台选择继续是同包分区。旧入口没有第二份数学、日志历史、sink、trace recorder 或测试状态；类型与非内联函数身份相同。公共使用仍推荐短入口 `import mlibs.math;`，不要求调用方理解兼容布局。

主要入口显式导出 legacy 是安装包正确性的要求：mcpp 的公开接口收集以 lib 根及其导出闭包为基础；不在闭包里的旁置转发模块可能只在源码树可用，未随包发出。该布局经当前 mcpp `2026.11.2.2` 实际打包验证，也符合其[二进制分发说明](https://github.com/mcpp-community/mcpp/blob/main/docs/12-binary-distribution.md)。兼容期结束后可再评估接口单元收敛，不能先删旧入口再宣布兼容。

### 5.2 包解析与发布边界

- **源码 path 消费**：当前 mcpp 按路径所指清单的真实 `mlibs.*` 身份解析；旧依赖键产生建议更名的提示，新旧键并存仍只有一个实际包。新清单使用 `mlibs.*`，不把这一行为称为永久的包别名协议。
- **构建命令**：workspace 选择器使用实际新身份，如 `mcpp test -p mlibs.math`；旧 `openmui.math` 选择器不列入兼容承诺。源码 import 兼容不代表命令行、库产物名称也自动成为别名。
- **真实二进制消费**：五个库分别 `mcpp pack`，只携带接口与静态库，不携带 `.cpp` 实现；临时外部工程实际链接这些产物并通过新名／旧名／混用验证。不得用在仓内编译通过替代安装包验证。
- **尚未发布索引别名**：没有发布新版本到 mcpp-index，没有承诺旧 registry 身份自动解析到新库，也不承诺 BMI／C++ 二进制 ABI 兼容。依赖更新后重新构建。
- **传递依赖打包缺口**：当前工具生成的 testing 二进制清单未保留对 hash 的依赖；本轮二进制消费者显式声明五个包，包括 hash。这证明实际新旧接口可以共同链接，但**不代表 testing 单依赖安装已经完整**。正式发布前应修复 mcpp 的依赖清单生成，并补一个只声明 testing 的外部消费用例；不在库里复制 hash 或另造空壳包补洞。

### 5.3 可复现验收

`tools/ci/foundation-migration/run.py` 创建仓外工程，覆盖 source／packed × canonical／legacy／mixed 六种组合，各含首次构建与未修改后的运行。它验证类型及函数身份、hash／math 值、共享 log sink／filter／history、共享 trace 开关及记录、同一测试上下文；检查实际 `resolution.json`、编译源和静态产物，拒绝重复 package、双编译或二进制消费者偷用源码实现。测试库是此验证工程明确选择的工具；原有独立 hanim 消费者继续禁止测试运行时进入产品图。

报告记录命令、版本、耗时、包尺寸与摘要。首次构建指新的消费工程，编译器全局缓存可能已热，不能据此宣称冷机性能改善。跨平台与实际消费者的回归记录在[实施台账](2026-11-05-system-implementation-checkpoints-design.md)，各平台 CI 异步执行。

## 6. N1第二组：几何、内存、Unicode与搜索

| 目录 | 当前包／主要 module | 当前 C++ API | 同包兼容 API |
|---|---|---|---|
| `modules/base/geom` | `mlibs.geom` | `mlibs::geom` | `openmui.geom`／`omui::geom` |
| `modules/base/memory` | `mlibs.memory` | `mlibs::memory` | `openmui.memory`／`omui` 的 Arena、Handle、SlotKey、SlotStorage、SlotMap、FunctionRef 等声明 |
| `modules/base/unicode` | `mlibs.unicode` | `mlibs::unicode` | `openmui.unicode`／`omui::unicode` |
| `modules/base/fuzzy` | `mlibs.fuzzy` | `mlibs::fuzzy` | `openmui.fuzzy`／`omui::fuzzy` |

沿用 §5 的单包／单实现／新旧入口布局。geom 继续是 constexpr 几何，不为改名新造运行时；memory 的 arena、slot、handle、function 保留同包分区；Unicode 表保留生成分区。hanim.object／engine 直接使用 `mlibs::memory`，hanim vector adapter 直接使用 `mlibs::geom`。fuzzy 实现依赖 `mlibs.unicode`，其他既有消费者可保留旧 import 逐步升级。

几何对象、颜色和变换的新旧名字是同一类型；不在 UI／动画／renderer 间增加转换对象。Handle 仍为原来的 32 位 index＋32 位 generation，bits/from_bits 不变；宽 SlotKey、容量、退役、失败原子性及查询零分配保持原契约。不要把 namespace 迁移视为句柄 wire ABI 重新设计。

Unicode 的生成器和生成结果一起迁移。重新使用[Unicode 官方 17.0.0 UCD](https://www.unicode.org/Public/17.0.0/ucd/)运行 `omui-gen unicode`，只改变生成 module／namespace，数值表及已有测试数据保持相同；记录归档与输入文件摘要，并用 `--check`核对生成一致性。此次没有升级 Unicode 版本或修改分词／断行算法。

`foundation-migration` gate 扩展到九个基础库，继续源码／安装包 × 新名／旧名／混用六种工程，增加几何互操作、同一个 arena／slot 的生命周期、UTF-8／grapheme／大小写与 Unicode 搜索验证。核对实际编译源集合，允许 geom 没有 `.cpp`、Unicode 有多个实现单元；不强迫“一库一 cpp”，也不重复创建清单。CLI 选择器仍使用实际新身份。

前批 mcpp 打包依赖清单的限制同样影响 fuzzy→unicode；当前安装包 gate 显式声明这两者以及 testing→hash。该证据不证明仅声明 fuzzy 或 testing 的索引安装可用。正式发布前修复构建工具的传递依赖清单并逐库验收，不能用另一份 Unicode／hash 实现代替依赖。

第二组交付时 N1 仍有 session／pipeline／image（现见 §7）、RHI／shader／raster、content 等后续依赖组；N2 引擎与 N3 UI／标准未因此自动完成。遵循原计划的功能与真实质量验收，不把命名数量计为动画或作品数量。

## 7. N1第三组：录制、管线和图像

| 目录 | 当前包／主要 module | 当前 C++ API | 同包兼容入口 |
|---|---|---|---|
| `modules/base/session` | `mlibs.session` | `mlibs::session` | `openmui.session`／`omui::session` |
| `modules/base/pipeline` | `mlibs.pipeline` | `mlibs::pipeline` | `openmui.pipeline`／`omui::pipeline` |
| `modules/base/image` | `mlibs.image` | `mlibs::image` | `openmui.image`／`omui::imageio` |

继续使用原包与单一实现。三者的 `.api`、主入口和兼容入口均位于原 `src/`；没有新增清单或 workspace 成员。image 的新命名空间与包名一致，旧 `imageio` 只是同一实现的别名。PNG／WebP 的 C 模块仍只在 `src/native/` 使用；pipeline 使用同一个 `mlibs.trace` recorder；session 继续复用 nlohmann.json 与 mcpplibs.os。

### 7.1 保留数据身份，明确源码边界

- 录制数据继续写入并接受 `openmui.session@0.1`，文件后缀仍为 `.session.omui.jsonl`，JSON 字段、排序、时间与回放行为不因包名变化升级。此处是既有数据兼容，不把实现的录制格式改称新的 UI 标准。
- pipeline 的插件 ID、资源类型名、配置及 `.pipeline.omui.toml` 后缀保持；资源、registry、热替换与 trace 只有一个 owner。
- `resource_type<T>` 的**限定名特化**仍可通过 `omui::pipeline::resource_type<T>` 定义，并被新 API 使用。C++ namespace alias 不能重新打开；原来写 `namespace omui::pipeline { ... }` 的扩展需要改为 `namespace mlibs::pipeline { ... }`。仓内 UI 的资源注册块已同步，不能把这种有迁移要求的扩展场景宣称为零修改兼容。
- 新清单、CLI 选择器与产物使用 `mlibs.*`／`mlibs-<name>`；同包旧 import 保留。C++ 使用方须重建，仍未发布 registry 别名或承诺旧 C++ ABI。

### 7.2 独立消费与回归范围

复用 `tools/ci/foundation-migration`，覆盖十二库的源码／真实静态安装包 × 新名／旧名／混用。新增用例检查录制文件读写与字节回环、限定名资源特化、同一管线的替换失败原子性和成功重配、共享 trace、PNG／无损 WebP 像素及动画 WebP mux／demux。外部依赖逐项核对真实 resolution 与仓库锁中的版本／来源／摘要；安装包消费者不编译十二库的 `.cpp`，第三方依赖的编译源另列，不能误算为偷用本仓实现。

独立 hanim playback 的 PNG 消费改用 `mlibs.image`，继续实际采样与反向回放；UI、官网等消费者保留旧 import，检验逐步迁移。相关原生／Wasm、无窗口视觉及浏览器结果见[实施台账 §18](2026-11-05-system-implementation-checkpoints-design.md)。

mcpp 目前的安装包依赖元数据缺口同时影响 pipeline→trace、session→os／json、image→mwrap／codec。fixture 显式提供这些真实依赖，并没有修改包来伪造完整发布能力。新旧 API 的实际链接通过不等于仅声明一个顶层二进制库就能安装；正式发布前仍需上游修复并验证完整依赖闭包。独立第三方库仍保留自己的身份。

锁检查需区分身份摘要与内容摘要：mcpp `v2026.11.2.2` 的 `index_package_digest` 对 `<namespace>:<lock-key>@<version>` 求值，短键 `os` 与限定键 `mcpplibs.os` 可对应同一个解析身份，却有不同 hash。gate 对照版本／来源，并通过现有 `mlibs.hash` 验证各自合法的身份摘要；不复制 hash 算法，不把该字段宣称为下载内容完整性证明，也不为测试改写仓库锁。依据为固定版本的[锁摘要实现](https://github.com/mcpp-community/mcpp/blob/v2026.11.2.2/src/pm/lock_io.cppm)与[记录入口](https://github.com/mcpp-community/mcpp/blob/v2026.11.2.2/src/build/prepare/records.cpp)。

N1 余下 gfx／content 基础库，N2 的 render／simulation／audio 与 N3 的 UI／标准连接继续按依赖顺序推进。名称迁移不增加 S1–S10 或 40／20 的功能验收计数。
