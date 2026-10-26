# openmui 组件、绑定与工程模型设计

> 状态：**草案 v0.1**（2026-10-26），待 review
> 总体架构见 [architecture](2026-10-26-openmui-architecture-design.md)；契约的结构由 [spec](2026-10-26-openmui-spec-design.md) 定义。

---

## 1. 组件分三层

```
应用：import openmui;            ← 伞包，一行导入，开箱即用
┌─────────────────────────────────────────────────────────────────┐
│ 设计系统层（有样式，可以替换）：openmui-theme-default · 紧凑 · 游戏皮肤 · 第三方 │  ← 上层：自由竞争
├─────────────────────────────────────────────────────────────────┤
│ 行为组件层（没有样式）：状态机 · 焦点与导航 · 语义 · 部件槽位        │  ← 官方提供，没有特权
├─────────────────────────────────────────────────────────────────┤
│ 运行时核心：原语（Box / Text / Image / Scroll / Input / Canvas）· 布局 · 文本 · 动画 │
├─────────────────────────────────────────────────────────────────┤
│ 规范：组件契约（Core 组件的语义和行为）                            │  ← 最底层
└─────────────────────────────────────────────────────────────────┘
```

- **先例**：Qt Quick 的 QtQuick.Templates 和 Controls 样式的分层；Flutter 3.41 起把 Material / Cupertino 拆成独立包；Web 上 Radix / React Aria 和 shadcn 的组合。
- **没有特权**：官方组件只能使用运行时的公开 API，用 lint 强制。如果官方组件需要某种能力，这种能力必须先变成公开 API。
- **版本节奏**：组件契约跟随规范（最慢）；行为组件跟随运行时；设计系统各自独立（最快）。

---

## 2. 组件工程范式

### 2.1 一个组件包的结构

```
openmui-ui-slider/
├── mcpp.toml                       # 包级声明：[package.metadata.openmui] kind = "component"
├── contract/slider.contract.toml   # 组件契约：接口的唯一事实来源
├── src/
│   ├── slider.cppm                 # C++ 行为实现
│   └── slider.parts.cppm           # 部件布局（外观交给主题）
├── views/slider.mui                # 可选：默认的视觉组合
└── tests/                          # S / A / B 用例（骨架由契约生成）
（没有手写的绑定代码）
```

### 2.2 契约文件

```toml
# contract/slider.contract.toml
[component.slider]
role = "slider"
spec = "0.1"

[props]
value    = { type = "f64",  default = 0,   bindable = true }
min      = { type = "f64",  default = 0 }
max      = { type = "f64",  default = 100 }
step     = { type = "f64",  default = 1 }
disabled = { type = "bool", default = false }

[events]
changed   = { args = { value = "f64" }, coalesce = "frame" }
committed = { args = { value = "f64" } }

[methods]                       # 少数需要命令式调用的操作（Slider 没有）

[actions]                       # 键盘、agent、脚本共用同一个入口
increment = { effect = "reversible" }
decrement = { effect = "reversible" }
set_value = { effect = "reversible", args = { value = "f64" } }

[keys]
"Right|Up"  = "increment"
"Left|Down" = "decrement"
"Home"      = "set_value(min)"
"End"       = "set_value(max)"

[parts]
track = {}
thumb = {}
label = {}

[motion]
thumb = "motion.fast"
```

### 2.3 从契约生成的内容

```
*.contract.toml ──openmui.rules──► C++：属性存储、事件枚举、动作分发、CRTP 基类（模块分区 :contract）
                                ├─► C ABI：属性 / 事件 / 动作的 ID 表
                                ├─► TS .d.ts / Python .pyi / Lua 的 LuaLS 注解和类型化封装
                                ├─► .mui 编译器的类型信息
                                ├─► 语义检查规则
                                ├─► 文档和画廊条目
                                ├─► CTS 骨架
                                └─► 组件目录（机器可读，供 agent 和生成式 UI 使用）
```

### 2.4 C++ 实现：编译器强制和契约一致

```cpp
export module openmui.ui.slider;
import openmui.core;
import :contract;                                    // 生成的部分

export namespace openmui::ui {

class Slider : public SliderContract<Slider> {       // CRTP：没有虚函数，S 边界零开销
public:
    void on_pointer(const PointerEvent& e);          // 热路径
    void on_action(SliderAction a, const Args& args);// 键盘 / agent / 脚本统一走这里
    void layout(LayoutCtx& ctx);
};

}
```

生成的基类要求每个动作和方法都有实现，**漏掉一个就编译失败**。

---

## 3. 契约的两种写法，编译成同一种 IR

```
*.contract.toml（数据形式：工具、agent、其他语言都容易读取） ─┐
                                                             ├─► 契约 IR（规范定义的结构）──► 所有生成产物
.mui 里的 export component（作者友好，适合用 DSL 组合的组件）─┘
```

