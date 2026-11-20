// omui's site, one page (G5): the mark, one sentence, the Live Design key and
// the install, and nothing else (the site's design §4). One picture in four
// states — at rest; Live Design (the page back to its sketch, grown again
// step by step, the progress along the mark's ring); waiting (the prompt or
// the commands copied: the ring open, breathing); connected (the ring closed,
// the mark holds Mui, the visitor's agent and its omui beside it). How one
// state becomes the next is the style's motion, not this page's (D-93).
import {
  signal, eq, or, select, when, stack, column, row, box, text, button, segmented, link, path, timeline, mount, verb, invoke, copy,
  fmt, linkAgent, agentLink,
} from "openmui";

type State = "rest" | "design" | "waiting" | "connected";
const state = signal<State>("rest", "page.state");
const how = signal("agent", "page.install");

const ink = "#1e1b4b";
const accent = "#4f46e5";
const muted = "#64748b";
const paper = "#faf9f6";
const sketch = "#cbd5e1";

// The mark: a plain circle around the word, in a 100-unit square.
const ring = "M50 10A42 42 0 1 1 50 94A42 42 0 1 1 50 10Z";
const letters = "M24 64L24 44M24 50C24 41 35 41 35 50L35 64M35 50C35 41 46 41 46 50L46 64M55 44L55 56C55 66 69 66 69 56L69 44M78 44L78 64";
const dot = "M75.5 35A2.5 2.5 0 1 0 80.5 35A2.5 2.5 0 1 0 75.5 35Z";
// The same circle in a 1×1 box: the course Live Design's steps lie along.
const course = "M0.5 0.1A0.42 0.42 0 1 1 0.5 0.94A0.42 0.42 0 1 1 0.5 0.1Z";

