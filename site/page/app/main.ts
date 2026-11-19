// omui's site, one page (G5): the mark, one sentence, the Live Design key and
// the install, and nothing else (the site's design §4). One picture in four
// states — at rest; Live Design (the page back to its sketch, grown again
// step by step, the progress along the mark's ring); waiting (the prompt or
// the commands copied: the ring open, breathing); connected (the ring closed,
// the mark become Mui, the visitor's agent and its omui beside it). How one
// state becomes the next is the style's motion, not this page's (D-93).
import {
  signal, eq, or, select, when, stack, column, row, box, text, button, segmented, path, timeline, mount, verb,
} from "openmui";

type State = "rest" | "design" | "waiting" | "connected";
const state = signal<State>("rest", "page.state");
const how = signal("agent", "page.install");

const ink = "#1e1b4b";
const accent = "#4f46e5";
const muted = "#64748b";
const paper = "#faf9f6";
const sketch = "#cbd5e1";

// The mark (site/brand/marks.mark.xdef.json, today's outline): an O around the word, in a 100-unit square.
const ring = "M50 5C73.2 5 92 25.1 92 50C92 74.9 73.2 95 50 95C26.8 95 8 74.9 8 50C8 25.1 26.8 5 50 5Z";
const letters = "M24 64L24 44M24 50C24 41 35 41 35 50L35 64M35 50C35 41 46 41 46 50L46 64M55 44L55 56C55 66 69 66 69 56L69 44M78 44L78 64";
const dot = "M75.5 35A2.5 2.5 0 1 0 80.5 35A2.5 2.5 0 1 0 75.5 35Z";
// The same O in a 1×1 box: the course Live Design's steps lie along.
const course = "M0.5 0.05C0.732 0.05 0.92 0.251 0.92 0.5C0.92 0.749 0.732 0.95 0.5 0.95C0.268 0.95 0.08 0.749 0.08 0.5C0.08 0.251 0.268 0.05 0.5 0.05Z";

// Mui's head (site/brand/mascot.sprite.xdef.json, head16 idle): every pixel a vector square, one path a colour.
const head = [
  "................", "...o........o...", "..oko......oko..", "..obko....okbo..",
  ".obbbooooooobbbo", ".obbbbbbbbbbbbbo", "obblbbbbbbbbblbo", "obbpwbbbbbbpwbbo",
  "obbppbbbbbbppbbo", "obkbbbobbobbbkbo", "obbbbbboobbbbbbo", ".obbbbbbbbbbbbo.",
  ".obbbbbbbbbbbbo.", "..oobbbbbbbboo..", "....oooooooo....", "................",
];
const palette: Record<string, string> = { o: "#1e1b4b", b: "#4f46e5", l: "#818cf8", w: "#ffffff", p: "#1e1b4b", k: "#f9a8d4", s: "#4338ca" };
function cells(letter: string): string {
  let data = "";
  head.forEach((line, y) => {
    for (let x = 0; x < line.length; x++) if (line[x] === letter) data += `M${x} ${y}h1v1h-1Z`;
  });
  return data;
}

const side = 240;            // the mark's box
const steps = 8;             // the page's design, step by step
const at = 3;                // where the sketch of Live Design stands

const resting = eq(state, "rest");
const designing = eq(state, "design");
const waiting = eq(state, "waiting");
const connected = eq(state, "connected");

const code = "7KQ2-M9";
const prompt = `Install omui (after the release; until then build it from GitHub), run \`omui connect ${code}\`, then call \`describe\` and follow it.`;
const commands = `# after the release; until then build it from GitHub\nomui connect ${code}`;

