# openmui 视图与数据文件设计：代码优先、视图文档、表达式、行为、后缀体系

> 状态：**草案 v0.3**（2026-10-26），待 review
> 决策来源：D-07、D-28 到 D-31、D-35、D-37（见 [architecture](2026-10-26-openmui-architecture-design.md) §13）
> **v0.3 的变化：取消 `.omui` 视图语言**（D-37），改为代码优先。前一版的语言草案保留在 git 历史里。
> **首要要求：人和 agent 的可读性都必须很高。**

---

## 1. 定位：代码优先，视图文件可选

| 写法 | 立即模式 | 保留模式 | 适合 |
|---|---|---|---|
| **各语言原生的代码**（C++ / TS，可选 TSX / Python / Lua），布局和逻辑写在一起 | ✅ | ✅ builder | **主要写法** |
| **视图文档** `*.view.omui.json`（纯数据，有 JSON Schema） | — | ✅ 可选 | 设计器、agent、生成式 UI、跨语言复用的组合组件、C++ 应用免编译热重载 |

- **只有视图 IR（规范契约②）是必需的。** builder 和视图文档是等价的，可以混用，比如加载一份视图文档，再用 builder 填充其中的插槽。
- **为什么不做视图语言**：
  - 一门视图语言加上条件、循环、绑定、事件以后，复杂度已经接近宿主语言，用户还要多学一门；
  - LLM 写 TS 和 Python 的水平最好，而一门新 DSL 没有训练数据；
  - 当前的趋势是“代码优先 + 给机器用的 JSON”：SwiftUI、Compose、Flutter、React / Lynx 都是代码优先，A2UI、OpenUI Lang 都是 JSON。
- **跨语言共享**通过两种方式实现：组件（契约 + C++）；带接口声明的视图文档（§3.3）。

---

## 2. 可读性要求（人和 agent）

代码优先以后，**可读性主要由宿主语言来保证**。openmui 负责让 API 和数据文件同样易读：

| # | 要求 | 具体规定 |
|---|---|---|
| R1 | **builder 的名字和契约一致** | 组件、属性、事件、动作的名字都来自契约。各语言只按自己的惯例调整大小写（TS / Lua 用驼峰，Python 用下划线） |
| R2 | **代码里写的就是运行时的树** | 可交互的节点写显式 `id`，它就是语义树里的 ID 段；没有写的节点使用结构化 ID（spec S-8）；**ID 永远不依赖显示文本**（S-9） |
| R3 | **一个概念只有一种写法** | builder 不提供多种同义写法；视图文档只有一种规范格式（由 `omui fmt` 输出），diff 稳定，agent 生成的结果也一致 |
| R4 | **事件处理一眼能看懂** | 视图文档里只允许三种操作：修改 signal、调用 verb、发出事件。代码里的处理函数没有限制，但动作的效果由契约或 verb 声明，**不用读实现就能知道一个按钮有什么后果** |
| R5 | **类型明确** | signal、model、组件属性都有类型；TS 有 `.d.ts`，Python 有 `.pyi`，Lua 有 LuaLS 注解，都由契约生成 |
| R6 | **诊断对人和机器都友好** | 报错带位置、原因和修改建议；`--json` 输出结构化诊断（和 xlings、mcpp 的机器接口风格一致） |
| R7 | **文件自带版本** | 视图文档和数据文件都声明 `omui` 版本；读取方跳过不认识的字段（只增不改） |
| R8 | **给 agent 的资料齐全** | `docs/llms.txt` 包含各语言 builder 的示例和常见错误；视图文档发布 JSON Schema，支持受约束的生成 |

---

## 3. 视图文档：`*.view.omui.json`

### 3.1 例子

```json
{
  "omui": "0.1",
  "imports": ["openmui.ui.base"],
  "view": {
    "name": "Counter",
    "signals": { "count": { "type": "int", "value": 0 } },
    "root": {
      "type": "Column", "props": { "gap": 8 },
      "children": [
        { "type": "Text",   "props": { "text": { "expr": "format('点击了 {} 次', count)" } } },
        { "type": "Button", "id": "inc", "props": { "text": "加一" },
          "on": { "activate": [ { "set": "count", "expr": "count + 1" } ] } }
      ]
    }
  }
}
```

