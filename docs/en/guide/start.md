# Start

Ten minutes: the tools, an application, running it, an agent pressing its button.

## 1. Tools

Building is [mcpp](https://github.com/mcpp-community/mcpp)'s, installed through xlings; the compiler and the rest are pinned by the project and fetched at the first build.

```sh
xlings install mcpp
```

The `omui` command is built from the repository for now (there is no release yet):

```sh
git clone --recursive https://github.com/Sunrisepeak/mui && cd mui
mcpp build -p omui.cli        # gives omui: put it on your PATH
omui doctor                   # which tools are missing, and what each is for
```

## 2. A project, running

```sh
omui new hello                # the TypeScript template; --template app-cpp for C++; --list for all
cd hello
omui dev                      # development mode: the window and its designer; a save shows at once
```

`app/main.ts` is the whole application:

```ts
import { signal, fmt, column, text, button, mount } from "openmui";

const clicks = signal(0);

mount("main", column({ padding: 24, gap: 12 }, [
  text({ id: "count", text: fmt("Clicked {} times", clicks), style: "heading" }),
  button({ id: "click", text: "Click me", variant: "primary",
           onActivated: () => clicks.update((n: number) => n + 1) }),
]));
```

Change the file while the window is open and save: the interface changes, and the count stays.

## 3. The everyday commands

| Command | What it does |
|---|---|
| `omui dev` | runs in development mode (`--headless` for no window) |
| `omui run [--release]` | runs as it ships |
| `omui test` | runs the tests |
| `omui build [--release]` | builds |

mcpp is underneath; `mcpp run` and `mcpp test` give the same results.

## 4. An agent presses the button

With the application running, in another terminal:

```sh
omui agent ls                                  # the applications that can be operated
omui agent snapshot hello --interactive        # what can be operated in it now
omui agent act hello main/click activate       # press that button
```

The count in the window went up by one. The agent used the button you use, not a coordinate.

## Next

- [Interfaces](ui.md): state, layout, components, themes.
- [Agents](agent.md): connecting Claude Code and others to your application.