```text
export component Markdown {
    in property <string> source;
    in property <Origin> origin: user_content;
    callback link_activated(href: string);
    action open_link   effect external   answer "yes";
    action toggle_task effect reversible;
}
```

- 用 C++ 实现的组件，使用 `.contract.toml`。
- 用 DSL 组合出来的组件，直接在 `.mui` 里声明。
- **规范 registry 里的 Core 组件契约也使用这套结构**，所以官方组件和第三方组件完全一致。

### 为什么不把契约写进 mcpp.toml

| 方案 | 结论 |
|---|---|
| 全部写进 mcpp.toml | ❌ 一个组件包可能有 20 多个组件，mcpp.toml 会膨胀；构建配置和 API 定义混在一起；`[package.metadata]` 更适合放简短的声明 |
| **独立文件，mcpp.toml 只引用路径** | ✅ 采用 |
| 写在 C++ 里反推 | ❌ 模块里不能用宏；契约应该是语言中立、可以独立评审的 |
| 只写在 `.mui` 里 | 部分采用（作为第二种写法） |

**判断标准**：包是什么、包含什么、应用需要什么权限，写进 mcpp.toml；某个组件的接口长什么样，写进契约文件。

---

## 4. `[package.metadata.openmui]` 的字段（草案）

| 字段 | 用于哪些 kind | 说明 |
|---|---|---|
| `kind` | 全部 | spec / runtime / backend / host / bridge / component / native / theme / lang / tool / app |
| `contracts` | component、native | 契约文件的 glob |
| `views` | component、app | `.mui` 的 glob |
| `themes` | theme、component | 主题和 token 文件 |
| `id` | app | 反向域名形式的应用 ID |
| `spec` | app、component | 需要的规范版本 |
| `extensions` | app | 必需的扩展（可选扩展在运行时查询） |
| `profile` | app | full / mobile / embedded-linux / mcu / engine / headless |
| `host` | app | `native`（编译 C++）或 `player`（预编译播放器） |
| `scripts` | app | `[{ lang, entry, deps }]` |
| `natives` | app | 应用自带的 native 契约 |
| `verbs.<name>` | app | 应用级 verb：effect、answer |
| `permissions` | app | 平台服务的权限：效果和范围 |

组件包和应用的示例：

```toml
# 组件包
[package.metadata.openmui]
kind      = "component"
contracts = ["contract/*.contract.toml"]
views     = ["views/*.mui"]

# 应用
[package.metadata.openmui]
kind       = "app"
id         = "org.example.notes"
spec       = "0.1"
extensions = ["MUI_EXT_ime"]
views      = ["ui/*.mui"]

[package.metadata.openmui.verbs."note.delete"]
effect = "irreversible"
answer = "yes"

[package.metadata.openmui.permissions]
fs = { effect = "reversible", scope = "~/Documents/Notes" }
```

---

## 5. 两种写法

### 5.1 立即模式

```cpp
if (ui.button("保存", {.verb = "doc.save", .effect = openmui::Effect::reversible})) doc.save();
// 签名：bool button(std::string_view label, ButtonOpts = {},
//                   std::source_location at = std::source_location::current());
// ID = ID 栈 + label（重名时用 at 的 file:line:column 区分）；不需要 "##id"，也不需要宏
```

### 5.2 保留模式和 DSL

```cpp
auto count = openmui::signal(0);
openmui::button("加一").verb("counter.increment", openmui::Effect::reversible)
                      .on_activate([=] { count.update([](int v) { return v + 1; }); });
```

```text
component Counter {
    property <int> count: 0;
    Button { text: "加一"; verb: "counter.increment"; activated => count += 1; }
}
```

### 5.3 统一

- 立即模式每帧声明，再按 ID 和影子树对账；没有事件、定时器、动画时**不调用 build**。
- 保留模式在 signal 变化时局部重建。
- 对账之后两者是同一棵树；可以互相嵌套。
- MCU 默认使用保留模式。

---

## 6. 脚本薄接口（Script Binding Profile）

### 6.1 操作清单（语言中立，映射到 C ABI）

| 分类 | 操作 |
|---|---|
| 视图 | `mount(slot, view)` / `unmount(slot)` |
| 节点 | `find(selector)` → 句柄；`get(h, prop)`；`set(h, props)`（批量，在帧边界生效） |
| 事务 | `batch(fn)` |
| 事件 | `on(h, event, fn)` / `off`（高频事件按帧合并） |
| 状态 | `signal(v)`；`bind(h.prop, signal)` |
| Verb | `verb(name, {effect, answer}, fn)` / `invoke(name, args)` |
| 模型 | `model(columns)` / `push(batch)` / `patch(range)`：列式或二进制，批量交付 |
| 异步 | `task`、`timer`、`await` |
| 语义 | `snapshot` / `act` / `wait`（**和 agent 用的接口相同**） |
| 平台服务 | `platform.*`（按能力提供，每个操作都标注效果） |
| 诊断 | `log` / `span` |

