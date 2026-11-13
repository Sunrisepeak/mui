# 写界面

一个界面是一棵树：每个节点有角色、名字、值、状态和它能做的动作。你用代码描述这棵树，框架负责画出来、交给读屏软件、交给 Agent。

## 三种写法，一棵树

| 写法 | 适合 | 示例 |
|---|---|---|
| TypeScript | 大多数应用：改了就生效，不用重新编译 | [02](../../../examples/02-hello-ts)、[06](../../../examples/06-theming) |
| C++ | 性能敏感的部分、自己的原生模块 | [01](../../../examples/01-hello-im)、[03](../../../examples/03-counter) |
| 视图文档（JSON） | 由工具或 Agent 生成的界面 | [12](../../../examples/12-notes) |

三种可以在一个应用里混用，共享状态和命令（[示例 12](../../../examples/12-notes)）。

## 状态：signal

```ts
import { signal, fmt, select, eq, column, text, textBox, button, mount } from "openmui";

const name = signal("");

mount("main", column({ padding: 16, gap: 10 }, [
  textBox({ id: "name", text: name, label: "Name", placeholder: "Your name" }),
  text({ id: "hello", text: select(eq(name, ""), "Type your name above.", fmt("Nice to meet you, {}!", name)) }),
  button({ id: "clear", text: "Clear", onActivated: () => name.set("") }),
]));
```

- `signal` 是一个会变的值；把它交给组件的属性，值变了界面跟着变。
- `fmt`、`select`、`eq`、`call` 是由运行时求值的表达式：视图只建一次，不必每次重建。
- 自己的逻辑放在 `signal.watch(...)` 或事件回调里；脚本不在每一帧里运行。

C++ 的即时模式写法是同一回事：

```cpp
import std;
import omui;

omui::AppSpec app() {
    static int clicks = 0;
    return {.id = "org.example.hello", .title = "Hello", .view = [] {
        return omui::immediate([](omui::Ui& ui) {
            if (ui.button(std::format("Clicked {} times", clicks), {.id = "click"})) ++clicks;
        });
    }};
}
```

## 三条规矩

1. **能点、能输入的节点都写 `id`**，而且 `id` 不依赖显示的文字。Agent 和测试靠它找到节点；换了语言、改了文案，它们还找得到。
2. **组件不写动画。** 组件只画状态；状态变化时怎么动，由主题和风格包决定（见[动效](motion.md)）。
3. **界面全是自己画的。** 同样的输入在 Linux、macOS、Windows 和 Web 上是同样的像素，所以截图测试可以钉住。

## 组件与主题

- 组件在 [`components`](../../../modules/projects/omui/components)：按钮、输入框、开关、滑块、表格、树、标签页、画布、Markdown……都从 `openmui` 导入。
- 主题是数据：`omui.theme("dark", "compact")` 叠加一组层（深色、紧凑、减弱动效）；自己的主题见[示例 06](../../../examples/06-theming)。
- 风格包（玻璃、霓虹、墨、Fluent、Material……）换的是整套外观与动效，语义树不变：[风格画廊](../../../showcase/style-gallery)。
- 自己的组件先写契约，绑定由工具生成：[示例 04](../../../examples/04-rating)。

## 测试

不用开窗口：

```sh
omui test
omui run -- --headless --png shot.png      # 出一张真实的离屏图
```

测试里可以查语义树（这个按钮在不在、是不是禁用）、执行动作、比对像素。[示例 07](../../../examples/07-headless-and-agent) 是一个完整的例子。
