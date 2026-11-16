// The site's delivery, accessibility, structure, catalog, workspace and reactions in a real browser
// (moved from tools/ci/site-perf.mjs, site-a11y.mjs, site-structure.mjs, site-catalog.mjs,
// site-workspace.mjs and site-reactions.mjs, D-206). Every threshold and expectation as the scripts
// had them.
module omui.devtools.check;

import std;
import mcpplibs.os;
import mlibs.hash;
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
// it is ready (window.omui answers), its frame tails while it flies to a place and back, the heap's
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
    // The frame tails: animation-frame intervals while it flies to a place and back.
    const json tails = script.value(R"(new Promise(async (done) => {
  const gaps = []; let last = null, running = true;
  const tick = (t) => { if (last !== null) gaps.push(t - last); last = t; if (running) requestAnimationFrame(tick); };
  requestAnimationFrame(tick);
  const acted = [];
  const act = async (id) => { const r = window.omui ? await window.omui.act(id, 'activate') : null; acted.push({ node: id, exitCode: r ? r.exitCode : null }); };
  await act('main/bar/nav/to_docs'); await new Promise((r) => setTimeout(r, 250));
  await act('main/body/view/docs/page/directory/to_features'); await new Promise((r) => setTimeout(r, 2500));
  await act('main/bar/mark'); await new Promise((r) => setTimeout(r, 2500));
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

// --- accessibility (site-a11y.mjs) ---
// Chromium's own accessibility tree once the program runs — the landmarks, a level-1 heading, every
// button and link named, the language, zoom allowed (WCAG 1.4.4), the static page hidden — the
// tab's identity, and the keyboard's way in: the skip link the first Tab stop, seen while focused,
// Enter into the main landmark (the page's focus and the program's); then the visible Explore link
// and the brand study through real clicks.

json a11y(const Context& context) {
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    auto launched = open(context, 1280, 800);
    if (!launched)
        return failed_report(launched.error());
    auto& page = **launched;
    cdp::Script script;
    script.page = &page;
    script.report = {{"url", url}, {"browser", page.version()}, {"checks", json::array()}};
    const auto check = [&](bool ok, std::string_view what) { return script.check(ok, what); };
    script.call("Page.navigate", {{"url", url}});
    // Running: window.omui answers and the mirror is filled.
    for (int index = 0; index < 300 && !script.thrown(); ++index) {
        if (truthy(script.value(
                "!!window.omui && (document.querySelector('[data-omui=mirror]')?.childElementCount "
                "?? 0) > 0")))
            break;
        script.sleep(100);
    }
    script.sleep(2500);  // the opening at rest

    script.call("Accessibility.enable");
    const json tree = script.call("Accessibility.getFullAXTree");
    json seen = json::array();
    for (const auto& node : dig(tree, "result", "nodes"))
        if (!truthy(at(node, "ignored")))
            seen.push_back(node);
    const auto role = [](const json& node) { return text(dig(node, "role", "value")); };
    const auto name = [](const json& node) { return text(dig(node, "name", "value")); };
    const auto prop = [](const json& node, std::string_view wanted) -> json {
        for (const auto& property : at(node, "properties"))
            if (text(at(property, "name")) == wanted)
                return dig(property, "value", "value");
        return nullptr;
    };
    json roles = json::object();
    for (const auto& node : seen)
        roles[role(node)] = (roles.contains(role(node)) ? roles[role(node)].get<int>() : 0) + 1;
    script.report["tree"] = {{"nodes", seen.size()}, {"roles", roles}};
    const auto first_of = [&](const std::function<bool(const json&)>& wanted) -> const json* {
        for (const auto& node : seen)
            if (wanted(node))
                return &node;
        return nullptr;
    };
    const json* navigation = first_of([&](const json& node) { return role(node) == "navigation"; });
    const json* main = first_of([&](const json& node) { return role(node) == "main"; });
    check(navigation && name(*navigation) == "Places",
          std::format("a navigation landmark, named ({})",
                      navigation ? q(name(*navigation)) : "none"));
    check(main, std::format("a main landmark ({})", main ? q(name(*main)) : "none"));
    const json* heading = first_of(
        [&](const json& node) { return role(node) == "heading" && prop(node, "level") == 1; });
    check(heading, std::format("a level-1 heading ({})", heading ? q(name(*heading)) : "none"));
    std::size_t unnamed = 0;
    for (const auto& node : seen) {
        std::string label = name(node);
        const auto blank = std::ranges::all_of(label, [](char letter) {
            return std::isspace(static_cast<unsigned char>(letter)) != 0;
        });
        if ((role(node) == "button" || role(node) == "link") && blank)
            ++unnamed;
    }
    check(unnamed == 0, std::format("every button and link named ({} not)", unnamed));
    json headings = json::array();
    for (const auto& node : seen)
        if (role(node) == "heading" && headings.size() < 12)
            headings.push_back({{"level", prop(node, "level")}, {"name", name(node)}});
    script.report["headings"] = headings;

    const json document = script.value(R"(({ lang: document.documentElement.lang,
  viewport: document.querySelector('meta[name=viewport]')?.content ?? '',
  statik: document.getElementById('omui-static')?.hidden ?? null }))");
    check(truthy(at(document, "lang")),
          std::format("the page's language ({})", text(at(document, "lang"))));
    static const std::regex no_zoom(R"(user-scalable\s*=\s*no|maximum-scale\s*=\s*1(\D|$))");
    const std::string viewport = text(at(document, "viewport"));
    check(!std::regex_search(viewport, no_zoom), std::format("zoom allowed ({})", viewport));
    check(at(document, "statik") == true, "the static page hidden once the program runs");

    // The implementation's identity, including the browser tab: the generated icon is the same
    // font-free original, rather than a separately drawn asset.
    const json identity = script.value(R"((() => {
  const href = document.querySelector('link[rel="icon"]')?.getAttribute('href') ?? '';
  const svg = href.startsWith('data:image/svg+xml,')
    ? new DOMParser().parseFromString(decodeURIComponent(href.slice(href.indexOf(',') + 1)), 'image/svg+xml') : null;
  return { title: document.title, icon: svg?.querySelector('title')?.textContent ?? '',
    paths: svg?.querySelectorAll('path').length ?? 0, external: svg?.querySelectorAll('text,image').length ?? 0,
    errors: svg?.querySelectorAll('parsererror').length ?? 1 };
})())");
    script.report["identity"] = identity;
    check(text(at(identity, "title")).starts_with("omui"),
          "the implementation is named omui in the browser tab");
    check(text(at(identity, "icon")) == "omui" && at(identity, "paths") == 5 &&
              at(identity, "external") == 0 && at(identity, "errors") == 0,
          "the tab icon is the generated O and inner letters, with no font or image dependencies");

    // The keyboard's way in: Tab from the top of the page, then Enter.
    const auto key = [&](std::string_view name_of_key, std::string_view code, int virtual_key) {
        script.call("Input.dispatchKeyEvent", {{"type", "keyDown"},
                                               {"key", name_of_key},
                                               {"code", code},
                                               {"windowsVirtualKeyCode", virtual_key}});
        script.call("Input.dispatchKeyEvent", {{"type", "keyUp"},
                                               {"key", name_of_key},
                                               {"code", code},
                                               {"windowsVirtualKeyCode", virtual_key}});
    };
    script.value("document.activeElement && document.activeElement.blur && "
                 "document.activeElement.blur(), true");
    key("Tab", "Tab", 9);
    script.sleep(200);
    const json first =
        script.value(R"((() => { const a = document.activeElement, r = a.getBoundingClientRect();
  return { omui: a.getAttribute('data-omui'), text: a.textContent, top: r.top, height: r.height }; })())");
    script.report["first_stop"] = first;
    check(text(at(first, "omui")) == "skip",
          std::format("the first Tab stop is the skip link ({})", at(first, "omui").is_null()
                                                                      ? text(at(first, "text"))
                                                                      : text(at(first, "omui"))));
    check(number(at(first, "top")) >= 0 && number(at(first, "height")) > 0, "seen while focused");
    key("Enter", "Enter", 13);
    script.sleep(800);
    const json after = script.value("(() => { const a = document.activeElement; return { id: "
                                    "a.getAttribute('data-omui-id'), tag: a.tagName }; })()");
    const json focused = script.value(
        "window.omui.find({ state: ['focused'] }).then((r) => { const d = r.data; const list = "
        "Array.isArray(d) ? d : (d?.nodes ?? []); return list.map((n) => n.id); })");
    script.report["after_skip"] = {{"page", after}, {"program", focused}};
    const auto in_main = [](const json& id) {
        return id.is_string() && id.get<std::string>().starts_with("main/body/view/");
    };
    check(in_main(at(after, "id")),
          std::format("Enter: the page's focus in the main landmark ({})",
                      at(after, "id").is_null() ? text(at(after, "tag")) : text(at(after, "id"))));
    check(focused.is_array() && std::ranges::any_of(focused, in_main),
          std::format("and the program's ({})",
                      focused.is_null() ? "undefined" : cdp::stringify(focused)));

    // Follow the visible Explore link and then open its brand program with real clicks.
    const auto click_node = [&](const std::string& id) {
        script.value("window.omui.act(" + q(id) + ",'scroll_into_view')");
        script.sleep(400);
        const json node = script.value(
            "window.omui.snapshot({root:" + q(id) +
            ",depth:0}).then(r => (r.data?.nodes ?? []).find(n => n.id === " + q(id) + "))");
        const json& bounds = at(node, "bounds");
        if (!bounds.is_array() || bounds.size() != 4)
            return false;
        const double x = number(bounds[0]), y = number(bounds[1]), width = number(bounds[2]),
                     height = number(bounds[3]);
        for (const std::string type : {"mouseMoved", "mousePressed", "mouseReleased"})
            script.call("Input.dispatchMouseEvent", {{"type", type},
                                                     {"x", x + width / 2},
                                                     {"y", y + height / 2},
                                                     {"button", "left"},
                                                     {"clickCount", 1}});
        script.sleep(1200);
        return true;
    };
    click_node("main/bar/nav/to_explore");
    click_node("main/body/view/explore/page/explore/works/brand/open");
    const json study = script.value(
        R"(window.omui.snapshot({root:'main/body/view/brand/page/identity/title',depth:0}).then(r =>
  (r.data?.nodes ?? []).find(n => n.id.endsWith('/identity/title'))?.name ?? ''))");
    check(text(study) == "A mark. Made here.",
          "Explore opens the live brand study with real clicks");
    click_node("main/bar/mark");
    check(
        truthy(script.value(
            R"(document.querySelector('[data-omui-id="main/body/view/home/page/landing"]') !== null)")),
        "the brand link returns home");

    if (const auto error = script.take())
        script.report["error"] =
            *error;  // a page that stopped answering (page.mjs: the script threw)
    script.report["ok"] = script.all_ok() && !script.report.contains("error");
    page.close();
    (*server)->close();
    return script.report;
}

