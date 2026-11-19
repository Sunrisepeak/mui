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
omui new hello                # the TypeScript template; --template app-cpp for C++; `omui templates` for all
cd hello
omui dev                      # development mode: the window and its designer; a save shows at once
```

`app/main.ts` is the whole application (as [example 13](../../../examples/13-live-design) starts):

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

## When something goes wrong

- **A tool is missing**: `omui doctor` names each missing tool and what it is for; `omui tools install` installs the ones the project names (through xlings).
- **The first build is slow**: the compiler and the libraries pinned by the project are fetched and built once; the builds after it are incremental.
- **`omui agent ls` does not list the application**: an application is listed while it runs, by a descriptor in the per-user runtime directory (`$XDG_RUNTIME_DIR/openmui`, else a temporary one). One left behind by a crashed run is listed too; connecting tells which are alive.
- **More detail**: `OMUI_LOG` (or `--log`) sets the log levels by category, for example `OMUI_LOG=agent=debug`.

## Next

- [Interfaces](ui.md): state, layout, components, themes.
- [Agents](agent.md): connecting Claude Code and others to your application.
