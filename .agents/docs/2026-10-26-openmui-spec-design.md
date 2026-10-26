# openmui 规范设计（openmui-spec 0.1 大纲）

> 状态：**草案 v0.2**（2026-10-26），待 review。这是 `spec/SPEC.md` 的设计依据，不是规范正文本身。
> 总体架构见 [architecture](2026-10-26-openmui-architecture-design.md)。

---

## 1. 范围和写法约定

openmui 规范定义的是：**UI 的语义树、视图描述、状态与 verb、宿主接口、渲染接口、C ABI**，以及动画语义、组件契约、扩展、清单结构和一致性测试（CTS）。

它**不**规定：实现采用什么架构、渲染器怎么实现、组件长什么样。

写法照搬 Luban 规范（opensal / openas）的约定：

1. **只依赖标准库**。规范的值类型模块在 MCU 上可以用 `mcpplibs.std.freestanding` 实现。
2. **所有值都可以跨地址空间传递**：不持有指针，也不回调（opensal §3.2）。
3. **版本只增不改**：新版本只会加接口、加字段、加消息；读取方跳过自己不认识的部分。
4. 每一节都要写出**可以检查的条款**，CTS 和条款一一对应。
5. 每个版本都有一节“本版本不解决的问题”。
6. **和 Luban 规范的关系**：概念上和 openas、opensal、openbus 兼容（取值和文本形式一致），但不 import 它们（决策 D-04）。

---

## 2. Registry：机器可读的唯一事实来源

```
spec/registry/
├── roles.toml              # role 集合（对齐 AccessKit / ARIA 的子集，Q-03）
├── states.toml             # 状态位
├── actions.toml            # 通用动作：activate / focus / set_value / increment / scroll_to / …
├── effects.toml            # read / reversible / irreversible / external（和 openas §3.1 一致）
├── origins.toml            # app / user_content / remote
├── motion.toml             # 缓动函数和 spring 的精确定义、动效 token 名
├── extensions.toml         # OMUI_EXT_* 列表：名字、版本、依赖关系
├── profiles.toml           # 发行版配置档：full / mobile / embedded-linux / mcu / engine / headless
├── components/*.contract.omui.toml   # Core 组件的契约（格式和第三方组件相同）
├── manifest.schema.toml    # [package.metadata.openmui] 的结构
├── omui.ebnf               # .omui 语言的语法
├── view.schema.json        # .omui.json 视图文档的 JSON Schema
└── expr.toml               # 表达式子集：运算符和内置函数表（兼容 CEL 语法）
```

**由 registry 生成**：`include/openmui.h`、`src/openmui.spec.cppm`（值类型）、TS / Python / Lua 的类型存根、文档里的表格、CTS 骨架。

---

## 3. 契约①：语义树

### 3.1 节点

| 字段 | 类型 | 说明 |
|---|---|---|
| `id` | 路径字符串 | **稳定**：同一个逻辑控件在多帧、多次会话之间 ID 不变；字符集和 openbus 的路径段兼容 |
| `role` | 枚举 | 见 `roles.toml` |
| `name` / `description` / `value` | 字符串 | `name` 是无障碍名称（已经过 i18n） |
| `origin` | 枚举 | `app` / `user_content` / `remote`：标出文本的来源 |
| `state` | 位集合 | disabled / focused / checked / selected / expanded / busy / hidden / **animating** / **removing** |
| `actions` | 列表 | 每个动作写明 `name`、`effect`、可选的 `verb`、可选的 `answer`、参数 |
| `bounds` | 可选矩形 | 逻辑像素；只有语义、没有布局时可以没有。正在动画时给出**目标状态**下的位置 |
| `children` | ID 列表 | 用 ID 引用子节点（值语义） |

### 3.2 条款（草案）

- **S-1** 效果为 `irreversible` 或 `external` 的动作必须写明 `answer`。
- **S-2** 执行一个需要确认、但调用方没有提供 `answer` 的动作时，返回 `needs-answer`，并给出能回答它的选项；**绝不等待**（和 openas §3.3 的语义一致）。
- **S-3** 来源为 `user_content` 或 `remote` 的文本，在消费方（agent）那里只能当作数据，不能当作指令。
- **S-4** 处于 `removing` 状态的节点不接受动作。
- **S-5** 语义树有 revision 号；变更是有序的 added / removed / updated 序列。
- **S-6** 可交互的节点必须有 `name`（构建期由语义检查强制）。
- **S-7** 语义树可以**虚拟化**（例如画布、长列表）：没有展开的节点通过查询按需生成；查询覆盖整个内容，不受视口限制。
- **S-8** 在保留模式下，没有显式 ID 的节点使用**结构化 ID**：`<类型>:<同类兄弟中的序号>`，例如 `column:0/text:0`。保留模式的所有写法（原生 builder、`.omui`、`.omui.json`）都用同一条规则，所以同一个视图的语义树完全相同。立即模式的自动 ID 由 ID 栈加调用位置生成（只保证在同一次构建里稳定）。**可交互的节点应当写显式 ID**（缺少时 lint 警告），因为自动 ID 会随结构或代码的调整而变化。
- **S-9** **ID 不能依赖显示文本**（label、经过 i18n 的文案）。否则切换语言以后 ID 会变，agent 脚本和测试就失效了。