和下面的 TS 代码产出**完全相同的语义树**（S 级用例）：

```ts
const count = signal(0);
export const Counter = () => column({ gap: 8 }, [
  text({ text: fmt("点击了 {} 次", count) }),
  button({ id: "inc", text: "加一", onActivated: () => count.update(v => v + 1) }),
]);
```

### 3.2 结构要素

| 要素 | 字段 | 说明 |
|---|---|---|
| 版本 | `omui` | 规范版本 |
| 导入 | `imports` | 组件包；名字从契约中解析 |
| 状态 | `signals` | 名字 → `{type, value}` |
| 数据 | `models` | 名字 → 元素类型；由逻辑层用 `model.push` 推送数据 |
| 节点 | `type` / `id` / `props` / `children` | `id` 可以省略，但可交互的节点省略时会有 lint 警告 |
| 绑定 | 属性值写成 `{ "expr": "…" }` 或 `{ "bind": "signal" }` | 分别是表达式（单向）和双向绑定 |
| 事件 | `on.<event>: [操作…]` | 操作只有 `set`、`verb`、`emit` 三种 |
| 条件 / 循环 | `{ "if": expr, "then": […], "else": […] }`、`{ "for": "x", "in": "model", "where": expr, "body": […] }` | 循环只能遍历 model，并且会自动虚拟化 |
| 过渡 | `animate.<prop>: { "spring": {…} }` 或 `{ "ease": "out_cubic", "ms": 200 }` | 公式由规范定义 |
| 插槽 | `{ "slot": "name" }` | 由代码或别的视图来填充 |
| 文案 | `{ "tr": "key" }` | 走 i18n 目录 |
| 注释 | `notes` | 给人和 agent 看的说明 |

### 3.3 带接口声明的视图文档：语言中立的组合组件

```json
{ "omui": "0.1",
  "component": {
    "name": "SearchBox",
    "interface": {
      "props":  { "placeholder": { "type": "string", "default": "" },
                  "value":       { "type": "string", "default": "", "bindable": true } },
      "events": { "submitted":   { "args": { "text": "string" } } }
    },
    "root": { "type": "TextBox", "id": "input",
              "props": { "placeholder": { "expr": "placeholder" }, "value": { "bind": "value" } },
              "on": { "submit": [ { "emit": "submitted", "args": ["value"] } ] } } } }
```

`openmui.rules` 根据 `interface` **自动产出契约 IR**，所以四种语言都能直接使用 `SearchBox`，绑定照样自动生成。用 C++ 实现的组件仍然写 `.contract.omui.toml`。

### 3.4 使用

```ts
app.mount("main", view("ui/counter.view.omui.json"));          // TS
```

```cpp
app.mount("main", omui::view("ui/counter.view.omui.json").slot("toolbar", my_toolbar()));   // C++
```

- **发布**：`openmui.rules` 在构建期检查视图文档（JSON Schema、名字、类型、ID、语义），然后嵌入成紧凑的二进制格式。
- **开发**：运行时直接加载视图文档；文件改动后重新加载、和现有的树对账，signal 的状态保留下来。**C++ 应用不需要重新编译就能调整界面。**
- **生成式 UI**：agent 通过总线发送一份视图文档，运行时负责渲染，并照常执行效果分级的检查。

---

## 4. 表达式：一个 AST，两种写法

**规范定义的是表达式 AST**；在代码里和在数据文件里有不同的写法，但都编译成同一个 AST，由原生代码在 UI 线程上**同步**求值。

| 场景 | 写法 | 例子 |
|---|---|---|
| 代码（四种语言） | **组合函数** | `fmt("点击了 {} 次", count)`、`select(cond, a, b)`、`not_(x)`、`eq(a, b)`、`lookup(model, key)` |
| 视图文档、行为文件 | **兼容 CEL 语法的字符串** | `"format('点击了 {} 次', count)"`、`"hit.kind == 'group.label'"` |

