module openmui.gen.dist_toy;

import std;
import openmui.hash;
import nlohmann.json;
import openmui.gen.font_subset;

namespace omui::gen {

namespace {

namespace fs = std::filesystem;

std::string utf8(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::expected<std::string, std::string> read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot read", utf8(file)));
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::expected<void, std::string> write(const fs::path& file, std::string_view bytes) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return std::unexpected(std::format("{}: cannot write", utf8(file)));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) return std::unexpected(std::format("{}: cannot write", utf8(file)));
    return {};
}

// A content hash for file names: FNV-1a, 64 bits, as 12 hex digits.
std::string content_hash(std::string_view bytes) {
    return std::format("{:012x}", omui::hash::fnv1a(bytes) & 0xffffffffffffull);
}

// openmui.text's directory of default fonts, from mcpp's resolved graph.
std::optional<fs::path> text_fonts(const fs::path& graph) {
    if (graph.empty()) return std::nullopt;
    auto text = read(graph);
    if (!text) return std::nullopt;
    try {
        const auto g = nlohmann::json::parse(*text);
        for (const auto& p : g.at("packages")) {
            const auto& id = p.at("package");
            if (id.value("namespace", "") == "openmui" && id.value("name", "") == "text") return fs::path(p.value("manifest_dir", "")) / "fonts";
        }
    } catch (const nlohmann::json::exception&) {
    }
    return std::nullopt;
}

std::string escape_html(std::string_view s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string js_string(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '<') {
            out += "\\u003c";  // never `</script>` inside the page's script
            continue;
        }
        out += c;
    }
    return out + "\"";
}