**不在接口里的**：任何手写的控件专属 API。控件的类型化方法都是由契约生成的语法糖，底层就是 get / set / on / act。

### 6.2 接口语义

- **值模型**和规范一致，所以同一套接口既能在进程内调用，也能通过总线远程调用。**脚本就是进程内的 agent。**
- **线程模型**：UI 线程（C++）持有树；每种语言的虚拟机在自己的逻辑线程上运行；两边通过消息队列交换值。
- **错误**：跨边界不抛异常；错误作为值返回，再由绑定映射成各语言惯用的形式。
- **生命周期**：句柄失效时返回明确的错误；脚本热重载时，状态保留在 C++ 侧的 signal 里。
- **安全**：平台服务受清单里的权限约束；第三方脚本插件放进沙箱（受限的 VM 环境或 Wasm）。

### 6.3 五条性能纪律

1. 脚本永远不进入每帧路径。
2. 能声明的就不写回调。
3. DSL 的绑定表达式不是 JS。
4. 跨语言调用要粗粒度、批量；高频事件按帧合并。
5. 大数据通过列式或二进制缓冲交付（ArrayBuffer / memoryview / userdata），虚拟化留在 C++ 里。

---

## 7. 三种语言

| 语言 | 引擎 | 模式 | 说明 |
|---|---|---|---|
| **TS / JS** | `quickjs-ng`（mcpp-index 中已有） | 嵌入；在 Web 上直接用浏览器的引擎 | TS 在构建期转成 JS（工具链待定，Q-09）；以后可以把 V8 / Hermes 作为可选引擎 |
| **Python** | CPython（C API 或 `pybind11`，index 中已有） | **作为宿主**（`pip install openmui`）+ 嵌入 | 嵌入时体积较大，作为可选特性 |
| **Lua** | Lua 5.4（`compat.lua`，index 中已有） | 嵌入 | 体积最小，适合游戏和嵌入式 Linux |

- **命名规则**：TS 和 Lua 用驼峰，Python 用下划线；ID 和 verb 名在三种语言里完全相同。
- **异步映射**：TS 用 Promise，Python 用 asyncio，Lua 用协程。
- **播放器变体**：`openmui-player`（JS + Lua）、`openmui-player-py`、`openmui-player-all`。
- **一起设计、分先后实现**：三种语言的接口形状从一开始就一起设计，避免混进某一种语言的假设；实现时先打磨 TS，Python 和 Lua 紧跟其后。

```ts
// TS
import { app, verb, signal } from "openmui";
const volume = signal(30);
app.bind("settings/volume.value", volume);
verb("audio.mute", { effect: "reversible" }, () => volume.set(0));
```

```python
# Python
import openmui as mui
volume = mui.signal(30)
mui.app.bind("settings/volume.value", volume)

@mui.verb("audio.mute", effect="reversible")
def mute():
    volume.set(0)
```

```lua
-- Lua
local mui = require "openmui"
local volume = mui.signal(30)
mui.app.bind("settings/volume.value", volume)
mui.verb("audio.mute", { effect = "reversible" }, function() volume:set(0) end)
```

---

## 8. native 模块（`kind = "native"`）

**非 UI 的原生能力**（计算、索引、编解码）同样由契约驱动、自动生成绑定，相当于 React Native 的 TurboModules 或 Tauri 的 commands。

```toml
# native/search.contract.toml
[native.search]
[functions.query]
args    = { q = "string", limit = "u32" }
returns = "SearchHit[]"
effect  = "read"
async   = true
```

```ts
import { search } from "openmui:native/search";   // 由契约生成
const hits = await search.query("spring", 20);
```

**规则**：“用 C++ 写一个能被脚本调用的东西”只有一种做法：写契约 + C++ 实现，绑定自动生成。组件和 native 模块用的是同一套机制。

---

## 9. 多语言混合

```
┌─────────────── 应用 ──────────────────────────────────────────────────────┐
│ 视图    ui/*.mui（DSL；绑定表达式由原生代码求值）                            │
│ 逻辑    logic/*.ts │ *.py │ *.lua（事件处理、verb、流程；不进入每帧路径）      │
│ 插件    plugins/*（脚本或 Wasm，放进沙箱，权限受效果分级约束）                │
│ 原生    native/*（kind=native：热点计算）+ 自定义组件（kind=component）       │
└───────────────────────────────────────────────────────────────────────────┘
   数据交换只有四种：signal（状态）· model（批量数据）· verb（命令）· event（事件）
```