- **子集**：字面量；signal 和 model 字段的访问；算术、比较、逻辑运算；三元表达式；`null` 判断；内置函数（`format`、`contains`、`len`、`clamp`、`min`、`max`、`tr`、`select`、`lookup` …）。**不能调用脚本函数。**
- **性质**：没有副作用；一定能求值结束；有类型；出错时返回规定好的默认值，并且发出一条诊断，不会抛异常。
- **和闭包的分工**：脚本里写的闭包（例如 `() => \`${count()}\``）在逻辑线程上**异步**求值，最多晚一帧生效；需要严格同步的场景（动画过程中、高频格式化）用表达式。在 C++ 里，闭包本身就是同步的。

---

## 5. 行为状态机：`*.behavior.omui.toml`

画布等组件的声明式交互，用数据化的 statechart 表示（语义参考 SCXML / XState）：

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

- `when` 是表达式（§4）；`do` 只能调用宿主组件声明过的内置操作（例如 canvas 的 `capture`、`move`、`commit`）。
- 行为可以声明自己带来的语义动作（例如 `move` 的效果是 reversible）。
- 更复杂的交互直接用 C++ 写行为（见 [canvas-markdown](2026-10-26-openmui-canvas-markdown-design.md) §1.3）。

---

## 6. 后缀体系（D-30，v0.3 更新）

规则是：**openmui 的数据文件一律命名为 `<名字>.<种类>.omui.{toml,json}`**；逐行追加的流式记录用 `.jsonl`（NDJSON，每行一个 JSON 对象）；逻辑代码和着色器保持各自原生的后缀。

| 文件 | 后缀 | 例子 |
|---|---|---|
| 视图文档 | `.view.omui.json` | `counter.view.omui.json` |
| 组件契约 | `.contract.omui.toml` | `slider.contract.omui.toml` |
| native 契约 | `.native.omui.toml` | `search.native.omui.toml` |
| 行为状态机 | `.behavior.omui.toml` | `group-drag.behavior.omui.toml` |
| 主题和 token | `.theme.omui.toml` | `default.theme.omui.toml` |
| 效果着色器的元数据（可选） | `.effect.omui.toml` | `ripple.effect.omui.toml`（参数范围、说明；不写时由 Slang 反射得到） |
| 时间轴叠加（视频预览里的修改，D-54） | `.timeline.omui.json` | `promo.timeline.omui.json`（属性、关键帧、镜头起点和长度的修改，叠加在代码之上） |
| 会话录制（语义动作 + 输入 + 时钟，`modules/record`） | `.session.omui.jsonl` | `bug-1234.session.omui.jsonl`（第一行是头部，之后每行一条带时间戳的记录；可以流式写入、跳转、在 headless 下精确回放、转成测试） |
| agent 端点的描述（运行时写在端点目录里，退出时删除） | `.endpoint.omui.json` | `org.example.notes.endpoint.omui.json`（名字、应用 ID、接口 `openmui.tree@0.1`；`omui agent ls` 读取它） |
| 应用包（播放器加载） | `.omuipkg` | `notes.omuipkg` |
| 着色器源码 | `.slang` | `ripple.slang` |
| 逻辑代码 | `.ts` / `.tsx` / `.py` / `.lua` / `.cppm` / `.cpp` | 保持原生后缀，编辑器和工具链才能正常工作 |

- 编辑器可以按 `*.omui.toml` / `*.omui.json` 统一关联 schema；种类写在文件名里，一眼就能看出来。
- 文件内的顶层表或字段（`[contract.x]`、`[native.x]`、`[behavior.x]`、`[theme.x]`、`"view"` / `"component"`）也会声明种类，即使文件名不规范也能识别。

---

## 7. 工具

| 工具 | 作用 |
|---|---|
| `omui check` | 检查契约、视图文档、行为文件：schema、名字、类型、ID、语义；`--json` 输出结构化诊断 |
| `omui fmt` | 把 `*.omui.{toml,json}` 格式化成唯一的规范格式 |
| 编辑器支持 | JSON / TOML 的 schema 关联（补全、校验）；由契约生成的 `.d.ts` / `.pyi` / LuaLS 注解提供 builder 的补全 |
| `docs/llms.txt` | 各语言 builder 的示例、视图文档的示例、常见错误和修改方法 |

---

## 8. 待细化

- 视图文档 JSON Schema 的全文；
- 表达式子集的完整函数表，以及各语言组合函数的名字对照表；
- `for` 和虚拟化的交互细节（估算高度、key 的稳定性）。
