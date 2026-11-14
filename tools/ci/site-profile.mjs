// The site's first screen, profiled (tools/ci/site-split.sh, D5): a packed
// page whose program is wasm-split's instrumented build is opened at a
// route, waits for its first frame, is used as a visitor uses a first
// screen — the pointer across it (Mui notices), the page scrolled down to
// its end and back — then left idle; then the program writes which of its
// functions ran (`__write_profile`), and that profile says what the first
// half of the split holds.
//
//   CHROME=<chromium> node tools/ci/site-profile.mjs DIR PROFILE [--route R] [--idle MS]
import { writeFileSync } from 'node:fs';
import { browser, serve, sleep, usage } from './page.mjs';

const args = process.argv.slice(2);
const option = (name, fallback) => (args.includes(name) ? args[args.indexOf(name) + 1] : fallback);
const [dir, out] = args.filter((a, i) => !a.startsWith('--') && !['--route', '--idle'].includes(args[i - 1]));
if (!dir || !out || !process.env.CHROME) usage('tools/ci/site-profile.mjs', ' DIR PROFILE [--route R] [--idle MS]');
const route = option('--route', '');
const idle = Number(option('--idle', '4000'));

const server = await serve(dir);
const page = await browser(process.env.CHROME, { width: 1280, height: 800 });
const errors = [];
page.on((method, params) => { if (method === 'Runtime.exceptionThrown') errors.push(params.exceptionDetails.exception?.description ?? params.exceptionDetails.text); });
// The instance that carries the profile: the loader's own instantiation.
await page.call('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
  const keep = (result) => { const instance = result.instance || result; if (instance.exports && instance.exports.__write_profile) window.omuiProfiled = instance; return result; };
  const instantiate = WebAssembly.instantiate, streaming = WebAssembly.instantiateStreaming;
  WebAssembly.instantiate = function (...a) { return instantiate.apply(this, a).then(keep); };
  if (streaming) WebAssembly.instantiateStreaming = function (...a) { return streaming.apply(this, a).then(keep); };
})();` });
await page.call('Page.navigate', { url: `${server.url}index.html${route ? `#${route}` : ''}` });
let presented = false;
for (let i = 0; i < 600 && !presented; i++) {
  presented = await page.value('!!(window.omuiProfiled && window.omuiHasPresented)');
  if (!presented) await sleep(100);
}
if (!presented) {
  console.error(`site-profile: the instrumented page never presented its first frame${errors.length ? `: ${errors[0]}` : ''}`);
  process.exit(1);
}
const mouse = (type, x, y, extra = {}) => page.call('Input.dispatchMouseEvent', { type, x, y, ...extra });
// The pointer across the first screen, slowly: hover states, Mui turning to it.
for (let step = 0; step <= 24; step++) {
  await mouse('mouseMoved', 80 + step * 46, 120 + (step % 6) * 100);
  await sleep(60);
}
await sleep(1200);
// Down the page to its end, a wheel's notch at a time, and back up.
for (let notch = 0; notch < 40; notch++) { await mouse('mouseWheel', 640, 400, { deltaX: 0, deltaY: 240 }); await sleep(80); }
await sleep(1000);
for (let notch = 0; notch < 40; notch++) { await mouse('mouseWheel', 640, 400, { deltaX: 0, deltaY: -240 }); await sleep(50); }
await sleep(idle);
const encoded = await page.value(`(() => {
  const exports = window.omuiProfiled.exports;
  const memory = Object.values(exports).find((v) => v instanceof WebAssembly.Memory);
  const pages = 64, at = memory.grow(pages) * 65536;
  const length = exports.__write_profile(at, pages * 65536);
  const bytes = new Uint8Array(memory.buffer, at, length);
  let text = ''; for (const byte of bytes) text += String.fromCharCode(byte);
  return btoa(text);
})()`);
await page.close();
server.close();
if (!encoded) { console.error('site-profile: no profile written'); process.exit(1); }
const profile = Buffer.from(encoded, 'base64');
writeFileSync(out, profile);
console.log(`site-profile: ${profile.length} bytes${route ? ` (route ${route})` : ''}${errors.length ? `; ${errors.length} page exception(s): ${errors[0]}` : ''}`);
if (errors.length) process.exit(1);
