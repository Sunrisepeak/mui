// The takeover of a page with a look, in a real browser (UI5, D-288; from the probe that set the bound).
//
// takeover  a built page whose document has a look (`omui build --web --look`), served as a static host
//           serves it, its program held back: each word the reader sees is placed where the page's own
//           document sets it; the program is let through, and when it has presented, the same word is
//           placed where the document layer sets it over the canvas. The bound: a word moves at most
//           2 px, half of them at most 1 px, and none goes to another line — at each width, at the top
//           of the document and with the reader scrolled down it. The report:
//           target/takeover.report.xdef.json.
module omui.devtools.check;

import std;
import mcpplibs.os;
import mlibs.delivery;
import mlibs.http;
import omui.devtools.browser;
import omui.devtools.cdp;
import :support;

namespace omui::devtools::check {

namespace fs = std::filesystem;
using namespace support;

namespace {

using cdp::json, cdp::at, cdp::number, cdp::text, cdp::truthy;

// The bound (design 2026-11-27-styled-projection-loading-design.md §4): set from the measurements of step 2.
constexpr double median_bound = 1.0, most_bound = 2.0;
constexpr int words_at_least = 50;

// Evaluated in the page. `before`: every word of every node's own text in the page's document, kept on the
// window. `after`: the same words in the layer over the canvas, and how far each one seen moved.
constexpr std::string_view words_script = R"js((() => {
// The middle of the line a word stands on: the layer's own line where the word is in one; else, in a block whose
// lines are of one stated height, the line of that height its letters' middle falls in; else its letters' middle.
const line = (node, r) => {
  const middle = r.top + r.height / 2;
  let up = node.parentElement;
  const set = up && up.closest ? up.closest('[data-omui-line]') : null;
  if (set) { const box = set.getBoundingClientRect(); return box.top + box.height / 2; }
  while (up && getComputedStyle(up).display === 'inline') up = up.parentElement;
  if (!up) return middle;
  const style = getComputedStyle(up), high = parseFloat(style.lineHeight);
  if (!(high > 0)) return middle;
  const from = up.getBoundingClientRect().top + (parseFloat(style.borderTopWidth) || 0) + (parseFloat(style.paddingTop) || 0);
  return from + (Math.max(0, Math.floor((middle - from) / high)) + 0.5) * high;
};
const words = (root, attribute) => {
  const all = [], top = document.querySelector(root);
  if (!top) return all;
  for (const el of top.querySelectorAll(`[${attribute}]`)) {
    const id = el.getAttribute(attribute);
    let index = 0;
    const walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT, { acceptNode: (node) => {
      for (let up = node.parentElement; up && up !== el; up = up.parentElement)
        if (up.hasAttribute(attribute) && !up.hasAttribute('data-omui-inline') && getComputedStyle(up).display !== 'inline') return NodeFilter.FILTER_REJECT;
      return NodeFilter.FILTER_ACCEPT;
    } });
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      const found = /\S+/g;
      for (let m = found.exec(node.data); m; m = found.exec(node.data)) {
        const range = document.createRange();
        range.setStart(node, m.index);
        range.setEnd(node, m.index + m[0].length);
        const r = range.getBoundingClientRect();
        // A word's place: its left edge and the middle of the line it stands on. Not the middle of its own letters'
        // box: that is its face's (a word of code in a line of prose, a character another face drew, stand on the
        // same line 2–4 px apart by their faces' heights), and the layer sets every word in one face.
        if (r.width > 0) all.push({ id, index: index++, word: m[0], x: r.left, y: line(node, r), h: r.height });
      }
    }
  }
  return all;
};
if ('@STEP@' === 'before') {
  const by = @SCROLL@;
  if (by) for (const el of document.querySelectorAll('[data-omui=static] *')) {
    const flow = getComputedStyle(el).overflowY;
    if (el.scrollHeight > el.clientHeight + 4 && flow !== 'visible' && flow !== 'hidden') { el.scrollTop = by; break; }
  }
  window.omuiTakeoverBefore = words('[data-omui=static]', 'data-omui-target');
  return { words: window.omuiTakeoverBefore.length, look: !!document.querySelector('[data-omui=static] [data-omui-look]'),
           faces: [...document.fonts].filter((face) => face.status === 'loaded').map((face) => face.family) };
}
const now = new Map(words('[data-omui=mirror]', 'data-omui-id').map((w) => [w.id + '#' + w.index, w]));
const moved = [];
let unmatched = 0, lines = 0;
for (const w of window.omuiTakeoverBefore || []) {
  const to = now.get(w.id + '#' + w.index);
  if (!to || to.word !== w.word) { ++unmatched; continue; }
  // What a reader sees change: the words on the screen before or after.
  if (!((w.y > 0 && w.y < innerHeight) || (to.y > 0 && to.y < innerHeight))) continue;
  const dx = to.x - w.x, dy = to.y - w.y;
  if (Math.abs(dy) > Math.max(w.h, to.h) * 0.6 && Math.abs(dx) > 40) ++lines;
  moved.push({ d: Math.hypot(dx, dy), where: `${w.id} "${w.word}" ${dx.toFixed(1)},${dy.toFixed(1)}` });
}
moved.sort((a, b) => a.d - b.d);
const part = (q) => (moved.length ? +moved[Math.min(moved.length - 1, Math.floor(q * moved.length))].d.toFixed(2) : null);
return { compared: moved.length, unmatched, median: part(0.5), p95: part(0.95), most: part(1), lines, furthest: moved.slice(-2).map((m) => m.where) };
})())js";