// --- structure (site-structure.mjs) ---
// Information architecture and the static-to-program handoff: routes without JavaScript, wasm held
// while a person clicks a static link, the handoff and its focus, Back and Forward in one document,
// the language of the missing page, the scroll offset kept across the handoff.

json structure(const Context& context) {
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    auto launched = open(context, 1280, 800);
    if (!launched)
        return failed_report(launched.error());
    auto& page = **launched;
    cdp::Script script;
    script.page = &page;
    const auto check = [&](bool ok, std::string_view what,
                           std::optional<json> detail = std::nullopt) {
        return script.check(ok, what, std::move(detail));
    };
    const auto until = [&](std::string_view expression, int timeout = 30000) {
        return script.until_count(expression, timeout / 100, 100);
    };
    std::string held;
    script.guard("browser scenario completed", [&] {
        script.call("Emulation.setScriptExecutionDisabled", {{"value", true}});
        for (const auto& [route, expected, language] :
             std::vector<std::array<std::string, 3>>{{"docs/features", "features", "en"},
                                                     {"zh/brand", "brand", "zh-CN"},
                                                     {"explore", "explore", "en"}}) {
            script.call("Page.navigate", {{"url", url + "#/" + route}});
            script.sleep(400);
            const json result = script.value(R"((() => {
      const root=document.getElementById('omui-static');
      return Array.from(root.querySelectorAll('section[data-place]')).filter(n=>getComputedStyle(n).display!=='none').map(n=>({place:n.dataset.place,lang:n.lang}));
    })())");
            check(result.is_array() && result.size() == 1 &&
                      text(dig(result, 0, "place")) == expected &&
                      text(dig(result, 0, "lang")) == language,
                  "without JavaScript: " + route + " shows only its own content", result);
            const json own_font = script.value(
                R"(document.fonts.ready.then(()=>[...document.fonts].some(f=>f.family.replace(/['"]/g,'')==='Inter'&&f.status==='loaded')))");
            check(truthy(own_font),
                  "without JavaScript: " + route + " loads its own body font through noscript");
        }
        script.call("Emulation.setScriptExecutionDisabled", {{"value", false}});
        // Hold wasm itself; the real static links must work throughout this wait.
        page.on([&](const std::string& method, const json& params) {
            if (method != "Fetch.requestPaused")
                return;
            // The program's second half (.deferred.wasm, D5) is not what is held.
            static const std::regex deferred(R"(\.deferred\.wasm(?:\?|$))");
            if (std::regex_search(text(dig(params, "request", "url")), deferred)) {
                page.post("Fetch.continueRequest", {{"requestId", at(params, "requestId")}});
                return;
            }
            held = text(at(params, "requestId"));
        });
        const json wasm_pattern = {
            {"patterns", json::array({{{"urlPattern", "*.wasm*"}, {"requestStage", "Request"}}})}};
        script.call("Fetch.enable", wasm_pattern);
        script.call("Page.navigate", {{"url", url}});
        for (int index = 0; held.empty() && index < 100 && !script.thrown(); ++index)
            script.sleep(100);
        check(!held.empty() && truthy(script.value("!document.getElementById('omui-static').hidden "
                                                   "&& !window.omuiHasPresented")),
              "static content stays visible while wasm is pending");
        const json bounds = script.value(
            R"((() => { const a=document.querySelector('#omui-static nav[data-locale=en] a[data-omui-target="main/bar/nav/to_docs"]'); const r=a.getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2}; })())");
        if (truthy(bounds))
            for (const std::string type : {"mouseMoved", "mousePressed", "mouseReleased"})
                script.call("Input.dispatchMouseEvent", {{"type", type},
                                                         {"x", at(bounds, "x")},
                                                         {"y", at(bounds, "y")},
                                                         {"button", "left"},
                                                         {"clickCount", 1}});
        script.sleep(300);
        check(truthy(
                  script.value("location.hash==='#/docs' && !document.querySelector('#omui-static "
                               "section[data-place=docs][lang=en]').hidden")),
              "a real click changes the readable page before wasm starts");
        if (!held.empty())
            script.call("Fetch.continueRequest", {{"requestId", held}});
        script.call("Fetch.disable");
        check(
            until(
                R"(window.omuiStaticHandedOff && document.querySelector('[data-omui-id="main/body/view/docs/page/read/search"]'))"),
            "handoff waits for the requested live page and a presented frame");
        const json state =
            script.value(R"(({ staticHidden:document.getElementById('omui-static').hidden,
    mirrorInert:document.querySelector('[data-omui=mirror]').inert,
    focus:document.activeElement.getAttribute('data-omui-id') }))");
        check(truthy(at(state, "staticHidden")) && !truthy(at(state, "mirrorInert")) &&
                  text(at(state, "focus")) == "main/bar/nav/to_docs",
              "handoff has one accessible surface and preserves the focused navigation", state);
        const json origin = script.value("performance.timeOrigin");
        for (const auto& [route, place] :
             std::vector<std::pair<std::string, std::string>>{{"zh/docs/features", "features"},
                                                              {"zh/brand", "brand"},
                                                              {"zh/missing-page", "not_found"}}) {
            script.value("location.hash=" + q("/" + route) + "; true");
            check(until(R"(document.querySelector('[data-omui-id="main/body/view/)" + place +
                        R"(/page"]'))"),
                  "the live route opens " + route);
        }
        script.value("history.back(); true");
        check(
            until(
                R"(location.hash==='#/zh/brand' && document.querySelector('[data-omui-id="main/body/view/brand/page"]'))"),
            "Back restores an alias without replacing the history entry");
        script.value("history.forward(); true");
        check(
            until(
                R"(location.hash==='#/zh/missing-page' && document.querySelector('[data-omui-id="main/body/view/not_found/page"]'))"),
            "Forward restores the missing-page state");
        check(script.value("performance.timeOrigin") == origin,
              "history navigation keeps the same document and runtime");
        script.value("window.omui.act('main/bar/lang','activate')");
        check(
            until(
                R"(location.hash==='#/missing-page' && document.querySelector('[data-omui-id="main/body/view/not_found/page/missing/title"]')?.getAttribute("aria-label")==="This page isn't here.")"),
            "changing the missing page language keeps its address and content consistent");
        held.clear();
        script.call("Network.enable");
        script.call("Network.setCacheDisabled", {{"cacheDisabled", true}});
        script.call("Fetch.enable", wasm_pattern);
        script.call("Page.navigate", {{"url", url + "?handoff-scroll=1#/docs/features"}});
        for (int index = 0; held.empty() && index < 100 && !script.thrown(); ++index)
            script.sleep(100);
        until("!window.omuiStaticHandedOff && document.querySelector('#omui-static "
              "section[data-place=features][lang=en]')?.hidden===false",
              10000);
        script.value("document.fonts.ready.then(() => true)");
        script.value("document.getElementById('omui-static').scrollTop=360; true");
        const json scrolled = script.value("document.getElementById('omui-static').scrollTop");
        if (!held.empty())
            script.call("Fetch.continueRequest", {{"requestId", held}});
        script.call("Fetch.disable");
        until("window.omuiStaticHandedOff");
        const json restored =
            script.value("window.omui.snapshot({root:'main/body/view/features',depth:0}).then(r => "
                         "(r.data?.nodes ?? [])[0]?.value?.y)");
        check(number(scrolled) > 100 && std::abs(number(restored) - number(scrolled)) < 1,
              "a reader scrolling during loading keeps the scroll offset on handoff",
              json{{"scrolled", scrolled}, {"restored", restored}});
    });
    page.close();
    (*server)->close();
    script.report["ok"] = script.all_ok();
    return script.report;
}

