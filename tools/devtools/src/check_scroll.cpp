// A long page scrolled in a real browser (red 164): where its frames go.
//
// scroll  a built site whose pages load a program (oweb site.build --engine), in a headed Chromium whose window is
//         on a monitor of the desktop (the one named; its rate is the bar's interval), drawn by the machine's GPU.
//         Each route asked is visited in turn, round after round (a long page, a short one: a busy machine shows as
//         such in both), and on each, once the program has the page: the page at rest, then the wheel turned down
//         to its end and back up — wheel events the browser is given (CDP, never the desktop's pointer). Measured
//         by the browser itself: its trace (Chromium's own frame accounting, the main thread's tasks, its layouts,
//         its collections), the page's long tasks, the frames its animation callbacks were given, the layer's
//         elements. The machine's load beside every phase. The report: target/scroll.report.xdef.json; with --trace,
//         each phase's trace for Perfetto beside it.
module omui.devtools.check;

import std;
import mcpplibs.os;
import omui.devtools.browser;
import omui.devtools.cdp;
import omui.devtools.run;
import omui.devtools.x11;
import :support;

namespace omui::devtools::check {

namespace fs = std::filesystem;
using namespace support;

namespace {

using cdp::json, cdp::at, cdp::number, cdp::text, cdp::truthy;

// What the page keeps while a phase is measured: the time each animation frame was given (the frame's start, as
// the browser paces them), the long tasks it saw; with the program's spans asked for, their summaries — the
// program's profile read and begun again by the page itself (`cycle`), so the wheel's pace is not held up for it.
constexpr std::string_view probe_script = R"js((() => {
  const probe = window.omuiScrollProbe = { frames: [], long: [], own: [], on: false };
  const tick = (at) => { if (!probe.on) return; probe.frames.push(at); requestAnimationFrame(tick); };
  probe.start = () => { probe.frames = []; probe.long = []; probe.own = []; probe.on = true; requestAnimationFrame(tick); return true; };
  probe.stop = () => { probe.on = false; return { frames: probe.frames, long: probe.long }; };
  probe.cycle = (again) => window.omui.request({ op: 'profile', action: 'stop' }).then((a) => {
    probe.own.push((a.data && a.data.summary) || []);
    return again ? window.omui.request({ op: 'profile', action: 'start' }).then(() => true) : true;
  });
  try {
    new PerformanceObserver((list) => { if (probe.on) for (const e of list.getEntries()) probe.long.push([e.startTime, e.duration]); }).observe({ type: 'longtask' });
  } catch (e) {}
})();)js";

// The page as the measure needs it: how far its view scrolls and where it is, the layer's elements, what draws.
constexpr std::string_view state_script = R"js((() => {
  const view = document.querySelector('[data-omui=mirror] [data-omui-scroll]');
  const canvas = document.querySelector('canvas');
  let drawn = 'none';
  if (canvas) for (const kind of ['webgpu', 'webgl2', 'webgl', '2d']) { try { if (canvas.getContext(kind)) { drawn = kind; break; } } catch (e) {} }
  return { y: view ? view.scrollTop : -1, most: view ? view.scrollHeight - view.clientHeight : -1,
           elements: document.querySelectorAll('[data-omui=mirror] *').length,
           lines: document.querySelectorAll('[data-omui=mirror] [data-omui-line]').length,
           words: document.querySelectorAll('[data-omui=mirror] [data-omui-word]').length,
           // Elements whose own words still flow rather than stand on the canvas's lines (a text not drawn yet).
           flowed: [...document.querySelectorAll('[data-omui=mirror] [data-omui-id]:not([data-omui-inline])')].filter((el) =>
             !el.querySelector(':scope > [data-omui-line]') && [...el.childNodes].some((n) => n.nodeType === 3 && n.data.trim())).length,
           width: innerWidth, height: innerHeight, ratio: devicePixelRatio, drawn };
})())js";

