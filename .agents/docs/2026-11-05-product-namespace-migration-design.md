# 产品归属、命名空间与兼容迁移

2026-11-05。补充形成 D-100，修订原“所有实现包都使用 openmui”规则；包粒度继续遵循 D-99。**命名规则立即用于新增能力；既有仓库尚未全量完成改名。** N0 已交付共享控制；N1 已迁移十二个 base 库、八个 gfx 包和五个 content 包，实际入口和兼容范围见 §5–§9；N2 的渲染引擎四包见 §10；N2 模拟组与旧 game facade 收敛见 §11；其余引擎和 UI 实现的后续迁移另行验收。

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
| gfx 的 rhi／shader／raster 及其子包 | 对应 `mlibs.*` | N1 八包已迁移，见 §8 |
| content 的 text／layout／markdown／i18n 与 cldr | 对应 `mlibs.*` | N1 五包已迁移，见 §9 |
| `modules/gfx/render/`：原 `openmui.render.*` | `xengine.render.*` | N2 本轮迁移，渲染特性仍记录进同一张图，见 §10 |
| `modules/base/simulation/` 和 gfx 下模拟 GPU adapter | `xengine.simulation.*` | N2 本轮迁移；旧 mgame.sim facade 收进本包，见 §11 |
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

## 8. N1第四组：GPU 契约、光栅器和着色器工具

| 原目录／职责 | 当前包／主要 module | 当前 C++ API |
|---|---|---|
| `modules/gfx/rhi` | `mlibs.rhi` | `mlibs::rhi` |
| `rhi/sdl`、`rhi/webgpu`、`rhi/webgl` | `mlibs.rhi.sdl`、`.webgpu`、`.webgl` | `mlibs::rhi::sdl`、`::webgpu`、`::webgl` |
| `rhi/cts` | `mlibs.rhi.cts` | `mlibs::rhi::cts` |
| `modules/gfx/raster` | `mlibs.raster` | `mlibs::raster` |
| `modules/gfx/shader` | `mlibs.shader` | `mlibs::shader` |
| `shader/rules`（宿主构建插件） | `mlibs.shader.rules` | `mlibs::shader::rules` |

八个既有包的目录和职责保留，不再按 api／legacy 增设清单。新旧入口指向同一 `.api` 声明和实现，`omui::rhi`、`omui::raster`、`omui::shader` 是新命名空间的别名。RHI 后端与 CTS 的旧入口同时导出父 RHI，子命名空间自然可达；不能再打开 `namespace omui::rhi` 写扩展，扩展应在 `mlibs::rhi` 下定义。构建插件保留 `openmui::shader::rules` 别名；它是宿主阶段的独立边界，不将 mcpp 的构建 API 带入运行时 shader 库。

### 8.1 生成链和数据兼容

- 清单依赖及 build.mcpp 使用新身份；生成器写出 `import mlibs.rhi;` 和 `mlibs::rhi::ShaderDesc`，输出放在构建目录的 `mlibs/shader/`。RHI CTS 的生成分区归 `mlibs.rhi.cts.api`。shader 自身的 Slang 路径分区归 `mlibs.shader.api:provisioned`。
- 命令行仍为 `omui-shader`，现有 `OMUI_*` 环境选项、Web JS／C 桥符号及 RHI wire 数字编码保留。改变包的 owner 不要求已配套的浏览器桥和着色器资源名字一起升级。
- RHI 的 Handle／SlotMap 和命令 arena 直接复用 `mlibs.memory`；raster 复用 `mlibs.geom` 与 `hanim.core`。没有复制句柄、数学或像素实现，也没有将 renderer 反向拉入动画创作内核。
- render／simulation 的构建插件消费者已使用新 rules，但它们自己的模块身份在 N2 迁移。源码 import 兼容、生成源码重建和构建 CLI 选择器是三个不同承诺；旧 `-p openmui.rhi` 不列入兼容保证。

### 8.2 独立消费者与交付边界

