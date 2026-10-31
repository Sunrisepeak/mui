// Text statistics: characters, words, sentences and reading time of what you
// type or paste. The view is built once; the counts follow the text.
import { signal, fmt, call, select, eq, column, row, text, textBox, button, mount } from "openmui";

const input = signal("");
const words = signal(0);
const sentences = signal(0);

// Counting words and sentences is the script's: it runs when the text changes
// (on the script's thread, never in a frame).
input.watch((t: string) => {
  words.set(t.split(/\s+/).filter((w) => w.length > 0).length);
  sentences.set((t.match(/[^.!?。！？]+[.!?。！？]+/g) ?? []).length);
});

const plural = (n: Parameters<typeof call>[1], one: string, many: string) => select(eq(n, 1), one, many);

mount("main", column({ padding: 16, gap: 10 }, [
  text({ text: "Text statistics", style: "title" }),
  textBox({ id: "input", text: input, multiline: true, placeholder: "Type or paste a text", label: "Text", height: 140 }),
  row({ id: "stats", gap: 16 }, [
    // Characters as people count them: grapheme clusters, evaluated by the runtime.
    text({ id: "characters", text: fmt("{} {}", call("size", input), plural(call("size", input), "character", "characters")) }),
    text({ id: "words", text: fmt("{} {}", words, plural(words, "word", "words")) }),
    text({ id: "sentences", text: fmt("{} {}", sentences, plural(sentences, "sentence", "sentences")) }),
    text({ id: "reading", text: fmt("{} min read", call("ceil", call("double", words).div(200))) }),
  ]),
  button({ id: "clear", text: "Clear", variant: "ghost", disabled: eq(call("size", input), 0), onActivated: () => input.set("") }),
]));