// The browser's own ceiling on this display: a page of nothing but a canvas whose picture changes at every frame,
// served by the check beside the site — how often this browser, this GPU and this monitor show a new frame at all.
constexpr std::string_view reference_address = "/omui-scroll-reference/";
constexpr std::string_view reference_page = R"html(<!doctype html><meta charset="utf-8"><title>reference</title>
<style>html,body{margin:0;height:100%;overflow:hidden;background:#fff}canvas{display:block;width:100vw;height:100vh}</style>
<canvas></canvas><script>
const canvas = document.querySelector('canvas'), context = canvas.getContext('2d');
let count = 0;
const draw = () => {
  canvas.width = innerWidth * devicePixelRatio; canvas.height = innerHeight * devicePixelRatio;
  context.fillStyle = '#fff'; context.fillRect(0, 0, canvas.width, canvas.height);
  context.fillStyle = '#123'; context.fillRect((count * 7) % canvas.width, 0, 40, canvas.height);
  context.font = '32px sans-serif'; context.fillText(String(++count), 40, 80);
  requestAnimationFrame(draw);
};
requestAnimationFrame(draw);
window.omuiHasPresented = true;
</script>)html";

// What the trace is asked for: the frames as the compositor and the display have them, the main thread's tasks
// (scripts, styles, layouts, paints), the collections.
const std::vector<std::string> categories{"devtools.timeline",
                                          "disabled-by-default-devtools.timeline",
                                          "disabled-by-default-devtools.timeline.frame",
                                          "toplevel",
                                          "benchmark",
                                          "cc",
                                          "viz",
                                          "gpu",
                                          "blink",
                                          "v8",
                                          "disabled-by-default-v8.gc",
                                          "blink.user_timing",
                                          "input",
                                          "latencyInfo"};

// The machine's load as uptime says it ("load average: 1.33, 2.29, 1.76"): the 1, 5 and 15 minute averages.
json load(const fs::path& root) {
    const std::string said = run::output(root, "uptime", {}).value_or("");
    json out = json::array();
    const auto found = said.find("average:");
    if (found == std::string::npos) return out;
    std::string_view rest = std::string_view(said).substr(found + 8);
    for (int index = 0; index < 3; ++index) {
        rest.remove_prefix(std::min(rest.find_first_not_of(" ,"), rest.size()));
        double value = 0;
        const auto [end, error] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
        if (error != std::errc{}) break;
        out.push_back(value);
        rest.remove_prefix(static_cast<std::size_t>(end - rest.data()));
    }
    return out;
}

double percentile(std::vector<double> values, double share) {
    if (values.empty()) return 0;
    std::ranges::sort(values);
    const auto rank = static_cast<std::size_t>(std::ceil(share * static_cast<double>(values.size())));
    return values[std::clamp<std::size_t>(rank, 1, values.size()) - 1];
}

double round2(double value) { return std::round(value * 100.0) / 100.0; }

// Frame intervals (ms) as a row: how many, the median, p95, p99, the longest, how many took two intervals or more.
json intervals_row(const std::vector<double>& gaps, double interval) {
    int doubled = 0;
    for (const double gap : gaps) doubled += gap >= 1.5 * interval;
    return {{"frames", gaps.size()},
            {"median_ms", round2(percentile(gaps, 0.5))},
            {"p95_ms", round2(percentile(gaps, 0.95))},
            {"p99_ms", round2(percentile(gaps, 0.99))},
            {"most_ms", round2(gaps.empty() ? 0 : std::ranges::max(gaps))},
            {"two_or_more", doubled}};
}

// A phase's trace read: whose threads, the frames the display showed, the main thread's work.
struct Thread {
    std::int64_t pid = 0, tid = 0;
    bool operator<(const Thread& other) const { return std::tie(pid, tid) < std::tie(other.pid, other.tid); }
};

// What a main frame's time went to, by the events Chromium records inside it (names of Chromium 159's trace): the
// page's animation callbacks (the program's step), the waits on the GPU process those callbacks made (a WebGL call
// that answers: glGetError, a framebuffer's status), the style and layout a script forced, the document's own
// lifecycle (style, layout, paint, layers), the collections, the input's handling (its hit tests).
constexpr std::array<std::pair<std::string_view, std::string_view>, 7> parts_of_a_frame{{
    {"callbacks", "FireAnimationFrame"},
    {"gpu_waits", "CommandBufferHelper::Finish"},
    {"forced_layout", "Blink.ForcedStyleAndLayout.UpdateTime"},
    {"lifecycle", "WebFrameWidgetImpl::UpdateLifecycle"},
    {"minor_gc", "MinorGC"},
    {"major_gc", "MajorGC"},
    {"input", "WidgetBaseInputHandler::OnHandleInputEvent"},
}};

