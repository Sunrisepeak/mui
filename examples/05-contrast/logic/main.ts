// The contrast checker: two colours in, how the pair reads out — the WCAG 2.2
// ratio and levels, a sample at both text sizes, the pair as each
// colour-vision deficiency shows it, and the nearest colour that passes.
// The colour science is the application's native module (native/
// contrast.native.omui.toml, C++); this file is all the interface.
import { signal, fmt, select, eq, ne, column, row, box, text, textBox, when, mount, OmuiError } from "openmui";
import { contrast } from "openmui:native/contrast";

const foreground = signal("#6b7280");
const background = signal("#ffffff");
const report = signal(contrast.check(foreground.get(), background.get()));
const views = signal(contrast.simulate(foreground.get(), background.get()));
const suggestion = signal(null);
const problem = signal("");

let fg: string = foreground.get();
let bg: string = background.get();
let asked = 0;

// What the colours say now. While a colour is half typed the last good
// answer stays, with why the new one is not a colour yet.
function update() {
  try {
    report.set(contrast.check(fg, bg));
    views.set(contrast.simulate(fg, bg));
    problem.set("");
  } catch (e) {
    problem.set(e instanceof OmuiError ? e.message.replace(/^.*?\): /, "") : String(e));
    return;
  }
  // The search runs on a worker; only the latest question's answer is shown.
  const ticket = ++asked;
  contrast.suggest(fg, bg, 4.5).then((s: string | null) => {
    if (ticket === asked) suggestion.set(s);
  });
}
update();

const level = (id: string, passes: unknown, name: string) =>
  text({ id, text: select(passes, fmt("✓ {}", name), fmt("✗ {}", name)), style: "heading" });

const kinds = ["protanopia", "deuteranopia", "tritanopia", "achromatopsia"];

export const Contrast = () =>
  column({ padding: 20, gap: 14 }, [
    row({ id: "colours", gap: 12 }, [
      column({ id: "fg", gap: 4, grow: 1, basis: 0, minWidth: 0 }, [
        text({ text: "Text colour", style: "caption" }),
        textBox({ id: "foreground", label: "Text colour", text: foreground, onChanged: (e: { text: string }) => { fg = e.text; update(); } }),
      ]),
      column({ id: "bg", gap: 4, grow: 1, basis: 0, minWidth: 0 }, [
        text({ text: "Background", style: "caption" }),
        textBox({ id: "background", label: "Background", text: background, onChanged: (e: { text: string }) => { bg = e.text; update(); } }),
      ]),
    ]),
    when(ne(problem, ""), text({ id: "problem", text: problem })),
    box({ id: "sample", background: report.field("background"), padding: 16, radius: 8, borderWidth: 1, borderColor: "#e5e7eb" }, [
      column({ gap: 6 }, [
        text({ id: "normal", text: "The quick brown fox jumps over the lazy dog.", color: report.field("foreground") }),
        text({ id: "large", text: "Large text reads at 3:1", color: report.field("foreground"), style: "title" }),
      ]),
    ]),
    text({ id: "ratio", text: report.field("text"), style: "display" }),
    row({ id: "levels", gap: 14 }, [
      level("aa", report.field("aa"), "AA"),
      level("aa_large", report.field("aa_large"), "AA large"),
      level("aaa", report.field("aaa"), "AAA"),
      level("ui", report.field("ui"), "UI"),
    ]),
    text({
      id: "suggestion",
      text: select(eq(suggestion, null), "No lightness reaches AA on this background",
        select(eq(suggestion, report.field("foreground")), "Meets AA as it is", fmt("Nearest that meets AA: {}", suggestion))),
    }),
    row({ id: "views", gap: 8 }, kinds.map((kind, i) =>
      box({ id: kind, background: views.at(i).field("background"), padding: 10, radius: 6, grow: 1, borderWidth: 1, borderColor: "#e5e7eb" }, [
        column({ gap: 2 }, [
          text({ text: views.at(i).field("label"), color: views.at(i).field("foreground"), style: "caption" }),
          text({ text: views.at(i).field("text"), color: views.at(i).field("foreground"), style: "heading" }),
        ]),
      ]))),
  ]);

mount("main", Contrast());
