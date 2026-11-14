// Material 3's controls in four states — at rest, under the pointer,
// pressed, keyboard-focused — each a 220×84 cell at 2× on the same
// background as ours (site/tests/site/quality_lab_test.cpp), one strip a control:
// the competitor's side of the controls' side-by-side review (D-135).
//
//   CHROME=<chromium> node capture.mjs OUT_DIR      (after npm install and the esbuild bundle)
//
// OUT_DIR/<control>-<state>.png; tools/eval/materials/controls.sh joins each control's four.
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { browser, serve, sleep } from '../../../../../tools/ci/page.mjs';

const out = process.argv[2];
mkdirSync(out, { recursive: true });
const server = await serve(new URL('.', import.meta.url).pathname);
const page = await browser(process.env.CHROME, { width: 220, height: 84 });
await page.call('Emulation.setDeviceMetricsOverride', { width: 220, height: 84, deviceScaleFactor: 2, mobile: false });
const states = ['rest', 'hover', 'pressed', 'focus'];
const open = async (name) => {
  await page.call('Page.navigate', { url: `${server.url}controls.html?control=${name}` });
  for (let i = 0; i < 100 && !(await page.value('!!window.controlReady')); i++) await sleep(100);
  await sleep(400);
};
const names = await (async () => { await open('primary'); return page.value('window.controlNames'); })();
const shot = async () => (await page.call('Page.captureScreenshot', { format: 'png', clip: { x: 0, y: 0, width: 220, height: 84, scale: 1 } })).result.data;
for (const name of names) {
  for (const state of states) {
    await open(name);
    const box = await page.value(`(() => { const r = document.getElementById('it').getBoundingClientRect(); return [r.x, r.y, r.width, r.height]; })()`);
    // A slider is pressed at its thumb (60 %), the rest at their middle.
    const x = name === 'slider' ? box[0] + 10 + (box[2] - 20) * 0.6 : box[0] + box[2] / 2, y = box[1] + box[3] / 2;
    if (state === 'hover' || state === 'pressed') await page.call('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
    if (state === 'pressed') await page.call('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', buttons: 1, clickCount: 1 });
    if (state === 'focus') await page.call('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 }),
                           await page.call('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 });
    await sleep(800);  // ours settle 0.8 s
    writeFileSync(join(out, `${name}-${state}.png`), Buffer.from(await shot(), 'base64'));
    if (state === 'pressed') await page.call('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', buttons: 0, clickCount: 1 });
  }
  console.log(`material3: ${name}`);
}
await page.close();
server.close();