示例工程（混合语言的笔记应用，对应示例 12）：

```
notes/
├── mcpp.toml
├── ui/{main,preview}.mui
├── logic/main.ts                  # 应用逻辑
├── plugins/export_pdf.py          # Python：导出插件
├── plugins/macros.lua             # Lua：用户可以自己写的宏
├── native/{search.contract.toml,search.cppm}   # C++：全文索引
└── tests/
```

```toml
[package.metadata.openmui]
kind    = "app"
id      = "org.example.notes"
views   = ["ui/*.mui"]
natives = ["native/*.contract.toml"]
scripts = [
  { lang = "ts",  entry = "logic/main.ts",         deps = "package.json"   },
  { lang = "py",  entry = "plugins/export_pdf.py", deps = "pyproject.toml" },
  { lang = "lua", entry = "plugins/macros.lua" },
]
```

**脚本语言自己的依赖**：仍然写在 `package.json`、`pyproject.toml` 或 rockspec 里，使用各自原生的 lock 文件；由 `openmui.rules` 在 `mcpp build` 时调用 xlings 提供的 npm / uv / luarocks。对开发者来说只有一个入口命令。

---

## 10. 应用的工程形态和构建流水线

| 形态 | 模板（示意） | 需要 C++ 工具链吗 |
|---|---|---|
| C++ 应用 | `mcpp new --template openmui-cpp` | 需要（由 mcpp 管理） |
| 脚本优先应用 | `mcpp new --template openmui-ts`（或 `-py`、`-lua`） | **不需要**（使用预编译播放器；依赖 mcpp 的支持，Q-05） |
| 混合应用 | `mcpp new --template openmui-mixed` | 需要 |
| 组件包 / native 包 | `mcpp new --template openmui-component`（或 `-native`） | 需要 |
| 嵌入到其他宿主 | 以插件形式提供 | 取决于宿主 |

```
契约 ──生成──► C++ 分区 / C ABI / 各语言封装 / 类型信息 / CTS 骨架
ui/*.mui ────► 发布：.cppm；开发 / 播放器：字节码
assets/ ─────► 按目标烘焙：字体子集、图片、着色器（SPIR-V → MSL / DXIL）
脚本 ────────► TS → JS 打包；Python / Lua 打包
清单 + 视图 ─► 语义检查
src/*.cppm ──► mcpp 编译模块 ─► 链接运行时 + 选定的后端 ─► mcpp-plugins dist：appimage / wix / apple / apk / web
```

| 动作 | 命令（示意） |
|---|---|
| 运行（热重载） | `mcpp run --features dev`（`.mui` 解释执行，脚本热重载） |
| 测试 | `mcpp test`：H0 / H1 / H2 / H3 + agent 语义脚本 |
| 打包 | `mcpp pack --target …` |
| 发布应用 | `mcpp emit xpkg` → 用户执行 `xlings install <app>` |
| 发布库 | `mcpp publish` → mcpp-index |

---

## 11. 主题和动效 token

- **三层 token**：基础层（调色板、字号阶梯、间距阶梯）→ 语义层（surface / accent / danger / focus-ring）→ 组件层（`button.padding`）。
- **动效 token**：`motion.fast` / `motion.normal`、标准缓动曲线、spring 预设。“减少动效”的设置通过 token 一次生效。
- **主题包**：default、compact（工具类）、touch，以后再加游戏皮肤。

---

## 12. 组件完成标准

每个组件都要通过同一份检查清单：

- [ ] 契约完整：role、状态、动作及其效果、按键、部件、动效
- [ ] 键盘和手柄导航，焦点可见
- [ ] 读屏器可用，支持双向文字和输入法
- [ ] 主题 token 全部生效，暗色、亮色、两种密度都正常
- [ ] S / A / B 级用例；agent 用例（能用 find + act 操作，不可逆动作会被拦下）
- [ ] 两种写法、三种语言都有示例；有文档；有画廊条目

---

## 13. 官方组件规划

| 包 | 内容 | 里程碑 |
|---|---|---|
| `openmui-ui-base` | Core 组件（见 spec §11） | M1–M3 |
| `openmui-ui-markdown` | Markdown 渲染和流式输出 | M2 |
| `openmui-ui-canvas` | 高性能画布核心（只提供机制） | M4 |
| `openmui-ui-canvas-kit` | 画布的可选行为：选择、拖动、缩放手柄、吸附、连线、小地图、网格 | M4 |
| `openmui-ui-data` | DataGrid、TreeTable、Chart | M5 之后 |
| `openmui-ui-editor` | PropertyInspector、Docking、NodeGraph、Timeline | M5 之后 |