// --- the component catalog (site-catalog.mjs) ---
// Static deep links, pointer navigation, real state, history, clipboard feedback and a phone's live
// detail.

json catalog(const Context& context) {
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    auto launched = open(context, 1280, 800);
    if (!launched)
        return failed_report(launched.error());
    auto& page = **launched;
    cdp::Script script;
    script.page = &page;
    const fs::path output = context.root / "target/site-catalog";
    const auto check = [&](bool ok, std::string_view what,
                           std::optional<json> detail = std::nullopt) {
        return script.check(ok, what, std::move(detail));
    };
    const auto until = [&](const std::string& expression, int timeout = 30000) {
        return script.until_count(expression, timeout / 100, 100);
    };
    const std::string root = "main/body/view/components/page/gallery";
    const std::string overview = root + "/overview";
    const auto detail = [&](std::string_view id) { return root + "/detail/" + std::string(id); };
    const auto node = [](const std::string& id) {
        return R"(document.querySelector('[data-omui-id=")" + id + R"("]'))";
    };
    const auto name = [&](const std::string& id) {
        return "(" + node(id) + "?.getAttribute('aria-label') ?? " + node(id) + "?.textContent)";
    };
    const auto act = [&](const std::string& id, std::string_view action,
                         json args = json::object()) {
        return script.value("window.omui.act(" + q(id) + "," + q(action) +
                            ",{args:" + cdp::stringify(args) + "})");
    };
    const auto pointer = [&](const std::string& id) {
        // Layout and disclosure decide where an action lives. Reach the actual control before
        // sending physical pointer input; do not pin scroll offsets.
        act(id, "scroll_into_view");
        script.sleep(250);
        const auto bounds =
            cdp::box(script.value("window.omui.snapshot({root:" + q(id) +
                                  ",depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)"));
        if (script.thrown())
            return;
        if (!bounds)
            return script.raise("Invalid bounds: " + id);
        const auto [x, y, width, height] = *bounds;
        for (const std::string type : {"mouseMoved", "mousePressed", "mouseReleased"})
            script.dispatch("Input.dispatchMouseEvent", {{"type", type},
                                                         {"x", x + width / 2},
                                                         {"y", y + height / 2},
                                                         {"button", "left"},
                                                         {"clickCount", 1}});
    };
    const auto shot = [&](std::string_view file) {
        script.sleep(400);
        script.screenshot(output / (std::string(file) + ".png"), {{"format", "png"}}, false);
    };
    script.guard("browser catalog scenario completed", [&] {
        std::error_code error;
        fs::create_directories(output, error);
        script.call("Emulation.setScriptExecutionDisabled", {{"value", true}});
        for (const std::string route :
             {"components/text-box", "zh/components/data-grid", "zh/components/code_editor"}) {
            script.call("Page.navigate", {{"url", url + "#/" + route}});
            script.sleep(300);
            const json shown =
                script.value("Array.from(document.querySelectorAll('#omui-static "
                             "section[data-place]')).filter(n=>getComputedStyle(n).display!=='none'"
                             ").map(n=>({id:n.id,title:n.querySelector('h1')?.textContent}))");
            check(shown.is_array() && shown.size() == 1 && truthy(dig(shown, 0, "title")) &&
                      text(dig(shown, 0, "id")).contains("components/"),
                  "static component destination: " + route, shown);
        }
        script.call("Emulation.setScriptExecutionDisabled", {{"value", false}});
        script.call("Page.navigate", {{"url", url + "?catalog=1#/components/text-box"}});
        const bool handed_off = until("window.omuiStaticHandedOff && " +
                                      node(detail("text_box") + "/stage/specimen/variants/name"));
        check(handed_off, "a direct detail hands off to its own live specimen");
        if (!handed_off)
            return script.raise("The requested live detail did not appear");
        const json origin = script.value("performance.timeOrigin");
        act(detail("text_box") + "/stage/specimen/variants/name", "set_value", {{"value", "Lin"}});
        act(detail("text_box") + "/panes/code", "activate");
        const std::string source_name = name(detail("text_box") + "/code/scroll/source");
        check(until(source_name + "?.includes('Lin')"),
              "builder reference follows the real input value");
        for (const auto& [language, prefix] :
             std::vector<std::pair<std::string, std::string>>{{"ts", "textBox("},
                                                              {"cpp", "text_box("}}) {
            act(detail("text_box") + "/code/language/" + language, "activate");
            check(until(source_name + "?.startsWith(" + q(prefix) + ") && " + source_name +
                        "?.includes('Lin')"),
                  language + " expression describes the same edited model");
        }
        act(detail("text_box") + "/stage/actions/reset", "activate");
        check(until(source_name + "?.includes('Ada')"),
              "reset restores the specimen and reference");
        script.call("Browser.grantPermissions",
                    {{"origin", url.substr(0, url.size() - 1)},
                     {"permissions", {"clipboardReadWrite", "clipboardSanitizedWrite"}}});
        pointer(detail("text_box") + "/code/copy");
        check(until(name(detail("text_box") + "/code/copied") + "==='Expression copied'"),
              "pointer copy shows completion feedback");
        const json source = script.value(source_name);
        // The host's writeText is asynchronous; label feedback is not its completion.
        const bool copied =
            truthy(source) &&
            until("navigator.clipboard.readText().then(t=>t===" + q(text(source)) + ")");
        check(copied, "the permitted browser clipboard contains the displayed expression",
              copied ? std::nullopt
                     : std::optional<json>(
                           json{{"source", source},
                                {"clipboard",
                                 script.value(
                                     "navigator.clipboard.readText().catch(e=>String(e))")}}));
        act("main/body/view/components", "scroll_to", {{"x", 0}, {"y", 0}});
        // All components: the studio's canvas (every component live on it); Back: this component's
        // page again.
        act(root + "/detail/directory/all", "activate");
        check(until("location.hash==='#/components' && " +
                    node("main/body/view/components/page/wall/canvas")),
              "All components returns to the studio canvas");
        script.value("history.back(); true");
        check(until("location.hash==='#/components/text-box' && " +
                    node(detail("text_box") + "/stage/specimen")),
              "Back returns to the component page");
        // The directory beside it: filtered, searched, recovered; a choice opens that component's
        // page.
        check(until(name(overview + "/results/count") + "==='8 / 36'"),
              "the page offers eight directory choices beside its live specimen");
        const json results = script.value("window.omui.snapshot({root:" + q(overview + "/results") +
                                          ",depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)");
        check(results.is_array() && number(at(results, 3)) <= 331,
              "desktop component choices stay inside a bounded scroll region", results);
        shot("overview-en-desktop");
        act(overview + "/filter/category", "set_value", {{"value", "editing"}});
        check(until(name(overview + "/results/count") + "==='2 / 36'"),
              "the editing category contains its two components");
        act(overview + "/filter/search", "set_value", {{"value", "no-such-part"}});
        check(until(node(overview + "/results/empty")),
              "an empty filtered search offers a recovery path");
        check(truthy(script.value(node(detail("text_box") + "/stage/specimen"))),
              "an empty directory preserves the current real specimen");
        act(overview + "/filter/clear", "activate");
        check(until(name(overview + "/results/count") + "==='8 / 36'"),
              "reset restores the selected overview");
        act(overview + "/filter/search", "set_value", {{"value", "button"}});
        check(until(name(overview + "/results/count") + "==='1 / 36'"),
              "search finds a component by name");
        pointer(overview + "/results/r0/button/heading/open");
        check(until("location.hash==='#/components/button' && " +
                    node(detail("button") + "/stage/specimen/variants/primary")),
              "a real directory click opens the page of that component");
        pointer(detail("button") + "/stage/actions/reset");
        check(until(name(detail("button") + "/stage/specimen/variants/primary") + "==='Save'"),
              "reset establishes the real initial model");
        pointer(detail("button") + "/stage/specimen/variants/primary");
        check(until(name(detail("button") + "/stage/specimen/variants/primary") + "==='Saved 1'"),
              "the sample button performs its own action");
        pointer(detail("button") + "/stage/actions/reset");
        check(until(name(detail("button") + "/stage/specimen/variants/primary") + "==='Save'"),
              "a pointer can reset the specimen");
        shot("button-en-desktop");
        script.value("history.back(); true");
        check(until("location.hash==='#/components/text-box' && " +
                    node(detail("text_box") + "/stage/specimen")),
              "Back returns to the component it came from");
        script.value("history.forward(); true");
        check(until("location.hash==='#/components/button' && " +
                    node(detail("button") + "/stage/specimen/variants/primary")),
              "Forward restores the chosen component");
        check(script.value("performance.timeOrigin") == origin,
              "component history keeps the runtime alive");
        script.call("Emulation.setDeviceMetricsOverride",
                    {{"width", 390}, {"height", 844}, {"deviceScaleFactor", 1}, {"mobile", true}});
        script.value("location.hash='/zh/components/text-box'; true");
        check(until(name(detail("text_box") + "/title") + "==='文本框'"),
              "Chinese phone opens the same component address");
        shot("text-box-zh-phone");
        const json inside = script.value(
            "window.omui.snapshot({root:" + q(detail("text_box")) +
            "}).then(r=>{const "
            "controls=(r.data?.nodes||[]).filter(n=>['button','text_box'].includes(n.role)&&n."
            "bounds&&!n.state?.includes('hidden')&&!n.state?.includes('offscreen'));return "
            "controls.length>2&&controls.every(n=>n.bounds[0]>=-0.5&&n.bounds[0]+n.bounds[2]<=390."
            "5)})");
        check(truthy(inside), "the phone detail keeps its controls inside the viewport");
        check(!truthy(script.value(node(overview + "/filter/search"))),
              "the phone directory starts collapsed");
        pointer(overview + "/browse");
        check(until(node(overview + "/filter/search")),
              "a phone reveals its searchable directory on demand");
        const json phone_results =
            script.value("window.omui.snapshot({root:" + q(overview + "/results") +
                         ",depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)");
        check(phone_results.is_array() && number(at(phone_results, 3)) <= 231,
              "the phone directory also has a bounded height", phone_results);
        act(overview + "/filter/search", "set_value", {{"value", "text_box"}});
        check(until(name(overview + "/results/count") + "==='1 / 36'"),
              "the phone searches the full catalog");
        pointer(overview + "/results/r0/text_box/heading/open");
        // E2d: the directory chooses a component's own page (the studio shows them all; the library
        // list under it is gone) — on a phone as on a desktop.
        check(until("location.hash==='#/zh/components/text-box' && " +
                    node(detail("text_box") + "/stage/specimen/variants/name") + " && !" +
                    node(overview + "/filter/search")),
              "choosing a phone specimen opens its own page and closes the directory");
        check(until(node(detail("text_box") + "/panes/code")),
              "the phone component page carries its code and deeper controls");
        shot("text-box-zh-phone-detail");
        pointer(detail("text_box") + "/stage/actions/details");
        check(until("location.hash==='#/zh/components' && " +
                    node("main/body/view/components/page/wall/input") + " && !" +
                    node(detail("text_box") + "/panes/code")),
              "Back to preview returns the phone to the live bands of every component, its deeper "
              "controls put away");
    });
    page.close();
    (*server)->close();
    script.report["ok"] = script.all_ok();
    return script.report;
}

