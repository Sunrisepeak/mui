# Start

Ten minutes: the tools, an application, running it.

## 1. Tools

Building is [mcpp](https://github.com/mcpp-community/mcpp)'s, installed through xlings; the compiler and the rest are pinned by the project and fetched at the first build.

```sh
xlings install mcpp
```

## 2. A project, running

```sh
omui new hello
cd hello
omui dev
```

`app/main.ts` is the whole application:

<!-- code: examples/13-live-design/design/start.ts#counter -->
```ts
import { signal, fmt, column, text, button, mount } from "openmui";

const clicks = signal(0);

export const App = () =>
  column({ padding: 24, gap: 12 }, [
    // The page's heading too (role and level): its document's h1 on the Web, a reader's first heading.
    text({ text: "Hello from subscription", style: "title", role: "heading", level: 1 }),
    text({ id: "count", text: fmt("Clicked {} times", clicks), style: "heading" }),
    button({ id: "click", text: "Click me", variant: "primary", onActivated: () => clicks.update((n: number) => n + 1) }),
  ]);

mount("main", App());
```

Change the file while the window is open and save: the interface changes, and the count stays.

## 3. The everyday commands

| Command | What it does |
|:---|:---|
| `omui dev` | runs in development mode (`--headless` for no window) |
| `omui run` | runs as it ships |
| `omui test` | runs the tests |
| `omui build` | builds |

Next: [interfaces](ui.md), or [what an agent reads](agent.md).
