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

[例子 02](../../../examples/02-hello-ts)（文本统计）的全部代码：

<!-- code: examples/02-hello-ts/logic/main.ts#statistics -->
```ts
import { signal, fmt, call, select, eq, column, row, text, textBox, button, mount } from "openmui";

const input = signal("");
const words = signal(0);
const sentences = signal(0);

// Counting words and sentences is the script's: it runs when the text changes
// (on the script's thread, never in a frame).
input.watch((t: string) => {
  words.set(t.split(/\s+/).filter((w) => w.length > 0).length);
  sentences.set((t.match(/[^.!?。！？]+[.!?。！？]+/g) ?? []).length);
});

const plural = (n: Parameters<typeof call>[1], one: string, many: string) => select(eq(n, 1), one, many);

mount("main", column({ padding: 16, gap: 10 }, [
  text({ text: "Text statistics", style: "title" }),
  textBox({ id: "input", text: input, multiline: true, placeholder: "Type or paste a text", label: "Text", height: 140 }),
  row({ id: "stats", gap: 16 }, [
    // Characters as people count them: grapheme clusters, evaluated by the runtime.
    text({ id: "characters", text: fmt("{} {}", call("size", input), plural(call("size", input), "character", "characters")) }),
    text({ id: "words", text: fmt("{} {}", words, plural(words, "word", "words")) }),
    text({ id: "sentences", text: fmt("{} {}", sentences, plural(sentences, "sentence", "sentences")) }),
    text({ id: "reading", text: fmt("{} min read", call("ceil", call("double", words).div(200))) }),
  ]),
  button({ id: "clear", text: "Clear", variant: "ghost", disabled: eq(call("size", input), 0), onActivated: () => input.set("") }),
]));
```

- `signal` 是一个会变的值；把它交给组件的属性，值变了界面跟着变。
- `fmt`、`select`、`eq`、`call` 是由运行时求值的表达式：视图只建一次，不必每次重建。
- 自己的逻辑放在 `signal.watch(...)` 或事件回调里；脚本不在每一帧里运行。

C++ 里是同一棵树、同样的 signal：[例子 03](../../../examples/03-counter) 的计数器（它的 TypeScript 版 `ts/tally.ts` 建出同一棵树；即时模式见[例子 01](../../../examples/01-hello-im)）：

<!-- code: examples/03-counter/src/tally.cpp#view -->
```cpp
omui::View view(const State& s) {
    using namespace omui;
    const Signal<int> count = s.count, goal = s.goal;
    return column({.padding = 24, .gap = 12}, {
        text({.id = "count", .text = derive([=] { return std::to_string(count()); }), .style = "display"}),
        progress({.id = "progress", .label = "Towards the goal",
                  .value = derive([=] { return std::clamp(static_cast<double>(count()) / std::max(goal(), 1), 0.0, 1.0); })}),
        text({.id = "status", .text = derive([=] {
                  return count() >= goal() ? std::format("Goal of {} reached", goal()) : std::format("{} to go", goal() - count());
              })}),
        row({.id = "controls", .gap = 8}, {
            button({.id = "minus", .disabled = derive([=] { return count() == 0; }), .text = "−1",
                    .on_activated = [=] { count.set(std::max(count.peek() - 1, 0)); }}),
            button({.id = "plus", .text = "+1", .variant = ButtonVariant::primary, .on_activated = [=] { count.set(count.peek() + 1); }}),
            button({.id = "reset", .text = "Reset", .variant = ButtonVariant::ghost, .on_activated = [=] { count.set(0); }}),
        }),
        row({.id = "goal", .gap = 8}, {
            text({.text = "Goal"}),
            slider({.id = "value", .label = "Goal", .max = 100, .min = 1, .step = 1, .value = goal}),
        }),
    });
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

## 出了问题怎么办

- **Agent 找控件的路径变了**：没有显式 `id` 的控件按位置命名；每个可交互的节点都给一个不依赖显示文字的 `id`。
- **组件的变化没有动画**：组件只画状态；变化怎么动由节点的 `motion` 槽和主题的配方决定（lint `motion`），在那里设。
- **请求被拒绝**：拒绝会说明原因和出路（这个节点没有的动作、超出范围的值、会话缺少的权限）；`omui agent snapshot <应用> --interactive` 列出现在能操作什么，带 `api` 的 `ui.describe` 列出每个组件的属性和动作。