// --- the release workspace (site-workspace.mjs) ---
// Real pointer, keyboard text entry and wheel scrolling beside semantic observations: editing, undo
// and redo, a Markdown clipboard, both views reading one model, language and phone layout, removal
// and reset reversible.

json workspace(const Context& context) {
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    auto launched = open(context, 1280, 800);
    if (!launched)
        return failed_report(launched.error());
    auto& page = **launched;
    cdp::Script script;
    script.page = &page;
    const fs::path output = context.root / "target/site-workspace";
    const auto check = [&](bool ok, std::string_view what) { return script.check(ok, what); };
    const auto until = [&](const std::string& expression) {
        return script.until_count(expression, 300, 100);
    };
    const std::string hero = "main/body/view/release/page/hero", board = hero + "/demo/specimen",
                      motion = "main/body/view/release/page/motion/shown";
    const auto node = [](const std::string& id) {
        return R"(document.querySelector('[data-omui-id=")" + id + R"("]'))";
    };
    const auto name = [&](const std::string& id) {
        return "(" + node(id) + "?.getAttribute('aria-label') ?? " + node(id) + "?.textContent)";
    };
    const auto act = [&](const std::string& id, std::string_view action,
                         json args = json::object()) {
        return script.value("window.omui.act(" + q(id) + "," + q(action) +
                            ",{args:" + cdp::stringify(args) + "})");
    };
    const auto into_view =
        [&](const std::string& id,
            const std::string& viewport =
                "main/body/view/release") -> std::optional<std::array<double, 4>> {
        // The control's own semantic action first, as a reader's tab or a link does: over a canvas
        // (the motion page's studio, E2d) the wheel moves the camera, not the page; the wheel stays
        // the fallback for scroll-linked layouts.
        act(id, "scroll_into_view");
        script.sleep(250);
        for (int index = 0; index < 20 && !script.thrown(); ++index) {
            const json both = script.value("Promise.all([" + q(id) + "," + q(viewport) +
                                           "].map(root=>window.omui.snapshot({root,depth:0}).then("
                                           "r=>r.data?.nodes?.[0]?.bounds)))");
            const json& bounds = at(both, 0);
            const json& view = at(both, 1);
            // A preceding click can mount this control on the next rendered frame.
            if (!bounds.is_array() || !view.is_array()) {
                script.sleep(100);
                continue;
            }
            const auto box = cdp::box(bounds);
            if (!box) {
                script.raise("Invalid bounds: " + id);
                return std::nullopt;
            }
            const double vy = number(at(view, 1)), vh = number(at(view, 3));
            if ((*box)[1] >= vy && (*box)[1] + (*box)[3] <= vy + vh)
                return box;
            // A scroll-linked layout may move again after scroll_into_view. A person's wheel brings
            // the target to the middle of the live viewport.
            script.call("Input.dispatchMouseEvent",
                        {{"type", "mouseWheel"},
                         {"x", number(at(view, 0)) + number(at(view, 2)) / 2},
                         {"y", vy + vh / 2},
                         {"deltaX", 0},
                         {"deltaY", (*box)[1] + (*box)[3] / 2 - vy - vh / 2}});
            script.sleep(300);
        }
        script.raise("The control did not enter the live viewport: " + id);
        return std::nullopt;
    };
    const auto click = [&](const std::string& id,
                           const std::string& viewport = "main/body/view/release") {
        const auto bounds = into_view(id, viewport);
        if (!bounds)
            return;
        const auto [x, y, width, height] = *bounds;
        for (const std::string type : {"mouseMoved", "mousePressed", "mouseReleased"})
            script.dispatch("Input.dispatchMouseEvent", {{"type", type},
                                                         {"x", x + width / 2},
                                                         {"y", y + height / 2},
                                                         {"button", "left"},
                                                         {"clickCount", 1}});
    };
    const auto shot = [&](std::string_view file) {
        script.sleep(400);
        script.screenshot(output / (std::string(file) + ".png"), {{"format", "png"}}, false);
    };
    const auto ctrl_a = [&] {
        for (const std::string type : {"keyDown", "keyUp"})
            script.call("Input.dispatchKeyEvent", {{"type", type},
                                                   {"key", "a"},
                                                   {"code", "KeyA"},
                                                   {"windowsVirtualKeyCode", 65},
                                                   {"modifiers", 2}});
    };
    script.guard("browser workspace scenario completed", [&] {
        std::error_code error;
        fs::create_directories(output, error);
        script.call("Page.navigate", {{"url", url + "#/scenes/release"}});
        const bool ready = until("window.omuiStaticHandedOff && " + node(board + "/add/title"));
        check(ready, "the release route opens the live workspace");
        if (!ready)
            return script.raise("Workspace did not become ready");
        check(!truthy(script.value(node(hero + "/demo/renderings"))),
              "technical views start closed");
        shot("home-en-desktop");
        click(board + "/add/title");
        if (!script.thrown() &&
            !until(R"(document.activeElement?.matches('textarea[data-omui="input"]'))"))
            return script.raise("The text input did not acquire browser focus");
        script.call("Input.insertText", {{"text", "Review the keyboard path"}});
        check(until("window.omui.snapshot({root:" + q(board + "/add/title") +
                    ",depth:0}).then(r=>r.data?.nodes?.[0]?.value==='Review the keyboard path')"),
              "real text input reaches the draft");
        click(board + "/add/submit");
        check(until(name(board + "/items/task-1/done") + "==='Review the keyboard path'"),
              "a pointer adds a task with its own stable id");
        check(until(name(motion + "/shape/task-1/title") + "==='Review the keyboard path'"),
              "the motion scene reads that same task");
        click(board + "/items/task-1/edit");
        check(until("window.omui.snapshot({root:" + q(board + "/editor/title") +
                    ",depth:0}).then(r=>r.data?.nodes?.[0]?.state?.includes('focused'))"),
              "editing focuses the real title field");
        if (!script.thrown() &&
            !until(R"(document.activeElement?.matches('textarea[data-omui="input"]'))"))
            return script.raise("The editor did not acquire browser focus");
        ctrl_a();
        script.call("Input.insertText", {{"text", "Review keyboard and focus"}});
        click(board + "/editor/actions/save");
        check(until(name(board + "/items/task-1/done") + "==='Review keyboard and focus'"),
              "rename updates the live task");
        click(board + "/history/undo");
        check(until(name(board + "/items/task-1/done") + "==='Review the keyboard path'"),
              "workspace undo restores the previous title");
        click(board + "/history/redo");
        check(until(name(board + "/items/task-1/done") + "==='Review keyboard and focus'"),
              "the actual redo control preserves and replays the edit");
        script.call("Browser.grantPermissions",
                    {{"origin", url.substr(0, url.size() - 1)},
                     {"permissions", {"clipboardReadWrite", "clipboardSanitizedWrite"}}});
        click(board + "/history/copy");
        check(
            until(
                R"(navigator.clipboard.readText().then(t=>t.includes('- [ ] Review keyboard and focus\n')))"),
            "the permitted clipboard receives a usable Markdown checklist");
        click(board + "/items/task-1/done");
        check(until(name(motion + "/shape/task-1/state") + "==='Complete'"),
              "a pointer completion updates the other view");
        // Entering this section can itself seek the layout. Sample its chosen target only after
        // scrolling, before testing the manual override.
        into_view(motion + "/controls/change");
        script.sleep(300);
        const json before = script.value("window.omui.snapshot({root:" + q(motion + "/shape") +
                                         "}).then(r=>r.data.nodes.map(n=>n.id).sort())");
        const json mode = script.value(name(motion + "/controls/change"));
        click(motion + "/controls/change");
        check(until(name(motion + "/controls/change") +
                    "!==" + (mode.is_null() ? std::string("undefined") : cdp::stringify(mode))),
              "a click changes layout without requiring a scroll gesture");
        const json after = script.value("window.omui.snapshot({root:" + q(motion + "/shape") +
                                        "}).then(r=>r.data.nodes.map(n=>n.id).sort())");
        check(cdp::stringify(before) == cdp::stringify(after),
              "layout changes keep all task node ids");
        check(text(script.value(name(motion + "/shape/task-1/title"))) ==
                  "Review keyboard and focus",
              "the edited title survives the layout change");
        shot("motion-en-desktop");
        act("main/bar/nav/to_docs", "activate");
        until(node("main/body/view/docs/page"));
        // The brand returns to the landing. The release workspace is a separate working tool under
        // Motion; open it without reloading the runtime.
        act("main/bar/nav/to_explore", "activate");
        const std::string release = "main/body/view/explore/page/explore/works/release/open";
        const bool tool_ready = until(node(release));
        check(tool_ready, "the motion page exposes the release workspace");
        if (!tool_ready)
            return script.raise("The release workspace destination did not appear");
        click(release, "main/body/view/explore");
        const bool restored =
            until("location.hash==='#/scenes/release' && " + name(board + "/items/task-1/done") +
                  "==='Review keyboard and focus'");
        check(restored, "visiting documentation preserves the workspace data");
        if (!restored)
            return script.raise(
                "Returning to the release route did not restore the edited workspace");
        script.call("Emulation.setDeviceMetricsOverride",
                    {{"width", 390}, {"height", 844}, {"deviceScaleFactor", 1}, {"mobile", true}});
        act("main/bar/lang", "activate");
        check(until(name(board + "/items/docs/done") + "==='文档已审阅'"),
              "Chinese translates the seeded tasks");
        check(text(script.value(name(board + "/items/task-1/done"))) == "Review keyboard and focus",
              "changing language preserves user text");
        act("main/body/view/release", "scroll_to", {{"x", 0}, {"y", 0}});
        shot("home-zh-phone");
        const json layout = script.value(
            "window.omui.snapshot({root:" + q(board) +
            "}).then(r=>r.data.nodes.filter(n=>n.role==='button'&&n.bounds&&!n.state?.includes('"
            "hidden')&&!n.state?.includes('offscreen')).map(n=>({id:n.id,b:n.bounds})))");
        if (script.thrown())
            return;
        if (!layout.is_array())
            return script.raise("The phone workspace snapshot was unavailable: " +
                                cdp::stringify(layout));
        check(layout.size() > 3 && std::ranges::all_of(layout,
                                                       [](const json& item) {
                                                           const json& bounds = at(item, "b");
                                                           return number(at(bounds, 0)) >= -0.5 &&
                                                                  number(at(bounds, 0)) +
                                                                          number(at(bounds, 2)) <=
                                                                      390.5;
                                                       }),
              "phone workspace controls fit the viewport");
        std::vector<json> short_labels;
        for (const auto& item : layout)
            if (text(at(item, "id")).ends_with("/edit") ||
                text(at(item, "id")).ends_with("/add/submit"))
                short_labels.push_back(item);
        check(!short_labels.empty() && std::ranges::all_of(short_labels,
                                                           [](const json& item) {
                                                               return number(dig(item, "b", 3)) <
                                                                      45;
                                                           }),
              "short Chinese action labels stay on one line");
        click(board + "/items/task-1/edit");
        click(board + "/editor/actions/remove");
        check(until("!" + node(board + "/items/task-1/done") + " && !" +
                    node(motion + "/shape/task-1/title")),
              "removing a task removes both views");
        click(board + "/history/undo");
        check(until(name(board + "/items/task-1/done") + "==='Review keyboard and focus'"),
              "undo restores the removed task with the same id");
        click(board + "/actions/reset");
        check(until("!" + node(board + "/items/task-1/done")), "reset returns to the seed board");
        click(board + "/history/undo");
        check(until(name(board + "/items/task-1/done") + "==='Review keyboard and focus'"),
              "reset itself is reversible");
    });
    page.close();
    (*server)->close();
    script.report["ok"] = script.all_ok();
    return script.report;
}

