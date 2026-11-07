// Skia's and resvg's side of omui-bench-vector (src/vector.cpp): the scenes
// it wrote — the same paths and paints — drawn by Skia's CPU raster through
// @napi-rs/canvas (one canvas call a path, the Path2D objects made before the
// clock starts) and by resvg (tiny-skia; one call a frame, the SVG parsed
// before). Then the three pictures compared, Skia's as the reference: the
// mean difference per channel and the share of pixels any channel of which
// differs by more than 8 of 255.
//
//   node vector.mjs DIR [--runs N]
import { createCanvas, loadImage, Path2D } from "@napi-rs/canvas";
import { Resvg } from "@resvg/resvg-js";
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const args = process.argv.slice(2);
const dir = args[0];
const runs = args.includes("--runs") ? Number(args[args.indexOf("--runs") + 1]) : 15;
const versions = JSON.parse(readFileSync(new URL("./package.json", import.meta.url))).dependencies;

const timed = (draw) => {
  for (let run = 0; run < 3; ++run) draw();
  const times = [];
  for (let run = 0; run < runs; ++run) {
    const begin = process.hrtime.bigint();
    draw();
    times.push(Number(process.hrtime.bigint() - begin) / 1e6);
  }
  const mean = times.reduce((sum, time) => sum + time, 0) / times.length;
  times.sort((a, b) => a - b);
  return { median_ms: +times[times.length >> 1].toFixed(3), mean_ms: +mean.toFixed(3), min_ms: +times[0].toFixed(3) };
};

const pixels = async (png, width, height) => {
  const canvas = createCanvas(width, height);
  canvas.getContext("2d").drawImage(await loadImage(png), 0, 0);
  return canvas.getContext("2d").getImageData(0, 0, width, height).data;
};
const compare = (reference, other) => {
  let total = 0, off = 0, worst = 0;
  for (let at = 0; at < reference.length; at += 4) {
    let most = 0;
    for (let channel = 0; channel < 4; ++channel) {
      const difference = Math.abs(reference[at + channel] - other[at + channel]);
      total += difference;
      most = Math.max(most, difference);
    }
    if (most > 8) ++off;
    worst = Math.max(worst, most);
  }
  return { mean_difference: +(total / reference.length).toFixed(3), pixels_off_percent: +(100 * off / (reference.length / 4)).toFixed(3), worst };
};

for (const file of readdirSync(dir).filter((name) => name.endsWith(".scene.json")).sort()) {
  const scene = JSON.parse(readFileSync(join(dir, file)));
  const { width, height } = scene;

  const canvas = createCanvas(width, height);
  const context = canvas.getContext("2d");
  const items = scene.items.map((item) => {
    const path = new Path2D(item.d);
    if (item.gradient) {
      const [x1, y1, x2, y2, from, to] = item.gradient;
      const gradient = context.createLinearGradient(x1, y1, x2, y2);
      gradient.addColorStop(0, from);
      gradient.addColorStop(1, to);
      return { path, fill: gradient };
    }
    return { path, fill: item.fill, stroke: item.stroke, width: item.width, cap: item.cap, join: item.join };
  });
  context.miterLimit = 4;
  const skia = timed(() => {
    context.fillStyle = "#ffffff";
    context.fillRect(0, 0, width, height);
    for (const item of items) {
      if (item.stroke) {
        context.strokeStyle = item.stroke;
        context.lineWidth = item.width;
        context.lineCap = item.cap;
        context.lineJoin = item.join;
        context.stroke(item.path);
      } else {
        context.fillStyle = item.fill;
        context.fill(item.path);
      }
    }
    context.getImageData(0, 0, 1, 1);  // the drawing done, not queued
  });
  const skiaPng = canvas.toBuffer("image/png");
  writeFileSync(join(dir, `${scene.name}.skia.png`), skiaPng);

  const resvg = new Resvg(readFileSync(join(dir, `${scene.name}.svg`)), { fitTo: { mode: "original" } });
  let rendered;
  const tiny = timed(() => { rendered = resvg.render(); });
  const resvgPng = rendered.asPng();
  writeFileSync(join(dir, `${scene.name}.resvg.png`), resvgPng);

  const reference = await pixels(skiaPng, width, height);
  const ours = await pixels(readFileSync(join(dir, `${scene.name}.xrender.png`)), width, height);
  const theirs = await pixels(resvgPng, width, height);
  console.log(JSON.stringify({ renderer: `skia (@napi-rs/canvas ${versions["@napi-rs/canvas"]})`, scene: scene.name, runs, ...skia }));
  console.log(JSON.stringify({ renderer: `resvg ${versions["@resvg/resvg-js"]}`, scene: scene.name, runs, ...tiny }));
  console.log(JSON.stringify({ scene: scene.name, "xrender against skia": compare(reference, ours), "resvg against skia": compare(reference, theirs) }));
  // xrender's own SVG of the scene, drawn back by resvg and by Skia (its SVG
  // module, through loadImage), against xrender's pixels.
  const svg = readFileSync(join(dir, `${scene.name}.xrender.svg`));
  const backByResvg = await pixels(new Resvg(svg, { fitTo: { mode: "original" } }).render().asPng(), width, height);
  const backBySkia = await pixels(svg, width, height);
  console.log(JSON.stringify({ scene: scene.name, svg_bytes: svg.length, "its SVG by resvg against xrender": compare(ours, backByResvg), "its SVG by skia against xrender": compare(ours, backBySkia) }));
}
