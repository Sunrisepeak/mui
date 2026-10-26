# openmui 视图与数据文件设计：`.omui` 语言、视图文档、表达式、行为、后缀体系

> 状态：**草案 v0.1**（2026-10-26），待 review
> 决策来源：D-07、D-26 到 D-30（见 [architecture](2026-10-26-openmui-architecture-design.md) §13）
> **首要要求：人和 agent 的可读性都必须很高。**

---

## 1. 定位：视图文件完全可选

| 写法 | 立即模式 | 保留模式 | 适合 |
|---|---|---|---|
| **各语言原生的 builder**（C++ / TS，可选 TSX / Python / Lua） | ✅ 代码本身就是视图 | ✅ | 应用开发者的日常写法 |
| **`.omui`**（视图语言） | — | ✅ 可选 | 多种语言共享一份 UI；设计器来回读写；C++ 不重新编译就能热重载 |
| **`.omui.json`**（视图文档） | — | ✅ 可选 | 工具、agent、生成式 UI；可以用 JSON Schema 约束输出 |

- **只有视图 IR（规范契约②）是必需的。** 三种保留模式的写法完全等价，可以混用（例如用 `.omui` 写骨架，用 TS 的 builder 填充某个插槽）。
- **`.omui` 和 `.omui.json` 可以无损互转**，包括注释（在 JSON 里存为 `notes` 字段）。
- **能力关系**：`.omui` ⊆ 视图 IR ⊆ 原生 builder。DSL 能表达的，builder 都能表达；反过来不一定。
- **为什么要有 `.omui`**：独立的视图语言在四种情况下有明显优势：多种宿主语言共享 UI（Slint 的核心卖点）、设计器来回读写、编译型语言需要热重载、AI 生成 UI。openmui 这四种情况都会遇到。业界趋势是：单一语言的生态里，“视图写在代码里”占主流（SwiftUI、Compose、Flutter、React）；需要多语言和工具支持的场景里，标记语言仍然有优势（QML、Slint、XAML、Unity UXML）。

---

## 2. 可读性原则（人和 agent 都要读得懂）

| # | 原则 | 具体规定 |
|---|---|---|
| R1 | **用完整的英文单词，不用符号堆砌** | 写 `on activate:`，不写 `@click`；写 `bind(query)`，不写 `<->`；关键字少而直白 |
| R2 | **一个概念只有一种写法** | 没有语法糖的多种变体。只有一种格式，由 `omui fmt` 输出唯一的规范格式，diff 稳定，agent 生成的结果也一致 |
| R3 | **文件里写的就是运行时的树** | 节点上可以写 `#id`，它就是语义树里的 ID 段；没有写的节点使用结构化 ID（`<类型>:<序号>`，spec S-8）；**可交互的节点没有写 `#id` 时，lint 会给出警告**。agent 在语义树里看到的 `notes/root/search`，在文件里能直接找到 |
| R4 | **没有隐藏的逻辑** | 事件处理只允许三种操作：修改 signal、调用 verb、发出事件。业务逻辑在 verb 里，verb 可以用任何语言实现 |
| R5 | **名字和契约一致** | 组件名、属性名、事件名、动作名都直接来自契约；写错名字在构建期就报错，并给出最接近的候选 |
| R6 | **类型明确** | signal、model、组件属性都声明类型；表达式是类型化的 |
| R7 | **表达式没有副作用，并且一定能求值结束** | 使用兼容 CEL 语法的子集；没有循环，没有 IO |
| R8 | **诊断对人和机器都友好** | 报错带行号和列号，说明原因，并给出修改建议；`--json` 输出结构化的诊断（和 xlings、mcpp 的机器接口风格一致） |
| R9 | **语法小到一页纸能写完** | 规范里给出完整的 EBNF；`docs/llms.txt` 附带语法摘要和示例；`.omui.json` 发布 JSON Schema，支持受约束的生成 |
| R10 | **文件自带版本** | 第一行写 `omui 0.1`；读取方遇到不认识的字段，按“只增不改”的规则跳过 |

---

## 3. `.omui` 语言草案

### 3.1 一个完整的例子

```text
omui 0.1
import openmui.ui.base
import openmui.ui.markdown

// 笔记应用的主视图
view Notes {
    signal query: string = ""
    signal selected: string? = null
    model notes: Note                          // 由逻辑层用 model.push 推送数据

    Row #root { gap: 8

        Column #sidebar { width: 280
            TextBox #search {
                placeholder: tr("search.placeholder")
                value: bind(query)                         // 双向绑定
            }
            List #list {
                for note in notes where contains(note.title, query) {
                    Item #note(note.id) {                  // 动态 ID：notes/root/sidebar/list/note(42)
                        text: note.title
                        selected: selected == note.id
                        on activate: selected = note.id
                    }
                }
            }
        }

        Column #main { grow: 1
            if selected != null {
                Markdown #preview { source: lookup(notes, selected).body; origin: user_content }
                Button #delete {
                    text: tr("note.delete")
                    on activate: verb("note.delete", selected)    // 效果为 irreversible，在契约或清单里声明
                }
            } else {
                Text #empty { text: tr("notes.empty") }
            }
        }
    }
}
```