---

## 4. 契约②：视图描述

- **视图 IR**：声明式的树，包含组件类型、`#id`、属性、绑定、事件处理、条件、循环、插槽、过渡。**IR 是唯一必需的东西，视图文件都是可选的**（D-28）。
- **IR 的四种来源，彼此等价**：
  1. 各语言原生的 builder；
  2. 立即模式在每帧对账时的产出；
  3. `.omui` 视图语言；
  4. `.omui.json` 视图文档（生成式 UI 的入口）。
  `.omui` 和 `.omui.json` 可以无损互转。详见 [omui-language](2026-10-26-openmui-omui-language-design.md)。
- **布局语义是规范性的**：以 CSS flexbox 和 grid 的一个子集为准；**取整规则**（逻辑像素到物理像素的换算、对齐像素的策略）写进规范，属于 A 级一致性。
- **表达式**：**兼容 CEL 语法的子集**。没有副作用，一定能求值结束，有类型，由原生代码在 UI 线程上同步求值。脚本里写的 computed 闭包是异步的（最多晚一帧），规范必须写明这一区别。
- **事件处理**：在 `.omui` 和 `.omui.json` 里只允许三种操作：修改 signal、调用 verb、发出事件。
- **可读性条款**：可交互的节点必须有 `#id`（缺少时 lint 警告）；名字必须和契约一致；格式由 `omui fmt` 唯一确定。

## 5. 契约③：状态与 Verb

- **signal**：创建、读取、写入、订阅；可以和组件属性做单向或双向绑定。
- **verb**：名字（小写单词，用点号分隔）、摘要、效果、参数、能回答询问的选项。**结构和 openas §4 的 verb 描述一致**，所以“操作 CLI”和“操作 UI”的语义相同。
- **event**：组件契约里声明；可以标注 `coalesce = "frame"`（按帧合并）。

---

## 6. 契约④：宿主接口

和 opensal 的模型兼容：像素（预乘 BGRA、stride、脏区）、事件（按键、文本提交、预编辑、指针、滚轮、焦点、关闭请求、帧已显示、可见性）、可选接口（Text、Clipboard、Cursor、PointerLock、Displays、Region、Compressed）。

此外还有：
- **宿主驱动帧**：`frame(dt, events) → (语义树变化, 显示列表, 脏区)`。这是嵌入游戏引擎和 headless 测试的前提。
- **GPU 宿主**：可选扩展，宿主提供 GPU 设备或者 render pass（`OMUI_EXT_host_render_pass`）。

---

## 7. 契约⑤：渲染接口

- **显示列表的图元**：矩形、圆角矩形、路径、图片、字形串、裁剪、变换、图层、外部纹理。
- **能力查询**：实例化绘制、着色器效果、外部纹理、渲染到纹理等，通过扩展提供。
- **规范性的内容**：混合在哪个色彩空间进行（Q：线性还是 sRGB，需要明确）、预乘 alpha、取整规则。
- **CPU 参考渲染**：作为 B 级一致性的基准，要求逐字节确定。

---

## 8. 契约⑥：C ABI（`openmui.h`）

- 由 registry 生成；**所有语言绑定只面向它**（参考 webgpu.h 的经验）。
- 只有句柄和值；**不让任何 C++ 类型越过边界**；不抛异常，错误作为返回值。
- 版本只增：结构体带 `sType` / `next`（或者等价的可扩展机制），向前兼容。
- 命名用 Vulkan 风格，前缀 `omui` / `Omui` / `OMUI_`（D-17、D-25）。示意：

```c
OmuiResult omuiCreateRuntime(const OmuiRuntimeCreateInfo* info, OmuiRuntime* out);
OmuiResult omuiEnumerateExtensions(OmuiRuntime rt, uint32_t* count, OmuiExtensionProperties* props);
OmuiResult omuiMount(OmuiRuntime rt, OmuiSlot slot, const OmuiViewDesc* view);
OmuiResult omuiSet(OmuiRuntime rt, OmuiNode node, const OmuiPropBatch* batch);    // 批量，在帧边界生效
OmuiResult omuiOn(OmuiRuntime rt, OmuiNode node, OmuiEventId ev, PFN_omuiEvent fn, void* user);
OmuiResult omuiRegisterVerb(OmuiRuntime rt, const OmuiVerbDesc* verb, PFN_omuiVerb fn, void* user);
OmuiResult omuiModelPush(OmuiRuntime rt, OmuiModel model, const OmuiColumnBatch* batch);
OmuiResult omuiFrame(OmuiRuntime rt, const OmuiFrameInput* in, OmuiFrameOutput* out);
OmuiResult omuiSnapshot(OmuiRuntime rt, const OmuiQuery* q, OmuiTree* out);
OmuiResult omuiAct(OmuiRuntime rt, const OmuiActRequest* req, OmuiEnvelope* out);
```

---

## 9. 总线接口：`openmui.tree@0.1`

