// Numbers over labelled steps, drawn as lines, bars or areas, with a scale
// that reads in round numbers; pointing at a step shows its values. A figure
// to every reader, each point an item of its own (`<id>/<series>/<step>`)
// whose name is the series and the step and whose value is the number — an
// agent reads the data exactly, not the picture.
import { defineContract } from "openmui/define";

export default defineContract({
  chart: {
    role: "figure",
    spec: "0.1",
    doc: "Numbers over labelled steps as lines, bars or areas; each point an item of its own an agent reads exactly.",
    props: {
      kind: { type: "enum", values: ["line", "bar", "area"], default: "line" },
      labels: { type: "list<string>", default: [], bindable: true, doc: "The steps (\"Jan\", \"Feb\" …)." },
      series: { type: "any", default: [], bindable: true, doc: "[{ name, values: number[], color? }]: each series a value a step." },
      label: { type: "string", default: "", doc: "Its name; also what its description summarises." },
      unit: { type: "string", default: "", doc: "Said after each number (\"ms\", \"%\")." },
      zero: { type: "bool", default: true, doc: "The scale starts at zero; else at the smallest value." },
    },
    parts: { axis: {}, grid: {}, series: {}, point: {}, guide: {} },
  },
});
