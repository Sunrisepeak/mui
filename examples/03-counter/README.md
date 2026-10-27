# Example 03 — a tally counter, written four times

`tally` counts people at a door, laps or reps towards a goal: **+1**, **−1** (it stops at zero), **Reset**, and a slider for the goal, with a progress bar and how many are left.

The same interface is written four times:

- in C++, with the builders: [`src/tally.cpp`](src/tally.cpp);
- as a view document: [`ui/tally.view.omui.json`](ui/tally.view.omui.json) (view-data-files §3), with CEL expressions for what the text shows and `set` operations for what the buttons do;
- in Lua: [`lua/tally.lua`](lua/tally.lua) ([openmui.lang.lua](../../lang/lua)). It uses table builders and signal arithmetic, and its handlers run on the script's own thread.
- in TypeScript: [`ts/tally.ts`](ts/tally.ts) ([openmui.lang.js](../../lang/js)). esbuild compiles it, and QuickJS-ng runs it.

The four are **one semantic tree and the same pixels**, before and after every click and every agent action (the CTS S level). The Python version joins them when that binding lands.

```sh
mcpp run -p openmui.example.counter                                     # the C++ view
mcpp run -p openmui.example.counter -- --doc examples/03-counter/ui/tally.view.omui.json
mcpp run -p openmui.example.counter -- --serve                          # then: omui agent act … main/controls/plus activate
```

With `--doc`, the document is **live**. The file is checked every 100 ms. A save shows within the next frame, and the count stays. A save with a mistake changes nothing and logs what is wrong and where, such as `1:43: error: no component is named "colum" (did you mean "column"?)`.

The tools work on the document too:

```sh
omui check examples/03-counter/ui/tally.view.omui.json   # names, types, ids, expressions
omui fmt examples/03-counter/ui/tally.view.omui.json     # the canonical format
```

The tests (`tests/counter_test.cpp`) check four things:
- every version is the same tree and pixels through a sequence of clicks and agent actions;
- the document passes `omui check` with no warning and is in the canonical format;
- a save shows in under 200 ms and keeps the count, and a broken save is refused;
- the pixels are pinned.