// The mark: its O (open while it waits, drawn as far as the design has come), the word or Mui, and in Live Design the
// course of the design's steps along the O.
const mark = stack({ id: "mark", width: side + 20, height: side + 20, alignItems: "center", justify: "center" }, [
  // Breathing while it waits: a wider, fainter O.
  path({ id: "breath", d: ring, stroke: accent, strokeWidth: 2, width: side, height: side,
         opacity: select(waiting, 0.28, 0), scale: 1.1 }),
  path({ id: "o", d: ring, stroke: select(designing, sketch, ink), strokeWidth: 6, width: side, height: side,
        trim: select(waiting, 0.9, 1), label: "omui" }),
  when(designing, [
    path({ id: "o-grown", d: ring, stroke: ink, strokeWidth: 6, width: side, height: side, trim: (at - 1) / steps }),
    path({ id: "word-sketch", d: letters, stroke: sketch, strokeWidth: 6, width: side, height: side }),
    path({ id: "word-grown", d: letters, stroke: ink, strokeWidth: 6, width: side, height: side, trim: 0.55 }),
    timeline({ id: "steps", shape: course, steps, value: at, label: "The page's design", width: side + 20, height: side + 20,
               captions: ["The mark", "Its O", "The sentence", "The Live Design key", "The install", "Its two tabs", "Light", "At rest"] }),
  ], [
    when(connected, [
      path({ id: "mui-o", d: cells("o"), fill: palette.o, viewBox: "0 0 16 16", width: side * 0.62, height: side * 0.62 }),
      path({ id: "mui-b", d: cells("b"), fill: palette.b, viewBox: "0 0 16 16", width: side * 0.62, height: side * 0.62 }),
      path({ id: "mui-l", d: cells("l"), fill: palette.l, viewBox: "0 0 16 16", width: side * 0.62, height: side * 0.62 }),
      path({ id: "mui-w", d: cells("w"), fill: palette.w, viewBox: "0 0 16 16", width: side * 0.62, height: side * 0.62 }),
      path({ id: "mui-p", d: cells("p"), fill: palette.p, viewBox: "0 0 16 16", width: side * 0.62, height: side * 0.62 }),
      path({ id: "mui-k", d: cells("k"), fill: palette.k, viewBox: "0 0 16 16", width: side * 0.62, height: side * 0.62 }),
    ], [
      path({ id: "word", d: letters, stroke: ink, strokeWidth: 6, width: side, height: side }),
      path({ id: "dot", d: dot, fill: ink, width: side, height: side }),
    ]),
  ]),
]);

const live = button({ id: "live", variant: "secondary", text: select(designing, "Pause", "Live Design") });

// Under the mark: the Live Design key, and who is at work — the step and its maker in Live Design, the visitor's agent
// once connected.
const beside = column({ id: "beside", height: 100, width: "100%", alignItems: "center", gap: 10 }, [
  live,
  when(connected, [
    row({ id: "agent", gap: 8, alignItems: "center", paddingX: 12, paddingY: 7, radius: 999, background: "#eef2ff" }, [
      box({ width: 8, height: 8, radius: 4, background: "#16a34a" }),
      text({ style: "label", color: ink, text: "Claude Code · omui 0.1.0 · linux-x64" }),
    ]),
  ]),
  when(designing, [
    text({ id: "step", style: "caption", color: muted, align: "center", text: `Step ${at} of ${steps} · agent:designer · the sentence` }),
    text({ id: "source", style: "code", color: ink, align: "center", wrap: true, maxWidth: "100%",
           text: `text({ id: "line", style: "title", text: "A UI framework …" })` }),
  ]),
]);

const sentence = text({
  id: "line", role: "heading", level: 1, style: "title", align: "center", wrap: true, maxWidth: 760,
  color: select(designing, sketch, ink),
  text: "A UI framework where one semantic tree drives the pixels, the motion, accessibility, agents and tests.",
});

const install = column({
  id: "install", gap: 12, width: "100%", maxWidth: 640, padding: 18, radius: 18, background: "#ffffff", shadow: "lg",
  borderWidth: select(or(designing, connected), 1, 0), borderColor: select(connected, accent, sketch),
  opacity: select(designing, 0.45, 1),
}, [
  row({ justify: "space_between", alignItems: "center" }, [
    segmented({ id: "how", options: ["agent", "command"], labels: ["Agent", "Command"], value: how, label: "How to install" }),
    button({ id: "copy", variant: "primary", text: select(or(waiting, connected), "Copied", "Copy") }),
  ]),
  text({ id: "words", style: "code", wrap: true, color: ink, text: select(eq(how, "agent"), prompt, commands) }),
  when(connected, [
    text({ id: "token", style: "caption", color: accent, text: "surface · radius.lg · shadow.lg — the structure your agent changed" }),
  ]),
]);

mount("main", stack({ width: "100%", height: "100%", background: paper }, [
  column({ width: "100%", height: "100%", alignItems: "center", justify: "center", gap: 18, padding: 32 }, [
    mark,
    beside,
    sentence,
    install,
  ]),
  row({ position: "absolute", top: 0, right: 0, padding: 20 }, [
    button({ id: "github", variant: "ghost", text: "GitHub" }),
  ]),
]));

// Which state it shows, for an agent drawing its sketches (and, later, the bridge and the Live Design key).
verb("page.state", { effect: "reversible", summary: "Which of the page's four states it shows: rest, design, waiting or connected" },
  (args: { state: State }) => { state.set(args.state); });