json read_trace(const std::vector<json>& events, double interval) {
    const auto thread_of = [](const json& e) { return Thread{static_cast<std::int64_t>(number(at(e, "pid"))), static_cast<std::int64_t>(number(at(e, "tid")))}; };
    std::map<Thread, std::string> names;
    for (const json& e : events)
        if (text(at(e, "ph")) == "M" && text(at(e, "name")) == "thread_name") names[thread_of(e)] = text(at(at(e, "args"), "name"));
    // The page's main thread: the renderer's whose animation frames fired (the page's program runs in them).
    std::map<Thread, int> fired;
    for (const json& e : events)
        if (text(at(e, "name")) == "FireAnimationFrame") ++fired[thread_of(e)];
    Thread main{};
    int most = 0;
    for (const auto& [thread, count] : fired)
        if (count > most) main = thread, most = count;
    // The main thread's complete events (X), by start: (start, end, name) in ms.
    struct Span {
        double begin = 0, end = 0;
        std::string_view name;
    };
    std::vector<Span> spans;
    for (const json& e : events) {
        if (text(at(e, "ph")) != "X" || thread_of(e).pid != main.pid || thread_of(e).tid != main.tid) continue;
        const double begin = number(at(e, "ts")) / 1000.0, took = number(at(e, "dur")) / 1000.0;
        if (!(took >= 0)) continue;
        const json& name = at(e, "name");
        spans.push_back({begin, begin + took, name.is_string() ? std::string_view(name.get_ref<const std::string&>()) : std::string_view{}});
    }
    std::ranges::sort(spans, {}, &Span::begin);
    // Each main frame (BeginMainFrame): how long, and what it held.
    std::vector<double> frames;
    json slow = json::object();  // the parts of the frames that took longer than an interval, summed
    int slow_frames = 0;
    double slow_total = 0;
    std::map<std::string_view, double> held;
    for (std::size_t index = 0; index < spans.size(); ++index) {
        const Span& frame = spans[index];
        if (frame.name != "ProxyMain::BeginMainFrame") continue;
        const double took = frame.end - frame.begin;
        frames.push_back(took);
        if (took <= interval) continue;
        ++slow_frames;
        slow_total += took;
        for (std::size_t inner = index + 1; inner < spans.size() && spans[inner].begin < frame.end; ++inner)
            for (const auto& [part, name] : parts_of_a_frame)
                if (spans[inner].name == name) held[part] += spans[inner].end - spans[inner].begin;
    }
    slow["frames"] = slow_frames;
    slow["total_ms"] = round2(slow_total);
    for (const auto& [part, name] : parts_of_a_frame) slow[std::string(part) + "_ms"] = round2(held[part]);
    // What the main thread did, by name: their total and longest.
    std::map<std::string_view, std::pair<double, double>> spent;  // name -> total, longest (ms)
    for (const Span& span : spans) {
        auto& [total, longest] = spent[span.name];
        total += span.end - span.begin;
        longest = std::max(longest, span.end - span.begin);
    }
    json by_name = json::array();
    std::vector<std::pair<std::string_view, std::pair<double, double>>> ordered(spent.begin(), spent.end());
    std::ranges::sort(ordered, [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
    for (std::size_t index = 0; index < ordered.size() && index < 30; ++index)
        by_name.push_back({{"name", std::string(ordered[index].first)}, {"total_ms", round2(ordered[index].second.first)}, {"most_ms", round2(ordered[index].second.second)}});
    // The frames the display showed (the display compositor's own presentation: Display::FrameDisplayed), and what
    // Chromium's frame accounting says of the page's frames (PipelineReporter: presented, dropped, nothing new).
    std::vector<double> displayed;
    std::map<std::string, int> states;
    for (const json& e : events) {
        const std::string& name = text(at(e, "name"));
        if (name == "Display::FrameDisplayed") displayed.push_back(number(at(e, "ts")) / 1000.0);
        else if (name == "PipelineReporter" && text(at(e, "ph")) == "b" && thread_of(e).pid == main.pid) {
            const json& reporter = at(at(e, "args"), "frame_reporter");
            if (truthy(at(reporter, "affects_smoothness"))) ++states[text(at(reporter, "state"))];
        }
    }
    std::ranges::sort(displayed);
    std::vector<double> gaps;
    for (std::size_t index = 1; index < displayed.size(); ++index) gaps.push_back(displayed[index] - displayed[index - 1]);
    json reported = json::object();
    for (const auto& [state, count] : states) reported[state.empty() ? "unsaid" : state] = count;
    return {{"main", {{"pid", main.pid}, {"tid", main.tid}, {"name", names[main]}}},
            {"displayed", intervals_row(gaps, interval)},
            {"reporter", reported},
            {"main_frames", intervals_row(frames, interval)},
            {"slow_main_frames", slow},
            {"spent", by_name}};
}

// The headed Chromium: OMUI_CHROME, else a full one on PATH (xlings' playwright-chromium, Chrome, Chromium) — not a
// headless shell, which opens no window.
std::optional<fs::path> headed_chromium() {
    if (const auto chosen = mcpplibs::os::env::get("OMUI_CHROME"); chosen && !chosen->empty()) return mcpplibs::os::fs::from_utf8(*chosen);
    for (const std::string_view name : {"playwright-chromium", "google-chrome", "google-chrome-stable", "chromium", "chromium-browser"})
        if (auto found = mcpplibs::os::env::find_executable(name)) return *found;
    return std::nullopt;
}

}  // namespace

int scroll(const fs::path& root, const Scroll& asked) {
    std::vector<std::string> routes;
    for (std::string route : asked.paths) {
        if (!route.starts_with('/')) route.insert(route.begin(), '/');
        if (!route.ends_with('/')) route += '/';
        if (!fs::is_regular_file(asked.page / mcpplibs::os::fs::from_utf8(route.substr(1)) / "index.html"))
            return failed(std::format("no page at {} in {} (a site built with its engine: oweb site.build --engine)", route, mcpplibs::os::fs::utf8(asked.page)));
        routes.push_back(std::move(route));
    }
    if (routes.empty()) return failed("no route to scroll (--paths /long/,/start/)");
    // The monitor the window goes on, and its rate: the bar's interval.
    x11::Rect monitor{0, 0, asked.width, asked.height};
    double hertz = 60;
    std::string monitor_name = "headless";
    if (!asked.headless) {
        const auto named = mcpplibs::os::env::get("DISPLAY");
        if (!named || !named->starts_with(':')) return failed("no desktop display ($DISPLAY) for a headed window; --headless measures without one");
        int number = 0;
        std::from_chars(named->data() + 1, named->data() + named->size(), number);
        auto display = x11::Display::open(number);
        if (!display) return failed(std::format("the display {}: {}", *named, display.error()));
        const auto monitors = display->monitors();
        if (!monitors) return failed(std::format("the display's monitors: {}", monitors.error()));
        const auto found = std::ranges::find_if(*monitors, [&](const x11::Monitor& m) { return asked.screen.empty() ? m.primary : m.name == asked.screen; });
        if (found == monitors->end()) return failed(std::format("no monitor {} on {}", asked.screen.empty() ? "primary" : asked.screen, *named));
        monitor = found->area;
        hertz = found->hertz > 0 ? found->hertz : 60;
        monitor_name = found->name;
    }
    const double interval = 1000.0 / hertz;
    const auto chrome = asked.headless ? browser::chromium() : headed_chromium();
    if (!chrome) return failed(asked.headless ? "no headless Chromium (OMUI_CHROME)" : "no Chromium that opens a window (OMUI_CHROME, playwright-chromium)");
    auto server = cdp::Server::start([served = cdp::pages(asked.page)](const cdp::Arrived& request) -> cdp::Reply {
        if (request.path == reference_address) return {.status = 200, .body = std::string(reference_page), .type = "text/html; charset=utf-8", .headers = {}};
        return served(request);
    });
    if (!server) return failed(server.error());
    std::vector<std::string> flags{std::format("--window-position={},{}", monitor.x + 40, monitor.y + 40)};
    flags.insert(flags.end(), asked.flags.begin(), asked.flags.end());
    auto launched = cdp::Browser::launch(*chrome, {.width = asked.width, .height = asked.height, .flags = flags, .headed = !asked.headless});
    if (!launched) return failed(launched.error());
    auto& page = **launched;
    // The trace's events as they come; the end of a trace.
    std::vector<json> collected;
    bool complete = false;
    page.on([&](const std::string& method, const json& params) {
        if (method == "Tracing.dataCollected")
            for (const json& e : at(params, "value")) collected.push_back(e);
        else if (method == "Tracing.tracingComplete")
            complete = true;
    });
    std::string problem;
    if (const auto added = page.call("Page.addScriptToEvaluateOnNewDocument", {{"source", std::string(probe_script)}}); !added) problem = added.error();
    const auto wait = [&](std::string_view ready, int tenths, std::string_view what) -> std::optional<std::string> {
        for (int attempt = 0; attempt < tenths; ++attempt) {
            const auto held = page.value(ready);
            if (!held) return held.error();
            if (truthy(*held)) return std::nullopt;
            page.pump(std::chrono::milliseconds(100));
        }
        return std::format("{} did not come", what);
    };
    // The program's own spans (mlibs.trace, through window.omui's `profile`: where its frame's time goes), when asked
    // for: a second run's attribution — recording them costs the program time of its own, so the frame numbers are
    // the runs without. The page reads them and begins again every half second (a thread keeps 65,536 events, about
    // a second of this page's frames); they are summed here.
    const auto own_spans = [&]() -> json {
        struct Own {
            std::int64_t count = 0;
            double total = 0, most = 0;
        };
        std::map<std::string, Own> own;
        static_cast<void>(page.value("window.omuiScrollProbe.cycle(false)"));
        const auto said = page.value("window.omuiScrollProbe.own");
        if (said && said->is_array())
            for (const json& part : *said)
                for (const json& span : part) {
                    Own& kept = own[text(at(span, "name"))];
                    kept.count += static_cast<std::int64_t>(number(at(span, "count")));
                    kept.total += number(at(span, "total_ms"));
                    kept.most = std::max(kept.most, number(at(span, "max_ms")));
                }
        std::vector<std::pair<std::string, Own>> spans(own.begin(), own.end());
        std::ranges::sort(spans, [](const auto& a, const auto& b) { return a.second.total > b.second.total; });
        json program = json::array();
        for (std::size_t index = 0; index < spans.size() && index < 30; ++index)
            program.push_back({{"name", spans[index].first}, {"count", spans[index].second.count}, {"total_ms", round2(spans[index].second.total)},
                               {"most_ms", round2(spans[index].second.most)}});
        return program;
    };
    // A phase measured: the trace and the probe around what `act` does.
    const auto measure = [&](const std::string& label, const std::function<void()>& act) -> json {
        json row = {{"phase", label}, {"load", {{"before", load(root)}}}};
        const auto state = page.value(state_script);
        row["before"] = state ? *state : json();
        collected.clear();
        complete = false;
        if (const auto started = page.call("Tracing.start", {{"traceConfig", {{"includedCategories", categories}, {"recordMode", "recordContinuously"}}}, {"transferMode", "ReportEvents"}});
            !started) {
            problem = started.error();
            return row;
        }
        static_cast<void>(page.value("window.omuiScrollProbe.start()"));
        if (asked.spans) static_cast<void>(page.value("window.omui.request({op: 'profile', action: 'start'}).then((a) => a.exitCode)"));
        act();
        const auto kept = page.value("window.omuiScrollProbe.stop()");
        if (asked.spans) row["program"] = own_spans();
        static_cast<void>(page.call("Tracing.end"));
        for (int attempt = 0; attempt < 600 && !complete; ++attempt) page.pump(std::chrono::milliseconds(50));
        row["load"]["after"] = load(root);
        const auto after = page.value(state_script);
        row["after"] = after ? *after : json();
        // The animation frames the page was given, their gaps.
        std::vector<double> gaps;
        if (kept) {
            const json& frames = at(*kept, "frames");
            for (std::size_t index = 1; index < frames.size(); ++index) gaps.push_back(number(frames[index]) - number(frames[index - 1]));
            json long_tasks = json::array();
            for (const json& task : at(*kept, "long")) long_tasks.push_back(round2(number(at(task, 1))));
            row["long_tasks_ms"] = long_tasks;
        }
        row["animation_frames"] = intervals_row(gaps, interval);
        row["trace"] = read_trace(collected, interval);
        row["trace"]["events"] = collected.size();
        if (asked.keep_trace) {
            std::error_code made;
            fs::create_directories(root / "target/scroll", made);
            json whole = {{"traceEvents", collected}};
            std::ofstream(root / "target/scroll" / std::format("{}.trace.json", label), std::ios::binary) << whole.dump();
        }
        return row;
    };
    // The wheel turned over the middle of the window, `speed` px a second, a step at each of the monitor's intervals
    // (a touchpad's or a free-spinning wheel's stream: what lets the view move at every frame), as far as the view
    // goes, on a schedule — what a step's sending was late by goes with the next, as a device's
    // events are coalesced. Nothing is asked of the page meanwhile. Then a moment for the view to come to rest.
    const auto wheel = [&](int direction) {
        const auto state = page.value(state_script);
        if (!state) return;
        const double x = number(at(*state, "width")) / 2, y = number(at(*state, "height")) / 2;
        const double distance = std::max(number(at(*state, "most")), 0.0) + 4;
        const auto every = std::chrono::duration<double, std::milli>(interval);
        const auto began = std::chrono::steady_clock::now();
        double sent = 0;
        for (int index = 1; sent < distance; ++index) {
            const auto due = began + std::chrono::duration_cast<std::chrono::steady_clock::duration>(every * index);
            const auto now = std::chrono::steady_clock::now();
            if (now < due) page.pump(std::chrono::duration_cast<std::chrono::milliseconds>(due - now));
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
            const double step = std::min(distance, asked.speed * elapsed) - sent;
            if (step < 0.5) continue;
            sent += step;
            page.post("Input.dispatchMouseEvent", {{"type", "mouseWheel"}, {"x", x}, {"y", y}, {"deltaX", 0}, {"deltaY", direction * step}});
            if (asked.spans && index % 60 == 0) page.post("Runtime.evaluate", {{"expression", "window.omuiScrollProbe.cycle(true)"}});
        }
        page.pump(std::chrono::milliseconds(600));
    };
    std::string address = (*server)->url();
    if (address.ends_with('/')) address.pop_back();
    json rows = json::array();
    const auto said = [](const json& phase) {
        const json& shown = at(at(phase, "trace"), "displayed");
        const json& frames = at(phase, "animation_frames");
        std::cout << std::format("  {:<16} load {}; shown {} median {} p99 {} most {} ms, {} two or more; callbacks {} p99 {} ms; long tasks {}; elements {} → {}\n",
                                 text(at(phase, "phase")), at(at(phase, "load"), "before").dump(), number(at(shown, "frames")), number(at(shown, "median_ms")),
                                 number(at(shown, "p99_ms")), number(at(shown, "most_ms")), number(at(shown, "two_or_more")), number(at(frames, "frames")),
                                 number(at(frames, "p99_ms")), at(phase, "long_tasks_ms").size(), number(at(at(phase, "before"), "elements")),
                                 number(at(at(phase, "after"), "elements")));
    };
    for (int round = 1; round <= asked.rounds && problem.empty(); ++round) {
        // The browser's ceiling first: a canvas that changes at every frame.
        if (const auto gone = page.call("Page.navigate", {{"url", address + std::string(reference_address)}}); !gone) {
            problem = gone.error();
            break;
        }
        if (const auto late = wait("window.omuiHasPresented === true", 300, "the reference page"); late) {
            problem = *late;
            break;
        }
        page.pump(std::chrono::milliseconds(1000));
        json reference = {{"round", round}, {"path", std::string(reference_address)}, {"phases", json::array()}};
        reference["phases"].push_back(measure(std::format("{}-reference", round), [&] { page.pump(std::chrono::milliseconds(2000)); }));
        said(reference["phases"].back());
        rows.push_back(std::move(reference));
        for (const std::string& route : routes) {
            if (!problem.empty()) break;
            if (const auto gone = page.call("Page.navigate", {{"url", address + route + asked.query}}); !gone) {
                problem = gone.error();
                break;
            }
            if (const auto late = wait("window.omuiHasPresented === true && document.querySelectorAll('[data-omui=mirror] [data-omui-line]').length > 0", 900, "the program's first frame"); late) {
                problem = *late;
                break;
            }
            page.pump(std::chrono::milliseconds(2000));
            std::string name = route == "/" ? "home" : route.substr(1, route.size() - 2);
            std::ranges::replace(name, '/', '-');
            const std::string prefix = std::format("{}-{}", round, name);
            json visit = {{"round", round}, {"path", route}, {"phases", json::array()}};
            visit["phases"].push_back(measure(prefix + "-rest", [&] { page.pump(std::chrono::milliseconds(2000)); }));
            visit["phases"].push_back(measure(prefix + "-down", [&] { wheel(1); }));
            visit["phases"].push_back(measure(prefix + "-up", [&] { wheel(-1); }));
            for (const json& phase : visit["phases"]) said(phase);
            rows.push_back(std::move(visit));
        }
    }
    const std::string version = page.version();
    page.close();
    (*server)->close();
    if (!problem.empty()) return failed(std::format("the scroll's check did not run: {}", problem));
    // The bar (AGENTS §2.10, D-187): in a browser a frame's p99 at most 16.7 ms (60 a second), on this machine's
    // monitor its interval (120 Hz: 8.3 ms) — the frames shown while a page's view moves, each route's worst; and
    // once scrolled to its end, no element whose words still flow (every text the canvas drew stands on its lines).
    constexpr double browser_bar = 1000.0 / 60;
    bool held = true;
    std::map<std::string, std::pair<double, double>> worst;  // route -> worst p99 of its moving phases, most flowed
    for (const json& visit : rows) {
        const std::string route = text(at(visit, "path"));
        if (route == reference_address) continue;
        for (const json& phase : at(visit, "phases")) {
            const std::string name = text(at(phase, "phase"));
            if (!name.ends_with("-down") && !name.ends_with("-up")) continue;
            auto& [p99, flowed] = worst[route];
            p99 = std::max(p99, number(at(at(at(phase, "trace"), "displayed"), "p99_ms")));
            if (name.ends_with("-down")) flowed = std::max(flowed, number(at(at(phase, "after"), "flowed")));
        }
    }
    json verdict = json::object();
    for (const auto& [route, seen] : worst) {
        const bool ok = seen.first <= browser_bar && seen.second == 0;
        held = held && ok;
        verdict[route] = {{"p99_ms", seen.first}, {"flowed", seen.second}, {"browser_bar", ok}, {"monitor_bar", seen.first <= interval + 0.01}};
        std::cout << std::format("  {} {}: shown frames' p99 while it moves {} ms (the browser's bar {:.1f}, the monitor's {:.1f}); {} element(s) left flowing at its end\n",
                                 ok ? "ok  " : "FAIL", route, seen.first, browser_bar, interval, seen.second);
    }
    const json report = {{"$format", "scroll-report@1"},
                         {"page", mcpplibs::os::fs::utf8(asked.page)},
                         {"browser", version},
                         {"monitor", {{"name", monitor_name}, {"hertz", hertz}, {"interval_ms", round2(interval)}}},
                         {"speed", asked.speed},
                         {"verdict", verdict},
                         {"rows", rows}};
    std::error_code made;
    fs::create_directories(root / "target", made);
    std::ofstream(root / "target/scroll.report.xdef.json", std::ios::binary) << cdp::pretty(report, 2) << '\n';
    if (!held) return failed("a page's frames while it scrolls are past the bar, or its words are left off the canvas's lines (target/scroll.report.xdef.json)");
    return 0;
}

}  // namespace omui::devtools::check
