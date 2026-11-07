// The notes application's views and logic. What it shares with the other
// languages is what the design allows: Lua's macros are verbs it invokes
// (plugins/macros.lua), the help panel is a view document the player mounts
// into this view's slot (ui/help.view.omui.json), the search is openmui's
// native module in C++ (fuzzy, on a worker as you type).
import { signal, fmt, model, verb, invoke, column, row, text, textBox, button, markdown, scrollView, each, slot, select, eq, mount } from "openmui";
import { fuzzy } from "openmui:native/fuzzy";

type Note = { key: string; title: string; body: string };

const notes = model("notes");
const shown = model("shown");  // the list as searched: { key, title }
notes.push([
  { key: "n1", title: "Groceries", body: "# Groceries\n\n- [ ] Milk\n- [x] Bread\n- [ ] Eggs" },
  { key: "n2", title: "Reading", body: "# Reading\n\n*Middlemarch*, then *The Name of the Rose*." },
  { key: "n3", title: "Ideas", body: "# Ideas\n\nA notes application in four languages." },
]);

const open = signal("n1");
const body = signal((notes.rows() as Note[])[0].body);
const query = signal("");
const found = signal(0);
let next = 4;
let asked = 0;

const all = (): Note[] => notes.rows();

// The list for the query: ranked on a worker while typing, or now (a verb).
async function search(now = false) {
  const ticket = ++asked;
  const rows = all();
  const texts = rows.map((n) => n.title + "\n" + n.body);
  const ranked = now ? fuzzy.rankSync(query.get(), texts, 50) : await fuzzy.rank(query.get(), texts, 50);
  if (ticket !== asked) return;  // a later question was asked meanwhile
  shown.clear();
  shown.push(ranked.map((m) => ({ key: rows[m.index].key, title: rows[m.index].title })));
  found.set(ranked.length);
}

// The open note's text, kept: its title is Lua's to say.
function save(text: string) {
  body.set(text);
  const rows = all();
  const i = rows.findIndex((n) => n.key === open.get());
  if (i < 0) return;
  notes.splice(i, 1, [{ ...rows[i], body: text, title: invoke("macro.title", { text }) }]);
  search(true);
}

function openNote(key: string) {
  const n = all().find((x) => x.key === key);
  if (!n) throw new Error(`no note is ${key}`);
  open.set(key);
  body.set(n.body);
}

function newNote(title: string): string {
  const key = `n${next++}`;
  notes.push([{ key, title, body: `# ${title}\n\n` }]);
  openNote(key);
  search(true);
  return key;
}

// What an agent does with the notes.
verb("notes.new", { summary: "Start a note", effect: "reversible" }, (a: { title?: string }) => newNote(a.title ?? "Untitled"));
verb("notes.open", { summary: "Open a note by its key", effect: "read" }, (a: { key: string }) => { openNote(a.key); return a.key; });
verb("notes.search", { summary: "Search the notes", effect: "read" }, (a: { query: string }) => {
  query.set(a.query);
  search(true);
  return (shown.rows() as { key: string }[]).map((r) => r.key);
});

search(true);

export const Notes = () =>
  row({ padding: 16, gap: 16, align: "stretch" }, [
    column({ id: "side", gap: 8, width: 260, justify: "start" }, [
      textBox({ id: "search", label: "Search", placeholder: "Search notes", text: query, onChanged: () => { search(); } }),
      text({ id: "found", text: fmt("{} shown", found), style: "caption" }),
      column({ id: "list", gap: 4 }, [
        each("shown", (n) => button({ text: n.title, variant: select(eq(n.key, open), "primary", "ghost"),
                                      onActivated: (e: { rows: { row: { key: string } } }) => openNote(e.rows.row.key) })),
      ]),
      button({ id: "new", text: "New note", onActivated: () => { newNote("Untitled"); } }),
      slot("help"),
    ]),
    column({ id: "page", gap: 8, grow: 1, basis: 0, minWidth: 0, justify: "start" }, [
      row({ id: "tools", gap: 8 }, [
        button({ id: "today", text: "Today", onActivated: () => save(invoke("macro.today", { text: body.get(), date: new Date().toISOString().slice(0, 10) })) }),
        button({ id: "task", text: "Task", onActivated: () => save(invoke("macro.task", { text: body.get() })) }),
      ]),
      row({ id: "panes", gap: 12, grow: 1, basis: 0, minHeight: 0, align: "stretch" }, [
        textBox({ id: "editor", label: "Note", multiline: true, text: body, grow: 1, basis: 0, minWidth: 0,
                  onChanged: (e: { text: string }) => save(e.text) }),
        // A task ticked in the preview rewrites its mark in the source: kept.
        scrollView({ id: "scroll", grow: 1, basis: 0, minWidth: 0 }, [
          markdown({ id: "preview", source: body, origin: "user_content", onChanged: (e: { source: string }) => save(e.source) }),
        ]),
      ]),
    ]),
  ]);

mount("main", Notes());
