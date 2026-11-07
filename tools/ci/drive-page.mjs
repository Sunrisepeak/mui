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
//       "window.omui.act('main/bar/nav/to_features', 'activate').then(r => r.exitCode)"
import { writeFileSync } from 'node:fs';
import { browser, sleep } from './page.mjs';
const [url, out, secs = '6', w = '1280', h = '800', script = ''] = process.argv.slice(2);
const chrome = process.env.CHROME;
if (!chrome || !url || !out) {
  console.error('usage: CHROME=<chromium> node drive-page.mjs <url> <out.png> [seconds] [width] [height] [script]');
  process.exit(2);
}
const b = await browser(chrome, { width: +w, height: +h, flags: ['--disable-gpu'] });
const logs = [];
b.on((method, params) => {
  if (method === 'Runtime.consoleAPICalled') logs.push(params.type + ' ' + params.args.map((a) => a.value ?? a.description).join(' '));
  if (method === 'Runtime.exceptionThrown') logs.push('EXC ' + JSON.stringify(params.exceptionDetails).slice(0, 300));
});
await b.call('Runtime.enable');
await b.call('Emulation.setDeviceMetricsOverride', { width: +w, height: +h, deviceScaleFactor: 1, mobile: false });
await b.call('Page.navigate', { url });
await sleep(+secs * 1000);
if (script) { const r = await b.call('Runtime.evaluate', { expression: script, awaitPromise: true, returnByValue: true }); logs.push('EVAL ' + JSON.stringify(r.result?.result?.value ?? r.result).slice(0, 2000)); await sleep(3000); }
const shot = await b.call('Page.captureScreenshot', { format: 'png' });
writeFileSync(out, Buffer.from(shot.result.data, 'base64'));
console.log(logs.join('\n'));
await b.close();
process.exit(0);
