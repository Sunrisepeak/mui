// The site's fonts and loading in a real browser (moved from tools/ci/site-fonts.mjs,
// site-loading.mjs and site-font-sharing.mjs, D-206): first-frame glyph coverage against the pinned
// reference frames, the opening pictures with real progress and failure recovery, and static and
// runtime fonts sharing one download. Every threshold and expectation as the scripts had them.
module omui.devtools.check;

import std;
import mcpplibs.os;
import mlibs.hash;
import omui.devtools.cdp;
import :pages;
import :support;

namespace omui::devtools::check::pages {

namespace {

using cdp::at, cdp::dig, cdp::q, cdp::text, cdp::truthy, cdp::number, cdp::includes;

json failed_report(std::string message) {
    return {{"checks", json::array({{{"ok", false},
                                     {"what", "the browser started"},
                                     {"detail", std::move(message)}}})},
            {"ok", false}};
}

// The page's `var omuiFonts = […];` (the line's last `;` ends it): [name, full, languages, initial,
// …] each.
json fonts_of(std::string_view html) {
    const std::string_view marker = "var omuiFonts = ";
    const std::size_t start = html.find(marker);
    if (start == std::string_view::npos)
        return nullptr;
    std::string_view line = html.substr(start + marker.size());
    line = line.substr(0, line.find('\n'));
    const std::size_t end = line.rfind(';');
    if (end == std::string_view::npos)
        return nullptr;
    return json::parse(line.substr(0, end), nullptr, false);
}

const json& font_named(const json& fonts, std::string_view name) {
    static const json none;
    if (fonts.is_array())
        for (const auto& font : fonts)
            if (text(at(font, 0)) == name)
                return font;
    return none;
}

std::uint64_t size_of(const fs::path& path) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    return error ? 0 : size;
}

const std::regex& deferred_wasm() {
    static const std::regex pattern(R"(\.deferred\.wasm(?:\?|$))");
    return pattern;
}

const std::regex& any_wasm() {
    static const std::regex pattern(R"(\.wasm(?:\?|$))");
    return pattern;
}

void click_at(cdp::Script& script, double x, double y) {
    for (const std::string type : {"mouseMoved", "mousePressed", "mouseReleased"})
        script.call("Input.dispatchMouseEvent",
                    {{"type", type}, {"x", x}, {"y", y}, {"button", "left"}, {"clickCount", 1}});
}

}  // namespace

// --- first-frame fonts (site-fonts.mjs) ---
// The full Chinese face held to verify the initial glyphs, then real pixels compared: initial and
// complete glyphs identical, another language's face waiting until wanted, the reference replay
// waiting for its fonts, and all 59 browser reference frames matching CI's native hashes after the
// deferred load.

