// GSAP's side of motion/drive-1000 (tests/bench): 1000 numeric values each
// tweened with an overshooting ease (elastic.out — GSAP has no spring), every
// one retargeted every 30th frame, the timeline advanced by one 60 Hz frame
// at a time and every value read — the work hanim's Engine does there. On
// V8 in node, GSAP's own ticker off (frames advanced by updateRoot).
//
//   node gsap-1000.mjs [--frames 3000] [--json]
import { gsap } from "gsap";

const args = process.argv.slice(2);
const frames = args.includes("--frames") ? Number(args[args.indexOf("--frames") + 1]) : 3000;
const asJson = args.includes("--json");

gsap.ticker.sleep();          // frames are ours, not the ticker's
gsap.ticker.lagSmoothing(0);
const values = Array.from({ length: 1000 }, () => ({ v: 0 }));
let now = 0, sum = 0;
const times = [];
const step = (frame) => {
  if (frame % 30 === 0)
    for (let i = 0; i < values.length; ++i)
      gsap.to(values[i], { v: (frame / 30) % 2 ? 0 : 1 + i, duration: 0.5, ease: "elastic.out(1, 0.3)", overwrite: true });
  now += 1 / 60;
  gsap.updateRoot(now);
  for (let i = 0; i < values.length; ++i) sum += values[i].v;  // read every value, as evaluate_driven writes them
};
for (let frame = 0; frame < 300; ++frame) step(frame);  // warm the JIT
for (let frame = 300; frame < 300 + frames; ++frame) {
  const begin = process.hrtime.bigint();
  step(frame);
  times.push(Number(process.hrtime.bigint() - begin) / 1e6);
}
times.sort((a, b) => a - b);
const at = (p) => times[Math.min(times.length - 1, Math.ceil((p / 100) * times.length) - 1)];
const mean = times.reduce((a, b) => a + b, 0) / times.length;
const report = { benchmark: "gsap/tween-1000", engine: `gsap ${gsap.version}`, runtime: `node ${process.version}`, frames,
  mean_ms: mean, p50_ms: at(50), p95_ms: at(95), max_ms: times[times.length - 1], checksum: sum };
console.log(asJson ? JSON.stringify(report) : `${report.benchmark}  ${report.engine} on ${report.runtime}: mean ${mean.toFixed(4)} ms, p50 ${at(50).toFixed(4)}, p95 ${at(95).toFixed(4)}, max ${report.max_ms.toFixed(4)} (${frames} frames)`);
