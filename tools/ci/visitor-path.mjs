// The visitor's path (D-95; W3's CI case): the built site in a real browser,
// as a person opens it, and that person's own agent operating it — the way
// the page's prompt tells an agent to: the page gives a pairing code (its
// "copy the prompt" press: window.omui.link()), the agent starts the bridge
// with it (`omui connect --code … --name …`), waits for the page
// (`omui page status --wait`), then reads and operates the page with
// `omui page …`: its semantic tree, an action, the interaction sweep over
// every control. Both ends say how it stands — the agent's `omui page
// status`, the page's window.omui.linkState() — and when the bridge stops,
// the page knows. One JSON report on stdout; exit 1 when a step fails.
//
//   CHROME=<chromium> OMUI=<omui> node tools/ci/visitor-path.mjs --serve target/site
import { spawn, spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { browser, serve, sleep, usage } from './page.mjs';

const args = process.argv.slice(2);
const opt = (name, fallback) => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : fallback; };
const chrome = process.env.CHROME, omui = process.env.OMUI, dir = opt('serve', '');
if (!chrome || !omui || !dir) usage('visitor-path.mjs', ' --serve DIR (and OMUI=<omui>)');

const report = { steps: [] };
const step = (ok, what, detail = '') => { report.steps.push({ ok: !!ok, what, ...(detail ? { detail } : {}) }); return ok; };
const bridgeDir = mkdtempSync(join(tmpdir(), 'omui-visitor-'));
// An agent's command: the bridge's directory its own (no person's bridge on this machine is touched).
const page = (...a) => {
  const r = spawnSync(omui, ['page', ...a, '--dir', bridgeDir], { encoding: 'utf8', timeout: 600000 });
  return { code: r.status, out: r.stdout ?? '', err: r.stderr ?? '' };
};

const server = await serve(dir);
const b = await browser(chrome, { width: 1440, height: 900 });
let bridge = null;
try {
  await b.call('Page.navigate', { url: server.url });
  for (let i = 0; i < 300; i++) {
    if (await b.value(`!!window.omui && (document.querySelector('[data-omui=mirror]')?.childElementCount ?? 0) > 0`)) break;
    await sleep(100);
  }
  step(await b.value('!!window.omui'), 'the page runs: window.omui answers');

  // 1. The person's press: the page makes a pairing code and looks for a bridge.
  const code = await b.value('window.omui.link()');
  step(/^[A-Z2-9]{4}-[A-Z2-9]{4}$/.test(code ?? ''), 'the page gives a pairing code', code);
  step((await b.value('window.omui.linkState()'))?.state === 'waiting', 'the page waits for its agent');

  // 2. The agent, as the prompt tells it: the bridge with the code, then waiting for the page.
  bridge = spawn(omui, ['connect', '--code', code, '--name', 'CI agent', '--dir', bridgeDir], { stdio: ['ignore', 'pipe', 'pipe'] });
  let said = '';
  bridge.stdout.on('data', (d) => { said += d; });
  bridge.stderr.on('data', (d) => { said += d; });
  const status = page('status', '--wait');
  step(status.code === 0 && status.out.includes('"paired":true'), 'the agent sees the page paired (omui page status --wait)', (status.out + status.err).slice(0, 400));
  for (let i = 0; i < 50 && (await b.value('window.omui.linkState()'))?.state !== 'paired'; i++) await sleep(100);
  const state = await b.value('window.omui.linkState()');
  step(state?.state === 'paired' && state?.agent === 'CI agent', 'the page sees its agent, by name', JSON.stringify(state));

  // 3. The agent operates the page through its semantic tree.
  const snap = page('snapshot', '--interactive');
  step(snap.code === 0 && snap.out.includes('main/bar/nav'), 'the agent reads the page (omui page snapshot)', snap.err.slice(0, 300));
  const act = page('act', 'main/bar/nav/to_features', 'activate');
  step(act.code === 0, 'the agent acts on it (omui page act … activate)', (act.out + act.err).slice(0, 300));
  await sleep(1500);
  // Where it is, as the agent reads it: the features' place on screen, home's gone from the tree.
  const there = page('snapshot');
  step(there.out.includes('"main/body/view/features') && !there.out.includes('"main/body/view/home'), 'the page went where the agent took it',
       there.err.slice(0, 300));
  const back = page('act', 'main/bar/nav/to_home', 'activate');
  step(back.code === 0, 'and back home', (back.out + back.err).slice(0, 300));
  await sleep(1000);

  // 4. Every control the agent can operate, operated through the page (the interaction sweep, D-91).
  const sweep = page('sweep');
  const summary = sweep.out.split('\n').filter((l) => l.includes('"nodes"')).pop() ?? '';
  const failed = sweep.out.split('\n').filter((l) => l.includes('"verdict":"failed"'));
  step(sweep.code === 0, 'the interaction sweep through the page (omui page sweep)', summary + (failed.length ? ' ' + failed.join(' ').slice(0, 800) : ''));

  // 5. The bridge stops: the page knows.
  bridge.kill();
  bridge = null;
  let after = null;
  for (let i = 0; i < 100; i++) {
    after = await b.value('window.omui.linkState()');
    if (after?.state !== 'paired') break;
    await sleep(100);
  }
  step(after && after.state !== 'paired', 'the bridge gone: the page says so', JSON.stringify(after));
} catch (e) {
  step(false, 'the path ran to its end', String(e));
} finally {
  if (bridge) bridge.kill();
  b.close();
  server.close();
  rmSync(bridgeDir, { recursive: true, force: true });
}
report.ok = report.steps.every((s) => s.ok);
console.log(JSON.stringify(report, null, 1));
process.exit(report.ok ? 0 : 1);