json fonts(const Context& context) {
    const std::string html = mcpplibs::os::fs::read(context.site / "index.html").value_or("");
    const json faces = fonts_of(html);
    const json chinese = font_named(faces, "NotoSansSC.ttf");
    cdp::Script script;
    script.report = {{"checks", json::array()},
                     {"assets", {{"full", at(chinese, 1)}, {"initial", at(chinese, 3)}}}};
    const auto check = [&](bool ok, std::string_view what,
                           std::optional<json> detail = std::nullopt) {
        return script.check(ok, what, std::move(detail));
    };
    check(includes(at(chinese, 2), "zh") && truthy(at(chinese, 3)),
          "the Chinese face has language selection and initial coverage");
    if (!truthy(at(chinese, 3))) {
        script.report["ok"] = false;
        return script.report;
    }
    const std::string full_face = text(at(chinese, 1)), initial_face = text(at(chinese, 3));
    script.report["assets"]["initial_bytes"] = size_of(context.site / initial_face);
    script.report["assets"]["full_bytes"] = size_of(context.site / full_face);
    const json pinned =
        at(json::parse(mcpplibs::os::fs::read(context.root / "site/reference/ci.frames.xdef.json")
                           .value_or("{}"),
                       nullptr, false),
           "frames");
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    const fs::path output = context.root / "target/site-fonts";
    std::error_code error;
    fs::create_directories(output, error);
    // The direct Chinese visit's heading, which a person's switch to Chinese after the full face arrives must draw
    // alike. The switch is a real click in a visit no agent acted in: an agent's acts leave its presence (the trail
    // of where it acted, D-160) over the page, which is no part of the glyphs compared.
    std::string direct_body;
    struct Case {
        std::string name, route;
        int width;
        bool full;
        std::string locale;
    };
    for (const Case& visit :
         std::vector<Case>{{"english", "", 1280, false, ""},
                           {"chinese", "zh", 1280, true, ""},
                           {"english-chinese-browser", "", 1280, false, "zh-CN"},
                           {"english-phone", "", 390, false, ""}}) {
        auto launched = open(context, visit.width, visit.width == 390 ? 844 : 800);
        if (!launched) {
            check(false, visit.name + ": scenario completes", "Error: " + launched.error());
            continue;
        }
        auto& page = **launched;
        script.page = &page;
        std::string held;
        std::vector<std::string> requests;
        page.on([&](const std::string& method, const json& params) {
            if (method == "Fetch.requestPaused")
                held = text(at(params, "requestId"));
            if (method == "Network.requestWillBeSent")
                requests.push_back(text(dig(params, "request", "url")));
        });
        const auto until = [&](const std::string& expression, int milliseconds = 30000) {
            return script.until_count(expression, milliseconds / 100, 100);
        };
        const auto capture = [&](json clip, std::string_view suffix) -> std::string {
            clip["scale"] = 1;
            const json answer =
                script.call("Page.captureScreenshot", {{"format", "png"}, {"clip", clip}});
            const std::string data = text(dig(answer, "result", "data"));
            if (!data.empty())
                cdp::write_base64(output / std::format("{}-{}.png", visit.name, suffix), data);
            return data;
        };
        const auto capture_heading = [&](std::string_view suffix) -> std::string {
            const auto bounds = cdp::box(
                script.value("window.omui.snapshot({root:'main/body/view/home/page/landing/intro/"
                             "title',depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)"));
            if (script.thrown())
                return {};
            if (!bounds) {
                script.raise("The actual home heading has no bounds");
                return {};
            }
            return capture({{"x", (*bounds)[0]},
                            {"y", (*bounds)[1]},
                            {"width", (*bounds)[2]},
                            {"height", (*bounds)[3]}},
                           suffix);
        };
        const auto ends_with_face = [&](std::string_view face) {
            return std::ranges::any_of(requests, [&](const std::string& request) {
                return request.ends_with(face);
            });
        };
        script.guard(visit.name + ": scenario completes", [&] {
            script.call("Network.enable");
            std::string source = "(() => { let value=false; "
                                 "Object.defineProperty(window,'omuiHasPresented',{get:()=>value,"
                                 "set:v=>{value=v;if(v&&!window.__fontPresentedAt)window.__"
                                 "fontPresentedAt=performance.now();},configurable:true}); })();";
            if (!visit.locale.empty())
                source += "Object.defineProperty(navigator,'languages',{get:()=>[" +
                          q(visit.locale) +
                          "]});Object.defineProperty(navigator,'language',{get:()=>" +
                          q(visit.locale) + "});";
            script.call("Page.addScriptToEvaluateOnNewDocument", {{"source", source}});
            if (!visit.full)
                script.call("Fetch.enable",
                            {{"patterns", json::array({{{"urlPattern", "*" + full_face},
                                                        {"requestStage", "Request"}}})}});
            script.call("Page.navigate", {{"url", url + "#/" + visit.route}});
            check(until("window.omuiStaticHandedOff"),
                  visit.name + ": the program presents and takes over");
            const json timing = script.value(
                "({ frame:window.__fontPresentedAt, language:window.omuiFontLanguage,\n      "
                "initial:performance.getEntriesByType('resource').find(r=>r.name.endsWith(" +
                q(initial_face) +
                "))?.responseEnd,\n      "
                "full:performance.getEntriesByType('resource').find(r=>r.name.endsWith(" +
                q(full_face) + "))?.responseEnd,\n      browserLanguage:navigator.language })");
            if (visit.full) {
                check(text(at(timing, "language")) == "zh" && number(at(timing, "full")) > 0 &&
                          number(at(timing, "full")) <= number(at(timing, "frame")) &&
                          !ends_with_face(initial_face),
                      "chinese: the full face arrives before presentation, without a redundant "
                      "initial download",
                      timing);
                script.sleep(500);
                direct_body = capture_heading("body");
                return;
            }
            check(text(at(timing, "language")) == "en" && number(at(timing, "initial")) > 0 &&
                      number(at(timing, "initial")) <= number(at(timing, "frame")) &&
                      !truthy(at(timing, "full")),
                  visit.name + ": initial glyphs arrive before the first presentation, the full "
                               "face not with them",
                  timing);
            script.sleep(2000);
            const bool waited = held.empty() && !ends_with_face(full_face);
            check(waited, visit.name + ": another language's full face waits until it is wanted",
                  json{{"wanted", script.value("window.omuiFontsWanted")}});
            script.sleep(250);
            const json node = script.value(
                "window.omui.snapshot({root:'main/bar/"
                "lang',depth:0}).then(r=>(r.data?.nodes??[]).find(n=>n.id==='main/bar/lang'))");
            check(text(at(node, "name")) == "中文" && number(dig(node, "bounds", 2)) > 24,
                  visit.name + ": the Chinese language control is present and measurable");
            const auto bounds = cdp::box(at(node, "bounds"));
            if (script.thrown())
                return;
            if (!bounds)
                return script.raise("Cannot read properties of undefined (reading 'bounds')");
            const json clip = {{"x", (*bounds)[0]},
                               {"y", (*bounds)[1]},
                               {"width", (*bounds)[2]},
                               {"height", (*bounds)[3]}};
            const std::string before = capture(clip, "initial");
            const auto act = [&](std::string_view id, std::string_view action = "activate",
                                 std::optional<json> args = std::nullopt) {
                return script.value(
                    "window.omui.act(" + q(id) + "," + q(action) + "," +
                    (args ? cdp::stringify(json{{"args", *args}}) : std::string("{}")) + ")");
            };
            const auto reference = [&] {
                return script.value("window.omui.snapshot({root:'main/body/"
                                    "check',depth:3}).then(r=>Object.fromEntries((r.data?.nodes??[]"
                                    ").map(n=>[n.id.split('/').pop(),n.name??''])))");
            };
            if (visit.name == "english") {
                act("main/foot/tools_toggle");
                act("main/foot/ci_check");
                script.sleep(250);
                act("main/body/check/ref_frame", "set_value", json{{"value", 30}});
                act("main/body/check/ref_frame", "set_value", json{{"value", 58}});
                script.sleep(250);
                act("main/foot/ci_check");
                script.sleep(150);
                act("main/foot/ci_check");
                script.sleep(150);
                const json waiting = reference();
                check(text(at(waiting, "verdict")).starts_with("Waiting for the reference fonts"),
                      "reference replay waits for the full font instead of reporting a pixel "
                      "mismatch",
                      waiting);
            }
            // Wanted: every face (as a reader drawing in all of them asks); the request held, then
            // let through.
            script.value("window.omuiWantFonts && window.omuiWantFonts()");
            for (int index = 0; index < 100 && held.empty() && !script.thrown(); ++index)
                script.sleep(100);
            if (!held.empty())
                script.call("Fetch.continueRequest", {{"requestId", held}});
            script.call("Fetch.disable");
            until("performance.getEntriesByType('resource').some(r=>r.name.endsWith(" +
                  q(full_face) + "))");
            script.sleep(1500);
            const std::string after = capture(clip, "complete");
            check(!before.empty() && before == after,
                  visit.name +
                      ": the two glyphs have identical pixels before and after the full font");
            if (visit.name == "english") {
                const auto frame_hash = [&](std::size_t frame) { return text(at(pinned, frame)); };
                const auto matches = [&](const json& result, std::size_t frame) {
                    return text(at(result, "mine")).ends_with(frame_hash(frame));
                };
                json first;
                for (int index = 0; index < 200 && !script.thrown(); ++index) {
                    first = reference();
                    if (matches(first, 58))
                        break;
                    script.sleep(100);
                }
                check(matches(first, 58) && text(at(first, "verdict")).starts_with("✓"),
                      "font readiness resumes the latest frame after closing and reopening the "
                      "panel",
                      first);
                json mismatch;
                for (std::size_t frame = 0;
                     frame < (pinned.is_array() ? pinned.size() : 0) && !script.thrown(); ++frame) {
                    act("main/body/check/ref_frame", "set_value", json{{"value", frame}});
                    json result;
                    const auto good = [&] {
                        return matches(result, frame) &&
                               text(at(result, "ci")).ends_with(frame_hash(frame)) &&
                               text(at(result, "verdict")).starts_with("✓");
                    };
                    for (int index = 0; index < 100 && !script.thrown(); ++index) {
                        result = reference();
                        if (good())
                            break;
                        script.sleep(50);
                    }
                    if (!good()) {
                        mismatch = {{"frame", frame}};
                        if (result.is_object())
                            mismatch.update(result);
                        break;
                    }
                }
                check(mismatch.is_null(),
                      "all 59 browser reference frames match the native CI hashes after deferred "
                      "font loading",
                      mismatch);
                if (!mismatch.is_null())
                    capture({{"x", 0}, {"y", 0}, {"width", 1280}, {"height", 800}},
                            "reference-mismatch");
                act("main/foot/ci_check");
                act("main/foot/tools_toggle");
            }
            if (visit.name == "english-chinese-browser") {
                click_at(script, (*bounds)[0] + (*bounds)[2] / 2, (*bounds)[1] + (*bounds)[3] / 2);
                script.sleep(1500);
                const std::string switched = capture_heading("chinese-body");
                check(!switched.empty() && !direct_body.empty() && switched == direct_body,
                      "switching to Chinese after the full face arrives matches a direct Chinese "
                      "visit");
            }
        });
        page.close();
        script.page = nullptr;
    }
    (*server)->close();
    script.report["ok"] = script.all_ok();
    return script.report;
}

// --- the opening pictures and loading (site-loading.mjs) ---
// Built opening pictures, real resource progress and failure recovery; UI input through CDP pointer
// events, runtime snapshots read-only.

