// A test program built for wasm32-emscripten (mcpp's wasm-browser profile:
// linked to suspend, -sASYNCIFY, and to report its exit) run in a real page:
// headless Chromium opens it from a server on localhost (a secure context,
// as WebGPU asks), the page sends back what the program prints, line by
// line and in order, and its exit code. Under Asyncify a main() that
// suspended returns its code to nobody, so an openmui.testing suite's own
// summary line (`<suite>: N passed, M failed`) is read too. Exit: 1 when
// the program exits non-zero, a suite failed or said nothing, the page
// failed to load it, or the time ran out; else 0.
//
//   node tools/ci/browser.mjs --chrome PATH [--timeout SECONDS] [--flag ARG]… PROGRAM.js [ARG…]
//
// --flag passes a switch to Chromium (--enable-unsafe-webgpu is always
// given: headless Chromium offers WebGPU only with it); the ARGs after the
// program are its arguments.
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { mkdtempSync, readFileSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, extname, join, resolve } from 'node:path';

const argv = process.argv.slice(2);
let chrome = '';
let timeout = 300;
const flags = ['--enable-unsafe-webgpu', '--enable-unsafe-swiftshader'];
while (argv.length && argv[0].startsWith('--')) {
  const a = argv.shift();
  if (a === '--chrome') chrome = argv.shift();
  else if (a === '--timeout') timeout = Number(argv.shift());
  else if (a === '--flag') flags.push(argv.shift());
  else { console.error(`browser.mjs: unknown option ${a}`); process.exit(2); }
}
const program = argv.shift();
if (!chrome || !program || !existsSync(program)) {
  console.error('usage: node tools/ci/browser.mjs --chrome PATH [--timeout S] [--flag ARG]… PROGRAM.js [ARG…]');
  process.exit(2);
}
const dir = dirname(resolve(program));
const script = basename(program);

const page = `<!doctype html>
<meta charset="utf-8">
<title>${script}</title>
<canvas id="canvas" width="64" height="64"></canvas>
<script>
  // Every line in order: one request after another.
  var queue = Promise.resolve();
  var send = function (path, body) { queue = queue.then(function () { return fetch(path, { method: 'POST', body: String(body) }); }).catch(function () {}); };
  var Module = {
    arguments: ${JSON.stringify(argv)},
    print: function (t) { send('/out', t); },
    printErr: function (t) { send('/err', t); },
    onExit: function (code) { send('/exit', code); },
    onAbort: function (what) { send('/err', 'aborted: ' + what); send('/exit', 1); },
  };
  window.addEventListener('error', function (e) { send('/err', 'page error: ' + e.message); send('/exit', 1); });
  window.addEventListener('unhandledrejection', function (e) { send('/err', 'unhandled rejection: ' + (e.reason && e.reason.stack || e.reason)); });
</script>
<script src="${script}"></script>
`;

const types = { '.js': 'text/javascript', '.wasm': 'application/wasm', '.html': 'text/html', '.data': 'application/octet-stream' };
const summaries = [];  // each suite's failures
let finish;
const finished = new Promise((r) => { finish = r; });
const server = createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (req.method === 'POST') {
    let body = '';
    req.setEncoding('utf8');
    req.on('data', (c) => { body += c; });
    req.on('end', () => {
      if (url.pathname === '/out') {
        process.stdout.write(body + '\n');
        const m = /^\S+: (\d+) passed, (\d+) failed$/.exec(body);
        if (m) summaries.push(Number(m[2]));
      }
      else if (url.pathname === '/err') process.stderr.write(body + '\n');
      else if (url.pathname === '/exit') finish(Number.parseInt(body, 10) || 0);
      res.end();
    });
    return;
  }
  if (url.pathname === '/' || url.pathname === '/index.html') {
    res.writeHead(200, { 'content-type': 'text/html' });
    res.end(page);
    return;
  }
  const file = join(dir, basename(url.pathname));
  if (!existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': types[extname(file)] || 'application/octet-stream' });
  res.end(readFileSync(file));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const port = server.address().port;

const profile = mkdtempSync(join(tmpdir(), 'omui-chrome-'));
const browser = spawn(chrome, ['--headless', '--no-sandbox', '--no-first-run', '--disable-gpu-sandbox', `--user-data-dir=${profile}`, ...flags,
                               `http://localhost:${port}/`], { stdio: ['ignore', 'ignore', 'pipe'] });
let log = '';
browser.stderr.on('data', (c) => { log += c; });
const timer = setTimeout(() => {
  process.stderr.write(`browser.mjs: ${script} did not finish in ${timeout} s\n`);
  finish(1);
}, timeout * 1000);
browser.on('exit', (code) => finish(code === 0 ? 1 : (code ?? 1)));
let code = await finished;
if (code === 0 && (summaries.length === 0 || summaries.some((n) => n > 0))) code = 1;
clearTimeout(timer);
browser.kill('SIGKILL');
server.close();
rmSync(profile, { recursive: true, force: true });
if (code !== 0 && process.env.OMUI_BROWSER_LOG) process.stderr.write(log);
process.exit(code);
