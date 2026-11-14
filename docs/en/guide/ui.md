# Interfaces

An interface is a tree: each node has a role, a name, a value, states and the actions it offers. You describe the tree in code; the framework draws it, gives it to screen readers, and gives it to agents.

## Three ways to write it, one tree

| Way | Suits | Examples |
|---|---|---|
| TypeScript | most applications: a change shows at once, nothing is rebuilt | [02](../../../examples/02-hello-ts), [06](../../../examples/06-theming) |
| C++ | what must be fast; your own native modules | [01](../../../examples/01-hello-im), [03](../../../examples/03-counter) |
| A view document (JSON) | interfaces a tool or an agent generates | [12](../../../examples/12-notes) |

The three mix in one application and share state and commands ([example 12](../../../examples/12-notes)).

## State: a signal

```ts
import { signal, fmt, select, eq, column, text, textBox, button, mount } from "openmui";

const name = signal("");

mount("main", column({ padding: 16, gap: 10 }, [
  textBox({ id: "name", text: name, label: "Name", placeholder: "Your name" }),
  text({ id: "hello", text: select(eq(name, ""), "Type your name above.", fmt("Nice to meet you, {}!", name)) }),
  button({ id: "clear", text: "Clear", onActivated: () => name.set("") }),
]));
```

- A `signal` is a value that changes; give it to a component's property and the interface follows it.
- `fmt`, `select`, `eq`, `call` are expressions the runtime evaluates: the view is built once, not rebuilt.
- Your own logic goes in `signal.watch(...)` or an event's callback; a script never runs inside a frame.

Immediate mode in C++ is the same thing:

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

## Three rules

1. **Give every node that is pressed or typed into an `id`**, one that does not depend on the text shown. Agents and tests find nodes by it — in another language, after a change of wording, still.
2. **Components do not animate.** A component draws its state; how a change of state moves is the theme's and the style pack's ([Motion](motion.md)).
3. **Everything is drawn by the framework.** The same input is the same pixels on Linux, macOS, Windows and the Web, so a screenshot test can be pinned.

## Components and themes

- Components are in [`components`](../../../modules/projects/omui/components): buttons, text boxes, switches, sliders, grids, trees, tabs, a canvas, Markdown … all imported from `openmui`.
- A theme is data: `omui.theme("dark", "compact")` stacks layers (dark, compact, reduced motion); your own theme is [example 06](../../../examples/06-theming).
- A style pack (glass, neon, ink, Fluent, Material …) changes the whole look and motion and leaves the semantic tree as it was: the [style gallery](../../../showcase/style-gallery).
- Your own component begins with its contract; the bindings are generated: [example 04](../../../examples/04-rating).

## Tests

No window is needed:

```sh
omui test
omui run -- --headless --png shot.png      # a real picture, drawn offscreen
```

A test can ask the semantic tree (is this button there, is it disabled), perform actions, and compare pixels. [Example 07](../../../examples/07-headless-and-agent) is a whole one.