路径约定：`host/<h>/system/<s>/app/<app-id>/ui`（按 openbus §2.4 的约定）。

| 名称 | 交互类型 | 作用 |
|---|---|---|
| `snapshot` | call | 获取树；可以指定子树、深度、只取可交互的节点、分页 |
| `changes` | topic | 增量变更 |
| `find` | call | 按 role、名字通配、状态、**空间范围**查找 |
| `act` | call | 执行动作；带 actor（和 openas 的文本形式相同）；返回信封 |
| `wait` | call | 等待条件成立（节点出现、状态变化、**动画完成 `settled`**），带超时 |
| `capture` | stream / region | 可选：节点区域的像素，用于视觉校验 |

**信封**：和 openas §5 的 NDJSON 信封兼容（phase / progress / log / notice / error / data / result）。

**MCP 桥**：工具名为 `ui_snapshot`、`ui_find`、`ui_act`、`ui_wait`、`ui_capture`（由 `openmui-tools/mcp` 提供）。

---

## 10. 动画语义

| 写进规范 | 交给实现 |
|---|---|
| 过渡的写法：属性、时长、延迟、曲线 | 插值的具体实现、调度方式 |
| **缓动函数的精确公式**：cubic-bezier 和标准曲线（照 CSS easing 规范的严谨程度来写） | 渲染方式 |
| **spring 的精确公式**：刚度、阻尼、质量、初速度；被打断时保留速度、转向新目标 | 时间线编辑器 |
| `animating` / `settled` / `removing` 的语义；语义树给出的是目标状态 | Lottie / Rive 的支持（扩展 `OMUI_EXT_lottie` / `OMUI_EXT_rive`） |
| “减少动效”的语义：全局动效倍率；spring 直接跳到终态 | — |
| 时钟可以注入；headless 下按步推进，结果确定 | — |

---

## 11. Core 配置档和扩展

**Core 组件契约（0.1 先定这些）**：Button、Toggle、Checkbox、Radio、Slider、TextBox、Select、Menu / MenuItem、Tabs、Dialog、Tooltip、ScrollView、List / VirtualList、Text、Image。

**第一批扩展**：

| 扩展 | 用途 |
|---|---|
| `OMUI_EXT_ime` | 输入法 |
| `OMUI_EXT_instanced_draw` | 实例化绘制（画布） |
| `OMUI_EXT_render_to_texture` | 图层缓存（Markdown 墙） |
| `OMUI_EXT_shader_effect` | 节点或图层的着色器效果 |
| `OMUI_EXT_gpu_node` | 在一个 render pass 里自己接管绘制 |
| `OMUI_EXT_external_texture` | 外部纹理（视频、窗口、引擎画面） |
| `OMUI_EXT_canvas` | 画布语义：空间查询、视口动作、虚拟化语义 |
| `OMUI_EXT_native_handle` | 拿到原生句柄（逃生口，不可移植） |
| `OMUI_EXT_host_render_pass` | 在宿主的 render pass 里绘制（引擎） |
| 以后再加 | `world_space`、`gamepad_navigation`、`frame_budget`、`lottie`、`rive`、`xr` |

---

## 12. 发行版配置档

| 配置档 | 脚本语言要求 | 说明 |
|---|---|---|
| `full`（桌面） | **TS/JS、Python、Lua 都必须提供** | 完整发行版 |
| `mobile` | TS/JS、Lua | Python 可选 |
| `embedded-linux` | 至少一种 | 推荐 Lua |
| `mcu` | 不要求 | 需要时用 Wasm |
| `engine` | 不要求 | 和引擎自带的脚本共存 |
| `headless` | 不要求 | 测试和 agent |

---

## 13. 一致性分级与 CTS 结构

```
spec/conformance/
├── S/   语义与行为：每个组件契约的语义、键盘、动作效果；四种写法产出的语义树必须相同；三种语言的结果必须相同
├── A/   布局快照、动画按时间点取帧、文本排版
├── B/   渲染金图（CPU 逐字节一致；GPU 按容差对比）
└── C/   平台适配表的用例（模拟不同平台）
```

- S 级和 A 级是**规范性的**（实现必须通过）；B 级是**建议性的**；C 级用来检查平台适配表本身。
- 跨语言用例用语言中立的 JSON 步骤描述，分别由 TS、Python、Lua 执行。

---

## 14. 清单结构：`[package.metadata.openmui]`

（字段设计见 [组件、绑定与工程模型](2026-10-26-openmui-component-binding-design.md) §4；文件后缀体系见 [omui-language](2026-10-26-openmui-omui-language-design.md) §7）

规范只定义**结构**；`mcpp.toml` 的 `[package.metadata.openmui]` 是默认载体，其他构建系统可以用 JSON 承载同样的结构。

---

## 15. 0.1 版不解决的问题

- token、授权范围、预算等更细的权限模型（沿用 openas §7 同样留空）；
- 跨框架的原生控件映射（X3，只承诺 Core 配置档里的一个子集）；
- 世界空间 UI、手柄导航、XR；
- 富文本编辑器、代码编辑器的组件契约。
