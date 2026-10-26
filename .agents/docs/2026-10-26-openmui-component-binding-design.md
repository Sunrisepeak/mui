# openmui 组件、绑定与工程模型设计

> 状态：**草案 v0.2**（2026-10-26），待 review
> 总体架构见 [architecture](2026-10-26-openmui-architecture-design.md)；契约的结构由 [spec](2026-10-26-openmui-spec-design.md) 定义；视图和数据文件见 [omui-language](2026-10-26-openmui-omui-language-design.md)。
> 命名（D-25）：包和模块用 `openmui`，C++ 命名空间用 `omui`。

---

## 1. 组件分三层

```
应用：import openmui;                    ← 伞包，一行导入，开箱即用
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
- **没有特权**：官方组件只能使用运行时的公开 API，用 lint 强制。官方组件需要的能力，必须先变成公开 API。
- **版本节奏**：组件契约跟随规范（最慢）；行为组件跟随运行时；设计系统各自独立（最快）。

---

## 2. 组件工程范式

### 2.1 一个组件包的结构（由 `mcpp new --template openmui:component` 生成）

```
openmui-ui-slider/
├── mcpp.toml                              # [package.metadata.openmui] kind = "component"
├── contract/slider.contract.omui.toml     # 组件契约：接口的唯一事实来源
├── src/
│   ├── slider.cppm                        # C++ 行为实现
│   └── slider.parts.cppm                  # 部件布局（外观交给主题）
├── views/slider.omui                      # 可选：默认的视觉组合
└── tests/test_slider.cpp                  # S / A / B 用例（骨架由契约生成）
（没有手写的绑定代码）
```

### 2.2 契约文件

```toml
# contract/slider.contract.omui.toml
[contract.slider]
role = "slider"
spec = "0.1"

[contract.slider.props]
value    = { type = "f64",  default = 0,   bindable = true }
min      = { type = "f64",  default = 0 }
max      = { type = "f64",  default = 100 }
step     = { type = "f64",  default = 1 }
disabled = { type = "bool", default = false }

[contract.slider.events]
changed   = { args = { value = "f64" }, coalesce = "frame" }
committed = { args = { value = "f64" } }

[contract.slider.actions]            # 键盘、agent、脚本共用同一个入口
increment = { effect = "reversible" }
decrement = { effect = "reversible" }
set_value = { effect = "reversible", args = { value = "f64" } }

[contract.slider.keys]
"Right|Up"  = "increment"
"Left|Down" = "decrement"
"Home"      = "set_value(min)"
"End"       = "set_value(max)"

[contract.slider.parts]
track = {}
thumb = {}
label = {}

[contract.slider.motion]
thumb = "motion.fast"
```

### 2.3 从契约生成的内容

```
*.contract.omui.toml ──openmui.rules──► C++：属性存储、事件枚举、动作分发、CRTP 基类（模块分区 :contract）
                                     ├─► C ABI：属性 / 事件 / 动作的 ID 表
                                     ├─► TS .d.ts / Python .pyi / Lua 的 LuaLS 注解和类型化封装
                                     ├─► .omui 编译器和语言服务器的类型信息
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

