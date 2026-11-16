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

[Example 02](../../../examples/02-hello-ts), text statistics, whole:

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

- A `signal` is a value that changes; give it to a component's property and the interface follows it.
- `fmt`, `select`, `eq`, `call` are expressions the runtime evaluates: the view is built once, not rebuilt.
- Your own logic goes in `signal.watch(...)` or an event's callback; a script never runs inside a frame.

In C++ it is the same tree, with the same signals: [example 03](../../../examples/03-counter)'s tally (its TypeScript twin, `ts/tally.ts`, builds the same tree; immediate mode is [example 01](../../../examples/01-hello-im)):

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
