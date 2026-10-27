# Example 02 — hello, TypeScript

`text-stats` shows the statistics of a text as you type or paste it: characters (grapheme clusters, so 👋🏽 counts once), words, sentences and reading time. It also has a Clear button. The whole application is [`logic/main.ts`](logic/main.ts); there is no C++ to write.

- **The player runs it.** `mcpp.toml` names the script in `[package.metadata.openmui.scripts]`. The one-line `src/main.cpp` hands over to the player (`entry-player`), which compiles the TypeScript with esbuild and runs it on its own thread.
- **The view is built once.** The character count, the plurals and the reading time are expressions that the runtime evaluates as the text changes (`call("size", input)`, `select(eq(n, 1), …)`). Counting words and sentences is the script's own code, in a watch that runs when the text changes and never in a frame.
- **Live.** Saving `logic/main.ts` while it runs reloads it in place, and the text you typed stays.

```sh
mcpp run -p openmui.example.hello-ts                       # a window
mcpp run -p openmui.example.hello-ts -- --headless --png stats.png
```

The test (`tests/hello_test.cpp`) checks:
- the manifest;
- that the player starts the script;
- that typing (an emoji included) updates every count;
- that an agent's Clear empties the text and disables itself;
- the pinned pixels.
