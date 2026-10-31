// A page in a real browser, in real time: headless Chromium driven over the
// DevTools protocol (no dependencies: node's own fetch and WebSocket). The
// page is loaded and left to run for `seconds` — its frames, its timers, its
// fonts arriving — then, when given, a script is evaluated in it (awaited;
// window.omui is the program's agent interface) and three seconds later the
// page is captured. What the page logged and threw is printed. (A headless
// shell's virtual time runs no animation frames: a program paced by them
// stops there, so this one runs in real time.)
//
//   CHROME=<chromium> node tools/ci/drive-page.mjs <url> <out.png> [seconds] [width] [height] [script]
//   CHROME=… node tools/ci/drive-page.mjs 'http://localhost:8000/#/overview' overview.png 8
//   CHROME=… node tools/ci/drive-page.mjs http://localhost:8000/ f.png 6 1280 800 \
//       "window.omui.act('main/body/nav/to_features', 'activate').then(r => r.exitCode)"
import { spawn } from 'node:child_process';
import { writeFileSync } from 'node:fs';
const [url, out, secs = '6', w = '1280', h = '800', script = ''] = process.argv.slice(2);
const chrome = process.env.CHROME;
if (!chrome || !url || !out) {
  console.error('usage: CHROME=<chromium> node drive-page.mjs <url> <out.png> [seconds] [width] [height] [script]');
  process.exit(2);
}
const port = 9300 + Math.floor(Math.random() * 500);
const proc = spawn(chrome, ['--headless=new', '--no-sandbox', '--disable-gpu', `--remote-debugging-port=${port}`, `--window-size=${w},${h}`, 'about:blank'], { stdio: 'ignore' });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let targets;
for (let i = 0; i < 50; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); break; } catch { await sleep(200); } }
const page = targets.find((t) => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r));
let id = 0; const waiting = new Map(); const logs = [];
ws.addEventListener('message', (m) => { const d = JSON.parse(m.data); if (d.id && waiting.has(d.id)) { waiting.get(d.id)(d); waiting.delete(d.id); } if (d.method === 'Runtime.consoleAPICalled') logs.push(d.params.type + ' ' + d.params.args.map((a) => a.value ?? a.description).join(' ')); if (d.method === 'Runtime.exceptionThrown') logs.push('EXC ' + JSON.stringify(d.params.exceptionDetails).slice(0, 300)); });
const call = (method, params = {}) => new Promise((r) => { const i = ++id; waiting.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
await call('Runtime.enable');
await call('Emulation.setDeviceMetricsOverride', { width: +w, height: +h, deviceScaleFactor: 1, mobile: false });
await call('Page.navigate', { url });
await sleep(+secs * 1000);
if (script) { const r = await call('Runtime.evaluate', { expression: script, awaitPromise: true, returnByValue: true }); logs.push('EVAL ' + JSON.stringify(r.result?.result?.value ?? r.result).slice(0, 2000)); await sleep(3000); }
const shot = await call('Page.captureScreenshot', { format: 'png' });
writeFileSync(out, Buffer.from(shot.result.data, 'base64'));
console.log(logs.join('\n'));
ws.close(); proc.kill();
process.exit(0);
