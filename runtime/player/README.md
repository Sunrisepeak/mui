# openmui.player

The player (component-binding §9, §10.2): what a script-first application runs in. The `entry-player` entry reads the application's manifest (`[package.metadata.openmui]`: `components`, `views`, `scripts`) and gives it to a `Player` over the runtime of a desktop or headless host.

- **Start**: the component documents are defined first, in whatever order they need one another. The view documents are then mounted, each at its file's stem (`ui/main.view.omui.json` is `main`). Last, the scripts run.
- **Languages are plugins** (`Language`): a name, extensions, how to make an `Engine` over the C ABI, and optionally how to load an entry (TypeScript through esbuild). The player knows none of them; the entry registers Lua ([lang/lua](../../lang/lua)), JavaScript and TypeScript. Each script runs on a thread of its own.
- **Reload in place** (`poll`, or `watch` on the runtime's timers):
  - a saved view keeps its nodes and state;
  - a saved component rebuilds its instances;
  - a saved script runs again, keeping its signals.
- **Problems**: a file with a mistake changes nothing, and the error is logged and kept in `problems()` with its place (`ui/main.view.omui.json:1:43: error: no component is named "colum"`). What cannot start is reported together by `start()`.

Observability: the log category `player`, and the trace spans `player.start` and `player.reload`.