json loading(const Context& context) {
    const json catalogue =
        json::parse(mcpplibs::os::fs::read(context.site / "first-frames.assets.xdef.json")
                        .value_or("{}"),
                    nullptr, false);
    const std::string html = mcpplibs::os::fs::read(context.site / "index.html").value_or("");
    const json inter_face = font_named(fonts_of(html), "Inter.ttf");
    const std::string inter =
        truthy(at(inter_face, 3)) ? text(at(inter_face, 3)) : text(at(inter_face, 1));
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    cdp::Script script(true);
    script.report = {{"checks", json::array()}, {"scenarios", json::array()}};
    const auto check = [&](bool ok, std::string_view what,
                           std::optional<json> detail = std::nullopt) {
        return script.check(ok, what, std::move(detail));
    };
    const auto until = [&](const std::string& expression, int timeout = 20000) {
        return script.until_time(expression, timeout, 50);
    };
    const auto click = [&](const std::string& selector) {
        const json bounds =
            script.value("(()=>{let e=document.querySelector(" + q(selector) +
                         ");if(!e)return null;let r=e.getBoundingClientRect();return "
                         "[r.x,r.y,r.width,r.height]})()");
        if (script.thrown())
            return;
        if (!truthy(bounds) || number(at(bounds, 2)) <= 0 || number(at(bounds, 3)) <= 0)
            return script.raise("No visible control: " + selector);
        click_at(script, number(bounds[0]) + number(bounds[2]) / 2,
                 number(bounds[1]) + number(bounds[3]) / 2);
    };
    const fs::path output = context.root / "target/site-loading";
    std::error_code error;
    fs::create_directories(output, error);
    const auto shot = [&](const std::string& name) {
        const json answer = script.call("Page.captureScreenshot", {{"format", "png"}});
        if (script.thrown())
            return;
        cdp::write_base64(output / (name + ".png"), text(dig(answer, "result", "data")));
    };
    std::set<std::string> previews;
    for (const auto& preview : at(catalogue, "previews")) {
        const std::string image = text(at(preview, "image"));
        previews.insert(image.substr(image.rfind('/') + 1));
    }
    const auto chosen_count = [&](const json& requested) {
        std::size_t count = 0;
        for (const auto& name : requested)
            if (previews.contains(text(name)))
                ++count;
        return count;
    };
    struct Scenario {
        std::string name, language;
        int width, height;
        bool reduced, navigate = false, retry = false, dedup = false;
        std::string start;
    };
    for (const Scenario& scenario :
         std::vector<Scenario>{{"english", "en", 1280, 800, false},
                               {"chinese-phone-reduced", "zh", 390, 844, true},
                               {"route-during-loading", "en", 1280, 800, false, true},
                               {"font-retry", "en", 1280, 800, false, false, true},
                               {"deduplicated-preview", "en", 1280, 800, false, false, false, true,
                                "components"}}) {
        json log = {{"name", scenario.name},       {"language", scenario.language},
                    {"width", scenario.width},     {"height", scenario.height},
                    {"reduced", scenario.reduced}, {"exceptions", json::array()}};
        if (scenario.navigate)
            log["navigate"] = true;
        if (scenario.retry)
            log["retry"] = true;
        if (scenario.dedup)
            log["dedup"] = true;
        if (!scenario.start.empty())
            log["start"] = scenario.start;
        auto launched = open(context, scenario.width, scenario.height);
        if (!launched) {
            check(false, scenario.name + ": scenario completes", "Error: " + launched.error());
            script.report["scenarios"].push_back(log);
            continue;
        }
        auto& page = **launched;
        script.page = &page;
        std::string held;
        bool fail_font = scenario.retry;
        script.guard(scenario.name + ": scenario completes", [&] {
            page.on([&](const std::string& method, const json& params) {
                if (method == "Runtime.exceptionThrown")
                    log["exceptions"].push_back(dig(params, "exceptionDetails", "text"));
                if (method != "Fetch.requestPaused")
                    return;
                const std::string requested = text(dig(params, "request", "url"));
                if (fail_font && requested.ends_with("/" + inter)) {
                    page.post("Fetch.fulfillRequest", {{"requestId", at(params, "requestId")},
                                                       {"responseCode", 404},
                                                       {"body", ""}});
                    return;
                }
                // The program's first half is held (its progress observed); its second half
                // (.deferred.wasm, D5) is not startup.
                if (!scenario.retry && std::regex_search(requested, any_wasm()) &&
                    !std::regex_search(requested, deferred_wasm())) {
                    held = text(at(params, "requestId"));
                    return;
                }
                page.post("Fetch.continueRequest", {{"requestId", at(params, "requestId")}});
            });
            script.call("Network.enable");
            script.call("Network.setCacheDisabled", {{"cacheDisabled", true}});
            script.call("Network.emulateNetworkConditions", {{"offline", false},
                                                             {"latency", 40},
                                                             {"downloadThroughput", 2500000},
                                                             {"uploadThroughput", 625000}});
            json patterns = json::array({{{"urlPattern", "*.wasm"}, {"requestStage", "Request"}}});
            if (scenario.retry)
                patterns.push_back({{"urlPattern", "*" + inter}, {"requestStage", "Request"}});
            script.call("Fetch.enable", {{"patterns", patterns}});
            script.call("Emulation.setEmulatedMedia",
                        {{"features",
                          json::array(
                              {{{"name", "prefers-reduced-motion"},
                                {"value", scenario.reduced ? "reduce" : "no-preference"}}})}});
            script.call("Page.addScriptToEvaluateOnNewDocument", {{"source", R"(
      Object.defineProperty(navigator,'languages',{value:['zh-CN']});
      window.__openingEvents=[];
      addEventListener('omui-load',event=>{
        let d=event.detail; window.__openingEvents.push({phase:d.phase,loaded:d.startup.loaded,total:d.startup.total,ratio:d.startup.ratio});
        if(d.phase==='presented'&&!window.__openingFirst){let c=document.getElementById('canvas'),x=c.getContext('2d');
          if(x)window.__openingFirst={width:c.width,height:c.height,pixels:x.getImageData(0,0,c.width,c.height).data};}
      });)"}});
            script.call("Page.navigate",
                        {{"url", url + "?renderer=cpu#/" +
                                     (scenario.language == "zh" ? "zh/" : "") +
                                     (scenario.start.empty() ? "home" : scenario.start)}});
            const bool visible = until(
                "!!window.omuiOpeningVisible && !document.getElementById('omui-static').hidden");
            check(visible,
                  scenario.name + ": native opening is visible while the runtime is loading");
            if (!visible)
                return script.raise("Opening did not become visible");
            if (!scenario.retry)
                check(
                    until(
                        R"([...document.fonts].some(f=>f.family.replace(/['"]/g,'')==='Inter'&&f.status==='loaded'))"),
                    scenario.name + ": the opening uses the actual initial Inter face");
            log["static_readable_ms"] = script.value("performance.now()");
            shot(scenario.name + "-static");
            log["static_capture_ms"] = script.value("performance.now()");
            log["opening"] = script.value("window.omuiOpeningVisible");
            log["paint"] = script.value(
                "performance.getEntriesByType('paint').map(x=>({name:x.name,at:x.startTime}))");
            check(text(dig(log, "opening", "language")) == scenario.language,
                  scenario.name + ": the opening follows the selected page language");
            check(truthy(script.value("(()=>{let "
                                      "s=document.querySelector('.omui-opening')?.closest('section'"
                                      ")?.querySelector('.landing-mark>svg');return "
                                      "!s||getComputedStyle(s).display==='none'})()")),
                  scenario.name +
                      ": the picture replaces its SVG fallback without duplicating the mark");
            check(truthy(script.value(
                      R"((()=>{let n=document.querySelector('nav[data-locale=")" +
                      scenario.language +
                      R"("]');return n&&!n.hidden&&getComputedStyle(n).display==='flex'})())")),
                  scenario.name + ": the active navigation preserves its flex layout");
            check(until("!!document.querySelector('.omui-opening[data-nodes]')"),
                  scenario.name + ": loading geometry comes from real semantic rectangles");
            const json requested = script.value("performance.getEntriesByType('resource').map(x=>"
                                                "new URL(x.name).pathname.split('/').pop())");
            json chosen = json::array();
            for (const auto& name : requested)
                if (previews.contains(text(name)))
                    chosen.push_back(name);
            check(chosen.size() == 1,
                  scenario.name + ": only the current opening picture is requested", chosen);
            if (!scenario.retry)
                check(number(log["static_readable_ms"]) <= 1000,
                      scenario.name +
                          ": the picture and initial face are ready within one second at 20 Mbit/s",
                      log["static_readable_ms"]);
            if (scenario.reduced) {
                check(truthy(script.value("getComputedStyle(document.querySelector('.omui-opening "
                                          "svg')).display==='none'")),
                      "reduced motion uses the static native picture without drawing the tree");
                json expected;
                for (const auto& preview : at(catalogue, "previews"))
                    if (text(at(preview, "route")) == "home" &&
                        text(at(preview, "language")) == "zh" &&
                        dig(preview, "viewport", 0) == 390 &&
                        text(at(preview, "motion")) == "reduced") {
                        expected = preview;
                        break;
                    }
                if (expected.is_null())
                    return script.raise("Cannot read properties of undefined (reading 'image')");
                check(dig(log, "opening", "image") == at(expected, "image"),
                      "reduced motion selects its matching assembled first frame");
                check(
                    truthy(script.value(
                        R"((()=>{let n=document.querySelector('nav[data-locale="zh"]');return !n.hidden&&!n.querySelector('.static-menu').hidden&&n.querySelector('.static-links').hidden&&n.getBoundingClientRect().height<55})())")),
                    "the narrow static header starts with one collapsed menu row");
                click(R"(nav[data-locale="zh"] .static-menu)");
                check(
                    truthy(script.value(
                        R"((()=>{let n=document.querySelector('nav[data-locale="zh"]');return n.querySelector('.static-menu').getAttribute('aria-expanded')==='true'&&!n.querySelector('.static-links').hidden})())")),
                    "the static phone menu opens through actual pointer input");
                click(R"(nav[data-locale="zh"] .static-menu)");
                check(
                    truthy(script.value(
                        R"(document.querySelector('nav[data-locale="zh"] .static-links').hidden)")),
                    "the static phone menu closes before live handoff");
            }
            if (scenario.retry) {
                const bool failed_font = until("['font:Inter.ttf','initial-font:Inter.ttf']."
                                               "includes(window.omuiLoad?.error?.id)",
                                               30000);
                check(failed_font, "a failed required font produces an explicit recoverable state");
                check(truthy(script.value(
                          "!window.omuiStaticHandedOff && "
                          "!document.getElementById('omui-static').hidden && "
                          "!document.querySelector('.omui-loading-note button').hidden")),
                      "font failure keeps the readable page and a visible retry action");
                fail_font = false;
                click(".omui-loading-note button");
            } else {
                check(!held.empty(),
                      scenario.name + ": wasm is genuinely held before presentation");
                check(truthy(
                          script.value("!window.omuiHasPresented && !window.omuiStaticHandedOff")),
                      scenario.name + ": the picture does not pretend to be the live UI");
                const json font_plan = script.value(
                    "window.omuiLoad.assets.find(x=>x.id==='font:NotoSansSC.ttf')?.group");
                check(text(font_plan) == (scenario.language == "zh" ? "startup" : "deferred"),
                      scenario.name +
                          ": the chosen page overrides browser language for startup fonts",
                      font_plan);
                if (scenario.navigate) {
                    click(R"(nav[data-locale="en"] a[href="#/motion"])");
                    check(until("window.omuiOpeningVisible?.route==='motion'"),
                          "static navigation selects the new page picture before wasm is "
                          "available");
                    const json count =
                        script.value("performance.getEntriesByType('resource').map(x=>new "
                                     "URL(x.name).pathname.split('/').pop())");
                    check(chosen_count(count) == 2,
                          "loading navigation fetches only the old and newly selected opening");
                }
                if (scenario.dedup) {
                    const json previous = script.value("window.omuiOpeningVisible");
                    script.call("Page.navigate", {{"url", url + "?renderer=cpu#/zh/components"}});
                    check(until("window.omuiOpeningVisible?.language==='zh'&&!!document."
                                "querySelector('.omui-opening[data-nodes]')"),
                          "a locale change updates a deduplicated picture and its geometry before "
                          "wasm loads");
                    const json translated = script.value("window.omuiOpeningVisible");
                    // Each locale's picture and geometry its own; where two locales' pictures are
                    // the same bytes (one deduplicated file), still never the other's geometry.
                    // (The components opening, the wall, is translated: its pictures differ — the
                    // shared case is exercised where they are the same.)
                    check(at(previous, "geometry") != at(translated, "geometry"),
                          "another locale's picture brings its own semantic geometry, shared bytes "
                          "or not",
                          json{{"previous", previous}, {"translated", translated}});
                    check(truthy(script.value(
                              "document.querySelector('.omui-opening "
                              "figcaption').textContent==='界面预览 · 加载完成后即可操作'")),
                          "the reused picture has the selected language's caption");
                    script.call("Emulation.setEmulatedMedia",
                                {{"features", json::array({{{"name", "prefers-reduced-motion"},
                                                            {"value", "reduce"}}})}});
                    check(until("window.omuiOpeningVisible?.motion==='reduced'&&!!document."
                                "querySelector('.omui-opening[data-nodes]')"),
                          "a motion preference change selects new geometry even when the PNG is "
                          "identical");
                    const json reduced = script.value("window.omuiOpeningVisible");
                    check(at(reduced, "image") == at(translated, "image") &&
                              at(reduced, "geometry") != at(translated, "geometry"),
                          "reduced-motion metadata is independent of deduplicated image bytes");
                }
                script.call("Fetch.continueRequest", {{"requestId", held}});
                held.clear();
                check(until("(window.__openingEvents||[]).some(x=>typeof "
                            "x.ratio==='number'&&x.ratio>0&&x.ratio<1)"),
                      scenario.name + ": intermediate decoded bytes drive actual progress");
            }
            const bool handed =
                until("window.omuiStaticHandedOff===true && window.omuiLoad?.presented===true",
                      45000);
            check(handed, scenario.name + ": presentation and semantic readiness hand over once");
            if (!handed)
                return script.raise("Runtime did not hand over");
            check(truthy(script.value("document.getElementById('omui-static').hidden && "
                                      "document.querySelector('[data-omui=mirror]').getAttribute('"
                                      "aria-hidden')!=='true'")),
                  scenario.name + ": only the live accessible surface remains");
            if (scenario.navigate) {
                check(
                    truthy(script.value(
                        R"(!!document.querySelector('[data-omui-id^="main/body/view/explore/page/"]'))")),
                    "the live handoff follows the page chosen during loading");
            } else if (scenario.dedup) {
                const json final_locale = script.value(
                    R"(({root:!!document.querySelector('[data-omui-id^="main/body/view/components/page/wall/"]'),language:document.documentElement.lang,fontPlan:window.omuiLoad.assets.find(x=>x.id==='font:NotoSansSC.ttf')?.group,fontLanguage:window.omuiFontLanguage}))");
                check(truthy(at(final_locale, "root")) &&
                          text(at(final_locale, "language")) == "zh-CN" &&
                          text(at(final_locale, "fontPlan")) == "startup",
                      "the locale selected while loading reaches the live component and its "
                      "startup font plan",
                      final_locale);
            } else if (!scenario.retry) {
                json reference;
                for (const auto& candidate : at(catalogue, "references"))
                    if (text(at(candidate, "language")) == scenario.language &&
                        dig(candidate, "viewport", 0) == scenario.width &&
                        text(at(candidate, "motion")) == (scenario.reduced ? "reduced" : "full")) {
                        reference = candidate;
                        break;
                    }
                if (reference.is_null())
                    return script.raise("Cannot read properties of undefined (reading 'image')");
                const json result = script.value(R"((async()=>{
        let actual=window.__openingFirst;if(!actual)return {error:'first CPU pixels not captured'};
        let image=new Image(); image.src=)" + q(text(at(reference, "image"))) +
                                                 R"(;await image.decode();
        let c=document.createElement('canvas');c.width=image.naturalWidth;c.height=image.naturalHeight;
        let x=c.getContext('2d');x.drawImage(image,0,0);let wanted=x.getImageData(0,0,c.width,c.height).data;
        let differences=0,first=-1;for(let i=0;i<wanted.length;i++)if(wanted[i]!==actual.pixels[i]){differences++;if(first<0)first=i;}
        return {width:actual.width,height:actual.height,expectedWidth:c.width,expectedHeight:c.height,differences,first};
      })())");
                check(truthy(result) && at(result, "differences") == 0 &&
                          at(result, "width") == at(result, "expectedWidth") &&
                          at(result, "height") == at(result, "expectedHeight"),
                      scenario.name + ": the first presented CPU frame matches the build-time full "
                                      "reference pixel for pixel",
                      result);
                if (scenario.name == "english") {
                    const json glyphs = script.value(R"((async()=>{
          let f=omuiFonts.find(x=>x[0]==='Inter.ttf');if(!f[3])return {error:'initial Inter asset missing'};
          let full=new FontFace('omui-check-full','url('+f[1]+')',{weight:'100 900'}),small=new FontFace('omui-check-initial','url('+f[3]+')',{weight:'100 900'});
          await Promise.all([full.load(),small.load()]);document.fonts.add(full);document.fonts.add(small);
          let c=document.createElement('canvas');c.width=512;c.height=80;let x=c.getContext('2d'),wrong=[];
          for(let weight of [400,600])for(let text of ['Office','Notification','configuration','One interface.','Every perspective.',':-)','UI (CPU)','A-B','2x3']){
            let draw=family=>{x.fillStyle='#fff';x.fillRect(0,0,512,80);x.font=weight+' 28px '+family;x.fillStyle='#16161a';x.fillText(text,4,55);return x.getImageData(0,0,512,80).data;};
            let a=draw('omui-check-full'),b=draw('omui-check-initial');if(a.some((v,i)=>v!==b[i]))wrong.push({text,weight});
          }return {differences:wrong};
        })())");
                    check(truthy(glyphs) && at(glyphs, "differences").is_array() &&
                              at(glyphs, "differences").empty(),
                          "initial Inter preserves UI glyphs and actual contextual substitutions "
                          "at both weights",
                          glyphs);
                }
            }
            check(log["exceptions"].empty(),
                  scenario.name + ": no unexpected browser runtime exceptions", log["exceptions"]);
            shot(scenario.name + "-live");
        });
        if (!held.empty())
            static_cast<void>(page.call("Fetch.continueRequest", {{"requestId", held}}));
        page.close();
        script.page = nullptr;
        script.report["scenarios"].push_back(log);
    }
    (*server)->close();
    script.report["ok"] = script.all_ok();
    return script.report;
}

