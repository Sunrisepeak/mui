# Example 12 — notes in four languages

`notes` keeps notes in Markdown:

- a list you search as you type;
- an editor beside the note's preview;
- tasks you tick in the preview, kept in the note's text;
- a toolbar that adds a heading for today or a new task;
- a new note in one click.

Each part is written in the language that suits it. They share only what the design allows: signals, models, verbs and events (component-binding §9).

- **TypeScript** ([`logic/main.ts`](logic/main.ts)) holds the views and the logic: the notes model, the list, the editor and the preview, and the verbs an agent uses (`notes.new`, `notes.open`, `notes.search`).
- **Lua** ([`plugins/macros.lua`](plugins/macros.lua)) holds the text macros: `macro.today`, `macro.task` and `macro.title` (a note's title is its first heading, or its first line). They are verbs, so the TypeScript toolbar invokes them, and so may an agent or any other script. None of them keeps state.
- **A view document** ([`ui/help.view.omui.json`](ui/help.view.omui.json)) is the help panel. The player mounts it under the name `help`. The TypeScript view has a `slot("help")` that no code fills, so the panel shows there (an outlet). Saving the file reloads it in place.
- **C++** does the search: openmui's native module `fuzzy`, imported as `openmui:native/fuzzy`. While you type it ranks on a worker (`fuzzy.rank`, a promise), and an answer that a later keystroke has overtaken is dropped. A verb ranks at once (`fuzzy.rankSync`).

Ticking a task in the preview rewrites its mark in the Markdown source. The preview's `changed` event then saves the note.

```sh
mcpp run -p openmui.example.notes
mcpp run -p openmui.example.notes -- --headless --png notes.png
```

The test (`tests/notes_test.cpp`) uses the tool as a person and an agent would:
- the help panel is in the TypeScript view's slot, not a view of its own;
- searching as you type, and clearing the search;
- the toolbar's Lua macros, and a title Lua reads from an edit;
- a task ticked in the preview, still ticked after opening another note and coming back;
- a new note;
- the agent's verbs, the application's and Lua's, including a refusal;
- the first frame's pixels, pinned.