// The page: a canvas filling the frame, a word while it loads, and the
// loader — the program's arguments from the hash route, its /fonts filled
// before main, its .wasm by its hashed name.
std::string page(const ToyPackage& p, std::string_view js, std::string_view wasm, std::span<const std::pair<std::string, std::string>> fonts,
                 std::span<const std::pair<std::string, std::string>> later, std::span<const std::pair<std::string, std::string>> initial,
                 const std::map<std::string, std::uintmax_t>& decoded_sizes) {
    std::string font_list, later_list;
    for (const auto& [name, file] : fonts) {
        // Its languages, if it is for some: ["zh"].
        std::string langs;
        const std::string stem = name.substr(0, name.rfind('.'));
        if (const auto fl = std::ranges::find(p.font_languages, stem, &std::pair<std::string, std::vector<std::string>>::first); fl != p.font_languages.end())
            for (const std::string& l : fl->second) langs += std::format("{}{}", langs.empty() ? "" : ", ", js_string(l));
        const auto first = std::ranges::find(initial, stem, &std::pair<std::string, std::string>::first);
        font_list += std::format("{}[{}, {}, [{}]{}]", font_list.empty() ? "" : ", ", js_string(name), js_string(file), langs,
                                 first == initial.end() ? "" : ", " + js_string(first->second));
    }
    for (const auto& [path, file] : later) later_list += std::format("{}[{}, {}]", later_list.empty() ? "" : ",\n  ", js_string(path), js_string(file));
    std::string html;
    html += "<!doctype html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover\">\n";
    html += std::format("<title>{}</title>\n", escape_html(p.title.empty() ? p.target : p.title));
    html += std::format(
        "<style>html,body{{margin:0;height:100%;overflow:hidden;background:{0};touch-action:none;-webkit-user-select:none;user-select:none}}"
        "#canvas{{display:block;width:100vw;height:100vh;outline:none}}"
        "#status{{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;color:#888;font:14px system-ui,sans-serif;pointer-events:none}}</style>\n",
        escape_html(p.background));
    // Toy's SDK (window.toy): Bilibili's own script, loaded by the page itself (Toy's guide).
    if (p.sdk) html += "<script src=\"//s1.hdslb.com/bfs/seed/toy/app/sdk/toy-sdk.js\"></script>\n";
    html += "</head>\n<body>\n<canvas id=\"canvas\" tabindex=\"0\"></canvas>\n<div id=\"status\">…</div>\n<script>\n";
    html += std::format("var omuiFonts = [{}];\n", font_list);
    html += std::format("var omuiWasm = {};\n", js_string(wasm));
    html += std::format("var omuiLauncher = {};\n", js_string(js));
    html += std::format("var omuiLater = [{}];\n", later_list);
    html += std::format("var omuiAssetBytes = {};\n", nlohmann::json(decoded_sizes).dump());
    html += "var omuiFontPending = 0;\n";
    html += R"(var omuiStatus = document.getElementById('status');
// Hash routes are the program's flags; the loader never mutates the route.
function omuiArguments() {
  return (document.URL.split('#')[1] || '').replace(/^\//, '').split('/').filter(function (s) { return /^[A-Za-z0-9_-]+$/.test(s); })
    .map(function (s) { return '--' + s; });
}
function omuiWants(langs) {
  if (typeof window.omuiFontLanguage === 'string') {
    var chosen = window.omuiFontLanguage.toLowerCase();
    return langs.some(function (l) { return chosen === l || chosen.indexOf(l + '-') === 0; });
  }
  var route = (document.URL.split('#')[1] || '').replace(/^\//, '').split('/')[0];
  var first = ((navigator.languages && navigator.languages[0]) || navigator.language || '').toLowerCase();
  return langs.some(function (l) { return route === l || first === l || first.indexOf(l + '-') === 0; });
}
// All counts are decoded body bytes for managed assets, not wire bytes or
// the launcher's script element. A retry starts that asset's count again.
var omuiAssets = [], omuiJobs = Object.create(null), omuiStartupFonts = [], omuiLaterJobs = [], omuiFontLater = [], omuiFontPlan = null;
var omuiFontRecords = Object.create(null), omuiFontTargets = Object.create(null), omuiFontCanonical = Object.create(null);
var omuiPhase = 'boot', omuiStatusText = '', omuiFatal = null;
var omuiInitialized = false, omuiPresented = false, omuiLaterStarted = false, omuiPreRan = false, omuiPartial = false, omuiWasmReceived = false;
function omuiAsset(id, url, kind, group, state) {
  var raw = omuiAssetBytes[url];
  var a = {id: id, url: url, kind: kind, group: group, loaded: 0, total: Number.isSafeInteger(raw) && raw >= 0 ? raw : null,
           state: state || 'queued', attempt: 0, error: null};
  omuiAssets.push(a); return a;
}
function omuiProgress(assets) {
  var loaded = 0, total = 0, known = true;
  assets.forEach(function (a) { if (a.state === 'skipped' || a.retired) return; loaded += a.loaded; if (a.total === null || a.loaded > a.total) known = false; else total += a.total; });
  return {loaded: loaded, total: known ? total : null, ratio: known && total > 0 ? loaded / total : null};
}
function omuiPublish(phase) {
  if (phase) omuiPhase = phase;
  var problem = omuiFatal || (omuiAssets.find(function (a) { return a.error && !a.retired; }) || {}).error || null;
  var all = omuiProgress(omuiAssets), startup = omuiProgress(omuiAssets.filter(function (a) { return a.group === 'startup'; }));
  var data = {version: 1, basis: 'decoded', phase: problem ? 'error' : omuiPhase, status: omuiStatusText,
    assets: omuiAssets.map(function (a) { return Object.freeze(Object.assign({}, a, {state: a.retired ? 'retired' : a.state, transferState: a.retired ? a.state : undefined, error: a.error && Object.freeze(Object.assign({}, a.error))})); }),
    loaded: all.loaded, total: all.total, ratio: all.ratio, startup: startup,
    runtimeInitialized: omuiInitialized, presented: omuiPresented, error: problem && Object.freeze(Object.assign({}, problem))};
  Object.freeze(data.assets); Object.freeze(data.startup); Object.freeze(data);
  Object.assign(window.omuiLoad, data);
  window.dispatchEvent(new CustomEvent('omui-load', {detail: data}));
}
function omuiFailure(a, error) {
  a.state = 'error';
  a.error = {id: a.id, url: a.url, message: String(error && error.message || error), attempt: a.attempt,
             retryable: !omuiFatal && !(a.kind === 'wasm' && omuiWasmReceived)};
  if (!a.retired) omuiStatus.textContent = a.error.message;
  omuiPublish(a.retired ? null : 'error');
}
function omuiError(message, automatic) { var e = new Error(message); e.automatic = !!automatic; return e; }
function omuiAllReady() { return omuiAssets.every(function (a) { return a.retired || a.state === 'ready' || a.state === 'skipped'; }); }
function omuiFinished() {
  if (omuiLaterStarted && omuiLaterJobs.length && omuiLaterJobs.every(function (j) { return j.asset.state === 'ready'; })) {
    FS.mkdirTree('/later'); FS.writeFile('/later/.ready', '');
  }
  if (omuiPresented && omuiAllReady()) omuiPublish('complete');
}
// The same response feeds streaming compilation. Synthetic Response headers
// describe decoded data: never copy a gzip Content-Length into that stream.
async function omuiFetchBody(a, controller) {
  var timer, generation = a.attempt;
  function arm(ms) { clearTimeout(timer); timer = setTimeout(function () { controller.abort(omuiError('No decoded data for ' + ms + 'ms loading ' + a.url, true)); }, ms); }
  function current() { if (a.attempt !== generation || controller.signal.aborted) throw controller.signal.reason || omuiError('Superseded asset request', false); }
  function reason(error) { return controller.signal.aborted && controller.signal.reason || error; }
  try {
    arm(15000);
    var response = await fetch(a.url, {credentials: 'same-origin', signal: controller.signal});
    if (!response.ok) throw omuiError('HTTP ' + response.status + ' loading ' + a.url, response.status >= 500 || response.status === 408 || response.status === 429);
    var reader = response.body && typeof response.body.getReader === 'function' && typeof ReadableStream === 'function' ? response.body.getReader() : null;
    if (!reader) {
      arm(60000); var buffer = await response.arrayBuffer(); clearTimeout(timer); current();
      a.loaded = buffer.byteLength;
      if (a.total !== null && a.total !== a.loaded) throw omuiError('Decoded size mismatch loading ' + a.url, true);
      if (a.total === null) a.total = a.loaded;
      a.state = 'downloaded'; omuiPublish(a.kind === 'wasm' ? 'compiling' : 'fetching');
      return {buffer: buffer};
    }
    var stream = new ReadableStream({
      async pull(output) {
        try {
          arm(15000); var part = await reader.read(); clearTimeout(timer); current();
          if (part.done) {
            if (a.total !== null && a.total !== a.loaded) throw omuiError('Decoded size mismatch loading ' + a.url, true);
            if (a.total === null) a.total = a.loaded;
            a.state = 'downloaded'; omuiPublish(a.kind === 'wasm' ? 'compiling' : 'fetching'); output.close(); return;
          }
          a.loaded += part.value.byteLength;
          if (a.total !== null && a.loaded > a.total) throw omuiError('Decoded size exceeds package metadata for ' + a.url, true);
          omuiPublish(); output.enqueue(part.value); arm(15000);
        } catch (error) { clearTimeout(timer); try { await reader.cancel(error); } catch (_) {} output.error(reason(error)); }
      },
      cancel(reason) { clearTimeout(timer); return reader.cancel(reason); }
    });
    var headers = new Headers(response.headers); headers.delete('Content-Length'); headers.delete('Content-Encoding');
    return {response: new Response(stream, {status: response.status, statusText: response.statusText, headers: headers})};
  } catch (error) { clearTimeout(timer); throw reason(error); }
}
async function omuiFetchBytes(a, controller) {
  var body = await omuiFetchBody(a, controller);
  return new Uint8Array(body.buffer || await body.response.arrayBuffer());
}
function omuiStart(job, manual) {
  if (job.font) return omuiStartFont(job, manual);
  if (job.running) return job.running;
  if (job.asset.state === 'ready' || job.asset.state === 'skipped' || job.readyState && job.asset.state === job.readyState) return Promise.resolve({id: job.asset.id, state: job.asset.state});
  job.running = (async function () {
    var limit = manual ? 1 : 3;
    for (var attempt = 0; attempt < limit; ++attempt) {
      // Locale planning can retire this asset while retry backoff is pending.
      // Recheck before incrementing attempts or beginning another request.
      if (job.asset.retired) return {id: job.asset.id, state: 'error'};
      var a = job.asset, controller = new AbortController(); a.attempt++; a.loaded = 0; a.error = null; a.state = 'fetching';
      omuiPublish('fetching');
      try {
        var result = await job.run(controller);
        a.state = job.readyState || 'ready'; a.error = null; omuiPublish();
        if (job.commit) job.commit(result);
        omuiFinished(); return {id: a.id, state: 'ready'};
      } catch (error) {
        controller.abort(); omuiFailure(a, error);
        var automatic = error && (error.automatic === true || error.automatic === undefined && !(error instanceof WebAssembly.CompileError) && !(error instanceof WebAssembly.LinkError));
        if (!automatic || a.retired || attempt + 1 === limit || omuiFatal || a.kind === 'wasm' && omuiWasmReceived) return {id: a.id, state: 'error'};
        await new Promise(function (resolve) { setTimeout(resolve, attempt === 0 ? 250 : 1000); });
      }
    }
  })().finally(function () { job.running = null; });
  return job.running;
}
window.omuiLoad = {fontBytes: omuiFontBytes, retry: function (id) {
  if (omuiFatal) return Promise.resolve({retried: [], blocked: [id || '*'], reason: 'reload_required'});
  var jobs = Object.values(omuiJobs).filter(function (j) { return (!id || (j.id || j.asset.id) === id) && j.asset.state === 'error'; });
  var runnable = jobs.filter(function (j) { return j.asset.error && j.asset.error.retryable && !j.asset.retired; });
  return Promise.all(runnable.map(function (j) { return omuiStart(j, true); })).then(function (results) {
    return {retried: results.map(function (r) { return r.id; }), blocked: jobs.filter(function (j) { return !runnable.includes(j); }).map(function (j) { return j.id || j.asset.id; }), results: results};
  });
}};
function omuiJob(asset, run, commit) { var job = {asset: asset, run: run, commit: commit, running: null}; omuiJobs[asset.id] = job; return job; }
function omuiEnsureFonts() { FS.mkdirTree('/fonts'); if (omuiFontPending && !omuiPartial) { FS.writeFile('/fonts/.partial', ''); omuiPartial = true; } }
function omuiLaterJob(f, asset) {
  var installed = false;
  var job = omuiJob(asset, async function (controller) {
    var bytes = await omuiFetchBytes(asset, controller);
    FS.mkdirTree(f[0].substring(0, f[0].lastIndexOf('/')));
    if (f[0].indexOf('/later/fonts/') === 0) {
      var canonical = '/fonts/' + f[0].substring('/later/fonts/'.length);
      FS.writeFile(canonical, bytes);
      try { FS.unlink(f[0]); } catch (_) {}
      FS.symlink(canonical, f[0]);
      if (!installed) {
        if (omuiFontPending === 1 && omuiPartial) { FS.unlink('/fonts/.partial'); omuiPartial = false; }
        --omuiFontPending;
      }
    } else FS.writeFile(f[0], bytes);
    installed = true;
  });
  omuiLaterJobs.push(job); return job;
}
// Ordinary later assets are language independent. Implicit font planning waits
// until instantiateWasm (or an explicit fontBytes call): bootstrap runs after this loader and
// sets the page's actual language before loading the external launcher.
omuiLater.forEach(function (f) { omuiLaterJob(f, omuiAsset('later:' + f[0], f[1], 'asset', 'deferred', 'deferred')); });
// A transfer belongs to its content URL, independently of a font's current
// startup/deferred installation role. DOM consumers explicitly opt into early
// I/O; ordinary packages still wait for preRun. Settled transfer promises hold
// no byte array, and installed bytes are dropped after the last DOM lease.
function omuiFontRecord(id, url, name) {
  var record = omuiFontRecords[url];
  if (!record) {
    var asset = omuiAsset(id, url, 'font', 'startup', 'queued');
    record = {asset: asset, buffer: null, leases: 0, done: false, targets: []};
    record.transfer = {asset: asset, running: null, readyState: 'downloaded', run: async function (controller) {
      var bytes = await omuiFetchBytes(asset, controller); record.buffer = bytes.buffer; record.done = true;
    }};
    omuiFontRecords[url] = record;
  } else if (!omuiAssets.includes(record.asset)) omuiAssets.push(record.asset);
  var target = omuiFontTargets[id];
  if (!target) {
    // Names/dependencies belong to installations, never to the shared URL.
    target = {id: id, name: name, record: record, asset: record.asset, role: null, installed: false, held: false};
    var job = {id: id, asset: record.asset, font: target}; target.job = job; omuiJobs[id] = job;
    job.depend = function () { if (!target.held) { Module.addRunDependency('font ' + name); target.held = true; } };
    record.targets.push(target); omuiFontTargets[id] = target;
    record.asset.names = Object.freeze(Array.from(new Set(record.targets.map(function (t) { return t.name; }))));
  }
  return target;
}
function omuiReleaseFont(record) {
  // Even when every DOM lease was released early, keep the transfer until
  // every currently planned basename has installed its own canonical file.
  if (!record.leases && omuiPreRan && record.targets.every(function (t) { return !t.role || t.installed; })) record.buffer = null;
}
function omuiInstallFont(target) {
  if (!target.role || target.installed) return;
  var record = target.record, bytes = new Uint8Array(record.buffer), name = target.name;
  omuiEnsureFonts(); FS.writeFile('/fonts/' + name, bytes); omuiFontCanonical[name] = record.asset.url;
  if (target.role === 'later') {
    FS.mkdirTree('/later/fonts'); var alias = '/later/fonts/' + name;
    try { FS.unlink(alias); } catch (_) {} FS.symlink('/fonts/' + name, alias);
    if (omuiFontPending === 1 && omuiPartial) { FS.unlink('/fonts/.partial'); omuiPartial = false; }
    --omuiFontPending;
  }
  target.installed = true; record.asset.error = null;
  record.asset.state = record.targets.every(function (t) { return !t.role || t.installed; }) ? 'ready' : 'downloaded';
  // Clear held first: removeRunDependency may synchronously initialize main.
  if (target.held) { target.held = false; Module.removeRunDependency('font ' + name); }
  omuiReleaseFont(record);
}
async function omuiStartFont(job, manual) {
  var target = job.font, record = target.record, asset = record.asset;
  if (asset.error && !manual && !record.transfer.running) return {id: target.id, state: 'error'};
  if (!record.done) {
    var result = await omuiStart(record.transfer, manual);
    if (result.state === 'error') return {id: target.id, state: 'error'};
  }
  try {
    // A transfer/error ID identifies the physical asset. Retrying that ID
    // must also resume eligible aliases whose earlier jobs failed with it.
    // Installation is per-target and idempotent, including an FS-only retry.
    record.targets.forEach(function (candidate) {
      if (candidate.role === 'startup' && omuiPreRan || candidate.role === 'later' && omuiLaterStarted) omuiInstallFont(candidate);
    });
    omuiReleaseFont(record); omuiPublish(); omuiFinished(); return {id: target.id, state: asset.state};
  } catch (error) { omuiFailure(asset, error); return {id: target.id, state: 'error'}; }
}
function omuiFontError(code, message, asset) {
  var error = new Error(message); error.code = code;
  if (asset) { error.id = asset.id; error.url = asset.url; error.retryable = !!(asset.error && asset.error.retryable); }
  return error;
}
async function omuiFontBytes(name) {
  if (omuiFatal) throw omuiFontError('reload_required', 'Reload is required after a runtime or launcher failure');
  var font = omuiFonts.find(function (f) { return f[0] === name; });
  if (!font) throw omuiFontError('font_not_found', 'Unknown packaged font: ' + name);
  omuiPlanFonts();
  var full = omuiFontTargets['font:' + name], deferred = full.role === 'later';
  var target = full.installed ? full : omuiFontTargets[(deferred && font[3] ? 'initial-font:' : 'font:') + name];
  var record = target.record;
  if (deferred && !font[3] && !omuiPresented && !(full && full.installed)) throw omuiFontError('font_deferred', 'This font is deferred until presentation', full && full.asset);
  if (record.asset.error) throw omuiFontError('font_fetch_failed', record.asset.error.message, record.asset);
  ++record.leases; // Reserve before awaiting: preRun cannot release our bytes.
  try {
    var result = await omuiStartFont(target.job, false);
    if (result.state === 'error') throw omuiFontError('font_fetch_failed', record.asset.error.message, record.asset);
    var buffer = record.buffer;
    if (!buffer) {
      if (omuiFontCanonical[name] !== record.asset.url) {
        // Full installation can race this await after the initial cache was
        // released. Select the real current face instead of mislabelling FS.
        if (full !== target && full.installed) { --record.leases; omuiReleaseFont(record); return omuiFontBytes(name); }
        throw omuiFontError('font_not_available', 'Installed font version has changed', record.asset);
      }
      // MEMFS owns the canonical copy after all earlier leases were released.
      var bytes = FS.readFile('/fonts/' + name); buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
      record.buffer = buffer;
    }
    // Each public lease owns a copy: DOM consumers cannot mutate the bytes
    // which an in-flight native startup still needs to install.
    var released = false;
    var lease = {id: target.id, url: record.asset.url, buffer: buffer.slice(0), release: function () {
      if (released) return; released = true; lease.buffer = null; --record.leases; omuiReleaseFont(record);
    }};
    return lease;
  } catch (error) { --record.leases; omuiReleaseFont(record); throw error; }
}
function omuiPlanFonts() {
  if (omuiPreRan) return;
  var plan = omuiFonts.map(omuiDeferredFont), key = JSON.stringify(plan); if (key === omuiFontPlan) return;
  // Preserve started transfers across static language navigation. Only untouched
  // plan entries can disappear; their bytes have never contributed to progress.
  omuiAssets = omuiAssets.filter(function (a) { return a.kind !== 'font' || a.attempt > 0; });
  Object.values(omuiFontTargets).forEach(function (t) { t.role = null; });
  omuiLaterJobs = omuiLaterJobs.filter(function (j) { return j.asset.kind !== 'font'; });
  omuiLater = omuiLater.filter(function (f) { return !omuiFontLater.includes(f); });
  omuiFontLater = []; omuiStartupFonts = []; omuiFontPending = 0;
  omuiFonts.forEach(function (f, index) {
    var deferred = plan[index], full = omuiFontRecord('font:' + f[0], f[1], f[0]);
    full.role = deferred ? 'later' : 'startup';
    if (deferred) {
      var queued = ['/later/fonts/' + f[0], f[1]]; omuiLater.push(queued); omuiFontLater.push(queued);
      ++omuiFontPending; omuiLaterJobs.push(full.job);
    }
    var initial = f[3] && omuiFontRecord('initial-font:' + f[0], f[3], f[0]);
    var startup = deferred ? initial : full;
    if (startup) { startup.role = 'startup'; omuiStartupFonts.push(startup.job); }
  });
  Object.values(omuiFontRecords).forEach(function (r) {
    var active = r.targets.filter(function (t) { return t.role; });
    r.asset.retired = !active.length && r.asset.attempt > 0;
    r.asset.group = active.some(function (t) { return t.role === 'startup'; }) ? 'startup' : 'deferred';
    if (!r.asset.attempt) r.asset.state = !active.length ? 'skipped' : r.asset.group === 'startup' ? 'queued' : 'deferred';
    omuiReleaseFont(r);
  });
  omuiFontPlan = key; omuiPublish();
}
function omuiDeferredFont(f) { return f[2].length ? !omuiWants(f[2]) : !!f[3]; }
var omuiWasmAsset = omuiAsset('wasm', omuiWasm, 'wasm', 'startup');
var Module = {
  canvas: document.getElementById('canvas'), arguments: omuiArguments(),
  locateFile: function (path) { return /\.wasm$/.test(path) ? omuiWasm : path; },
  print: function (text) { console.log(text); }, printErr: function (text) { console.warn(text); },
  instantiateWasm: function (imports, receive) {
    omuiPlanFonts();
    var job = omuiJob(omuiWasmAsset, async function (controller) {
      var body = await omuiFetchBody(omuiWasmAsset, controller);
      if (body.response && typeof WebAssembly.instantiateStreaming === 'function' && /^application\/wasm(?:;|$)/i.test(body.response.headers.get('Content-Type') || ''))
        return WebAssembly.instantiateStreaming(body.response, imports);
      var buffer = body.buffer || await body.response.arrayBuffer();
      return WebAssembly.instantiate(buffer, imports);
    }, function (result) {
      if (omuiWasmReceived) return; omuiWasmReceived = true; receive(result.instance, result.module);
    });
    omuiStart(job, false); return {};
  },
  preRun: [function () {
    if (omuiPreRan) return; omuiPlanFonts(); omuiPreRan = true;
    omuiStartupFonts.forEach(function (j) { j.depend(); });
    Object.values(omuiFontRecords).forEach(omuiReleaseFont);
    try { omuiEnsureFonts(); omuiStartupFonts.forEach(function (j) { omuiStart(j, false); }); }
    catch (error) { omuiStartupFonts.forEach(function (j) { omuiFailure(j.asset, error); }); }
  }],
  onRuntimeInitialized: function () {
    omuiInitialized = true; omuiPublish('initialized'); omuiStatus.style.display = 'none'; Module.canvas.focus();
    window.omuiFirstFrame = omuiFetchLater;
    // A fallback may begin deferred I/O, but must preserve the native first
    // presentation hook. It is never evidence that a frame was presented.
    setTimeout(function () { omuiFetchLater(); }, 4000);
  },
  setStatus: function (text) { omuiStatusText = String(text || ''); if (text) omuiStatus.textContent = text; omuiPublish(/^Running/.test(omuiStatusText) ? 'initializing' : null); },
  onAbort: function (reason) {
    omuiFatal = {id: 'runtime', message: String(reason), retryable: false}; omuiStatus.style.display = ''; omuiStatus.textContent = String(reason); omuiPublish('error');
  }
};
function omuiFetchLater() {
  if (window.omuiHasPresented === true && !omuiPresented) { omuiPresented = true; omuiPublish('presented'); }
  if (omuiLaterStarted) { omuiFinished(); return; }
  omuiLaterStarted = true;
  omuiLaterJobs.forEach(function (j) { omuiStart(j, false); }); omuiFinished();
}
if (typeof WebAssembly !== 'object') { omuiFatal = {id: 'wasm', message: 'WebAssembly is needed', retryable: false}; omuiStatus.textContent = omuiFatal.message; }
omuiPublish('boot');
window.addEventListener('hashchange', function () { if (!window.omuiFollowsRoute) location.reload(); });
window.addEventListener('error', function (event) {
  var launcher = new URL(omuiLauncher, document.URL).href;
  if (event.target && event.target.src === launcher || event.filename === launcher) {
    omuiFatal = {id: 'launcher', url: launcher, message: event.message || 'Program launcher failed to load', retryable: false};
    omuiStatus.textContent = omuiFatal.message; omuiPublish('error');
  }
}, true);
document.getElementById('canvas').addEventListener('pointerdown', function () { this.focus(); });
)";
    html += "</script>\n";
    html += std::format("<script src=\"{}\"></script>\n</body>\n</html>\n", js);
    return html;
}

}  // namespace