### 3.2 语法要素（全部列在这里）

| 要素 | 写法 | 说明 |
|---|---|---|
| 文件头 | `omui 0.1` | 规范版本 |
| 导入 | `import openmui.ui.base` | 导入组件包；名字从契约中解析 |
| 视图 | `view Name { … }` | 一个可以挂载的视图 |
| 组件声明 | `component Name { … }` | 用 DSL 组合出来的组件；可以声明接口（§3.4） |
| 状态 | `signal name: Type = value` | 类型是必需的 |
| 数据 | `model name: Type` | 列表或表格数据，由逻辑层批量推送 |
| 节点 | `Type #id { 属性… 子节点… }` | `#id` 可以省略，但可交互的节点省略时 lint 会警告 |
| 动态 ID | `#note(expr)` | 用在 `for` 里，生成稳定的 ID |
| 属性 | `name: expr` | 单向绑定；值是字面量时就是常量 |
| 双向绑定 | `name: bind(signal)` | 只用于契约里标了 `bindable` 的属性 |
| 事件 | `on event: 操作` / `on event { 操作; 操作 }` | 只能是 §3.3 规定的操作 |
| 条件 | `if expr { … } else { … }` | 依赖的是 signal 或 model |
| 循环 | `for x in model [where expr] { … }` | 只能遍历 model；会自动虚拟化 |
| 过渡 | `animate prop: spring(stiffness: 300, damping: 30)` / `ease(out_cubic, 200ms)` | 公式由规范定义（spec §10） |
| 插槽 | `slot name` | 由宿主语言的 builder 或别的视图来填充 |
| 文案 | `tr("key")` | 走 i18n 目录 |
| 注释 | `// …` | 转成 `.omui.json` 时保留为 `notes` 字段 |

### 3.3 事件处理里允许的操作

| 操作 | 例子 |
|---|---|
| 修改 signal | `selected = note.id`、`count += 1`、`toggle(open)` |
| 调用 verb | `verb("note.delete", selected)` |
| 发出事件 | `emit("submitted", value)`（只用在 `component` 里） |

**不允许**：函数定义、循环、IO、直接调用脚本函数。所有逻辑都写在 verb 里。由于 verb 的名字和效果在契约或清单里声明，**读者和 agent 不用读任何脚本就能知道一个按钮会做什么、有什么后果**。

### 3.4 用 DSL 组合组件并声明接口

```text
component SearchBox {
    in property placeholder: string = ""
    in-out property value: string = ""
    callback submitted(text: string)

    TextBox #input {
        placeholder: placeholder
        value: bind(value)
        on submit: emit("submitted", value)
    }
}
```

`openmui.rules` 会根据这里的声明**自动产出契约 IR**。所以别的语言（TS、Python、Lua、C++）可以像使用 C++ 组件一样使用 `SearchBox`，绑定照样自动生成。用 C++ 实现的组件仍然写 `.contract.omui.toml`。

### 3.5 和宿主语言配合

```ts
// logic/main.ts
import { app, verb, view } from "openmui";
verb("note.delete", { effect: "irreversible", answer: "yes" }, async (id: string) => store.remove(id));
app.model("notes").push(await store.list());
app.mount("main", view("ui/notes.omui"));
```

```cpp
// C++ 里挂载同一份视图，并填充插槽
app.mount("main", omui::view("ui/notes.omui").slot("toolbar", my_toolbar()));
```

### 3.6 编译和热重载

- **发布**：`openmui.rules` 把 `.omui` 编译成 IR，再生成 C++ 代码，或者生成二进制的视图格式（`.omuic`）；构建期完成类型检查和语义检查。
- **开发**：`.omui` 由运行时解释执行；文件改动后重新加载视图，和现有的树对账，signal 的状态保留下来。

---

## 4. 视图文档：`.omui.json`

上面那个例子中的一部分，用视图文档表示：

```json
{
  "omui": "0.1",
  "imports": ["openmui.ui.base"],
  "view": {
    "name": "Counter",
    "signals": { "count": { "type": "int", "value": 0 } },
    "root": {
      "type": "Column", "id": "root", "props": { "gap": 8 },
      "children": [
        { "type": "Text",   "id": "label", "props": { "text": { "expr": "format('点击了 {} 次', count)" } } },
        { "type": "Button", "id": "inc",   "props": { "text": "加一" },
          "on": { "activate": [ { "set": "count", "expr": "count + 1" } ] } }
      ]
    }
  }
}
```