std::string scripted(std::string_view step, int scroll) {
    std::string script(words_script);
    const auto put = [&](std::string_view mark, std::string_view value) {
        for (auto found = script.find(mark); found != std::string::npos; found = script.find(mark, found + value.size())) script.replace(found, mark.size(), value);
    };
    put("@STEP@", step);
    put("@SCROLL@", std::format("{}", scroll));
    return script;
}

// The page's program, held until the document was read.
struct Held {
    std::mutex              lock;
    std::condition_variable let;
    bool                    through = false;
};

}  // namespace

int takeover(const fs::path& root, const fs::path& directory, std::string_view path, std::span<const int> widths) {
    // The page's address on the check's own host: a site's route, or the directory's own page.
    std::string route(path);
    if (!route.starts_with('/')) route.insert(route.begin(), '/');
    if (!route.ends_with('/')) route += '/';
    if (!fs::is_regular_file(directory / mcpplibs::os::fs::from_utf8(route.substr(1)) / "index.html"))
        return failed(std::format("no page at {} in {} (omui build --web --look, or a site built with its engine)", route, mcpplibs::os::fs::utf8(directory)));
    const auto chrome = browser::chromium();
    if (!chrome) return failed("no headless Chromium for the takeover's check");
    auto held = std::make_shared<Held>();
    auto server = cdp::Server::start([directory, held](const cdp::Arrived& request) -> cdp::Reply {
        std::string name = mlibs::http::decoded(request.path).substr(request.path.starts_with("/") ? 1 : 0);
        if (name.empty() || name.ends_with('/')) name += "index.html";
        if (name.contains("..")) return {.status = 400, .body = "", .type = ""};
        if (name.ends_with(".wasm")) {
            // The program waits for the document to be read (at most a minute: a check that stalls says so itself).
            std::unique_lock hold(held->lock);
            held->let.wait_for(hold, std::chrono::seconds(60), [&] { return held->through; });
        }
        const auto bytes = mcpplibs::os::fs::read(directory / mcpplibs::os::fs::from_utf8(name));
        if (!bytes) return {.status = 404, .body = "", .type = ""};
        // A picture said to be what it is: one served as bytes of no kind is one the browser does not draw.
        const std::size_t dot = name.rfind('.');
        const std::string_view type = mlibs::http::media_type(dot == std::string::npos ? std::string_view{} : std::string_view(name).substr(dot));
        return {.status = 200, .body = mlibs::delivery::gzip(*bytes, 6), .type = std::string(type), .headers = {{"Content-Encoding", "gzip"}, {"Cache-Control", "no-store"}}};
    });
    if (!server) return failed(server.error());
    auto launched = cdp::Browser::launch(*chrome, {});
    if (!launched) return failed(launched.error());
    auto& page = **launched;
    const auto let = [&](bool through) {
        {
            const std::scoped_lock hold(held->lock);
            held->through = through;
        }
        held->let.notify_all();
    };
    // `ready` evaluated until it holds, or why it never did.
    const auto wait = [&](std::string_view ready, int tenths, std::string_view what) -> std::optional<std::string> {
        for (int attempt = 0; attempt < tenths; ++attempt) {
            const auto held_true = page.value(ready);
            if (!held_true) return held_true.error();
            if (truthy(*held_true)) return std::nullopt;
            page.pump(std::chrono::milliseconds(100));
        }
        return std::format("{} did not come", what);
    };
    std::string address = (*server)->url();
    if (address.ends_with('/')) address.pop_back();
    address += route;
    json rows = json::array();
    std::string problem;
    bool within = true;
    for (const int width : widths) {
        for (const int scroll : {0, 700}) {
            if (!problem.empty()) break;
            let(false);
            if (const auto sized = page.call("Emulation.setDeviceMetricsOverride", {{"width", width}, {"height", 800}, {"deviceScaleFactor", 1}, {"mobile", false}}); !sized) problem = sized.error();
            if (const auto gone = page.call("Page.navigate", {{"url", address}}); problem.empty() && !gone) problem = gone.error();
            if (problem.empty())
                if (const auto late = wait("document.readyState !== 'loading' && !!document.fonts && document.fonts.status === 'loaded' && !!document.querySelector('[data-omui=static] [data-omui-target]')", 300,
                                           "the page's document");
                    late)
                    problem = *late;
            if (!problem.empty()) break;
            // The faces the page declares, loaded before its words are placed: a face that swaps in later would
            // move them (the page shows its text at once, in the reader's own font until its face is there).
            if (const auto faces = page.value("Promise.all([...document.fonts].map((face) => face.load())).then(() => true, () => false)"); !faces) {
                problem = faces.error();
                break;
            } else if (!truthy(*faces)) {
                problem = "a face the page declares did not load";
                break;
            }
            page.pump(std::chrono::milliseconds(600));
            const auto before = page.value(scripted("before", scroll));
            if (!before) {
                problem = before.error();
                break;
            }
            if (!truthy(at(*before, "look"))) {
                problem = "the page's document has no look (omui build --web --look)";
                break;
            }
            page.pump(std::chrono::milliseconds(300));
            let(true);
            // A page that waits for its reader to operate something is told to take its program now.
            if (const auto taken = page.value("(window.omuiLoad && typeof window.omuiLoad.start === 'function' && (window.omuiLoad.start(), true)) || false"); !taken) {
                problem = taken.error();
                break;
            }
            if (const auto late = wait("window.omuiHasPresented === true && document.querySelectorAll('[data-omui=mirror] [data-omui-line]').length > 0", 900, "the program's first frame"); late) {
                problem = *late;
                break;
            }
            page.pump(std::chrono::milliseconds(1500));
            const auto after = page.value(scripted("after", scroll));
            if (!after) {
                problem = after.error();
                break;
            }
            json row = *after;
            row["faces"] = at(*before, "faces");
            row["width"] = width;
            row["scrolled"] = scroll;
            const bool ok = number(at(row, "compared")) >= words_at_least && number(at(row, "median")) <= median_bound && number(at(row, "most")) <= most_bound && number(at(row, "lines")) == 0;
            row["ok"] = ok;
            within = within && ok;
            rows.push_back(std::move(row));
        }
    }
    let(true);  // nothing waits on a check that ended
    // The page's live examples (what a block of the document gave when it ran: `data-output`) answer once the
    // program has the page: each one's first button that can be pressed, pressed where the canvas shows it, changes
    // what the example shows. The page as the last row left it: taken over, at the narrowest width.
    int examples = 0, operated = 0, answered = 0;
    if (problem.empty()) {
        const auto found = page.value("document.querySelectorAll('[data-omui=mirror] [data-output]').length");
        examples = found ? static_cast<int>(number(*found)) : 0;
        for (int index = 0; index < examples; ++index) {
            const std::string example = std::format("document.querySelectorAll('[data-omui=mirror] [data-output]')[{}]", index);
            const std::string button = std::format("{}.querySelector('button:not([aria-disabled=true]):not([disabled]), [role=button]:not([aria-disabled=true])')", example);
            // Brought into view as a reader's tools bring an element there: the page tells the program, the view follows.
            const auto there = page.value(std::format("(() => {{ const b = {}; if (!b) return false; b.scrollIntoView({{block: 'center'}}); return true; }})()", button));
            if (!there || !truthy(*there)) continue;
            page.pump(std::chrono::milliseconds(700));
            const auto shown = page.value(std::format("(() => {{ const r = {}.getBoundingClientRect(); return {{x: r.left + r.width / 2, y: r.top + r.height / 2, text: {}.textContent}}; }})()", button, example));
            if (!shown) continue;
            ++operated;
            const double x = number(at(*shown, "x")), y = number(at(*shown, "y"));
            page.post("Input.dispatchMouseEvent", {{"type", "mouseMoved"}, {"x", x}, {"y", y}});
            page.pump(std::chrono::milliseconds(100));
            page.post("Input.dispatchMouseEvent", {{"type", "mousePressed"}, {"x", x}, {"y", y}, {"button", "left"}, {"buttons", 1}, {"clickCount", 1}});
            page.pump(std::chrono::milliseconds(60));
            page.post("Input.dispatchMouseEvent", {{"type", "mouseReleased"}, {"x", x}, {"y", y}, {"button", "left"}, {"buttons", 0}, {"clickCount", 1}});
            page.pump(std::chrono::milliseconds(700));
            const auto now = page.value(std::format("{}.textContent", example));
            if (now && text(*now) != text(at(*shown, "text"))) ++answered;
        }
    }
    // A link to another page of the site leads there once the program has the page: the first one the page shows
    // (an address without a scheme, to another path), pressed where the canvas shows it — the browser is then at
    // its address. A page whose program answers presses but follows nothing is a page one cannot leave.
    int links = 0, followed = 0;
    std::string led_to;
    if (problem.empty()) {
        const std::string link = "[...document.querySelectorAll('[data-omui=mirror] a[href]')].find((a) => { const h = a.getAttribute('href'); return !/^[a-z][a-z0-9+.-]*:/i.test(h) && "
                                 "new URL(h, document.baseURI).pathname !== location.pathname; })";
        const auto there = page.value(std::format("(() => {{ const a = {}; if (!a) return false; a.scrollIntoView({{block: 'center'}}); return true; }})()", link));
        if (there && truthy(*there)) {
            links = 1;
            page.pump(std::chrono::milliseconds(700));
            const auto shown = page.value(std::format("(() => {{ const a = {}; const r = a.getClientRects()[0]; return {{x: r.left + Math.min(8, r.width / 2), y: r.top + r.height / 2, to: new URL(a.getAttribute('href'), document.baseURI).href}}; }})()", link));
            if (shown) {
                const double x = number(at(*shown, "x")), y = number(at(*shown, "y"));
                led_to = text(at(*shown, "to"));
                page.post("Input.dispatchMouseEvent", {{"type", "mouseMoved"}, {"x", x}, {"y", y}});
                page.pump(std::chrono::milliseconds(100));
                page.post("Input.dispatchMouseEvent", {{"type", "mousePressed"}, {"x", x}, {"y", y}, {"button", "left"}, {"buttons", 1}, {"clickCount", 1}});
                page.pump(std::chrono::milliseconds(60));
                page.post("Input.dispatchMouseEvent", {{"type", "mouseReleased"}, {"x", x}, {"y", y}, {"button", "left"}, {"buttons", 0}, {"clickCount", 1}});
                if (!wait(std::format("location.href === {}", json(led_to).dump()), 80, "the page the link leads to")) followed = 1;
            }
        }
    }
    const std::string version = page.version();
    page.close();
    (*server)->close();
    if (!problem.empty()) return failed(std::format("the takeover's check did not run: {}", problem));
    const json report = {{"$format", "takeover-report@1"},
                         {"page", mcpplibs::os::fs::utf8(directory)},
                         {"path", route},
                         {"browser", version},
                         {"bound", {{"median", median_bound}, {"most", most_bound}, {"lines", 0}, {"words", words_at_least}}},
                         {"rows", rows},
                         {"examples", {{"found", examples}, {"operated", operated}, {"answered", answered}}},
                         {"links", {{"pressed", links}, {"followed", followed}, {"to", led_to}}},
                         {"ok", within && operated == answered && links == followed}};
    std::error_code made;
    fs::create_directories(root / "target", made);
    std::ofstream(root / "target/takeover.report.xdef.json", std::ios::binary) << cdp::pretty(report, 2) << '\n';
    std::cout << std::format("the takeover of a page with a look ({}): a word moves at most {} px, half of them at most {} px, none to another line\n", version, most_bound, median_bound);
    for (const auto& row : rows)
        std::cout << std::format("  {} {:>4} wide, scrolled {:>3}: {} words seen, median {} px, most {} px, {} to another line\n", truthy(at(row, "ok")) ? "ok  " : "FAIL", number(at(row, "width")),
                                 number(at(row, "scrolled")), number(at(row, "compared")), number(at(row, "median")), number(at(row, "most")), number(at(row, "lines")));
    if (examples > 0)
        std::cout << std::format("  {} {} live example{}: {} operated (its first button pressed on the canvas), {} answered\n", operated == answered ? "ok  " : "FAIL", examples, examples == 1 ? "" : "s", operated,
                                 answered);
    if (links > 0) std::cout << std::format("  {} a link to another page of the site, pressed on the canvas: {} {}\n", followed == links ? "ok  " : "FAIL", followed == links ? "led to" : "did not lead to", led_to);
    if (!within) return failed("a word moved past the bound when the program took the page over (target/takeover.report.xdef.json)");
    if (operated != answered) return failed("a live example did not answer when its button was pressed (target/takeover.report.xdef.json)");
    if (links != followed) return failed("a link to another page of the site led nowhere when pressed (target/takeover.report.xdef.json)");
    return 0;
}

}  // namespace omui::devtools::check