export namespace omui::ui {

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

## 3. 契约的两种来源，编译成同一种 IR

```
*.contract.omui.toml（用 C++ 实现的组件；数据形式，工具和 agent 都容易读取） ─┐
                                                                            ├─► 契约 IR ──► 所有生成产物
.omui 里的 component 声明（用 DSL 组合出来的组件，见 omui-language §3.4）   ─┘
```

- **规范 registry 里的 Core 组件契约也使用同样的结构**，所以官方组件和第三方组件完全一致。
- **只在一种语言里使用的组合组件**（比如 TS 里写的一个函数），可以不写契约，因为它是由契约组件组合出来的，语义仍然完整。**需要跨语言复用时，就必须有契约**：要么写 `.contract.omui.toml`，要么用 `.omui` 的 `component` 声明。
- **为什么契约不写进 mcpp.toml**：一个组件包可能有 20 多个组件，mcpp.toml 会膨胀，构建配置和 API 定义也会混在一起。判断标准是：**包是什么、包含什么、应用需要什么权限**，写进 mcpp.toml；**某个组件的接口长什么样**，写进契约文件。

---

## 4. `[package.metadata.openmui]` 的字段（草案）

| 字段 | 用于哪些 kind | 说明 |
|---|---|---|
| `kind` | 全部 | spec / runtime / backend / host / bridge / component / native / theme / lang / tool / app |
| `contracts` | component、native | 契约文件的 glob |
| `views` | component、app | `.omui` / `.omui.json` 的 glob（可选） |
| `behaviors` | component、app | `.behavior.omui.toml` 的 glob |
| `themes` | theme、component | `.theme.omui.toml` 的 glob |
| `id` | app | 反向域名形式的应用 ID |
| `spec` | app、component | 需要的规范版本 |
| `extensions` | app | 必需的扩展（可选扩展在运行时查询） |
| `profile` | app | full / mobile / embedded-linux / mcu / engine / headless |
| `scripts` | app | `[{ lang, entry }]` |
| `natives` | app | 应用自带的 native 契约 |
| `deps.<lang>` | app、component | 非 C++ 依赖（§9.2） |
| `verbs.<name>` | app | 应用级 verb：effect、answer |
| `permissions` | app | 平台服务的权限：效果和范围 |

**入口由 openmui 的特性决定**（§10.2），不写在这里。

```toml
# 组件包
[package.metadata.openmui]
kind      = "component"
contracts = ["contract/*.contract.omui.toml"]
views     = ["views/*.omui"]

# 应用
[package.metadata.openmui]
kind       = "app"
id         = "org.example.notes"
spec       = "0.1"
extensions = ["OMUI_EXT_ime"]
views      = ["ui/*.omui"]

[package.metadata.openmui.verbs."note.delete"]
effect = "irreversible"
answer = "yes"

[package.metadata.openmui.permissions]
fs = { effect = "reversible", scope = "~/Documents/Notes" }
```

---

## 5. 写视图的几种方式（视图文件可选，D-28）

| 写法 | 立即模式 | 保留模式 |
|---|---|---|
| C++ / TS / Python / Lua 原生代码 | ✅ | ✅ builder |
| `.omui` | — | ✅ 可选 |
| `.omui.json` | — | ✅ 可选 |

### 5.1 立即模式（C++）

```cpp
if (ui.button("保存", {.verb = "doc.save", .effect = omui::Effect::reversible})) doc.save();
// 签名：bool button(std::string_view label, ButtonOpts = {},
//                   std::source_location at = std::source_location::current());
// ID：显式的 .id 优先；没有时用 ID 栈 + 调用位置（at 的 file:line:column）
// 不用 label 做 ID：label 是经过 i18n 的文本，切换语言后 ID 会变（spec S-9）；不需要 "##id"，也不需要宏
```

### 5.2 保留模式：同一个视图的几种写法（S 级用例：语义树必须完全相同）

只有可交互的按钮写了显式 ID `inc`；Column 和 Text 使用结构化的自动 ID（`column:0`、`column:0/text:0`），五种写法用同一条规则（spec S-8）。

```cpp
// C++
auto counter() -> omui::View {
    auto count = omui::signal(0);
    return omui::column({.gap = 8}, {
        omui::text({.text = omui::computed([=] { return std::format("点击了 {} 次", count()); })}),
        omui::button({.id = "inc", .text = "加一",
                      .on_activate = [=] { count.update([](int v) { return v + 1; }); }}),
    });
}
```

```ts
// TS（builder；也可以用 TSX，esbuild 原生支持，jsxFactory 指向 openmui）
const count = signal(0);
export const Counter = () => column({ gap: 8 }, [
  text({ text: () => `点击了 ${count()} 次` }),
  button({ id: "inc", text: "加一", onActivate: () => count.update(v => v + 1) }),
]);
```

```python
# Python（with 上下文管理器）
count = omui.signal(0)
def counter():
    with omui.column(gap=8):
        omui.text(text=lambda: f"点击了 {count()} 次")
        omui.button(id="inc", text="加一", on_activate=lambda: count.update(lambda v: v + 1))
```

```lua
-- Lua（表构造语法）
local count = omui.signal(0)
local function Counter()
  return omui.Column { gap = 8,
    omui.Text   { text = function() return ("点击了 %d 次"):format(count()) end },
    omui.Button { id = "inc", text = "加一", on_activate = function() count:update(function(v) return v + 1 end) end },
  }
end
```

```text
// .omui
omui 0.1
import openmui.ui.base
view Counter {
    signal count: int = 0
    Column { gap: 8
        Text { text: format("点击了 {} 次", count) }
        Button #inc { text: "加一"; on activate: count += 1 }
    }
}
```

### 5.3 统一的机制

- 立即模式每帧声明，再按 ID 和影子树对账；没有事件、定时器、动画时**不调用 build**。
- 保留模式在 signal 变化时局部重建。
- 对账之后两者是同一棵树，可以互相嵌套。MCU 默认用保留模式。
- **脚本里写的 computed 闭包是异步的**（在逻辑线程上求值，最多晚一帧）；需要同步的场景，用 `.omui` 里的表达式或原生绑定。
- **从脚本里使用立即模式**：不推荐，因为每个控件调用都要跨语言。Lua 写工具时可以例外。

---

## 6. 脚本薄接口（Script Binding Profile）

### 6.1 操作清单（语言中立，映射到 C ABI）

| 分类 | 操作 |
|---|---|
| 视图 | `mount(slot, view)` / `unmount(slot)`；`view(path)` 加载 `.omui` / `.omui.json` |
| 节点 | `find(selector)` → 句柄；`get(h, prop)`；`set(h, props)`（批量，在帧边界生效） |
| 事务 | `batch(fn)` |
| 事件 | `on(h, event, fn)` / `off`（高频事件按帧合并） |
| 状态 | `signal(v)`、`computed(fn)`；`bind(h.prop, signal)` |
| Verb | `verb(name, {effect, answer}, fn)` / `invoke(name, args)` |
| 模型 | `model(name)` / `push(batch)` / `patch(range)`：列式或二进制，批量交付 |
| 异步 | `task`、`timer`、`await` |
| 语义 | `snapshot` / `act` / `wait`（**和 agent 用的接口相同**） |
| 平台服务 | `platform.*`（按能力提供，每个操作都标注效果） |
| 诊断 | `log` / `span` |

**不在接口里的**：任何手写的控件专属 API。控件的类型化方法和 builder 都是由契约生成的语法糖，底层就是 mount / set / on / act。

### 6.2 接口语义

- **值模型**和规范一致，所以同一套接口既能在进程内调用，也能通过总线远程调用。**脚本就是进程内的 agent。**
- **线程模型**：UI 线程（C++）持有树；每种语言的虚拟机在自己的逻辑线程上运行；两边通过消息队列交换值。
- **错误**：跨边界不抛异常；错误作为值返回，再由绑定映射成各语言惯用的形式。
- **生命周期**：句柄失效时返回明确的错误；脚本热重载时，状态保留在 C++ 侧的 signal 里。
- **安全**：平台服务受清单里的权限约束；第三方脚本插件放进沙箱（受限的 VM 环境或 Wasm）。

### 6.3 五条性能纪律

1. 脚本永远不进入每帧路径。
2. 能声明的就不写回调。
3. 同步的绑定用表达式（兼容 CEL 的子集）由原生代码求值，不用 JS。
4. 跨语言调用要粗粒度、批量；高频事件按帧合并。
5. 大数据通过列式或二进制缓冲交付（ArrayBuffer / memoryview / userdata），虚拟化留在 C++ 里。

---

## 7. 三种语言

| 语言 | 引擎 | 模式 | 说明 |
|---|---|---|---|
| **TS / JS** | `quickjs-ng`（mcpp-index 中已有） | 嵌入；在 Web 上直接用浏览器的引擎 | 构建期用 esbuild 转换（D-23）；TSX 可选 |
| **Python** | CPython（C API 或 `pybind11`） | **作为宿主**（`pip install openmui`，M3 先做）+ 嵌入（D-24） | 宿主模式下主线程运行 C++ 事件循环并释放 GIL；Python 运行在逻辑线程上 |
| **Lua** | Lua 5.4（`compat.lua`） | 嵌入 | 体积最小，适合游戏和嵌入式 Linux |

- **命名规则**：TS 和 Lua 用驼峰，Python 用下划线；ID 和 verb 名在三种语言里完全相同。惯用的导入别名都是 `omui`。
- **异步映射**：TS 用 Promise，Python 用 asyncio，Lua 用协程。
- **一起设计、分先后实现**：接口形状一起设计；实现时先打磨 TS，Python 和 Lua 紧跟其后。

---

## 8. native 模块（`kind = "native"`）

非 UI 的原生能力（计算、索引、编解码）同样由契约驱动、自动生成绑定，相当于 React Native 的 TurboModules 或 Tauri 的 commands。

```toml
# native/search.native.omui.toml
[native.search.functions.query]
args    = { q = "string", limit = "u32" }
returns = "SearchHit[]"
effect  = "read"
async   = true
```

```ts
import { search } from "openmui:native/search";   // 由契约生成
const hits = await search.query("spring", 20);
```

“用 C++ 写一个能被脚本调用的东西”只有一种做法：写契约 + C++ 实现，绑定自动生成。

---

## 9. 多语言混合

### 9.1 结构

```
┌─────────────── 应用 ──────────────────────────────────────────────────────┐
│ 视图    原生 builder，或可选的 ui/*.omui / *.omui.json                        │
│ 逻辑    logic/*.ts │ *.py │ *.lua（事件处理、verb、流程；不进入每帧路径）      │
│ 插件    plugins/*（脚本或 Wasm，放进沙箱，权限受效果分级约束）                │
│ 原生    native/*（kind=native：热点计算）+ 自定义组件（kind=component）       │
└───────────────────────────────────────────────────────────────────────────┘
   数据交换只有四种：signal（状态）· model（批量数据）· verb（命令）· event（事件）
```

```
notes/                                   （mcpp new notes --template openmui:app-mixed）
├── mcpp.toml
├── ui/{notes,preview}.omui
├── logic/main.ts
├── plugins/export_pdf.py
├── plugins/macros.lua
├── native/{search.native.omui.toml,search.cppm}
├── lock/                                # 各语言的锁文件（§9.2），提交到仓库
└── tests/
```

```toml
[dependencies]
openmui             = { version = "0.1", features = ["entry-player"] }   # 脚本主导：播放器加载视图和脚本，native 模块链接进来
openmui-ui-markdown = "0.1"

[package.metadata.openmui]
kind    = "app"
id      = "org.example.notes"
views   = ["ui/*.omui"]
natives = ["native/*.native.omui.toml"]
scripts = [
  { lang = "ts",  entry = "logic/main.ts" },
  { lang = "py",  entry = "plugins/export_pdf.py" },
  { lang = "lua", entry = "plugins/macros.lua" },
]
```

### 9.2 非 C++ 依赖：写在 mcpp.toml 的扩展子段里，由 `openmui.rules` 处理（D-29）

```toml
[package.metadata.openmui.deps.ts]
"date-fns" = "^4.1"

[package.metadata.openmui.deps.py]
reportlab = ">=4.2"

[package.metadata.openmui.deps.lua]
inspect = { git = "https://github.com/kikito/inspect.lua", tag = "v3.1.3" }
lpeg    = { rock = "lpeg", version = "1.1.0", native = true }    # C 模块由 mcpp 编译，可以交叉编译
```

```
mcpp build
 └─ openmui.rules 读取 deps.*（通过 mcpp::graph_file()）
     ├─ ts ：在 target/omui/ts/ 下生成 package.json → npm（node 由 xlings 提供）→ esbuild 打包
     ├─ py ：在 target/omui/py/ 下生成 pyproject.toml → uv 安装到隔离环境
     ├─ lua：纯 Lua 模块按 git 或 tarball 下载并校验 sha256；C 模块作为 C 目标交给 mcpp 编译
     ├─ 锁文件：lock/package-lock.json、lock/uv.lock、lock/lua.lock
     └─ 编辑器配置：tsconfig.json、.venv、.luarc.json，指向由契约生成的类型存根
```

- 外部工具通过 `[feature-xlings]` 声明、按需安装。xim 索引里已经有 `node`、`esbuild`、`uv`、`python`；**还没有 `luarocks`**，所以第一阶段 Lua 依赖走 git / tarball + sha256。
- 提供 `omui add <lang> <pkg>` 来修改 mcpp.toml（代替 `npm add` 等命令）。
- 逃生口：`[package.metadata.openmui.deps.ts] manifest = "package.json"`，直接使用已有项目的清单。

---

## 10. 应用的工程形态、入口和模板

### 10.1 模板（由 openmui 包的 `templates/` 目录提供，D-27）

mcpp 的模板是包自带的纯数据：`templates/<name>/template.toml` 加若干 `*.in` 文件，没有钩子，也不执行脚本。

| 模板 | 命令 | 用途 |
|---|---|---|
| `app-cpp`（默认） | `mcpp new notes --template openmui` | C++ 应用 |
| `app-ts` / `app-py` / `app-lua` | `mcpp new notes --template openmui:app-ts` | 脚本优先的应用 |
| `app-mixed` | `… --template openmui:app-mixed` | 多语言混合的应用 |
| `component` | `… --template openmui:component` | 组件包 |
| `native` | `… --template openmui:native` | native 能力包 |
| `theme` | `… --template openmui:theme` | 主题包 |

列出全部模板：`mcpp new --list-templates openmui`。

### 10.2 入口由框架提供，用特性来控制（D-26）

| openmui 的特性 | 入口 | 适合 |
|---|---|---|
| `entry-player` | 播放器：读取清单，加载视图和脚本 | 纯 TS / Python / Lua 应用 |
| `entry-app` | 框架提供 `main`，再调用应用实现的 `omui_app()` 回调（类似 SDL3 的 main callbacks） | 不想自己写 `main` 的 C++ 应用 |
| 都不启用 | 应用自己写 `main` | 需要定制启动流程、或者嵌入其他宿主的情况 |

```cpp
// entry-app：C++ 应用只实现回调（声明在全局模块上）
extern "C++" auto omui_app() -> omui::AppSpec {
    return { .id = "org.example.notes", .view = main_view, .verbs = notes_verbs() };
}
```

- **过渡办法（现在就能用）**：mcpp 的 `kind = "app"` 要求 `main` 指向本包里的一个文件，所以模板先生成一个可见的 `src/main.cpp`，只有一行，注释写明“由 openmui 模板生成，一般不需要修改”。
- **最终形态**：已经向 mcpp 提了 issue [#845](https://github.com/mcpp-community/mcpp/issues/845)，提议在目标里支持 `entry = "openmui"`（由依赖的某个特性提供入口），用来代替 `main`。落地之后，模板默认启用 `entry-player` 或 `entry-app`，工程里就看不到 `main.cpp` 了。
- **测试**：mcpp test 只发现 `tests/**/*.cpp`，所以模板里带一个 `tests/test_ui.cpp`，由它运行 `tests/**/*.test.{ts,py,lua}` 这些语义测试脚本。

### 10.3 构建流水线

```
契约 ──生成──► C++ 分区 / C ABI / 各语言封装 / 类型信息 / CTS 骨架
*.omui / *.omui.json ─► 类型检查 + 语义检查 ─► 发布：C++ 代码或 .omuic；开发：解释执行，可热重载
*.behavior.omui.toml ─► 编译成原生状态机
assets/ ──────────────► 按目标烘焙：字体子集、图片、着色器（SPIR-V → MSL / DXIL）
deps.* + 脚本 ────────► npm / uv / Lua 获取 → TS 转 JS 并打包 → 打包 Python / Lua
src/*.cppm ───────────► mcpp 编译模块 ─► 链接运行时 + 选定的后端 ─► mcpp-plugins dist：appimage / wix / apple / apk / web
```

| 动作 | 命令（示意） |
|---|---|
| 运行（热重载） | `mcpp run --features dev` |
| 测试 | `mcpp test`：H0 / H1 / H2 / H3 + 语义脚本 |
| 检查和格式化 | `omui check` / `omui fmt` |
| 打包 | `mcpp pack --target …` |
| 发布应用 | `mcpp emit xpkg` → 用户执行 `xlings install <app>` |
| 发布库 | `mcpp publish` → mcpp-index |

---

## 11. 主题和动效 token

- **三层 token**：基础层（调色板、字号阶梯、间距阶梯）→ 语义层（surface / accent / danger / focus-ring）→ 组件层（`button.padding`）。
- **动效 token**：`motion.fast` / `motion.normal`、标准缓动曲线、spring 预设。“减少动效”的设置通过 token 一次生效。
- **主题包**：default、compact（工具类）、touch，以后再加游戏皮肤。文件是 `*.theme.omui.toml`。

---

## 12. 组件完成标准

- [ ] 契约完整：role、状态、动作及其效果、按键、部件、动效
- [ ] 键盘和手柄导航，焦点可见
- [ ] 读屏器可用，支持双向文字和输入法
- [ ] 主题 token 全部生效，暗色、亮色、两种密度都正常
- [ ] S / A / B 级用例；agent 用例（能用 find + act 操作，不可逆动作会被拦下）
- [ ] 原生 builder 和 `.omui` 两种写法、三种语言都有示例；有文档；有画廊条目

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