- 结构由规范定义，并发布 JSON Schema。agent 可以按 Schema 做受约束的生成，再用 `omui fmt --to omui` 转成 `.omui`，方便人阅读。
- 这也是**生成式 UI**的入口：agent 通过总线发送一份视图文档，运行时负责渲染，并照常执行效果分级的检查。

---

## 5. 表达式：兼容 CEL 语法的子集

- **语法**沿用 CEL（Common Expression Language），不自己发明。
- **子集**：字面量、signal 和 model 字段的访问、算术、比较、逻辑运算、三元表达式、`null` 判断，以及一组内置函数（`format`、`contains`、`len`、`clamp`、`min`、`max`、`tr`、`select`、`lookup(model, key)` …）。**不能调用脚本函数**。
- **性质**：没有副作用；一定能求值结束；有类型；出错时返回规定好的默认值，并且发出一条诊断，不会抛异常。
- **求值**：由原生代码完成，在 UI 线程上同步执行（属于 A 级一致性）。
- **和 computed 闭包的分工**：脚本里写的 computed 闭包在逻辑线程上**异步**求值，最多晚一帧生效；需要严格同步的场景（动画、高频格式化）用表达式。

---

## 6. 行为状态机：`.behavior.omui.toml`

画布等组件的声明式交互，用数据化的 statechart 表示（语义参考 SCXML / XState），**不引入新语法**：

```toml
# group-drag.behavior.omui.toml
[behavior.group-drag]
on      = "canvas"
initial = "idle"

[behavior.group-drag.states.idle]
on.pointer_down = { when = "hit.kind == 'group.label'", to = "dragging", do = ["capture()"] }

[behavior.group-drag.states.dragging]
on.pointer_move = { do = ["move(hit.node, delta.world)"] }                     # 每帧在 C++ 里执行
on.pointer_up   = { to = "idle", do = ["commit('group.moved', hit.node)"] }     # 只在提交时通知脚本
```

- `when` 是表达式（§5）；`do` 只能调用宿主组件声明过的内置操作（例如 canvas 的 `capture`、`move`、`commit`）。
- 行为可以声明自己带来的语义动作，例如 `move` 的效果是 reversible。
- 更复杂的交互直接用 C++ 写行为（见 [canvas-markdown](2026-10-26-openmui-canvas-markdown-design.md) §1.3）。

---

## 7. 后缀体系

| 文件 | 后缀 | 例子 |
|---|---|---|
| 视图语言 | `.omui` | `notes.omui` |
| 视图文档 | `.omui.json` | `notes.omui.json` |
| 组件契约 | `.contract.omui.toml` | `slider.contract.omui.toml` |
| native 契约 | `.native.omui.toml` | `search.native.omui.toml` |
| 行为状态机 | `.behavior.omui.toml` | `group-drag.behavior.omui.toml` |
| 主题和 token | `.theme.omui.toml` | `default.theme.omui.toml` |
| 应用包（播放器加载） | `.omuipkg` | `notes.omuipkg` |
| 编译后的视图 | `.omuic` | 构建产物，不提交 |
| 逻辑代码 | `.ts` / `.py` / `.lua` / `.cppm` | 保持原生后缀，编辑器和工具链才能正常工作 |

规则：
- 所有 openmui 的数据文件都匹配 `*.omui.toml` 或 `*.omui.json`，编辑器可以按这个模式统一关联 schema；种类写在文件名里，一眼就能看出来。
- 文件内的顶层表（`[contract.x]`、`[native.x]`、`[behavior.x]`、`[theme.x]`）也会声明种类，即使文件名不规范也能识别。

---

## 8. 工具

| 工具 | 作用 |
|---|---|
| `omui fmt` | 输出唯一的规范格式；在 `.omui` 和 `.omui.json` 之间互转 |
| `omui check` | 类型检查、语义检查、ID 检查；`--json` 输出结构化诊断 |
| 语言服务器（LSP） | 补全（名字来自契约）、悬停时显示动作的效果、跳转到 verb 的实现 |
| `docs/llms.txt` | 语法摘要、规范格式的示例、常见错误和修改方法 |

---

## 9. 待细化

- EBNF 全文、保留字列表；
- 表达式子集的完整函数表；
- `for` 和虚拟化的交互细节（估算高度、key 的稳定性）；
- 视图文档中 `notes` 的位置规则（保证往返转换不丢失注释的位置）。
