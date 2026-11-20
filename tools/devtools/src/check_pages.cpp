// The site's delivery in a real browser (moved from tools/ci/site-perf.mjs, D-206): what the page transfers, its
// first frame and readiness, its frame tails while it moves, against its budget.
module omui.devtools.check;

import std;
import mcpplibs.os;
import omui.devtools.cdp;
import :pages;
import :support;

namespace omui::devtools::check::pages {

namespace {

using cdp::at, cdp::dig, cdp::q, cdp::text, cdp::truthy, cdp::number;

json failed_report(std::string message) {
    return {{"checks", json::array({{{"ok", false},
                                     {"what", "the browser started"},
                                     {"detail", std::move(message)}}})},
            {"ok", false}};
}

// A report's value at a dotted path ("transfer.total", "errors.length").
json at_path(const json& report, std::string_view path) {
    json current = report;
    for (auto part : std::views::split(path, '.')) {
        const std::string key(part.begin(), part.end());
        if (key == "length" && (current.is_array() || current.is_string()))
            current = current.is_array() ? current.size() : current.get<std::string>().size();
        else
            current = at(current, key);
    }
    return current;
}

}  // namespace

// --- delivery (site-perf.mjs) ---
// What the page transfers (each response's encoded bytes, by kind), how long until the program
// draws its first frame (its semantic mirror filled after it) and what had arrived by then, until
// it is ready (window.omui answers), its frame tails while it moves (asked.moves), the heap's
// and the wasm memory's size at the end. A cold visit (a fresh profile), over a connection when
// asked; a fresh profile still permits HTTP cache reuse within one visit.

json perf(const Context& context, const Perf& asked) {
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    const fs::path profile =
        fs::temp_directory_path() / std::format("omui-site-perf-{:08x}", std::random_device{}());
    std::error_code error;
    fs::create_directories(profile, error);
    auto launched = open(context, asked.width, asked.height,
                         {"--user-data-dir=" + mcpplibs::os::fs::utf8(profile)});
    if (!launched)
        return failed_report(launched.error());
    auto& page = **launched;
    std::map<std::string, std::string> responses;
    json bytes = json::object(), errors = json::array(), request_log = json::array();
    std::map<std::string, std::size_t> by_id;
    page.on([&](const std::string& method, const json& params) {
        const std::string id = text(at(params, "requestId"));
        if (method == "Network.requestWillBeSent") {
            json row = {{"id", id},
                        {"path", pathname(text(dig(params, "request", "url")))},
                        {"kind", at(params, "type")},
                        {"encoded_body_event_bytes", 0},
                        {"finished_bytes", json::array()}};
            if (const json& initiator = dig(params, "initiator", "type"); !initiator.is_null())
                row["initiator"] = initiator;
            request_log.push_back(std::move(row));
            by_id[id] = request_log.size() - 1;
        }
        if (const auto found = by_id.find(id); found != by_id.end()) {
            json& row = request_log[found->second];
            const json& response = at(params, "response");
            if (method == "Network.responseReceived") {
                row["status"] = at(response, "status");
                row["cache"] = truthy(at(row, "cache")) || truthy(at(response, "fromDiskCache")) ||
                               truthy(at(response, "fromPrefetchCache"));
                row["service_worker"] = truthy(at(response, "fromServiceWorker"));
            }
            if (method == "Network.requestServedFromCache")
                row["cache"] = true;
            if (method == "Network.dataReceived")
                row["encoded_body_event_bytes"] = number(row["encoded_body_event_bytes"]) +
                                                  number(at(params, "encodedDataLength"));
            if (method == "Network.loadingFinished")
                row["finished_bytes"].push_back(at(params, "encodedDataLength"));
        }
        if (method == "Network.responseReceived")
            responses[id] = text(at(params, "type"));
        if (method == "Network.loadingFinished") {
            std::string kind =
                responses.contains(id) && !responses[id].empty() ? responses[id] : "Other";
            for (char& letter : kind)
                letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
            bytes[kind] = (bytes.contains(kind) ? bytes[kind].get<double>() : 0.0) +
                          number(at(params, "encodedDataLength"));
        }
        if (method == "Runtime.exceptionThrown")
            errors.push_back(cdp::stringify(at(params, "exceptionDetails")).substr(0, 300));
    });
    cdp::Script script;
    script.page = &page;
    script.call("Network.enable");
    script.call("Network.setCacheDisabled", {{"cacheDisabled", !asked.cache}});
    script.call("Network.setBypassServiceWorker", {{"bypass", true}});
    if (!asked.network.empty()) {
        const auto slash = asked.network.find('/');
        const double mbit = std::stod(asked.network.substr(0, slash)),
                     rtt = std::stod(asked.network.substr(slash + 1));
        script.call("Network.emulateNetworkConditions", {{"offline", false},
                                                         {"latency", rtt},
                                                         {"downloadThroughput", mbit * 125000},
                                                         {"uploadThroughput", mbit * 125000 / 4}});
    }
    // From navigation: the first frame the program has drawn (its semantic mirror filled after it),
    // and when it answers as an agent.
    script.call("Page.addScriptToEvaluateOnNewDocument", {{"source", R"(
  window.__perf = { first: null, ready: null, contentful: null };
  // The first screen a visitor sees: the browser's first contentful paint (the static page, before the program).
  try {
    new PerformanceObserver((list) => {
      for (const e of list.getEntries()) if (e.name === 'first-contentful-paint' && window.__perf.contentful === null) window.__perf.contentful = e.startTime;
    }).observe({ type: 'paint', buffered: true });
  } catch (e) {}
  (function watch() {
    const m = document.querySelector('[data-omui=mirror]');
    if (m && m.childElementCount > 0 && window.__perf.first === null) window.__perf.first = performance.now();
    if (window.omui && window.__perf.ready === null) window.__perf.ready = performance.now();
    if (window.__perf.first === null || window.__perf.ready === null) requestAnimationFrame(watch);
  })();)"}});
    script.call("Page.navigate", {{"url", url}});
    json first, ready, before_first, contentful;
    const auto sum = [&] {
        double total = 0;
        for (const auto& item : bytes.items())
            total += number(item.value());
        return total;
    };
    for (int index = 0; index < 300 && (first.is_null() || ready.is_null()) && !script.thrown();
         ++index) {
        script.sleep(100);
        const json perf_now = script.value("window.__perf");
        if (truthy(perf_now)) {
            first = at(perf_now, "first");
            ready = at(perf_now, "ready");
            contentful = at(perf_now, "contentful");
        }
        if (!first.is_null() && before_first.is_null())
            before_first = sum();  // what had arrived when it first drew (to within 100 ms)
    }
    script.sleep(1500);  // its fonts arrived, its opening at rest
    // The frame tails: animation-frame intervals while it moves (the site: to a place and back).
    const std::string moves = asked.moves.empty() ? R"(
  await act('main/bar/nav/to_docs'); await pause(250);
  await act('main/body/view/docs/page/directory/to_features'); await pause(2500);
  await act('main/bar/mark'); await pause(2500);)"
                                                  : asked.moves;
    const json tails = script.value(R"(new Promise(async (done) => {
  const gaps = []; let last = null, running = true;
  const tick = (t) => { if (last !== null) gaps.push(t - last); last = t; if (running) requestAnimationFrame(tick); };
  requestAnimationFrame(tick);
  const acted = [];
  const act = async (id) => { const r = window.omui ? await window.omui.act(id, 'activate') : null; acted.push({ node: id, exitCode: r ? r.exitCode : null }); };
  const pause = (ms) => new Promise((r) => setTimeout(r, ms));)" + moves + R"(
  running = false;
  gaps.sort((a, b) => a - b);
  const q = (p) => gaps.length ? gaps[Math.min(gaps.length - 1, Math.floor(p * gaps.length))] : null;
  done({ acted, frames: gaps.length, p50: q(0.5), p95: q(0.95), p99: q(0.99), max: gaps.length ? gaps[gaps.length - 1] : null,
         long: gaps.filter((g) => g > 33.4).length });
}))");
    const json memory =
        script.value(R"(({ heap: performance.memory ? performance.memory.usedJSHeapSize : null,
  wasm: typeof HEAP8 !== 'undefined' ? HEAP8.length : null }))");
    // Whole bytes stay whole numbers.
    for (auto& value : bytes)
        if (value.is_number_float() && value.get<double>() == std::floor(value.get<double>()))
            value = static_cast<std::int64_t>(value.get<double>());
    for (auto& row : request_log)
        if (row["encoded_body_event_bytes"].is_number_float())
            row["encoded_body_event_bytes"] =
                static_cast<std::int64_t>(row["encoded_body_event_bytes"].get<double>());
    const auto whole = [](double value) {
        return value == std::floor(value) ? json(static_cast<std::int64_t>(value)) : json(value);
    };
    json report = {{"url", url},
                   {"size", {asked.width, asked.height}},
                   {"network", asked.network.empty() ? json(nullptr) : json(asked.network)},
                   {"cache", asked.cache ? "enabled" : "disabled"},
                   {"browser", page.version()},
                   {"transfer",
                    {{"total", whole(sum())},
                     {"before_first_frame",
                      before_first.is_null() ? before_first : whole(number(before_first))},
                     {"by_kind", bytes}}},
                   {"first_contentful_ms", contentful},
                   {"first_frame_ms", first},
                   {"ready_ms", ready},
                   {"frames", tails},
                   {"memory", memory},
                   {"errors", errors}};
    if (script.thrown())
        report["error"] = *script.take();
    if (!asked.request_log.empty()) {
        std::ofstream out(asked.request_log, std::ios::binary);
        out << cdp::pretty(request_log);
    }
    bool ok = !report.contains("error");
    if (!asked.budget.empty()) {
        report["budgets"] = json::array();
        const auto budget =
            json::parse(mcpplibs::os::fs::read(asked.budget).value_or("{}"), nullptr, false);
        for (const auto& limit : at(budget, "budgets")) {
            const json value = at_path(report, text(at(limit, "metric")));
            const bool pass = value.is_number() && number(value) <= number(at(limit, "max"));
            ok = ok && pass;
            report["budgets"].push_back({{"metric", at(limit, "metric")},
                                         {"value", value},
                                         {"max", at(limit, "max")},
                                         {"pass", pass}});
        }
    }
    report["ok"] = ok;
    page.close();
    // Chrome may still be writing its profile as it exits: a leftover temporary directory is not a
    // result.
    for (int attempt = 0; attempt < 10 && fs::exists(profile, error); ++attempt) {
        fs::remove_all(profile, error);
        if (fs::exists(profile, error))
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    (*server)->close();
    return report;
}

}  // namespace omui::devtools::check::pages
