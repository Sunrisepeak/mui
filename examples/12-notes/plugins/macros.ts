// Text macros, as verbs: any script calls them (logic/main.ts does, from
// its toolbar and whenever it keeps a note), and so may an agent. Each takes
// the note's text and answers the new text (or its title); none keeps any
// state.
import { verb } from "openmui";

type Args = { text?: string; date?: string; task?: string };

// A separator before what is appended: none at the start or after a line's end.
const separator = (text: string): string => (text === "" || text.endsWith("\n") ? "" : "\n");

// "## 2026-10-27" under the text: a day's heading. The date is the caller's
// (the program's clock, or a test's).
verb("macro.today", { summary: "Append a heading for a day", effect: "reversible" }, (a: Args) => {
  const text = a.text ?? "";
  return `${text}${separator(text)}\n## ${a.date}\n`;
});

// "- [ ] " on a new line: a task to write.
verb("macro.task", { summary: "Append a task", effect: "reversible" }, (a: Args) => {
  const text = a.text ?? "";
  return `${text}${separator(text)}- [ ] ${a.task ?? ""}`;
});

// A note's title: its first heading, or its first line.
verb("macro.title", { summary: "A note's title", effect: "read" }, (a: Args) => {
  const text = a.text ?? "";
  const heading = /^#+\s+([^\n]+)/.exec(text) ?? /\n#+\s+([^\n]+)/.exec(text);
  const first = heading?.[1] ?? /^\s*([^\n]+)/.exec(text)?.[1] ?? "Untitled";
  return first.replace(/\s+$/, "");
});