// Mui's head (site/brand/mascot.sprite.xdef.json, head16 idle): every pixel a vector square, one path a colour.
const head = [
  "................", "...o........o...", "..oko......oko..", "..obko....okbo..",
  ".obbbooooooobbbo", ".obbbbbbbbbbbbbo", "obblbbbbbbbbblbo", "obbpwbbbbbbpwbbo",
  "obbppbbbbbbppbbo", "obkbbbobbobbbkbo", "obbbbbboobbbbbbo", ".obbbbbbbbbbbbo.",
  ".obbbbbbbbbbbbo.", "..oobbbbbbbboo..", "....oooooooo....", "................",
];
const palette: Record<string, string> = { o: "#1e1b4b", b: "#4f46e5", l: "#818cf8", w: "#ffffff", p: "#1e1b4b", k: "#f9a8d4", s: "#4338ca" };
// One colour of Mui's head, its pixels as one path: connected, her head is the avatar in the circle.
function pixels(letter: string): string {
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

// The visitor's own agent, through a bridge on their machine (spec §6): Copy starts looking for it, and the pairing code
// is in the words copied and shown; paired, the page is connected, and says who and with what.
const agent = agentLink();
const code = signal("····-····", "page.code");  // its shape until Copy gives one
const promptWords = "Install omui (after the release; until then build it from GitHub), run `omui connect {}`, then call `describe` and follow it.";
const commandWords = "# after the release; until then build it from GitHub\nomui connect {}";
agent.watch((now) => {
  if (now.state === "paired") state.set("connected");
  else if (state.get() === "connected" && (now.state === "lost" || now.state === "stopped")) state.set("waiting");
});

// The mark: its O (open while it waits, drawn as far as the design has come), the word or Mui, and in Live Design the
// course of the design's steps along the O.
const mark = stack({ id: "mark", width: side + 20, height: side + 20, alignItems: "center", justify: "center" }, [
  // Breathing while it waits: a wider, fainter O, its breath the style's ambient motion (spec M-7); unseen, it rests.
  path({ id: "breath", d: ring, stroke: accent, strokeWidth: 2, width: side, height: side,
         opacity: select(waiting, 0.28, 0), scale: 1.1, motion: { ambient: "breathe" } }),
  path({ id: "o", d: ring, stroke: select(designing, sketch, ink), strokeWidth: 6, width: side, height: side,
        trim: select(waiting, 0.9, 1), label: select(connected, "Mui", "omui") }),
  when(designing, [
    path({ id: "o-grown", d: ring, stroke: ink, strokeWidth: 6, width: side, height: side, trim: (at - 1) / steps }),
    path({ id: "word-sketch", d: letters, stroke: sketch, strokeWidth: 6, width: side, height: side }),
    path({ id: "word-grown", d: letters, stroke: ink, strokeWidth: 6, width: side, height: side, trim: 0.55 }),
    // The course is the O's own stroke (grown in ink as far as the design has come): the timeline adds its marks and
    // its playhead on it, no line of its own.
    timeline({ id: "steps", shape: course, steps, value: at, line: false, label: "The page's design", width: side + 20, height: side + 20,
               captions: ["The mark", "Its O", "The sentence", "The Live Design key", "The install", "Its two tabs", "Light", "At rest"] }),
  ], [
    when(connected, [
      stack({ id: "mui", width: side * 0.6, height: side * 0.6 },
        ["o", "b", "l", "w", "p", "k"].map((letter) =>
          path({ id: `mui-${letter}`, d: pixels(letter), fill: palette[letter], viewBox: "0 0 16 16", width: side * 0.6, height: side * 0.6 }))),
    ], [
      path({ id: "word", d: letters, stroke: ink, strokeWidth: 6, width: side, height: side }),
      path({ id: "dot", d: dot, fill: ink, width: side, height: side }),
    ]),
  ]),
]);

// The design of this page, played in place (generate.replay: each step the real page as it was, from what `omui new`
// wrote); where none ships, the sketch of it.
const live = button({
  id: "live", variant: "secondary", text: select(designing, "Pause", "Live Design"),
  onPress: () => {
    try { invoke("generate.replay", { play: true }); } catch { state.set(state.get() === "design" ? "rest" : "design"); }
  },
});

// Under the mark: the Live Design key, and who is at work — the step and its maker in Live Design, the visitor's agent
// once connected.
const beside = column({ id: "beside", height: 100, width: "100%", alignItems: "center", gap: 10 }, [
  live,
  when(connected, [
    row({ id: "agent", gap: 8, alignItems: "center", paddingX: 12, paddingY: 7, radius: 999, background: "#eef2ff" }, [
      box({ width: 8, height: 8, radius: 4, background: "#16a34a" }),
      text({ id: "who", style: "label", color: ink,
             text: select(eq(agent.field("state"), "paired"), fmt("{} · omui {} · {}", agent.field("agent"), agent.field("version"), agent.field("platform")),
                          "Your agent · omui · your machine") }),
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
  id: "install", label: "Install", gap: 12, width: "100%", maxWidth: 640, padding: 18, radius: 18, background: "#ffffff", shadow: "lg",
  borderWidth: select(or(designing, connected), 1, 0), borderColor: select(connected, accent, sketch),
  opacity: select(designing, 0.45, 1),
}, [
  row({ justify: "space_between", alignItems: "center" }, [
    segmented({ id: "how", options: ["agent", "command"], labels: ["Agent", "Command"], value: how, label: "How to install" }),
    // The chosen tab's words on the clipboard; the page waits for the agent (or the command) to connect.
    button({ id: "copy", variant: "primary", text: select(or(waiting, connected), "Copied", "Copy"),
             onPress: () => {
               const given = linkAgent();  // "" where the page cannot link one (off the Web)
               if (given) code.set(given);
               copy((how.get() === "agent" ? promptWords : commandWords).replace("{}", code.get()));
               if (state.get() !== "connected") state.set("waiting");
             } }),
  ]),
  text({ id: "words", style: "code", wrap: true, color: ink,
         text: select(eq(how, "agent"), fmt(promptWords, code), fmt(commandWords, code)) }),
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
    link({ id: "github", text: "GitHub", href: "https://github.com/Sunrisepeak/mui" }),
  ]),
]));

// Which state it shows, for an agent drawing its sketches (and, later, the bridge and the Live Design key).
verb("page.state", { effect: "reversible", summary: "Which of the page's four states it shows: rest, design, waiting or connected" },
  (args: { state: State }) => { state.set(args.state); });
