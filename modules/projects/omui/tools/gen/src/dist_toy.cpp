module omui.gen.dist_toy;

import std;
import mcpplibs.cmdline;
import mlibs.hash;
import nlohmann.json;
import omui.lang.ts;
import mlibs.text.subset;

namespace omui::gen {

namespace {

namespace fs = std::filesystem;
using mlibs::text::subset::always_kept, mlibs::text::subset::characters_of, mlibs::text::subset::compact_font_post, mlibs::text::subset::font_blocks,
    mlibs::text::subset::FontDeliveryReport, mlibs::text::subset::FontSubsetReport, mlibs::text::subset::subset_font;

std::string utf8(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::expected<std::string, std::string> read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot read", utf8(file)));
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// The characters of text files (.md, .cpp, .cppm, .toml, .json, .txt, .js; a directory's, recursively), into `into`.
std::expected<void, std::string> characters_of_files(const std::vector<fs::path>& paths, std::set<char32_t>& into) {
    static constexpr std::array<std::string_view, 7> text_kinds{".md", ".cpp", ".cppm", ".toml", ".json", ".txt", ".js"};
    std::error_code ec;
    const auto take = [&](const fs::path& file) {
        if (std::ranges::find(text_kinds, utf8(file.extension())) == text_kinds.end()) return;
        if (auto text = read(file)) characters_of(*text, into);
    };
    for (const fs::path& path : paths) {
        if (fs::is_directory(path, ec)) {
            for (const auto& entry : fs::recursive_directory_iterator(path, ec))
                if (entry.is_regular_file()) take(entry.path());
        } else if (fs::is_regular_file(path, ec)) {
            take(path);
        } else {
            return std::unexpected(std::format("{} is neither a file nor a directory", utf8(path)));
        }
    }
    return {};
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

// The page's line a split replaces (dist-toy-split): the second half's
// address, size and the routes whose first screen the first half covers.
constexpr std::string_view deferred_marker = "var omuiDeferred = null;";

// A content hash for file names: FNV-1a, 64 bits, as 12 hex digits.
std::string content_hash(std::string_view bytes) {
    return std::format("{:012x}", mlibs::hash::fnv1a(bytes) & 0xffffffffffffull);
}

// The text library's default fonts from the resolved graph. Older projects
// may still resolve the original package identity during the migration.
std::optional<fs::path> text_fonts(const fs::path& graph) {
    if (graph.empty()) return std::nullopt;
    auto text = read(graph);
    if (!text) return std::nullopt;
    try {
        const auto g = nlohmann::json::parse(*text);
        for (const auto& p : g.at("packages")) {
            const auto& id = p.at("package");
            const auto owner = id.value("namespace", "");
            if ((owner == "mlibs" || owner == "openmui") && id.value("name", "") == "text")
                return fs::path(p.value("manifest_dir", "")) / "fonts";
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

// The document's place in a page: over the canvas until the program has presented, then put aside.
std::string static_part(std::string_view fragment) {
    return std::format("<div data-omui=\"static\">\n{}</div>\n"
                       "<script>window.addEventListener('omui-load', function (e) {{\n"
                       "  // Presented: the program draws the page and its mirror reads it to assistive technology; the document steps aside.\n"
                       "  var page = e.detail && e.detail.presented && document.querySelector('[data-omui=static]');\n"
                       "  if (page) page.hidden = true;\n"
                       "}});</script>\n",
                       fragment);
}

// How the document is set: over the canvas's place until it steps aside; a document's own scrolling, selection and
// touch. `set`: its own box; `inside`: the rules of what it holds.
std::string static_style(std::string_view set, std::string_view colour, std::string_view inside) {
    return std::format("<style>[data-omui=static]{{position:fixed;inset:0;z-index:1;{}color:{};background:inherit;"
                       "touch-action:auto;-webkit-user-select:text;user-select:text}}[data-omui=static][hidden]{{display:none}}"
                       "{}"
                       // Printed before the program has it (or where it never does): the document whole, over as many
                       // sheets as it takes, not the one screen it lies over.
                       "@media print{{html,body{{height:auto!important;overflow:visible!important}}"
                       "[data-omui=static]{{position:static;overflow:visible;padding:0}}}}</style>\n",
                       set, colour, inside);
}

// A document with a look is set by it: every box as its node's, the window's box its root's, the scrolling its own
// views'.
constexpr std::string_view looked_set = "overflow:hidden;";
constexpr std::string_view looked_inside = "@media screen{[data-omui=static]>[data-omui-look]{width:100%!important;height:100%!important}}";

// The page: a canvas filling the frame, a word while it loads, and the
// loader — the program's arguments from the hash route, its /fonts filled
// before main, its .wasm by its hashed name.
std::string page(const ToyPackage& p, std::string_view js, std::string_view wasm, std::span<const std::pair<std::string, std::string>> fonts,
                 std::span<const std::pair<std::string, std::string>> later, std::span<const std::pair<std::string, std::string>> initial,
                 const std::map<std::string, std::uintmax_t>& decoded_sizes, const std::map<std::string, std::string>& coverage,
                 const std::map<std::string, std::string>& files) {
    std::string font_list, later_list;
    for (const auto& [name, file] : fonts) {
        // Its languages, if it is for some: ["zh"].
        std::string langs;
        const std::string stem = name.substr(0, name.rfind('.'));
        if (const auto fl = std::ranges::find(p.font_languages, stem, &std::pair<std::string, std::vector<std::string>>::first); fl != p.font_languages.end())
            for (const std::string& l : fl->second) langs += std::format("{}{}", langs.empty() ? "" : ", ", js_string(l));
        const auto first = std::ranges::find(initial, stem, &std::pair<std::string, std::string>::first);
        // [name, file, [languages], initial file (or null), blocks covered (a language's face, an on-demand one), on demand]
        const auto blocks = coverage.find(stem);
        const bool on_demand = std::ranges::find(p.fonts_on_demand, stem) != p.fonts_on_demand.end();
        const std::string initial_file = first == initial.end() ? std::string(blocks == coverage.end() ? "" : ", null") : ", " + js_string(first->second);
        font_list += std::format("{}[{}, {}, [{}]{}{}{}]", font_list.empty() ? "" : ", ", js_string(name), js_string(file), langs, initial_file,
                                 blocks == coverage.end() ? std::string(on_demand ? ", null" : "") : ", " + blocks->second, on_demand ? ", true" : "");
    }
    for (const auto& [path, file] : later) later_list += std::format("{}[{}, {}]", later_list.empty() ? "" : ",\n  ", js_string(path), js_string(file));
    std::string html;
    html += std::format("<!doctype html>\n<html lang=\"{}\">\n<head>\n<meta charset=\"utf-8\">\n", p.world ? "en" : "zh-CN");
    // A document's page is a reader's to zoom; an application's canvas is not.
    html += p.world ? "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">\n"
                    : "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover\">\n";
    html += std::format("<title>{}</title>\n", escape_html(p.title.empty() ? p.target : p.title));
    html += std::format(
        "<style>html,body{{margin:0;height:100%;overflow:hidden;background:{0};touch-action:none;-webkit-user-select:none;user-select:none}}"
        "#canvas{{display:block;width:100vw;height:100vh;outline:none}}"
        "#status{{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;color:#888;font:14px system-ui,sans-serif;pointer-events:none}}</style>\n",
        escape_html(p.background));
    // The page's faces by name (omui-<file's letters and digits>): what a document with a look sets its text in
    // (omui.semantic's font_key). A face is fetched only where an element uses it.
    if (!p.world) {
        std::string faces;
        for (const auto& [name, file] : fonts) {
            std::string key = "omui-";
            for (const char c : name.substr(0, name.rfind('.')))
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) key += c;
                else if (c >= 'A' && c <= 'Z') key += static_cast<char>(c - 'A' + 'a');
            faces += std::format("@font-face{{font-family:'{}';src:url('{}');font-weight:1 1000;font-display:swap}}", key, escape_html(file));
        }
        if (!faces.empty()) html += std::format("<style data-omui=\"faces\">{}</style>\n", faces);
    }
    // A page whose program follows the person's preference for a dark appearance is dark from its first paint.
    if (!p.background_dark.empty())
        html += std::format("<style>@media (prefers-color-scheme:dark){{html,body{{color-scheme:dark;background:{}}}}}</style>\n", escape_html(p.background_dark));
    // Toy's SDK (window.toy): Bilibili's own script, loaded by the page itself (Toy's guide).
    if (p.sdk) html += "<script src=\"//s1.hdslb.com/bfs/seed/toy/app/sdk/toy-sdk.js\"></script>\n";
    // A world's page waits for its document: where it goes, how it is set (by its look), and what its head says more.
    if (p.world) html += std::format("{}{}\n", static_style(looked_set, "inherit", looked_inside), world_head);
    html += "</head>\n<body>\n";
    if (p.world) html += static_part(std::format("{}\n", world_document));
    html += "<canvas id=\"canvas\" tabindex=\"0\"></canvas>\n<div id=\"status\">…</div>\n<script>\n";
    // Where a world's files are, as its page finds them, and when the page takes its program: a builder's to say.
    if (p.world) html += std::format("{}\n{}\n", world_root, world_takes);
    html += std::format("var omuiFonts = [{}];\n", font_list);
    html += std::format("var omuiWasm = {};\n", js_string(wasm));
    html += std::format("var omuiLauncher = {};\n", js_string(js));
    html += std::format("var omuiLater = [{}];\n", later_list);
    // The application's files, by their paths in the program's file system ("<" escaped: never `</script>`).
    std::string file_list = nlohmann::json(files).dump();
    for (std::size_t at = file_list.find('<'); at != std::string::npos; at = file_list.find('<', at)) file_list.replace(at, 1, "\\u003c");
    html += std::format("var omuiFiles = {};\n", file_list);
    html += std::format("var omuiAssetBytes = {};\n", nlohmann::json(decoded_sizes).dump());
    // The program's second half, when `omui-gen dist-toy-split` cut it in two
    // (this exact line is what it replaces): {url, bytes, lazy: [routes]}.
    html += std::string(deferred_marker) + "\n";
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
// Another language's faces wait until the program asks (omuiWantFonts): text
// with a glyph no face covers, a reader that wants every face. Each batch of
// later files ready is announced by /later/.ready, its content the count.
var omuiReadyCount = 0, omuiReadyGeneration = 0;
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
function omuiAllReady() { return omuiAssets.every(function (a) { return a.retired || a.state === 'ready' || a.state === 'skipped' || (a.demand && !a.wanted); }); }
function omuiStartedLater() { return omuiLaterJobs.filter(function (j) { return !j.demand || j.asset.wanted; }); }
function omuiFinished() {
  var started = omuiStartedLater();
  if (omuiLaterStarted && started.length && started.every(function (j) { return j.asset.state === 'ready'; }) && started.length !== omuiReadyCount) {
    omuiReadyCount = started.length;
    FS.mkdirTree('/later'); FS.writeFile('/later/.ready', String(++omuiReadyGeneration));
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
  // A face on demand: a lease taken once the program is drawn asks for the face itself (the static page's
  // lease before then takes the initial cut, as for any deferred face).
  var asked = deferred && font[5] === true && omuiPresented && !full.installed;
  if (asked) full.asset.wanted = true;
  var target = full.installed || asked ? full : omuiFontTargets[(deferred && font[3] ? 'initial-font:' : 'font:') + name];
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
    // Another language's face, or one on demand: asked for, not fetched after the first frame.
    full.job.demand = full.asset.demand = deferred && (f[2].length > 0 || f[5] === true);
    full.job.family = omuiFamilyKey(f[0]);
    full.job.blocks = f[4] || null;  // the 256-character blocks it covers
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
// A split program (omui-gen dist-toy-split): the first screen's functions
// in omuiWasm, the rest in omuiDeferred.url. The primary imports a
// placeholder for each function it left out; the first call of one
// instantiates the second half, which fills the primary's table, then the
// call goes on to the real function. The second half is compiled in the
// background — after the first frame on a route it was profiled for (lazy),
// else beside the primary, main waiting for it — so a placeholder normally
// finds it ready; fetched synchronously only if one is called before.
var omuiPrimary = null, omuiDeferredModule = null, omuiDeferredPromise = null, omuiDeferredDone = false;
window.omuiDeferredState = omuiDeferred ? 'waiting' : 'none';
function omuiDeferredLazy() {
  // The route's first part after its language, if it names one (#/zh/components: components).
  var parts = (document.URL.split('#')[1] || '').replace(/^\//, '').split('/');
  if (/^[a-z]{2}$/.test(parts[0])) parts.shift();
  return !!omuiDeferred && omuiDeferred.lazy.indexOf(parts[0] || '') >= 0;
}
function omuiFetchDeferred() {
  if (!omuiDeferred || omuiDeferredModule) return Promise.resolve(omuiDeferredModule);
  if (omuiDeferredPromise) return omuiDeferredPromise;
  window.omuiDeferredState = 'fetching';
  omuiDeferredPromise = fetch(omuiDeferred.url, {credentials: 'same-origin'}).then(function (response) {
    if (!response.ok) throw new Error('HTTP ' + response.status + ' loading ' + omuiDeferred.url);
    if (typeof WebAssembly.compileStreaming === 'function' && /^application\/wasm(?:;|$)/i.test(response.headers.get('Content-Type') || ''))
      return WebAssembly.compileStreaming(response);
    return response.arrayBuffer().then(function (buffer) { return WebAssembly.compile(buffer); });
  }).then(function (module) {
    if (!omuiDeferredModule) omuiDeferredModule = module;
    if (!omuiDeferredDone) window.omuiDeferredState = 'compiled';
    return omuiDeferredModule;
  }, function (error) { omuiDeferredPromise = null; window.omuiDeferredState = 'error'; console.warn(String(error)); throw error; });
  return omuiDeferredPromise;
}
function omuiInstantiateDeferred() {
  if (omuiDeferredDone) return;
  if (!omuiDeferredModule) {
    var request = new XMLHttpRequest();
    request.open('GET', omuiDeferred.url, false);
    request.overrideMimeType('text/plain; charset=x-user-defined');
    request.send(null);
    if (request.status !== 200 && request.status !== 0) throw new Error('HTTP ' + request.status + ' loading ' + omuiDeferred.url);
    var text = request.responseText, bytes = new Uint8Array(text.length);
    for (var i = 0; i < text.length; ++i) bytes[i] = text.charCodeAt(i) & 255;
    omuiDeferredModule = new WebAssembly.Module(bytes);
    window.omuiDeferredSync = true;
  }
  new WebAssembly.Instance(omuiDeferredModule, {primary: omuiPrimary.exports});
  omuiDeferredDone = true; window.omuiDeferredState = 'instantiated';
}
function omuiSplitTable() {
  // With reference types the placeholders live in a table of their own,
  // exported under the split's prefix (%); else in the one table.
  var exports = omuiPrimary.exports, tables = Object.keys(exports).filter(function (n) { return exports[n] instanceof WebAssembly.Table; });
  var own = tables.filter(function (n) { return n.charAt(0) === '%'; });
  return exports[(own.length ? own : tables)[0]];
}
function omuiSplitImports(imports) {
  if (!omuiDeferred) return imports;
  var placeholders = new Proxy({}, {get: function (_, base) {
    return function () { omuiInstantiateDeferred(); return omuiSplitTable().get(Number(base)).apply(null, arguments); };
  }});
  return new Proxy(imports, {get: function (target, name) { return String(name).indexOf('placeholder') === 0 ? placeholders : target[name]; }});
}
var Module = {
  canvas: document.getElementById('canvas'), arguments: omuiArguments(),
  locateFile: function (path) { return /\.wasm$/.test(path) ? omuiWasm : path; },
  print: function (text) { console.log(text); }, printErr: function (text) { console.warn(text); },
  instantiateWasm: function (imports, receive) {
    omuiPlanFonts();
    imports = omuiSplitImports(imports);
    if (omuiDeferred && !omuiDeferredLazy()) omuiFetchDeferred().catch(function () {});
    var job = omuiJob(omuiWasmAsset, async function (controller) {
      var body = await omuiFetchBody(omuiWasmAsset, controller);
      if (body.response && typeof WebAssembly.instantiateStreaming === 'function' && /^application\/wasm(?:;|$)/i.test(body.response.headers.get('Content-Type') || ''))
        return WebAssembly.instantiateStreaming(body.response, imports);
      var buffer = body.buffer || await body.response.arrayBuffer();
      return WebAssembly.instantiate(buffer, imports);
    }, function (result) {
      if (omuiWasmReceived) return; omuiWasmReceived = true; omuiPrimary = result.instance; receive(result.instance, result.module);
    });
    omuiStart(job, false); return {};
  },
  preRun: [function () {
    if (omuiPreRan) return; omuiPlanFonts(); omuiPreRan = true;
    Object.keys(omuiFiles).forEach(function (path) {
      var dir = path.substring(0, path.lastIndexOf('/')); if (dir) FS.mkdirTree(dir); FS.writeFile(path, omuiFiles[path]);
    });
    if (omuiDeferred && !omuiDeferredLazy()) {
      // Not a route the first half was profiled for: main waits for the second half.
      Module.addRunDependency('omui-deferred');
      omuiFetchDeferred().then(function () { Module.removeRunDependency('omui-deferred'); },
                               function () { Module.removeRunDependency('omui-deferred'); });
    }
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
  if (omuiDeferred) omuiFetchDeferred().catch(function () {});
  omuiStartedLater().forEach(function (j) { omuiStart(j, false); }); omuiFinished();
}
// A character no face the program has covers (its code point), or every
// face (no argument: a reader drawing in all of them): the deferred faces
// that could draw it, fetched once each.
// A family's name as the program says it (Noto Sans SC) and a font's file stem (NotoSansSC), compared.
function omuiFamilyKey(name) { return String(name).replace(/\.ttf$/, '').replace(/[^0-9a-z]/gi, '').toLowerCase(); }
// Text in `family` has a character its face here lacks (`codePoint`): that family's face the page holds back
// until asked, if it has the character's block, fetched once.
window.omuiWantFace = function (family, codePoint) {
  var key = omuiFamilyKey(family), block = codePoint >> 8, any = false;
  omuiLaterJobs.forEach(function (j) {
    if (!j.demand || j.asset.wanted || j.family !== key) return;
    if (j.blocks && !j.blocks.some(function (r) { return block >= r[0] && block <= r[1]; })) return;
    j.asset.wanted = true; any = true;
    if (omuiLaterStarted) omuiStart(j, false);
  });
  window.omuiFacesWanted = (window.omuiFacesWanted || []).concat([{family: family, codePoint: codePoint, fetched: any}]);
  if (any) { omuiPublish(); omuiFinished(); }
};
window.omuiWantFonts = function (codePoint) {
  var every = typeof codePoint !== 'number', block = every ? -1 : codePoint >> 8, any = false;
  omuiLaterJobs.forEach(function (j) {
    if (!j.demand || j.asset.wanted) return;
    if (!every && j.blocks && !j.blocks.some(function (r) { return block >= r[0] && block <= r[1]; })) return;
    j.asset.wanted = true; any = true;
    if (omuiLaterStarted) omuiStart(j, false);
  });
  window.omuiFontsWanted = (window.omuiFontsWanted || []).concat([{codePoint: every ? null : codePoint, fetched: any}]);
  if (any) { omuiPublish(); omuiFinished(); }
};
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
    // Deferred, though it is the page's last element (so it runs as it would anyway): a parser-blocking script is
    // fetched twice by Chrome 157 when it caches nothing (the preload scanner's request and the parser's), 51 KB
    // over the delivery budget measured without a cache (red 106); deferred, the two are one.
    if (p.load == "eager" && !p.world) {
        html += std::format("<script defer src=\"{}\"></script>\n</body>\n</html>\n", js);
        return html;
    }
    const std::size_t loader = html.size();
    // Later (D-260): the page is its document until the program is taken — once it is idle, or when the reader
    // first operates something only the program answers. Reading never takes it: scrolling, the browser's find, a
    // selection and links are the document's own. The press that took it is kept — the node of the element
    // pressed — and handed to the program once (omuiLoad.takePress), which answers it when it has presented.
    html += std::format("<script>\n(function () {{\n  var launcher = \"{}\", idle = {};\n", js, p.load == "idle" ? "true" : "false");
    html += R"(  var started = false, pressed = null;
  var operable = 'button,input,select,textarea,summary,[contenteditable],[role=button],[role=checkbox],[role=switch],[role=radio],' +
                 '[role=tab],[role=slider],[role=spinbutton],[role=menuitem],[role=option],[role=textbox],[role=combobox],[role=searchbox]';
  omuiStatus.style.display = 'none';
  function start() {
    if (started) return;
    started = true;
    ['pointerdown', 'keydown'].forEach(function (type) { window.removeEventListener(type, operated, true); });
    omuiStatus.style.display = '';
    var script = document.createElement('script');
    script.src = launcher;
    document.body.appendChild(script);
  }
  function operated(event) {
    if (!event.isTrusted || started) return;
    var target = event.target && event.target.closest ? event.target.closest(operable) : null;
    if (!target || target.closest('a[href]')) return;
    // A key operates what has the focus: Enter or Space on a control, anything typed in a field.
    if (event.type === 'keydown' && !target.matches('input,select,textarea,[contenteditable],[role=textbox],[role=combobox],[role=searchbox]') &&
        event.key !== 'Enter' && event.key !== ' ') return;
    if (event.type === 'pointerdown' && event.button) return;
    var node = target.closest('[data-omui-target]');
    pressed = event.type === 'pointerdown' && node ? node.getAttribute('data-omui-target') : null;
    start();
  }
  ['pointerdown', 'keydown'].forEach(function (type) { window.addEventListener(type, operated, true); });
  Object.defineProperty(window.omuiLoad, 'start', {value: function () { start(); }});
  Object.defineProperty(window.omuiLoad, 'takePress', {value: function () { var node = pressed; pressed = null; return node; }});
  Object.defineProperty(window.omuiLoad, 'strategy', {value: idle ? 'idle' : 'interaction'});
  if (idle) {
    var later = function () { (window.requestIdleCallback || function (run) { return setTimeout(run, 200); })(start, {timeout: 2000}); };
    if (document.readyState === 'complete') later(); else window.addEventListener('load', later);
  }
  omuiPublish('waiting');
})();
</script>
</body>
</html>
)";
    if (!p.world) return html;
    // A world's page: the program is found from the world's directory and taken when the page says; what follows
    // `#` in its address is a place in its document — not the program's flags, not a route the page is loaded again
    // for; a font for some languages is the document's language's.
    bool whole = true;
    const auto swap = [&](std::string_view from, std::string_view to, std::size_t after = 0) {
        const std::size_t at = html.find(from, after);
        if (at == std::string::npos || html.find(from, at + 1) != std::string::npos) whole = false;
        else html.replace(at, from.size(), to);
    };
    swap(std::format("  var launcher = \"{}\", idle = {};\n", js, p.load == "idle" ? "true" : "false"),
         "  var launcher = omuiRoot + omuiLauncher, idle = omuiTakes === 'idle', eager = omuiTakes === 'eager';\n", loader);
    swap("  omuiStatus.style.display = 'none';\n  function start() {", "  if (!eager) omuiStatus.style.display = 'none';\n  function start() {", loader);
    swap("{value: idle ? 'idle' : 'interaction'}", "{value: omuiTakes}", loader);
    swap("  omuiPublish('waiting');\n})();", "  if (eager) start(); else omuiPublish('waiting');\n})();", loader);
    // The program draws the document's pictures from the page's own (its browser decoded them): it is taken once
    // they are there, or known not to come.
    swap("    var script = document.createElement('script');\n    script.src = launcher;\n    document.body.appendChild(script);\n",
         "    var take = function () {\n      var script = document.createElement('script');\n      script.src = launcher;\n      document.body.appendChild(script);\n    };\n"
         "    var coming = [].filter.call(document.querySelectorAll('[data-omui=static] img'), function (picture) { return !picture.complete; });\n"
         "    var left = coming.length;\n    if (!left) return take();\n"
         "    coming.forEach(function (picture) {\n      var arrived = function () { if (--left === 0) take(); };\n"
         "      picture.addEventListener('load', arrived);\n      picture.addEventListener('error', arrived);\n    });\n",
         loader);
    swap("var response = await fetch(a.url, {", "var response = await fetch(omuiRoot + a.url, {");
    swap("  var launcher = new URL(omuiLauncher, document.URL).href;", "  var launcher = new URL(omuiRoot + omuiLauncher, document.URL).href;");
    swap("function omuiArguments() {\n", "function omuiArguments() {\n  return [];\n");
    swap("function omuiWants(langs) {\n", "function omuiWants(langs) {\n  if (typeof window.omuiFontLanguage !== 'string' && document.documentElement.lang) "
                                          "window.omuiFontLanguage = document.documentElement.lang;\n");
    swap("window.addEventListener('hashchange', function () { if (!window.omuiFollowsRoute) location.reload(); });\n", "");
    return whole ? html : std::string();
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

// The package's report and the list beside it (<out>.toy.xdef.json): every
// file, its size, the total — written after packing and after a split.
std::expected<ToyReport, std::string> finish_package(const fs::path& out, const std::string& target, const std::string& title, std::uintmax_t index_bytes,
                                                     bool toy_limit = true) {
    std::error_code ec;
    ToyReport report;
    for (const auto& e : fs::recursive_directory_iterator(out, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string rel = utf8(fs::relative(e.path(), out, ec));
        if (!toy_keeps(utf8(e.path().extension())))
            return std::unexpected(std::format("{}: a file type Toy does not keep", rel));
        report.files.push_back({rel, e.file_size()});
        report.total += e.file_size();
    }
    std::ranges::sort(report.files, {}, &ToyFile::path);
    report.index = index_bytes;
    if (toy_limit && report.index > toy_index_limit)
        return std::unexpected(std::format("index.html is {} bytes: Toy takes at most {}", report.index, toy_index_limit));

    // What `toy create` asks, and what is in the package, beside it (Toy has no manifest file).
    nlohmann::ordered_json about;
    about["kind"] = "toy-package";
    about["target"] = target;
    about["title"] = title;
    about["entry"] = "index.html";
    about["index_bytes"] = report.index;
    about["index_limit"] = toy_index_limit;
    about["total_bytes"] = report.total;
    about["publish"] = std::format("toy create --title \"{}\" (the slug, the cover and the category are the publisher's; see `toy create --help-json`)", title);
    auto& list = about["files"] = nlohmann::ordered_json::array();
    for (const ToyFile& f : report.files) list.push_back({{"path", f.path}, {"bytes", f.size}});
    fs::path side = out;
    side += ".toy.xdef.json";
    if (auto w = write(side, about.dump(2) + "\n"); !w) return std::unexpected(w.error());
    return report;
}

std::expected<ToyReport, std::string> package_for_toy(const ToyPackage& p) {
    std::error_code ec;
    if (p.load != "eager" && p.load != "idle" && p.load != "interaction")
        return std::unexpected(std::format("--load: `{}` is not when a page takes its program (eager, idle, interaction)", p.load));
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
    // Each font beside the others or in its pack's directory (mlibs.text's tree: fonts/<pack>/, D-214).
    std::map<std::string, fs::path> font_files;  // stem → file
    for (auto at = fs::recursive_directory_iterator(fonts_dir, ec); at != fs::recursive_directory_iterator(); at.increment(ec)) {
        if (at.depth() > 0) at.disable_recursion_pending();
        if (at->is_regular_file(ec) && at->path().extension() == ".ttf") font_files.try_emplace(utf8(at->path().stem()), at->path());
    }
    std::vector<std::string> stems = p.fonts;
    if (stems.empty() || (stems.size() == 1 && stems[0] == "all")) {
        stems.clear();
        for (const auto& [stem, file] : font_files) stems.push_back(stem);
    }
    const auto font_file = [&](const std::string& stem, std::string_view suffix) {
        const auto found = font_files.find(stem);
        return (found != font_files.end() ? found->second.parent_path() : fonts_dir) / (stem + std::string(suffix));
    };
    for (const auto& [font, text] : p.font_initial) {
        if (text.empty() || std::ranges::find(stems, font) == stems.end())
            return std::unexpected(std::format("--font-initial {} needs nonempty text and a selected font", font));
        if (std::ranges::count(p.font_initial, font, &std::pair<std::string, std::string>::first) != 1)
            return std::unexpected(std::format("--font-initial {} is repeated", font));
    }
    std::vector<std::pair<std::string, std::string>> fonts, initial;
    std::map<std::string, std::string> coverage;  // a language's face: the blocks it covers, as JSON ranges
    for (const std::string& stem : stems) {
        auto bytes = read(font_file(stem, ".ttf"));
        if (!bytes) return std::unexpected(std::format("font {}: {}", stem, bytes.error()));
        const auto first = std::ranges::find(p.font_initial, stem, &std::pair<std::string, std::string>::first);
        if (first != p.font_initial.end()) {
            std::set<char32_t> keep;
            characters_of(first->second, keep);
            if (const auto more = std::ranges::find(p.font_initial_text, stem, &std::pair<std::string, std::vector<fs::path>>::first); more != p.font_initial_text.end())
                if (auto read_all = characters_of_files(more->second, keep); !read_all) return std::unexpected(std::format("--font-initial-text {}: {}", stem, read_all.error()));
            auto cut = subset_font(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()), keep);
            if (!cut) return std::unexpected(std::format("initial font {}: {}", stem, cut.error()));
            auto delivered = compact_font_post(*cut);
            if (!delivered) return std::unexpected(std::format("initial font {}: {}", stem, delivered.error()));
            const std::string_view data(reinterpret_cast<const char*>(delivered->data()), delivered->size());
            const std::string asset = std::format("assets/{}.ttf", content_hash(data));
            if (auto w = write(p.out / asset, data); !w) return std::unexpected(w.error());
            initial.emplace_back(stem, asset);
            std::println("dist-toy: {} initial glyphs: {} bytes", stem, delivered->size());
        }
        // Cut down to the characters the program's text holds, where asked.
        if (const auto sub = std::ranges::find(p.subsets, stem, &ToyPackage::Subset::font); sub != p.subsets.end()) {
            std::set<char32_t> keep;
            always_kept(keep);
            if (first != p.font_initial.end()) characters_of(first->second, keep);
            if (auto common = read(font_file(stem, ".common.txt"))) characters_of(*common, keep);
            if (auto read_all = characters_of_files(sub->text, keep); !read_all) return std::unexpected(std::format("--subset {}: {}", stem, read_all.error()));
            FontSubsetReport r;
            auto cut = subset_font(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()), keep, &r);
            if (!cut) return std::unexpected(std::format("font {}: {}", stem, cut.error()));
            bytes->assign(reinterpret_cast<const char*>(cut->data()), cut->size());
            std::println("dist-toy: {} cut to {} of its {} glyphs: {} → {} bytes", stem, r.kept, r.glyphs, r.bytes_before, r.bytes_after);
        }
        FontDeliveryReport delivery_report;
        auto delivered = compact_font_post(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()), &delivery_report);
        if (!delivered) return std::unexpected(std::format("font {}: {}", stem, delivered.error()));
        bytes->assign(reinterpret_cast<const char*>(delivered->data()), delivered->size());
        if (delivery_report.names_removed)
            std::println("dist-toy: {} delivery post names: {} → {} bytes", stem, delivery_report.bytes_before, delivery_report.bytes_after);
        const std::string asset = std::format("assets/{}.ttf", content_hash(*bytes));
        if (auto w = write(p.out / asset, *bytes); !w) return std::unexpected(w.error());
        fonts.emplace_back(stem + ".ttf", asset);
        // Another language's face, and a face fetched on demand, wait until a character it covers is wanted: the page knows which.
        const bool on_demand = std::ranges::find(p.fonts_on_demand, stem) != p.fonts_on_demand.end();
        if (on_demand && first == p.font_initial.end()) return std::unexpected(std::format("--font-on-demand {}: it needs an initial cut (--font-initial)", stem));
        if (on_demand || std::ranges::find(p.font_languages, stem, &std::pair<std::string, std::vector<std::string>>::first) != p.font_languages.end()) {
            // A face whose cmap cannot be read says nothing: fetched for any character no face covers.
            const auto blocks = font_blocks(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()));
            if (blocks) {
                std::string json;
                for (const auto& [from, to] : *blocks) json += std::format("{}[{}, {}]", json.empty() ? "" : ", ", from, to);
                coverage[stem] = "[" + json + "]";
            }
        }
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
    // A font's licence is beside it: the directories of the fonts carried.
    std::set<fs::path> font_dirs;
    for (const std::string& stem : stems) font_dirs.insert(font_file(stem, ".ttf").parent_path());
    for (const fs::path& dir : font_dirs)
        for (const auto& e : fs::directory_iterator(dir, ec)) {
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
    // The application's own files, read now and written into the page.
    std::map<std::string, std::string> files;
    for (const ToyPackage::File& f : p.files) {
        if (!f.path.starts_with('/')) return std::unexpected(std::format("a file's path in the program's file system is absolute ({})", f.path));
        auto bytes = read(f.source);
        if (!bytes) return std::unexpected(bytes.error());
        if (f.data) {
            auto evaluated = omui::lang::ts::evaluate(f.source);
            if (!evaluated) return std::unexpected(std::format("{}: {}", utf8(f.source), evaluated.error().text()));
            bytes = evaluated->snapshot();
        }
        try {
            (void)nlohmann::json(*bytes).dump();  // text the page can carry: UTF-8
        } catch (const nlohmann::json::exception&) {
            return std::unexpected(std::format("{}: not UTF-8 text (the page carries the application's text files)", utf8(f.source)));
        }
        files[f.path] = std::move(*bytes);
    }
    const std::string index = page(p, js_name, wasm_name, fonts, later, initial, decoded_sizes, coverage, files);
    if (index.empty()) return std::unexpected("a world's page could not be made: the loader is not as dist-toy's world expects it (omui.gen's dist_toy.cpp)");
    if (auto w = write(p.out / "index.html", index); !w) return std::unexpected(w.error());

    return finish_package(p.out, p.target, p.title, index.size());
}

std::expected<ToyDocument, std::string> document_of(const mcpplibs::cmdline::ParsedArgs& args);  // below, with dist-toy-document

int dist_toy_main(const mcpplibs::cmdline::ParsedArgs& args) {
    auto usage = [](std::string_view why) {
        std::println(std::cerr, "omui-gen dist-toy: {} (omui-gen dist-toy --help)", why);
        return 2;
    };
    // A comma-separated list's items, the empty ones left out.
    const auto items = [](std::string_view list) {
        std::vector<std::string> out;
        for (auto part : list | std::views::split(','))
            if (!std::ranges::empty(part)) out.emplace_back(part.begin(), part.end());
        return out;
    };
    ToyPackage p;
    p.stage = args.positional(0);
    p.target = args.value("target").value_or("");
    p.out = args.value("out").value_or("");
    if (auto title = args.value("title")) p.title = *title;
    if (auto dir = args.value("fonts-dir")) p.fonts_dir = *dir;
    if (auto graph = args.value("graph")) p.graph = *graph;
    if (auto background = args.value("background")) p.background = *background;
    if (auto dark = args.value("background-dark")) p.background_dark = *dark;
    if (auto load = args.value("load")) p.load = *load;
    if (auto fonts = args.value("fonts")) p.fonts = items(*fonts);
    if (args.is_flag_set("no-sdk")) p.sdk = false;
    if (args.is_flag_set("world")) p.world = true;
    for (const std::string& value : args.option_or_empty("subset").values) {  // <font>=<path>,<path>…
        const auto eq = value.find('=');
        if (eq == std::string::npos) return usage("--subset is <font>=<path>,<path>…");
        ToyPackage::Subset subset{value.substr(0, eq), {}};
        for (const std::string& file : items(std::string_view(value).substr(eq + 1))) subset.text.emplace_back(file);
        p.subsets.push_back(std::move(subset));
    }
    for (const std::string& value : args.option_or_empty("font-initial").values) {  // <font>=<UTF-8 text>
        const auto eq = value.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 == value.size()) return usage("--font-initial is <font>=<text>");
        p.font_initial.emplace_back(value.substr(0, eq), value.substr(eq + 1));
    }
    for (const std::string& value : args.option_or_empty("font-initial-text").values) {  // <font>=<path>,<path>…
        const auto eq = value.find('=');
        if (eq == std::string::npos || eq == 0) return usage("--font-initial-text is <font>=<path>,<path>…");
        std::vector<fs::path> paths;
        for (const std::string& file : items(std::string_view(value).substr(eq + 1))) paths.emplace_back(file);
        p.font_initial_text.emplace_back(value.substr(0, eq), std::move(paths));
    }
    for (const std::string& value : args.option_or_empty("font-on-demand").values) p.fonts_on_demand.push_back(value);
    for (const std::string& value : args.option_or_empty("font-languages").values) {  // <font>=<lang>,<lang>…
        const auto eq = value.find('=');
        if (eq == std::string::npos) return usage("--font-languages is <font>=<lang>,<lang>…");
        p.font_languages.emplace_back(value.substr(0, eq), items(std::string_view(value).substr(eq + 1)));
    }
    for (const std::string& value : args.option_or_empty("later").values) {  // <dir>|<suffix>|<mount>
        const auto bar1 = value.find('|'), bar2 = value.find('|', bar1 + 1);
        if (bar1 == std::string::npos || bar2 == std::string::npos) return usage("--later is <dir>|<suffix>|<mount>");
        p.later.push_back({value.substr(0, bar1), value.substr(bar1 + 1, bar2 - bar1 - 1), value.substr(bar2 + 1)});
    }
    for (const auto& [option, data] : {std::pair{"file", false}, std::pair{"data", true}})
        for (const std::string& value : args.option_or_empty(option).values) {  // <path in the program>=<file>
            const auto eq = value.find('=');
            if (eq == std::string::npos) return usage(std::format("--{} is <path in the program>=<file>", option));
            p.files.push_back({value.substr(0, eq), fs::path(std::u8string(value.begin() + static_cast<std::ptrdiff_t>(eq) + 1, value.end())), data});
        }
    if (p.stage.empty() || p.target.empty() || p.out.empty()) return usage("a staged tree, --target and --out");
    auto document = document_of(args);
    if (!document) return usage(document.error());
    if (p.world && !document->static_html.empty()) return usage("--world makes a page waiting for its document: --static-html gives one page its own");

    auto r = package_for_toy(p);
    if (!r) {
        std::println(std::cerr, "dist-toy: {}", r.error());
        return 1;
    }
    // The page a document before its program runs, when its first screen's projection is given.
    if (!document->static_html.empty()) {
        document->dir = p.out;
        r = adopt_document(*document);
        if (!r) {
            std::println(std::cerr, "dist-toy: {}", r.error());
            return 1;
        }
    }
    std::println("dist-toy: {} — {} files, {} bytes (index.html {} of {})", utf8(p.out), r->files.size(), r->total, r->index, toy_index_limit);
    return 0;
}

std::expected<ToyReport, std::string> adopt_split(const ToySplit& split) {
    const fs::path index_file = split.dir / "index.html";
    auto page = read(index_file);
    if (!page) return std::unexpected(page.error());
    std::string html = std::move(*page);
    constexpr std::string_view key = "var omuiWasm = \"";
    const auto at = html.find(key);
    const auto close = at == std::string::npos ? std::string::npos : html.find('"', at + key.size());
    if (close == std::string::npos) return std::unexpected(std::format("{}: no `var omuiWasm` (not a page dist-toy made)", utf8(index_file)));
    const std::string old_name = html.substr(at + key.size(), close - at - key.size());
    const auto dash = old_name.rfind('-');
    if (dash == std::string::npos || !old_name.ends_with(".wasm")) return std::unexpected(std::format("{}: not <target>-<hash>.wasm", old_name));
    const std::string target = old_name.substr(0, dash);
    const auto marker = html.find(deferred_marker);
    if (marker == std::string::npos)
        return std::unexpected(std::format("{}: no `{}` (split already, or made by an older omui-gen)", utf8(index_file), deferred_marker));
    auto primary = read(split.primary);
    if (!primary) return std::unexpected(primary.error());
    auto deferred = read(split.deferred);
    if (!deferred) return std::unexpected(deferred.error());
    const std::string primary_name = std::format("{}-{}.wasm", target, content_hash(*primary));
    const std::string deferred_name = std::format("{}-{}.deferred.wasm", target, content_hash(*deferred));
    if (auto w = write(split.dir / primary_name, *primary); !w) return std::unexpected(w.error());
    if (auto w = write(split.dir / deferred_name, *deferred); !w) return std::unexpected(w.error());
    std::error_code ec;
    if (old_name != primary_name) fs::remove(split.dir / old_name, ec);

    // The deferred line first: the replacements after it shift nothing before it.
    nlohmann::json about = {{"url", deferred_name}, {"bytes", deferred->size()}, {"lazy", split.lazy}};
    html.replace(marker, deferred_marker.size(), std::format("var omuiDeferred = {};", about.dump()));
    // The decoded size the loader checks the primary against.
    const std::string bytes_key = std::format("\"{}\":", old_name);
    if (const auto b = html.find(bytes_key); b != std::string::npos) {
        const auto value_end = html.find_first_of(",}", b + bytes_key.size());
        html.replace(b, value_end - b, std::format("\"{}\":{}", primary_name, primary->size()));
    } else {
        return std::unexpected(std::format("{}: no decoded size for {}", utf8(index_file), old_name));
    }
    html.replace(at + key.size(), old_name.size(), primary_name);
    if (auto w = write(index_file, html); !w) return std::unexpected(w.error());

    std::string title = target;
    fs::path side = split.dir;
    side += ".toy.xdef.json";
    if (auto listed = read(side)) {
        const auto json = nlohmann::json::parse(*listed, nullptr, false);
        if (json.is_object() && json.contains("title") && json["title"].is_string()) title = json["title"].get<std::string>();
    }
    return finish_package(split.dir, target, title, html.size(), false);
}

namespace {

// Text readable on the page's background (the colour dist-toy's style gives it, after `key`): light on a dark page,
// dark on a light one; none where the page has no such style.
std::string_view text_colour_on(std::string_view html, std::string_view key = "overflow:hidden;background:#", std::string_view otherwise = "#16161a") {
    const auto at = html.find(key);
    unsigned rgb = 0;
    if (at == std::string_view::npos || at + key.size() + 6 > html.size()) return otherwise;
    const char* from = html.data() + at + key.size();
    if (std::from_chars(from, from + 6, rgb, 16).ptr != from + 6) return "#16161a";
    const double luma = (0.2126 * ((rgb >> 16) & 0xff) + 0.7152 * ((rgb >> 8) & 0xff) + 0.0722 * (rgb & 0xff)) / 255.0;
    return luma < 0.5 ? "#f4f4f6" : "#16161a";
}

}  // namespace

std::expected<ToyReport, std::string> adopt_document(const ToyDocument& document) {
    const fs::path index_file = document.dir / "index.html";
    auto page = read(index_file);
    if (!page) return std::unexpected(page.error());
    std::string html = std::move(*page);
    constexpr std::string_view key = "var omuiWasm = \"";
    const auto wasm = html.find(key);
    const auto close = wasm == std::string::npos ? std::string::npos : html.find('"', wasm + key.size());
    const auto canvas = html.find("<canvas id=\"canvas\"");
    const auto head = html.find("</head>");
    if (close == std::string::npos || canvas == std::string::npos || head == std::string::npos)
        return std::unexpected(std::format("{}: not a page dist-toy made", utf8(index_file)));
    if (html.contains(world_document)) return std::unexpected(std::format("{}: a world's page — a site's builder gives each page its document", utf8(index_file)));
    if (html.contains("data-omui=\"static\"")) return std::unexpected(std::format("{}: has its document already (once a page)", utf8(index_file)));
    if (html.contains("id=\"omui-static\"")) return std::unexpected(std::format("{}: has a static page of its own", utf8(index_file)));
    const std::string name = html.substr(wasm + key.size(), close - wasm - key.size());
    const std::string target = name.substr(0, name.rfind('-'));
    auto fragment = read(document.static_html);
    if (!fragment) return std::unexpected(fragment.error());
    if (fragment->find_first_not_of(" \t\r\n") == std::string::npos) return std::unexpected(std::format("{}: empty", utf8(document.static_html)));
    if (fragment->contains("<script") || fragment->contains("<SCRIPT"))
        return std::unexpected(std::format("{}: a projection runs nothing (it holds a script)", utf8(document.static_html)));
    // A document with a look (omui.semantic's: each element says its node's layout, face, colour and ground) comes
    // with the look's sheet before it: the page's, in its head.
    std::string look_sheet;
    if (constexpr std::string_view opening = "<style data-omui=\"look\">", closing = "</style>"; fragment->starts_with(opening)) {
        const auto end = fragment->find(closing);
        if (end == std::string::npos) return std::unexpected(std::format("{}: its look's sheet does not end", utf8(document.static_html)));
        look_sheet = fragment->substr(opening.size(), end - opening.size());
        if (look_sheet.contains('<')) return std::unexpected(std::format("{}: its look's sheet holds markup", utf8(document.static_html)));
        fragment->erase(0, fragment->find_first_not_of(" \t\r\n", end + closing.size()));
    }

    // Body first (after the head): the inserts in the head shift nothing before them.
    const std::string_view colour = text_colour_on(html);
    html.insert(canvas, static_part(*fragment));
    std::string meta;
    std::string title;
    if (const auto from = html.find("<title>"), to = html.find("</title>"); from != std::string::npos && to != std::string::npos && from < to)
        title = html.substr(from + 7, to - from - 7);  // escaped already
    if (!document.description.empty()) meta += std::format("<meta name=\"description\" content=\"{}\">\n", escape_html(document.description));
    if (!document.canonical.empty()) meta += std::format("<link rel=\"canonical\" href=\"{}\">\n", escape_html(document.canonical));
    for (const auto& [lang, address] : document.alternates)
        meta += std::format("<link rel=\"alternate\" hreflang=\"{}\" href=\"{}\">\n", escape_html(lang), escape_html(address));
    meta += "<meta property=\"og:type\" content=\"website\">\n";
    if (!title.empty()) meta += std::format("<meta property=\"og:title\" content=\"{}\">\n", title);
    if (!document.description.empty()) meta += std::format("<meta property=\"og:description\" content=\"{}\">\n", escape_html(document.description));
    if (!document.canonical.empty()) meta += std::format("<meta property=\"og:url\" content=\"{}\">\n", escape_html(document.canonical));
    if (!document.image.empty()) meta += std::format("<meta property=\"og:image\" content=\"{}\">\n", escape_html(document.image));
    meta += std::format("<meta name=\"twitter:card\" content=\"{}\">\n", document.image.empty() ? "summary" : "summary_large_image");
    // Over the canvas's place until it steps aside; a document's own scrolling, selection and touch.
    // How the document is set: by its look where it has one — every box as its node's, the window's box its
    // root's, the scrolling its own views' — else as plain text in a column that scrolls.
    const bool looked = !look_sheet.empty();
    const std::string set = looked
        ? std::string(looked_set)
        : std::string("overflow:auto;box-sizing:border-box;padding:32px max(24px,calc((100vw - 760px) / 2));font:16px/1.6 system-ui,sans-serif;");
    const std::string inside = looked
        ? std::format("{}{}", look_sheet, looked_inside)
        : std::string("[data-omui=static] a{color:inherit}[data-omui=static] button{font:inherit;color:inherit;background:none;"
                      "border:1px solid currentColor;border-radius:8px;padding:4px 12px;margin:4px 8px 4px 0}");
    meta += static_style(set, colour, inside);
    // Where the page is dark for a person who prefers dark, its document's text is readable on that too.
    if (const std::string_view dark = text_colour_on(html, "(prefers-color-scheme:dark){html,body{color-scheme:dark;background:#", {}); !dark.empty())
        meta += std::format("<style>@media (prefers-color-scheme:dark){{[data-omui=static]{{color:{}}}}}</style>\n", dark);
    html.insert(head, meta);
    // The document's language: as given, else as the projection's root says it (static_html's `lang`).
    std::string lang = document.lang;
    if (lang.empty()) {
        const std::string_view root = std::string_view(*fragment).substr(0, fragment->find('>'));
        constexpr std::string_view said = " lang=\"";
        if (const auto at = root.find(said); at != std::string_view::npos)
            lang = std::string(root.substr(at + said.size(), root.find('"', at + said.size()) - at - said.size()));
    }
    if (!lang.empty()) {
        constexpr std::string_view opening = "<html lang=\"";
        if (const auto at = html.find(opening); at != std::string::npos) {
            const auto end = html.find('"', at + opening.size());
            html.replace(at + opening.size(), end - at - opening.size(), escape_html(lang));
        }
    }
    if (auto w = write(index_file, html); !w) return std::unexpected(w.error());
    std::string listed_title = target;
    fs::path side = document.dir;
    side += ".toy.xdef.json";
    if (auto listed = read(side)) {
        const auto json = nlohmann::json::parse(*listed, nullptr, false);
        if (json.is_object() && json.contains("title") && json["title"].is_string()) listed_title = json["title"].get<std::string>();
    }
    return finish_package(document.dir, target, listed_title, html.size(), html.contains("toy-sdk.js"));  // Toy's limit on Toy's pages
}

// The document's options as dist-toy and dist-toy-document take them.
std::expected<ToyDocument, std::string> document_of(const mcpplibs::cmdline::ParsedArgs& args) {
    ToyDocument document;
    document.static_html = args.value("static-html").value_or("");
    document.lang = args.value("lang").value_or("");
    document.description = args.value("description").value_or("");
    document.canonical = args.value("canonical").value_or("");
    document.image = args.value("image").value_or("");
    for (const std::string& value : args.option_or_empty("alternate").values) {  // <hreflang>=<address>
        const auto equals = value.find('=');
        if (equals == std::string::npos || equals == 0) return std::unexpected(std::format("--alternate takes HREFLANG=ADDRESS, not `{}`", value));
        document.alternates.emplace_back(value.substr(0, equals), value.substr(equals + 1));
    }
    return document;
}

int dist_toy_document_main(const mcpplibs::cmdline::ParsedArgs& args) {
    auto read_options = document_of(args);
    if (!read_options) {
        std::println(std::cerr, "omui-gen dist-toy-document: {}", read_options.error());
        return 2;
    }
    ToyDocument document = std::move(*read_options);
    document.dir = args.positional(0);
    if (document.dir.empty() || document.static_html.empty()) {
        std::println(std::cerr, "omui-gen dist-toy-document: a package directory and --static-html (omui-gen dist-toy-document --help)");
        return 2;
    }
    auto r = adopt_document(document);
    if (!r) {
        std::println(std::cerr, "omui-gen dist-toy-document: {}", r.error());
        return 1;
    }
    std::println("dist-toy-document: index.html {} bytes; {} files, {} bytes", r->index, r->files.size(), r->total);
    return 0;
}

std::vector<std::string> lazy_routes(std::string_view list) {
    // Not views::split: over an empty list it yields no field, and the bare page would be no route.
    std::vector<std::string> routes;
    for (std::size_t from = 0; from <= list.size();) {
        const std::size_t comma = std::min(list.find(',', from), list.size());
        routes.emplace_back(list.substr(from, comma - from));
        from = comma + 1;
    }
    return routes;
}

int dist_toy_split_main(const mcpplibs::cmdline::ParsedArgs& args) {
    ToySplit split;
    split.dir = args.positional(0);
    split.primary = args.value("primary").value_or("");
    split.deferred = args.value("deferred").value_or("");
    if (auto lazy = args.value("lazy")) split.lazy = lazy_routes(*lazy);
    else split.lazy = {"", "home"};
    if (split.dir.empty() || split.primary.empty() || split.deferred.empty()) {
        std::println(std::cerr, "omui-gen dist-toy-split: a package directory, --primary and --deferred (omui-gen dist-toy-split --help)");
        return 2;
    }

    auto r = adopt_split(split);
    if (!r) {
        std::println(std::cerr, "omui-gen dist-toy-split: {}", r.error());
        return 1;
    }
    std::println("dist-toy-split: {} files, {} bytes", r->files.size(), r->total);
    return 0;
}

}  // namespace omui::gen