bool toy_keeps(std::string_view extension) noexcept {
    // The publishing guide's FAQ, verbatim: the file types a Toy package keeps.
    static constexpr std::array<std::string_view, 32> kept{
        ".html", ".htm", ".css", ".js", ".json", ".wasm", ".data", ".md", ".csv", ".tsv", ".png", ".jpg",
        ".jpeg", ".gif", ".svg", ".webp", ".ico", ".woff2", ".woff", ".ttf", ".eot", ".mp3", ".wav", ".ogg",
        ".m4a", ".mp4", ".webm", ".atlas", ".ani", ".part", ".nani", ".unityweb"};
    return std::ranges::find(kept, extension) != kept.end();
}

std::expected<ToyReport, std::string> package_for_toy(const ToyPackage& p) {
    std::error_code ec;
    const fs::path bin = p.stage / "bin";
    const fs::path js_in = bin / (p.target + ".js"), wasm_in = bin / (p.target + ".wasm");
    auto js = read(js_in);
    if (!js) return std::unexpected(std::format("{} (dist-toy packages a wasm32-emscripten program: pack with --target wasm32-emscripten)", js.error()));
    auto wasm = read(wasm_in);
    if (!wasm) return std::unexpected(wasm.error());
    if (fs::exists(bin / (p.target + ".data"), ec))
        return std::unexpected("the program carries a .data file (--preload-file): dist-toy gives the page's files as hashed assets instead");

    fs::remove_all(p.out, ec);
    fs::create_directories(p.out / "assets", ec);
    if (ec) return std::unexpected(std::format("{}: {}", utf8(p.out), ec.message()));

    const std::string js_name = std::format("{}-{}.js", p.target, content_hash(*js));
    const std::string wasm_name = std::format("{}-{}.wasm", p.target, content_hash(*wasm));
    if (auto w = write(p.out / js_name, *js); !w) return std::unexpected(w.error());
    if (auto w = write(p.out / wasm_name, *wasm); !w) return std::unexpected(w.error());

    // The fonts: openmui.text's defaults the program asks for, each a hashed asset.
    fs::path fonts_dir = p.fonts_dir;
    if (fonts_dir.empty()) fonts_dir = text_fonts(p.graph).value_or(fs::path());
    if (fonts_dir.empty() || !fs::is_directory(fonts_dir, ec))
        return std::unexpected("openmui.text's fonts were not found (give --fonts-dir, or pack through mcpp so the graph names them)");
    std::vector<std::string> stems = p.fonts;
    if (stems.empty() || (stems.size() == 1 && stems[0] == "all")) {
        stems.clear();
        for (const auto& e : fs::directory_iterator(fonts_dir, ec))
            if (e.path().extension() == ".ttf") stems.push_back(utf8(e.path().stem()));
        std::ranges::sort(stems);
    }
    for (const auto& [font, text] : p.font_initial) {
        if (text.empty() || std::ranges::find(stems, font) == stems.end())
            return std::unexpected(std::format("--font-initial {} needs nonempty text and a selected font", font));
        if (std::ranges::count(p.font_initial, font, &std::pair<std::string, std::string>::first) != 1)
            return std::unexpected(std::format("--font-initial {} is repeated", font));
    }
    std::vector<std::pair<std::string, std::string>> fonts, initial;
    for (const std::string& stem : stems) {
        auto bytes = read(fonts_dir / (stem + ".ttf"));
        if (!bytes) return std::unexpected(std::format("font {}: {}", stem, bytes.error()));
        const auto first = std::ranges::find(p.font_initial, stem, &std::pair<std::string, std::string>::first);
        if (first != p.font_initial.end()) {
            std::set<char32_t> keep;
            characters_of(first->second, keep);
            auto cut = subset_font(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()), keep);
            if (!cut) return std::unexpected(std::format("initial font {}: {}", stem, cut.error()));
            const std::string_view data(reinterpret_cast<const char*>(cut->data()), cut->size());
            const std::string asset = std::format("assets/{}.ttf", content_hash(data));
            if (auto w = write(p.out / asset, data); !w) return std::unexpected(w.error());
            initial.emplace_back(stem, asset);
            std::println("dist-toy: {} initial glyphs: {} bytes", stem, cut->size());
        }
        // Cut down to the characters the program's text holds, where asked.
        if (const auto sub = std::ranges::find(p.subsets, stem, &ToyPackage::Subset::font); sub != p.subsets.end()) {
            std::set<char32_t> keep;
            always_kept(keep);
            if (first != p.font_initial.end()) characters_of(first->second, keep);
            if (auto common = read(fonts_dir / (stem + ".common.txt"))) characters_of(*common, keep);
            static constexpr std::array<std::string_view, 7> text_kinds{".md", ".cpp", ".cppm", ".toml", ".json", ".txt", ".js"};
            auto take = [&](const fs::path& f) {
                if (std::ranges::find(text_kinds, utf8(f.extension())) == text_kinds.end()) return;
                if (auto t = read(f)) characters_of(*t, keep);
            };
            for (const fs::path& t : sub->text) {
                if (fs::is_directory(t, ec)) {
                    for (const auto& e : fs::recursive_directory_iterator(t, ec))
                        if (e.is_regular_file()) take(e.path());
                } else if (fs::is_regular_file(t, ec)) {
                    take(t);
                } else {
                    return std::unexpected(std::format("--subset {}: {} is neither a file nor a directory", stem, utf8(t)));
                }
            }
            FontSubsetReport r;
            auto cut = subset_font(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()), keep, &r);
            if (!cut) return std::unexpected(std::format("font {}: {}", stem, cut.error()));
            bytes->assign(reinterpret_cast<const char*>(cut->data()), cut->size());
            std::println("dist-toy: {} cut to {} of its {} glyphs: {} → {} bytes", stem, r.kept, r.glyphs, r.bytes_before, r.bytes_after);
        }
        const std::string asset = std::format("assets/{}.ttf", content_hash(*bytes));
        if (auto w = write(p.out / asset, *bytes); !w) return std::unexpected(w.error());
        fonts.emplace_back(stem + ".ttf", asset);
    }

    // The licences travel with what they cover: the program's (staged by
    // mcpp) and the fonts', as text Toy keeps (.md).
    auto keep_licence = [&](const fs::path& from, const fs::path& to) -> std::expected<void, std::string> {
        auto bytes = read(from);
        if (!bytes) return std::unexpected(bytes.error());
        fs::path dest = to;
        if (!toy_keeps(utf8(dest.extension()))) dest += ".md";
        return write(dest, *bytes);
    };
    if (fs::is_directory(p.stage / "licenses", ec))
        for (const auto& e : fs::recursive_directory_iterator(p.stage / "licenses", ec))
            if (e.is_regular_file()) {
                if (auto r = keep_licence(e.path(), p.out / "licenses" / fs::relative(e.path(), p.stage / "licenses", ec)); !r) return std::unexpected(r.error());
            }
    for (const auto& e : fs::directory_iterator(fonts_dir, ec)) {
        const std::string name = utf8(e.path().filename());
        if (name.starts_with("OFL") || name.starts_with("LICENSE"))
            if (auto r = keep_licence(e.path(), p.out / "licenses" / "fonts" / name); !r) return std::unexpected(r.error());
    }

    // The files fetched once the program is drawn, each a hashed asset.
    std::vector<std::pair<std::string, std::string>> later;
    for (const ToyPackage::Later& l : p.later) {
        if (!l.mount.starts_with("/later/")) return std::unexpected(std::format("later files go under /later/ ({})", l.mount));
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(l.dir, ec))
            if (e.is_regular_file() && utf8(e.path().filename()).ends_with(l.suffix)) files.push_back(e.path());
        if (ec) return std::unexpected(std::format("{}: {}", utf8(l.dir), ec.message()));
        std::ranges::sort(files);
        for (const fs::path& f : files) {
            auto bytes = read(f);
            if (!bytes) return std::unexpected(bytes.error());
            const std::string asset = std::format("assets/{}{}", content_hash(*bytes), utf8(f.extension()));
            if (auto w = write(p.out / asset, *bytes); !w) return std::unexpected(w.error());
            later.emplace_back(std::format("{}/{}", l.mount, utf8(f.filename())), asset);
        }
    }
    // Fetch exposes decoded bodies even when the server reports a compressed
    // Content-Length. These raw sizes give that body an honest denominator.
    std::map<std::string, std::uintmax_t> decoded_sizes;
    auto record_size = [&](const std::string& file) { if (!ec) decoded_sizes[file] = fs::file_size(p.out / file, ec); };
    record_size(wasm_name);
    for (const auto& [name, file] : fonts) record_size(file);
    for (const auto& [name, file] : initial) record_size(file);
    for (const auto& [name, file] : later) record_size(file);
    if (ec) return std::unexpected(std::format("managed asset size: {}", ec.message()));
    const std::string index = page(p, js_name, wasm_name, fonts, later, initial, decoded_sizes);
    if (auto w = write(p.out / "index.html", index); !w) return std::unexpected(w.error());

    ToyReport report;
    for (const auto& e : fs::recursive_directory_iterator(p.out, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string rel = utf8(fs::relative(e.path(), p.out, ec));
        if (!toy_keeps(utf8(e.path().extension())))
            return std::unexpected(std::format("{}: a file type Toy does not keep", rel));
        report.files.push_back({rel, e.file_size()});
        report.total += e.file_size();
    }
    std::ranges::sort(report.files, {}, &ToyFile::path);
    report.index = index.size();
    if (report.index > toy_index_limit)
        return std::unexpected(std::format("index.html is {} bytes: Toy takes at most {}", report.index, toy_index_limit));

    // What `toy create` asks, and what is in the package, beside it (Toy has no manifest file).
    nlohmann::ordered_json about;
    about["kind"] = "toy-package";
    about["target"] = p.target;
    about["title"] = p.title;
    about["entry"] = "index.html";
    about["index_bytes"] = report.index;
    about["index_limit"] = toy_index_limit;
    about["total_bytes"] = report.total;
    about["publish"] = std::format("toy create --title \"{}\" (the slug, the cover and the category are the maintainer's; see `toy create --help-json`)", p.title);
    auto& list = about["files"] = nlohmann::ordered_json::array();
    for (const ToyFile& f : report.files) list.push_back({{"path", f.path}, {"bytes", f.size}});
    fs::path side = p.out;
    side += ".toy.omui.json";
    if (auto w = write(side, about.dump(2) + "\n"); !w) return std::unexpected(w.error());
    return report;
}

