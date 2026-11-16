# omui — the project's commands, for an agent

What the commands are, their arguments and effects: `cli.ops.omui.ts` (the `omui_*` tools of `omui mcp`, or `omui <command> --json` in a shell). This page is the order to use them in.

## Make, run, check, ship

1. `omui doctor`: what is installed. A required tool missing (`mcpp`, `xlings`) is the person's to install. Say which, and stop.
2. `omui new NAME`: a project from a template (`app-ts` unless asked). It writes a directory to keep, so the person is asked first. Never answer for them.
3. `omui dev`: the application with its designer (Live Design). From here the work goes through the application's own tools (`ui_*`, `design_*`), not through files.
4. `omui check`: the TypeScript against the API the application registers. Fix every problem it lists before running the tests. `--portable` also reports what ties the scripts to omui.
5. `omui test`: the project's tests. A failure is a fact to read (`omui test --json`), not to retry blindly.
6. `omui build --release`: what people get, with no designer linked.
7. `omui package`: an artifact to hand out. It writes something to keep, so the person is asked first.

## The design's history

- `omui design sync` carries `.omui/design/` into git as `design/<branch>`. It writes references to keep, so the person is asked.
- `omui design film --out design.webm` makes a film of the history.

## How it is verified

Every command answers its exit code (0 done, 1 failed, 2 a wrong command) and JSON records with `--json`. A command that writes something to keep (`new`, `package`, `design sync`, `design film`) needs the person's answer. With MCP, the client asks them. Without one, the tool refuses until it is called again with `answer: "yes"`, which only the person's own word allows.