`foundation-migration` 扩展到十五个 mlibs 库，加上 raster 实际需要且未改名的 `hanim.core`，分别打包十六个库。source／packed × 新名／旧名／混用六种真实工程检查 RHI 命令和 wire、同一画布的裁剪与 PNG 回环、Slang 实际生成 SPIR-V／WGSL／ESSL，以及既有基础库的状态与类型身份。shader 同包有库和工具两个 target，打包显式选 `mlibs-shader`；工具入口不计入库实现，检查实际 consumer link 中没有它。

另有三个仓外 build.mcpp 消费工程，分别经新、旧和混合 rules 生成模块分区、编译并读取实际 ShaderDesc。它们复用原宿主插件，不是三个新的库。GPU 后端由各自真实 CTS 和浏览器门禁验证；不能把原生平台上返回 unsupported 的 Web 后端分支记作 WebGPU／WebGL 的设备验收。

当前 mcpp 的安装包传递依赖元数据缺口仍在，fixture 显式提供各依赖。shader 包内生成的 Slang provisioned 路径在本机有效，跨机器重定位尚未验收。此轮证明源码／静态包 API 兼容和实际生成链，未发布 registry 别名，也不承诺可搬到任意机器后直接运行的编译器安装包。完整记录见[实施台账 §19](2026-11-05-system-implementation-checkpoints-design.md#19-d-100n1第四组gfx-基础库与实际后端)。

N1 的 content 组和 N2／N3 继续开放；已有二十个包的迁移不等于全仓库命名收口，也不增加动画、效果或作品的功能数量。

## 9. N1第五组：文字、布局、Markdown 和本地化

| 目录 | 当前包／主要 module | 当前 C++ API | 同包旧 API |
|---|---|---|---|
| `modules/content/text` | `mlibs.text` | `mlibs::text` | `openmui.text`／`omui::typeset` |
| `modules/content/layout` | `mlibs.layout` | `mlibs::layout` | `openmui.layout`／`omui::layout` |
| `modules/content/markdown` | `mlibs.markdown` | `mlibs::markdown` | `openmui.markdown`／`omui::md` |
| `modules/content/i18n` | `mlibs.i18n` | `mlibs::i18n` | `openmui.i18n`／`omui::i18n` |
| `modules/content/i18n/cldr` | `mlibs.i18n.cldr` | `mlibs::i18n::cldr` | `openmui.i18n.cldr`／`omui::i18n::cldr` |

五个原有清单直接改为新归属，内部源码、分区和兼容入口共用清单。text 和 markdown 的新 C++ 名字与各自包名一致；旧 typeset／md 只是别名，不是另一套排版／文档实现。字体集合的身份和存活注册、CLDR 的惰性共享缓存、布局树与 Markdown stream 均只有一个 owner。namespace alias 不可重新打开；旧扩展块需改用实际命名空间。

文字库继续消费同一个矢量 renderer 的 FontRef、GlyphSource 和 DisplayList，使用限定名 `omui::render` 标明当前尚待 N2 迁移的 owner；未另外创建字体或显示列表类型，也不因其处于 mlibs 就宣称 renderer 已被裁掉。i18n 核心不默认带语言包，CLDR 仍是按需选择的既有数据包。content 迁移没有增加清单；同一检查点的 Hanim 内部收敛另移除六个清单，workspace 从 129 个成员降为 123 个。

### 9.1 资源、生成器和兼容

CLDR 的四个 JSON 原文件、数据格式／版本、`.locale.omui.json`／`.messages.omui.toml` 后缀及 `OMUI_FONTS` 环境选项保留；只调整 embed 的目标 module／namespace 为 `mlibs.i18n.cldr.api`／`mlibs::i18n::cldr`。通用 embed 构建插件仍用原身份，迁移在 N3 单独处理；不将宿主构建工具误算为 i18n 的 UI 运行时依赖。

官网／Toy 的字体目录查找原先只识别 graph 中的 `openmui.text`。现在接受 `mlibs.text` 和旧身份，拒绝其他 owner 的同名 `text` 包。回归分别从两种身份的 graph 实际打包、重新加载真实 Inter 字体并检查字形；官网另从真实 mcpp graph 重新打包，避免仅依赖测试工程的 `fonts_dir` 覆盖值掩盖资产丢失。

### 9.2 独立消费及后续

原 namespace consumer 扩为二十个 mlibs 库，并带上真实依赖的 hanim.core、render 与 render.vector，合计二十三个静态包。六种新／旧／混用 × 源码／静态包组合增加视口重排、Markdown stream／HTML、Locale 及共享 CLDR 格式化，以及拉丁／中文／阿拉伯文字的真实 PNG；比对六个工程及其重复运行的像素和 PNG 摘要。字体作为应用选择的资源显式提供，不宣称二进制 text 包已经自动捆绑字体。

审计只允许实际包 outDir 下明确的 shader、provisioned 和四个 locale 生成分区，校验 module 声明；不放行任意生成目录。各编译器／第三方包仍保留原身份。打包清单传递依赖缺口和 shader 工具重定位限制继续见 §8；此检查不是 mcpp-index 发布或顶层单依赖安装验收。

本地 CPU／Web、字体交付、官网与过程结果见[实施台账 §20](2026-11-05-system-implementation-checkpoints-design.md#20-d-100n1第五组content-与字体交付)。N1 列出的 base／gfx／content 身份已迁移；N2 的渲染／模拟／音频及 media 公共库、N3 的 UI／工具／标准类型、N4 的正式发布与兼容收口仍需独立验收。

### 9.3 安装包的链接顺序限制

真实 CLDR 静态包消费暴露了本地 path 依赖边缺失的另一后果：GNU ld 按错误顺序扫描 i18n／CLDR 归档，留下未解析符号。当前独立安装包 fixture 显式列出所用归档并通过 GNU ld group 重新扫描；没有编辑生成的包、改变接口摘要、强制导出全部对象或回退编译原实现。此结果只证明显式集成可用，正式 N4 发布仍需 mcpp 保留依赖边、正确自动链接顺序与仅声明顶层库的消费验证。

## 10. N2首组：渲染引擎及可选效果加载

| 现有目录 | 当前包／主要 module | 当前 C++ API | 同包兼容入口 |
|---|---|---|---|
| `modules/gfx/render` | `xengine.render` | `xengine::render` 的 Graph／TexturePool | `openmui.render`／`omui::render` |
| `render/vector` | `xengine.render.vector` | `xengine::render` 的 DisplayList／CpuRenderer／GpuRenderer／effects | `openmui.render.vector`／`omui::render` |
| `render/effect` | `xengine.render.effect` | `xengine::render::effect` | `openmui.render.effect`／`omui::effect` |
| `render/cts` | `xengine.render.cts`，仅设备测试 | 不导出运行时 module | CLI 选择器改为新身份 |

### 10.1 四个包的保留理由

这里按实际使用边界保留四个清单，workspace 仍为 123。Graph／TexturePool 有独立设备 pass 的完整任务；实际 CTS 在不构造 DisplayList 的情况下创建、裁剪、执行图并读回纹理。只用图与纹理池的消费者不用引入矢量几何、hanim、raster 或 shader 生成。vector 是完整的显示列表、资源、damage、CPU／GPU 渲染特性，被 Hanim、text、UI 与工具直接消费；内部 paint／pass／effect 不再各建清单。

effect 加载器隔离文件轮询、TOML 与运行时 Slang 编译，原生宿主可选；普通 vector／文字／动画使用内置效果不引入这个加载器。CTS 只隔离 SDL 与桌面设备测试，不制造运行时空库，Web 保留 portable CPU 测试。每个 README 已记录用途和隔离收益。后续新特性仍先比较包内目录／feature，不能将本轮保留理解为一特性一清单的规则。

### 10.2 唯一实现、生成链与实际消费者

三个运行时包采用 N1 已验证的主入口＋`.api`＋legacy 布局；Graph、DisplayList、资源、paint 和 EffectRegistry 的类型与状态只有一份。新旧 namespace 指向相同类型／函数；旧 namespace alias 不可重新打开，C++ 消费者需要重建。动态效果原来位于 `omui::effect`，新归属明确为 renderer 的 `xengine::render::effect`，不与 hanim 的参数计划或其他引擎的效果混用命名。

Hanim vector adapter 与 text 直接消费新 renderer 入口和类型；UI／官网保留旧源码入口验证兼容，清单依赖键更新为真实新身份。25 个原接口／实现单元仅作明确的 module、namespace 与底层类型限定调整，渲染、damage、混色、shader 算法不重写。

生成的绘图／效果 shader 分区现在归 `xengine.render.vector.api`／`xengine::render::shaders`；嵌入模板由真实 omui-gen 重新生成并通过一致性测试。`openmui_effect.slang`、effect ID、参数名、`.effect.omui.toml`、RHI wire 与 UI C ABI 保留。浏览器 shader 收集器按当前 workspace owner 过滤旧命名留下的缓存目录，不能让过期产物替代当前构建验收。

独立 namespace gate 包含 20 个 mlibs 库、hanim.core、三个 renderer 包，共 24 个实际归档。源码／安装包 × 新名／旧名／混用，增加共享注册表、真实 CPU 中间态像素和真实 Slang 动态效果注册，共 23 项；渲染 GPU／Web 与官网过程回归另行验收。§9.3 的显式依赖／归档组限制继续保留。完整证据归[实施台账](2026-11-05-system-implementation-checkpoints-design.md)的 N2 渲染检查点。

simulation、audio、timeline／video、UI／工具与标准连接继续按 N2／N3 迁移；本轮不宣称 3D 渲染、任意 GPU 调度、完整公共动画目录或整体美术已达标。

## 11. N2第二组：模拟归属与游戏时钟空壳收敛

四个既有产品边界改为 `xengine.simulation`、`xengine.simulation.cellular`、`xengine.simulation.cellular.rhi`、`xengine.simulation.cellular.rhi.cts`，C++ 归 `xengine::simulation` 及其子命名空间。每包的主入口、`.api` 和旧 import 共用清单与实现；shader 生成模块归新 API，规则、整数算子和 Slang 内核保持原行为。

保留独立边界是因为消费者确实不同：Clock／Timeline 供应用和游戏使用；CPU 规则库可不使用时钟；GPU adapter 显式添加 RHI；CTS 隔离测试，并同时服务 SDL／Vulkan 与 WebGPU。真实 resolver 图检查 CPU 不带 GPU、规则和 GPU 不反向引入 Clock。没有为兼容入口新增清单。

`mgame.sim` 原来只转发共享时钟并增加 trivially-copyable State 的 Game 约束，不构成独立实现。本轮删除其清单，源码入口移入 `modules/base/simulation/src/legacy_mgame.cppm`，原三项测试移到该包。canonical 入口也导出这个兼容模块，真实静态包包含它；workspace 从 123 减为 122。游戏 UI 和 breakout 改为依赖 `xengine.simulation`。旧 `import mgame.sim;`、`mgame::Clock`／`Timeline` 及更严格的 Game 约束仍可用。

兼容范围必须分开：旧 **C++ import** 有真实 source／packed 消费证据，旧 **依赖键** `mgame.sim` 指向新路径会被 mcpp 因包名 `sim`／`simulation` 不同而拒绝，旧目录及 `-p mgame.sim` 也不保留。应用须改清单键，不再为这个选择器建立一个空包。

旧根 `omui::simulation` 使用 Clock／Model／Timeline 的 using 声明；旧 cellular 入口仅给其下 cellular 子命名空间加 alias，因此单独使用规则库不会被兼容层带入时钟。它们没有重复实现或状态。

此后命名工作作为功能实现的配套事项；下一条主线是 Hanim 公共属性动画及实际消费者。命名收敛不增加公共动画、创意作品或平台完成数。