int dist_toy_main(int argc, char** argv) {
    auto usage = [] {
        std::println(std::cerr, "usage: omui-gen dist-toy <staged tree> --target <name> --out <dir> [--title T] [--fonts a,b|all]\n"
                                "                         [--fonts-dir <dir>] [--graph <file>] [--background #rrggbb] [--no-sdk]\n"
                                "                         [--later <dir>|<suffix>|</later/mount>]... [--subset <font>=<path>,<path>...]...\n"
                                "                         [--font-languages <font>=<lang>,<lang>...]... [--font-initial <font>=<UTF-8 text>]...");
        return 2;
    };
    ToyPackage p;
    for (int i = 2; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc) return std::nullopt;
            return std::string(argv[++i]);
        };
        std::optional<std::string> v;
        if (a == "--target" && (v = value())) p.target = *v;
        else if (a == "--out" && (v = value())) p.out = *v;
        else if (a == "--title" && (v = value())) p.title = *v;
        else if (a == "--fonts-dir" && (v = value())) p.fonts_dir = *v;
        else if (a == "--graph" && (v = value())) p.graph = *v;
        else if (a == "--background" && (v = value())) p.background = *v;
        else if (a == "--fonts" && (v = value())) {
            for (std::size_t from = 0; from <= v->size();) {
                const std::size_t comma = std::min(v->find(',', from), v->size());
                if (comma > from) p.fonts.push_back(v->substr(from, comma - from));
                from = comma + 1;
            }
        } else if (a == "--no-sdk") p.sdk = false;
        else if (a == "--subset" && (v = value())) {  // <font>=<path>,<path>…
            const auto eq = v->find('=');
            if (eq == std::string::npos) {
                std::println(std::cerr, "omui-gen dist-toy: --subset is <font>=<path>,<path>…");
                return 2;
            }
            ToyPackage::Subset s{v->substr(0, eq), {}};
            for (std::size_t at = eq + 1; at <= v->size();) {
                const std::size_t end = std::min(v->find(',', at), v->size());
                if (end > at) s.text.emplace_back(v->substr(at, end - at));
                at = end + 1;
            }
            p.subsets.push_back(std::move(s));
        }
        else if (a == "--font-initial" && (v = value())) {
            const auto eq = v->find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 == v->size()) return usage();
            p.font_initial.emplace_back(v->substr(0, eq), v->substr(eq + 1));
        }
        else if (a == "--font-languages" && (v = value())) {  // <font>=<lang>,<lang>…
            const auto eq = v->find('=');
            if (eq == std::string::npos) return usage();
            std::vector<std::string> langs;
            for (std::size_t at = eq + 1; at <= v->size();) {
                const std::size_t end = std::min(v->find(',', at), v->size());
                if (end > at) langs.emplace_back(v->substr(at, end - at));
                at = end + 1;
            }
            p.font_languages.emplace_back(v->substr(0, eq), std::move(langs));
        }
        else if (a == "--later" && (v = value())) {  // <dir>|<suffix>|<mount>
            const auto bar1 = v->find('|'), bar2 = v->find('|', bar1 + 1);
            if (bar1 == std::string::npos || bar2 == std::string::npos) return usage();
            p.later.push_back({v->substr(0, bar1), v->substr(bar1 + 1, bar2 - bar1 - 1), v->substr(bar2 + 1)});
        }
        else if (!a.starts_with("--") && p.stage.empty()) p.stage = a;
        else return usage();
    }
    if (p.stage.empty() || p.target.empty() || p.out.empty()) return usage();
    auto r = package_for_toy(p);
    if (!r) {
        std::println(std::cerr, "dist-toy: {}", r.error());
        return 1;
    }
    std::println("dist-toy: {} — {} files, {} bytes (index.html {} of {})", utf8(p.out), r->files.size(), r->total, r->index, toy_index_limit);
    return 0;
}

}  // namespace omui::gen