// --- shared font delivery (site-font-sharing.mjs) ---
// Static FontFace bytes and runtime FS fonts share downloads: the HTTP cache disabled, a fresh
// profile per scenario, service workers bypassed. encoded_body_bytes sums Network.dataReceived;
// encoded_transport_bytes is Network.loadingFinished, response headers included.

json font_sharing(const Context& context) {
    const std::string html = mcpplibs::os::fs::read(context.site / "index.html").value_or("");
    const json faces = fonts_of(html);
    const json inter = font_named(faces, "Inter.ttf");
    cdp::Script script(true);
    script.report = {{"checks", json::array()},
                     {"scenarios", json::array()},
                     {"cache",
                      "disabled; fresh Chrome profile per scenario; service workers bypassed"},
                     {"bytes",
                      "encoded_body_bytes sums Network.dataReceived; encoded_transport_bytes is "
                      "Network.loadingFinished including response headers"}};
    const auto check = [&](bool ok, std::string_view what,
                           std::optional<json> detail = std::nullopt) {
        return script.check(ok, what, std::move(detail));
    };
    struct Identity {
        std::string name, variant;
        std::uint64_t length;
        std::string sha256;
    };
    std::vector<std::pair<std::string, Identity>> font_files;  // in the page's order
    const auto identity_of = [&](std::string_view path) -> const Identity* {
        for (const auto& [url, identity] : font_files)
            if (url == path)
                return &identity;
        return nullptr;
    };
    for (const auto& face : faces.is_array() ? faces : json::array()) {
        std::vector<std::pair<std::string, std::string>> variants{{text(at(face, 1)), "full"}};
        if (truthy(at(face, 3)))
            variants.emplace_back(text(at(face, 3)), "initial");
        for (const auto& [path, variant] : variants) {
            const std::string bytes = mcpplibs::os::fs::read(context.site / path).value_or("");
            if (!identity_of(path))
                font_files.emplace_back(path, Identity{text(at(face, 0)), variant, bytes.size(),
                                                       mlibs::hash::sha256_hex(bytes)});
        }
    }
    const std::map<std::string, std::string> families{{"Inter.ttf", "Inter"},
                                                      {"JetBrainsMono.ttf", "JetBrains Mono"},
                                                      {"NotoSansSC.ttf", "Noto Sans SC"}};
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    const fs::path output = context.root / "target/site-font-delivery";
    std::error_code error;
    fs::create_directories(output, error);
    const auto until = [&](const std::string& expression, int timeout = 20000) {
        return script.until_time(expression, timeout, 50);
    };
    struct Scenario {
        std::string name, route;
        int width;
        bool pending = false, runtime_first = false, retry = false;
    };
    const std::vector<Scenario> scenarios{{"english-home", "home", 1280},
                                          {"chinese-home", "zh/home", 1280},
                                          {"english-phone", "home", 390},
                                          {"chinese-phone", "zh/home", 390},
                                          {"documentation-code", "docs/start", 1280},
                                          {"pending-shared-response", "home", 1280, true},
                                          {"runtime-before-static-consumer", "home", 1280, false,
                                           true},
                                          {"shared-failure-and-retry", "home", 1280, false, false,
                                           true}};
    const std::string initial = truthy(at(inter, 3)) ? text(at(inter, 3)) : text(at(inter, 1));
    // `new URL(address, server.url).pathname.slice(1)`.
    const auto path_of = [&](std::string_view address) {
        std::string absolute(address);
        if (!address.starts_with("http://") && !address.starts_with("https://"))
            absolute = address.starts_with("/") ? url.substr(0, url.size() - 1) + absolute
                                                : url + absolute;
        const std::string path = pathname(absolute);
        return path.empty() ? path : path.substr(1);
    };
    bool supported = true;
    for (std::size_t scenario_index = 0; scenario_index < scenarios.size(); ++scenario_index) {
        const Scenario& scenario = scenarios[scenario_index];
        if (!supported) {
            json not_run = json::array();
            for (std::size_t index = script.report["scenarios"].size(); index < scenarios.size();
                 ++index)
                not_run.push_back(scenarios[index].name);
            script.report["not_run"] = not_run;
            break;
        }
        const fs::path profile = fs::temp_directory_path() /
                                 std::format("omui-font-sharing-{:08x}", std::random_device{}());
        fs::create_directories(profile, error);
        json log = {{"name", scenario.name},
                    {"route", scenario.route},
                    {"width", scenario.width},
                    {"requests", json::array()},
                    {"exceptions", json::array()}};
        if (scenario.pending)
            log["pending"] = true;
        if (scenario.runtime_first)
            log["runtimeFirst"] = true;
        if (scenario.retry)
            log["retry"] = true;
        std::unique_ptr<cdp::Browser> browser;
        std::string wasm, font_response;
        std::map<std::string, std::size_t> by_id;
        const auto count = [&](std::string_view path) {
            return static_cast<std::size_t>(
                std::ranges::count_if(log["requests"], [&](const json& row) {
                    return text(at(row, "path")) == path;
                }));
        };
        const auto ensure = [&](bool ok, std::string_view what,
                                std::optional<json> detail = std::nullopt) {
            check(ok, scenario.name + ": " + std::string(what), std::move(detail));
            if (!ok)
                script.raise(std::string(what));
        };
        script.attempt(
            [&] {
                auto launched = cdp::Browser::launch(context.chrome,
                                                     {.width = scenario.width,
                                                      .height = scenario.width == 390 ? 844 : 800,
                                                      .flags = {"--user-data-dir=" +
                                                                mcpplibs::os::fs::utf8(profile)}});
                if (!launched)
                    return script.raise(launched.error());
                browser = std::move(*launched);
                auto& page = *browser;
                script.page = &page;
                script.report["browser"] = page.version();
                page.on([&](const std::string& method, const json& params) {
                    if (method == "Runtime.exceptionThrown")
                        log["exceptions"].push_back(dig(params, "exceptionDetails", "text"));
                    const std::string id = text(at(params, "requestId"));
                    if (method == "Network.requestWillBeSent") {
                        const std::string path = path_of(text(dig(params, "request", "url")));
                        if (identity_of(path)) {
                            json row = {{"id", id},
                                        {"path", path},
                                        {"url", dig(params, "request", "url")},
                                        {"type", at(params, "type")},
                                        {"encoded_body_bytes", 0}};
                            if (const json& initiator = dig(params, "initiator", "type");
                                !initiator.is_null())
                                row["initiator"] = initiator;
                            log["requests"].push_back(std::move(row));
                            by_id[id] = log["requests"].size() - 1;
                        }
                    }
                    if (const auto found = by_id.find(id); found != by_id.end()) {
                        json& row = log["requests"][found->second];
                        const json& response = at(params, "response");
                        if (method == "Network.responseReceived") {
                            row["status"] = at(response, "status");
                            row["cache"] = truthy(at(response, "fromDiskCache"));
                            row["service_worker"] = truthy(at(response, "fromServiceWorker"));
                        }
                        if (method == "Network.requestServedFromCache")
                            row["cache"] = true;
                        if (method == "Network.dataReceived")
                            row["encoded_body_bytes"] =
                                static_cast<std::int64_t>(number(row["encoded_body_bytes"]) +
                                                          number(at(params, "encodedDataLength")));
                        if (method == "Network.loadingFinished")
                            row["encoded_transport_bytes"] = at(params, "encodedDataLength");
                        if (method == "Network.loadingFailed")
                            row["error"] = at(params, "errorText");
                    }
                    if (method != "Fetch.requestPaused")
                        return;
                    const std::string requested = text(dig(params, "request", "url"));
                    // The program's first half is held; its second half (.deferred.wasm, D5) is not
                    // startup and goes on.
                    if (std::regex_search(requested, deferred_wasm())) {
                        page.post("Fetch.continueRequest", {{"requestId", id}});
                        return;
                    }
                    if (std::regex_search(requested, any_wasm()))
                        wasm = id;
                    else if (requested.ends_with("/" + initial))
                        font_response = id;
                    else
                        page.post("Fetch.continueRequest", {{"requestId", id}});
                });
                script.call("Network.enable");
                script.call("Network.setCacheDisabled", {{"cacheDisabled", true}});
                script.call("Network.setBypassServiceWorker", {{"bypass", true}});
                script.call("Emulation.setEmulatedMedia",
                            {{"features", json::array({{{"name", "prefers-reduced-motion"},
                                                        {"value", "reduce"}}})}});
                // Hide only the public protocol until both font consumers can be observed. Native
                // presentation and its deferred-font hook retain their real values. The
                // runtime-first case delays external fontBytes consumers; the original byte API and
                // runtime startup remain real and run without interception.
                script.call("Page.addScriptToEvaluateOnNewDocument", {{"source",
                                                                       std::string(R"((() => {
      let protocol;
      window.__fontSharingHold = true; window.__fontSharingPresented = false;
      Object.defineProperty(window,'omui',{configurable:true,get:()=>window.__fontSharingHold?undefined:protocol,set:value=>{protocol=value;}});
      Object.defineProperty(window,'omuiHasPresented',{configurable:true,get:()=>window.__fontSharingPresented,set:value=>{window.__fontSharingPresented=value;}});
      Object.defineProperty(navigator,'languages',{get:()=>['en-US']});
      if ()") + (scenario.runtime_first ? "true" : "false") + R"() {
        let load; window.__fontSharingExternalReady=false; window.__fontSharingExternalWaiters=[];
        Object.defineProperty(window,'omuiLoad',{configurable:true,get:()=>load,set:value=>{
          load=value; if(typeof value?.fontBytes!=='function')return;
          const actual=value.fontBytes.bind(value);
          value.fontBytes=(...args)=>window.__fontSharingExternalReady?actual(...args):new Promise(resolve=>window.__fontSharingExternalWaiters.push(resolve)).then(()=>actual(...args));
        }});
      }
    })();)"}});
                json patterns =
                    json::array({{{"urlPattern", "*.wasm"}, {"requestStage", "Request"}}});
                if (scenario.pending || scenario.retry)
                    patterns.push_back(
                        {{"urlPattern", "*" + initial}, {"requestStage", "Response"}});
                script.call("Fetch.enable", {{"patterns", patterns}});
                script.call("Page.navigate", {{"url", url + "?renderer=cpu#/" + scenario.route}});
                supported = until("typeof window.omuiLoad?.fontBytes==='function' && "
                                  "window.omuiStaticFonts?.supported===true",
                                  3000);
                if (script.thrown())
                    return;
                ensure(supported,
                       "the shared fontBytes lease and static-font data contracts are available");
                for (int index = 0; wasm.empty() && index < 100 && !script.thrown(); ++index)
                    script.sleep(50);
                ensure(!wasm.empty(),
                       "wasm is held before preRun so consumer ordering is controlled");
                const auto release_wasm = [&] {
                    const std::string id = wasm;
                    wasm.clear();
                    script.call("Fetch.continueRequest", {{"requestId", id}});
                };
                const auto release_font = [&] {
                    const std::string id = font_response;
                    font_response.clear();
                    script.call("Fetch.continueResponse", {{"requestId", id}});
                };
                const auto static_state = [&] {
                    return script.value(
                        R"(({state:window.omuiStaticFonts,faces:[...document.fonts].map(f=>({family:f.family.replace(/['"]/g,''),status:f.status})),hidden:document.getElementById('omui-static').hidden,handed:!!window.omuiStaticHandedOff,assets:window.omuiLoad.assets}))");
                };

                if (scenario.runtime_first) {
                    ensure(truthy(script.value("window.__fontSharingExternalWaiters.length>0")),
                           "the static consumer is actually delayed before the real runtime "
                           "starts");
                    release_wasm();
                    ensure(until("window.omuiLoad.assets.some(a=>a.id==='initial-font:Inter.ttf'&&"
                                 "a.state==='ready')"),
                           "runtime installs the initial font before the delayed static consumer");
                    const std::size_t before = count(initial);
                    script.value(
                        "window.__fontSharingExternalReady=true;window.__"
                        "fontSharingExternalWaiters.splice(0).forEach(resolve=>resolve());true");
                    ensure(until("window.omuiStaticFonts.loaded.includes('Inter.ttf')"),
                           "the delayed static consumer loads a real face from ready runtime "
                           "bytes");
                    ensure(count(initial) == before && before == 1,
                           "runtime-first and later DOM consumption make one initial-font request",
                           json{{"before", before}, {"after", count(initial)}});
                } else if (scenario.pending || scenario.retry) {
                    for (int index = 0; font_response.empty() && index < 100 && !script.thrown();
                         ++index)
                        script.sleep(50);
                    ensure(!font_response.empty(),
                           "the initial font response is genuinely held before preRun");
                    script.value(R"(window.__fontSharingLeases={resolved:0,error:null,values:[]};
        Promise.all([window.omuiLoad.fontBytes('Inter.ttf'),window.omuiLoad.fontBytes('Inter.ttf')]).then(values=>{
          window.__fontSharingLeases.values=values;window.__fontSharingLeases.resolved=values.length;
        },error=>{window.__fontSharingLeases.error=String(error.message||error);});true)");
                    script.sleep(100);
                    const json pending = script.value(
                        "({static:window.omuiStaticFonts,leases:window.__fontSharingLeases."
                        "resolved,runtimeInitialized:window.omuiLoad.runtimeInitialized})");
                    ensure(includes(dig(pending, "static", "pending"), "Inter.ttf") &&
                               at(pending, "leases") == 0 &&
                               at(pending, "runtimeInitialized") == false,
                           "DOM and concurrent lease consumers stay pending before preRun",
                           pending);
                    release_wasm();
                    ensure(until("window.omuiLoad.assets.some(a=>a.id==='initial-font:Inter.ttf'&&"
                                 "a.state==='fetching')"),
                           "runtime preRun joins the same pending font response");
                    ensure(count(initial) == 1,
                           "all pending consumers share exactly one CDP request",
                           json{{"requests", count(initial)}});
                    if (scenario.retry) {
                        const std::string failed_id = font_response;
                        font_response.clear();
                        script.call("Fetch.fulfillRequest",
                                    {{"requestId", failed_id},
                                     {"responseCode", 404},
                                     {"responseHeaders", json::array({{{"name", "Content-Type"},
                                                                       {"value", "text/plain"}}})},
                                     {"body", ""}});
                        ensure(until("window.omuiStaticFonts.error && "
                                     "window.omuiLoad.assets.some(a=>a.id==='initial-font:Inter."
                                     "ttf'&&a.state==='error')"),
                               "one failed request reaches both consumers as a recoverable error");
                        const json failure = static_state();
                        ensure(!truthy(at(failure, "handed")) && !truthy(at(failure, "hidden")) &&
                                   !includes(dig(failure, "state", "loaded"), "Inter.ttf"),
                               "font failure retains readable static content and does not report a "
                               "ready face",
                               at(failure, "state"));
                        const json bounds = script.value(
                            "(()=>{let "
                            "r=document.getElementById('omui-load-retry').getBoundingClientRect();"
                            "return {x:r.x+r.width/2,y:r.y+r.height/2,w:r.width,h:r.height};})()");
                        ensure(number(at(bounds, "w")) > 0 && number(at(bounds, "h")) > 0,
                               "an actual visible retry control is available");
                        click_at(script, number(at(bounds, "x")), number(at(bounds, "y")));
                        // Fetch and Network are separate CDP domains: the held response can arrive
                        // before Network's request event. Observe both before counting.
                        for (int index = 0; (font_response.empty() || count(initial) < 2) &&
                                            index < 100 && !script.thrown();
                             ++index)
                            script.sleep(50);
                        ensure(!font_response.empty() && count(initial) == 2,
                               "simultaneous DOM/runtime retries share one new request",
                               json{{"requests", count(initial)}});
                        ensure(truthy(script.value(
                                   "!window.omuiStaticFonts.loaded.includes('Inter.ttf') && "
                                   "!window.omuiLoad.assets.some(a=>a.id==='initial-font:Inter.ttf'"
                                   "&&a.state==='ready')")),
                               "a held retry cannot prematurely report either consumer ready");
                    }
                    release_font();
                    if (!scenario.retry) {
                        ensure(until("window.__fontSharingLeases.resolved===2"),
                               "both pending public leases resolve after the shared response");
                        log["concurrent_leases"] = script.value(
                            R"((async()=>{let values=window.__fontSharingLeases.values;let result=await Promise.all(values.map(async v=>({id:v.id,url:v.url,bytes:v.buffer.byteLength,arrayBuffer:v.buffer instanceof ArrayBuffer,sha256:Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',v.buffer))).map(x=>x.toString(16).padStart(2,'0')).join('')})));values[0].release();values[0].release();result[1].surviving_bytes=values[1].buffer.byteLength;values[1].release();values[1].release();return result;})())");
                        const Identity* expected = identity_of(initial);
                        const json& leases = log["concurrent_leases"];
                        const bool right =
                            expected && leases.is_array() && leases.size() == 2 &&
                            std::ranges::all_of(leases,
                                                [&](const json& lease) {
                                                    return truthy(at(lease, "arrayBuffer")) &&
                                                           at(lease, "bytes") == expected->length &&
                                                           text(at(lease, "sha256")) ==
                                                               expected->sha256 &&
                                                           text(at(lease, "id")) ==
                                                               "initial-font:Inter.ttf" &&
                                                           path_of(text(at(lease, "url"))) ==
                                                               initial;
                                                }) &&
                            number(dig(leases, 1, "surviving_bytes")) > 0;
                        ensure(right,
                               "pending leases return the actual initial font; repeated release "
                               "preserves the other consumer",
                               leases);
                    }
                } else {
                    ensure(until("window.omuiStaticFonts.loaded.includes('Inter.ttf') && "
                                 "window.omuiStaticFonts.pending.length===0"),
                           "static FontFaces finish while wasm is held");
                    ensure(truthy(script.value("window.omuiLoad.runtimeInitialized===false")),
                           "static consumers complete before runtime startup");
                    release_wasm();
                }

                ensure(until("window.__fontSharingPresented && window.omuiLoad.presented===true && "
                             "window.omuiStaticFonts.pending.length===0 && "
                             "window.omuiLoad.assets.filter(a=>a.kind==='font'&&a.group==='startup'"
                             ").every(a=>['ready','skipped'].includes(a.state))"),
                       "real native presentation and runtime startup fonts become ready");
                log["before_handoff"] = static_state();
                const json& before_handoff = log["before_handoff"];
                const json& state = at(before_handoff, "state");
                ensure(!truthy(at(before_handoff, "hidden")) &&
                           !truthy(at(before_handoff, "handed")) && truthy(at(state, "active")) &&
                           !at(state, "requested").empty() && at(state, "pending").empty() &&
                           !truthy(at(state, "error")),
                       "the observation barrier retains a genuinely loaded static surface "
                       "alongside runtime fonts",
                       state);
                const auto every_requested =
                    [&](const std::function<bool(const std::string&)>& holds) {
                        return at(state, "requested").is_array() &&
                               std::ranges::all_of(at(state, "requested"), [&](const json& name) {
                                   return holds(text(name));
                               });
                    };
                ensure(every_requested([&](const std::string& name) {
                           return includes(at(state, "loaded"), name) &&
                                  std::ranges::any_of(at(before_handoff, "faces"),
                                                      [&](const json& face) {
                                                          return families.contains(name) &&
                                                                 text(at(face, "family")) ==
                                                                     families.at(name) &&
                                                                 text(at(face, "status")) ==
                                                                     "loaded";
                                                      });
                       }),
                       "every requested static font is an actual loaded DOM FontFace",
                       at(before_handoff, "faces"));
                ensure(every_requested([&](const std::string& name) {
                           return std::ranges::any_of(at(before_handoff, "assets"),
                                                      [&](const json& asset) {
                                                          return (text(at(asset, "id")) ==
                                                                      "initial-font:" + name ||
                                                                  text(at(asset, "id")) ==
                                                                      "font:" + name) &&
                                                                 text(at(asset, "state")) ==
                                                                     "ready";
                                                      });
                       }),
                       "each static font also has ready runtime installation");
                if (scenario.route == "docs/start")
                    ensure(includes(at(state, "loaded"), "JetBrainsMono.ttf"),
                           "documentation uses the real code FontFace, not only body text");
                if (scenario.route.starts_with("zh/"))
                    ensure(includes(at(state, "loaded"), "NotoSansSC.ttf"),
                           "Chinese static content uses the actual Chinese face");
                if (!scenario.pending && !scenario.runtime_first && !scenario.retry) {
                    const json capture = script.call("Page.captureScreenshot", {{"format", "png"}});
                    if (!script.thrown())
                        cdp::write_base64(output / (scenario.name + ".png"),
                                          text(dig(capture, "result", "data")));
                }
                script.value("window.__fontSharingHold=false;window.omuiLoad.fontBytes('Inter.ttf')"
                             ".then(lease=>{lease.release();return true;})");
                ensure(until("window.omuiStaticHandedOff && window.omuiStaticFonts.active===false "
                             "&& window.omuiLoad.phase==='complete'"),
                       "handoff releases static ownership and all planned font loads complete");
                log["after_handoff"] = static_state();
                const json& after_handoff = log["after_handoff"];
                const json& after_state = at(after_handoff, "state");
                ensure(std::ranges::all_of(std::array<std::string_view, 3>{"requested", "loaded",
                                                                           "pending"},
                                           [&](std::string_view key) {
                                               return at(after_state, key).is_array() &&
                                                      at(after_state, key).empty();
                                           }) &&
                           !std::ranges::any_of(at(after_handoff, "faces"),
                                                [&](const json& face) {
                                                    return std::ranges::
                                                        any_of(families, [&](const auto& family) {
                                                            return family.second ==
                                                                   text(at(face, "family"));
                                                        });
                                                }),
                       "handoff removes owned DOM faces and clears static-font data", after_state);
                const json& assets = at(after_handoff, "assets");
                ensure(assets.is_array() &&
                           std::ranges::all_of(assets,
                                               [&](const json& asset) {
                                                   if (text(at(asset, "kind")) != "font" ||
                                                       text(at(asset, "state")) != "ready")
                                                       return true;
                                                   const std::string address =
                                                       text(at(asset, "url"));
                                                   return count(path_of(address)) ==
                                                          (scenario.retry && address == initial
                                                               ? 2u
                                                               : 1u);
                                               }),
                       "each installed font URL has exactly one real download per expected "
                       "attempt");
                const std::size_t before_late = log["requests"].size();
                log["full_lease"] = script.value(
                    "(async()=>{let a=await window.omuiLoad.fontBytes('Inter.ttf');let "
                    "hash=Array.from(new Uint8Array(await "
                    "crypto.subtle.digest('SHA-256',a.buffer))).map(x=>x.toString(16).padStart(2,'"
                    "0')).join('');let "
                    "value={id:a.id,url:a.url,bytes:a.buffer.byteLength,arrayBuffer:a.buffer "
                    "instanceof ArrayBuffer,sha256:hash};a.release();a.release();return "
                    "value;})()");
                const json& lease = log["full_lease"];
                const Identity* full = identity_of(text(at(inter, 1)));
                ensure(full && text(at(lease, "id")) == "font:Inter.ttf" &&
                           path_of(text(at(lease, "url"))) == text(at(inter, 1)) &&
                           truthy(at(lease, "arrayBuffer")) && at(lease, "bytes") == full->length &&
                           text(at(lease, "sha256")) == full->sha256,
                       "a late lease identifies and returns the actual full FS font, not initial "
                       "bytes or an initial URL",
                       lease);
                script.sleep(100);
                ensure(log["requests"].size() == before_late,
                       "late ready-font reads and repeated release perform no network requests");
                ensure(log["exceptions"].empty(),
                       "the browser has no unexpected runtime exceptions", log["exceptions"]);
            },
            [&](const std::string& message) {
                log["error"] = message;
                check(false, scenario.name + ": scenario completes", message);
            });
        script.finally([&] {
            if (!browser)
                return;
            if (!wasm.empty())
                script.call("Fetch.continueRequest", {{"requestId", wasm}});
            if (!font_response.empty())
                script.call("Fetch.continueResponse", {{"requestId", font_response}});
            json network = json::array();
            for (const auto& [path, identity] : font_files) {
                std::vector<const json*> rows;
                for (const auto& row : log["requests"])
                    if (text(at(row, "path")) == path)
                        rows.push_back(&row);
                json types = json::object();
                std::int64_t body = 0, transport = 0;
                for (const json* row : rows) {
                    const std::string type = text(at(*row, "type"));
                    types[type] = (types.contains(type) ? types[type].get<int>() : 0) + 1;
                    body += static_cast<std::int64_t>(number(at(*row, "encoded_body_bytes")));
                    if (const double sent = number(at(*row, "encoded_transport_bytes"));
                        !std::isnan(sent))
                        transport += static_cast<std::int64_t>(sent);
                }
                network.push_back({{"path", path},
                                   {"name", identity.name},
                                   {"variant", identity.variant},
                                   {"requests", rows.size()},
                                   {"types", types},
                                   {"encoded_body_bytes", body},
                                   {"encoded_transport_bytes", transport}});
            }
            log["network"] = network;
            if (supported) {
                check(std::ranges::all_of(network,
                                          [&](const json& file) {
                                              return number(at(file, "requests")) <=
                                                     (scenario.retry &&
                                                              text(at(file, "path")) == initial
                                                          ? 2
                                                          : 1);
                                          }),
                      scenario.name + ": each content URL has one actual request per allowed "
                                      "attempt, with HTTP cache disabled",
                      network);
                check(!std::ranges::any_of(log["requests"],
                                           [](const json& row) {
                                               return truthy(at(row, "cache")) ||
                                                      truthy(at(row, "service_worker"));
                                           }),
                      scenario.name +
                          ": HTTP or service-worker caches do not hide repeated consumers");
                if (scenario.route.starts_with("zh/"))
                    check(count(text(at(font_named(faces, "NotoSansSC.ttf"), 3))) == 0,
                          scenario.name +
                              ": direct Chinese startup skips the initial Chinese subset");
            }
            browser->close();
        });
        // A page that stopped answering in the finally ends the script, as it did there.
        if (const auto stalled = script.take()) {
            log["error"] = *stalled;
            check(false, scenario.name + ": scenario completes", *stalled);
        }
        script.page = nullptr;
        script.report["scenarios"].push_back(log);
        for (int attempt = 0; attempt < 3 && fs::exists(profile, error); ++attempt) {
            fs::remove_all(profile, error);
            if (fs::exists(profile, error))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    (*server)->close();
    script.report["ok"] = script.all_ok();
    return script.report;
}

}  // namespace omui::devtools::check::pages