// --- the reaction workspace (site-reactions.mjs) ---
// Real browser input and intermediate rendered frames of the shared Inspector workspace: actual
// browser pixels and input; offline process samples do not certify real-time performance.

json reactions(const Context& context) {
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    auto launched = open(context, 1280, 1000);
    if (!launched)
        return failed_report(launched.error());
    auto& page = **launched;
    cdp::Script script;
    script.page = &page;
    script.report = {{"browser", page.version()},
                     {"checks", json::array()},
                     {"samples", json::array()},
                     {"scope", "Actual browser pixels and input; offline process samples do not "
                               "certify real-time performance."}};
    const fs::path output = context.root / "target/site-reactions";
    const auto check = [&](bool ok, std::string_view what) { return script.check(ok, what); };
    const std::string root = "main/body/view/reactions/page/cellular",
                      viewport = "main/body/view/reactions";
    const auto node = [](const std::string& id) {
        return R"(document.querySelector('[data-omui-id=")" + id + R"("]'))";
    };
    const auto until = [&](const std::string& expression) {
        return script.until_count(expression, 300, 100);
    };
    const auto rendered = [&] {
        return script.value("new "
                            "Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(()=>"
                            "resolve(true))))");
    };
    const auto invoke = [&](std::string_view operation, json args = json::object()) -> json {
        const json reply =
            script.value("window.omui.invoke(" + q("cellular." + std::string(operation)) + "," +
                         cdp::stringify(args) + ")");
        if (script.thrown())
            return nullptr;
        if (!truthy(reply) || at(reply, "exitCode") != 0) {
            script.raise(std::string(operation) + ": " +
                         (reply.is_null() ? "undefined" : cdp::stringify(reply)));
            return nullptr;
        }
        rendered();
        return at(reply, "data");
    };
    const auto bounds = [&](const std::string& id) {
        return script.value("window.omui.snapshot({root:" + q(id) +
                            ",depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)");
    };
    const auto into_view = [&](const std::string& id) -> json {
        for (int index = 0; index < 20 && !script.thrown(); ++index) {
            const json box = bounds(id), view = bounds(viewport);
            if (!box.is_array() || !view.is_array()) {
                script.sleep(100);
                continue;
            }
            if (!std::ranges::all_of(box, [](const json& value) {
                    return std::isfinite(number(value));
                })) {
                script.raise("Invalid bounds: " + id);
                return nullptr;
            }
            if (number(box[1]) >= number(view[1]) &&
                number(box[1]) + number(box[3]) <= number(view[1]) + number(view[3]))
                return box;
            script.call("Input.dispatchMouseEvent",
                        {{"type", "mouseWheel"},
                         {"x", number(view[0]) + number(view[2]) / 2},
                         {"y", number(view[1]) + number(view[3]) / 2},
                         {"deltaX", 0},
                         {"deltaY", number(box[1]) + number(box[3]) / 2 - number(view[1]) -
                                        number(view[3]) / 2}});
            script.sleep(200);
        }
        script.raise("Control did not enter viewport: " + id);
        return nullptr;
    };
    const auto point = [&](double x, double y) {
        for (const std::string type : {"mouseMoved", "mousePressed", "mouseReleased"})
            script.dispatch("Input.dispatchMouseEvent", {{"type", type},
                                                         {"x", x},
                                                         {"y", y},
                                                         {"button", "left"},
                                                         {"clickCount", 1}});
        rendered();
    };
    const auto click = [&](const std::string& id) {
        const json box = into_view(id);
        if (script.thrown())
            return;
        point(number(box[0]) + number(box[2]) / 2, number(box[1]) + number(box[3]) / 2);
    };
    const auto shot = [&](const std::string& file, const json& box = nullptr) -> json {
        rendered();
        json params = {{"format", "png"}};
        if (box.is_array())
            params["clip"] = {{"x", box[0]},
                              {"y", box[1]},
                              {"width", box[2]},
                              {"height", box[3]},
                              {"scale", 1}};
        const json answer = script.call("Page.captureScreenshot", params);
        if (script.thrown())
            return nullptr;
        const std::string data = text(dig(answer, "result", "data"));
        if (answer.contains("error") || data.empty()) {
            script.raise("Screenshot failed: " + cdp::stringify(at(answer, "error")));
            return nullptr;
        }
        const fs::path path = output / (file + ".png");
        cdp::write_base64(path, data);
        return {{"image", "target/site-reactions/" + file + ".png"},
                {"sha256", mlibs::hash::sha256_hex(cdp::from_base64(data))}};
    };
    script.guard("browser reaction scenario completed", [&] {
        std::error_code error;
        fs::create_directories(output, error);
        script.call("Page.navigate", {{"url", url + "#/explore/reactions"}});
        const bool ready = until("window.omuiStaticHandedOff && " + node(root + "/field"));
        check(ready, "the website opens the shared reaction workspace");
        if (!ready)
            return script.raise("Workspace did not become ready");
        check(!truthy(script.value(node(root + "/details"))),
              "rules and project details start closed");
        script.report["initial"] = invoke("project");
        script.report["opening"] = shot("opening-en");

        const json box = into_view(root + "/field");
        if (script.thrown())
            return;
        const double cell = std::min(number(box[2]) / 64, number(box[3]) / 36);
        point(number(box[0]) + (number(box[2]) - cell * 64) / 2 + 49.5 * cell,
              number(box[1]) + (number(box[3]) - cell * 36) / 2 + 27.5 * cell);
        const json human = invoke("snapshot");
        check(dig(invoke("inspect", {{"x", 49}, {"y", 27}}), "values", 1) == 1,
              "a real pointer paints the intended cell");
        invoke("reset");
        invoke("paint", {{"x", 49}, {"y", 27}, {"radius", 1}, {"brush", "spark"}});
        check(at(invoke("snapshot"), "state_hash") == at(human, "state_hash") && !script.thrown(),
              "Agent and pointer produce identical material state");

        invoke("reset");
        script.call("Input.dispatchMouseEvent", {{"type", "mouseMoved"}, {"x", 1}, {"y", 1}});
        const json canvas = into_view(root + "/field");
        for (int index = 0; index <= 24 && !script.thrown(); ++index) {
            if (index)
                invoke("advance", {{"count", 1}});
            const json state = invoke("snapshot");
            json sample = {{"frame", at(state, "frame")},           {"tick", at(state, "tick")},
                           {"state_hash", at(state, "state_hash")}, {"time_numerator", index},
                           {"time_denominator", at(state, "rate")}, {"bounds", canvas}};
            sample.update(shot("tick-" + zero_padded(index, 2), canvas));
            script.report["samples"].push_back(std::move(sample));
        }
        std::set<std::string> states, images;
        for (const auto& sample : script.report["samples"]) {
            states.insert(cdp::stringify(at(sample, "state_hash")));
            images.insert(text(at(sample, "sha256")));
        }
        check(states.size() == 25, "all 25 intermediate states differ");
        check(images.size() == 25, "all 25 actual canvas images differ");
        for (const int frame : {17, 8, 0}) {
            const json state = invoke("seek", {{"frame", frame}});
            const json image = shot("reverse-" + std::to_string(frame), canvas);
            const json& sample = at(script.report["samples"], static_cast<std::size_t>(frame));
            check(at(state, "state_hash") == at(sample, "state_hash") &&
                      at(image, "sha256") == at(sample, "sha256") && !script.thrown(),
                  "reverse seek reproduces state and actual canvas pixels at frame " +
                      std::to_string(frame));
        }

        click(root + "/more");
        click(root + "/details/config");
        if (!script.thrown() &&
            !until(R"(document.activeElement?.matches('textarea[data-omui="input"]'))"))
            return script.raise("Editor did not focus");
        for (const std::string type : {"keyDown", "keyUp"})
            script.call("Input.dispatchKeyEvent", {{"type", type},
                                                   {"key", "a"},
                                                   {"code", "KeyA"},
                                                   {"windowsVirtualKeyCode", 65},
                                                   {"modifiers", 2}});
        script.call("Input.insertText", {{"text", R"({"rate":18})"}});
        click(root + "/details/actions/apply");
        check(at(invoke("snapshot"), "rate") == 18,
              "real keyboard input edits and applies the rule configuration");

        click(root + "/transport/play");
        script.sleep(500);
        check(number(at(invoke("snapshot"), "tick")) > 0,
              "real Play input advances the live simulation");
        const json brand = bounds("main/bar/mark");
        if (script.thrown())
            return;
        point(number(at(brand, 0)) + number(at(brand, 2)) / 2,
              number(at(brand, 1)) + number(at(brand, 3)) / 2);
        if (!script.thrown() && !until("!" + node(root + "/field")))
            return script.raise("Navigation did not leave reaction workspace");
        const json paused = invoke("snapshot");
        script.sleep(300);
        check(!truthy(at(paused, "playing")) &&
                  at(invoke("snapshot"), "tick") == at(paused, "tick") && !script.thrown(),
              "leaving the route cancels ongoing playback");

        script.call("Emulation.setDeviceMetricsOverride",
                    {{"width", 390}, {"height", 844}, {"deviceScaleFactor", 1}, {"mobile", true}});
        script.value(R"(location.hash="#/zh/explore/reactions")");
        if (!script.thrown() && !until(node(root + "/field")))
            return script.raise("Chinese route did not open");
        check(at(invoke("snapshot"), "state_hash") == at(paused, "state_hash") && !script.thrown(),
              "navigation and language changes preserve the edited project");
        into_view(root + "/field");
        const json phone = bounds(root + "/field");
        check(number(at(phone, 0)) >= 0 && number(at(phone, 2)) > 0 &&
                  number(at(phone, 0)) + number(at(phone, 2)) <= 390.5,
              "the real resized phone canvas fits its viewport");
        script.report["phone"] = shot("phone-zh");
    });
    page.close();
    (*server)->close();
    script.report["ok"] = script.all_ok();
    return script.report;
}

}  // namespace omui::devtools::check::pages
