// The design shown in place and a page's customization as it ships (G8, D-253): an application's page built for
// the Web (`omui build --web`; example 13's by default) in a real browser, driven through window.omui as an agent
// in the tab drives it — the design's replay played in place (each step a real interface under its caption, the
// end the page as it ships), a theme overlay previewed (nothing left of it), committed and undone, a view document
// in a surface, both kept across a reload (the page's storage), and the layer exported, the storage cleared and
// the layer imported again to the same picture. Report: target/live-page.json; pictures under target/live-page/.
module omui.devtools.check;

import std;
import mcpplibs.os;
import omui.devtools.browser;
import omui.devtools.cdp;
import :pages;
import :support;

namespace omui::devtools::check {

namespace fs = std::filesystem;
using namespace support;

namespace {

using cdp::json, cdp::at, cdp::dig, cdp::q, cdp::text, cdp::truthy;

}  // namespace

int live_page(const fs::path& root, const fs::path& built) {
    const auto chrome = browser::chromium();
    if (!chrome)
        return failed("no headless Chromium for the page");
    // Example 13's page as `omui build --web` packs it (build.mcpp's dist_toy: <the build's out>/toy/subscription), the
    // newest when the build has packed it more than once.
    fs::path page_dir = built.is_absolute() || built.empty() ? built : root / built;
    if (built.empty()) {
        fs::file_time_type newest{};
        std::error_code walked;
        for (const auto& out : fs::directory_iterator(root / "target/.build-mcpp/out", walked)) {
            const fs::path candidate = out.path() / "omui.example.live-design/toy/subscription";
            std::error_code missing;
            const auto written = fs::last_write_time(candidate / "index.html", missing);
            if (!missing && (page_dir.empty() || written > newest)) page_dir = candidate, newest = written;
        }
    }
    if (!fs::is_regular_file(page_dir / "index.html"))
        return failed(std::format("no page at {} (omui build --web in examples/13-live-design first)", mcpplibs::os::fs::utf8(page_dir)));
    const fs::path pictures = root / "target/live-page";
    std::error_code error;
    fs::create_directories(pictures, error);
    auto served = cdp::Server::start(cdp::pages(page_dir));
    if (!served)
        return failed(served.error());
    auto launched = pages::open({.root = root, .site = page_dir, .chrome = *chrome}, 720, 720);
    if (!launched)
        return failed(launched.error());
    auto& page = **launched;
    cdp::Script script;
    script.page = &page;
    const auto step = [&](bool ok, std::string_view what, std::optional<json> detail = std::nullopt) { return script.check(ok, what, std::move(detail)); };

    // A verb as the page's agent invokes it: {exitCode, data, errors}.
    const auto invoke = [&](std::string_view verb, const json& args) -> json {
        const json answer = script.value(std::format("window.omui.invoke({}, {}).then(r => JSON.stringify(r))", q(verb), args.dump()));
        return json::parse(answer.is_string() ? answer.get<std::string>() : std::string("{}"), nullptr, false);
    };
    // The names the semantic tree has now.
    const auto names = [&]() -> std::vector<std::string> {
        const json answer = script.value("window.omui.snapshot({}).then(r => JSON.stringify((r.data.nodes || []).map(n => n.name || '')))");
        const json list = json::parse(answer.is_string() ? answer.get<std::string>() : std::string("[]"), nullptr, false);
        std::vector<std::string> out;
        if (list.is_array())
            for (const json& name : list) out.push_back(text(name));
        return out;
    };
    const auto shows_now = [&](std::string_view name) { return std::ranges::contains(names(), std::string(name)); };
    // Whether a node so named is there within a few seconds (a page's script settles later; what was kept is shown
    // once its place is on screen).
    const auto shows = [&](std::string_view name) { return script.until_time([&] { return shows_now(name); }, 5000, 100); };
    const auto caption_shown = [&] {
        return std::ranges::any_of(names(), [](const std::string& name) { return name.find(" · ") != std::string::npos && name.find('/') < name.find(" · "); });
    };
    const auto ready = [&] {
        for (int index = 0; index < 300 && !script.thrown(); ++index) {
            if (truthy(script.value("!!window.omui && (document.querySelector('[data-omui=mirror]')?.childElementCount ?? 0) > 0")))
                return true;
            script.sleep(100);
        }
        return false;
    };
    // The page at rest (what moved has settled), its picture.
    const auto picture = [&](std::string_view name) {
        script.sleep(1500);
        return script.screenshot(pictures / std::format("{}.png", name));
    };

    script.attempt(
        [&] {
            script.call("Page.navigate", {{"url", (*served)->url()}});
            step(ready(), "the page runs: window.omui answers, its mirror is there");

            // 1. The design, in place.
            const json listed = invoke("generate.replay", json::object());
            const std::size_t steps = at(dig(listed, "data"), "steps").is_array() ? at(dig(listed, "data"), "steps").size() : 0;
            step(cdp::number(at(listed, "exitCode")) == 0 && steps >= 9 && cdp::number(dig(listed, "data", "at")) == 0,
                 std::format("the replay it ships: {} steps; the page as it ships", steps), listed);
            (void)invoke("generate.replay", {{"at", 1}});
            step(shows("Clicked 0 times") && caption_shown(), "step 1 is a real interface: the counter omui new wrote, under its caption");
            (void)picture("replay-1");
            (void)invoke("generate.replay", {{"at", 5}});
            step(shows("Renew automatically") && !shows_now("Save"), "step 5: the renewal's details added, no Save button yet");
            (void)picture("replay-5");
            const json playing = invoke("generate.replay", {{"from", 1}, {"speed", 4}});
            step(truthy(dig(playing, "data", "playing")), "played from the first step, four times as fast", playing);
            const bool moved_on = script.until_time(
                [&] { return cdp::number(dig(invoke("generate.replay", json::object()), "data", "at")) >= 3; }, 10000, 100);
            step(moved_on, "a step follows another as its time is up");
            const bool ended = script.until_time([&] { return truthy(dig(invoke("generate.replay", json::object()), "data", "ended")); },
                                                 static_cast<int>(steps * 1250 / 4) + 8000, 200);
            step(ended && shows("Your subscription") && !caption_shown(), "at the end: the page as it ships, the caption gone");

            // 2. A new look, as data: previewed with nothing left of it, committed, undone.
            const std::string before = picture("as-it-ships");
            const json drafted = invoke("generate.draft", {{"surface", "look"}, {"document", {{"colors", {{"accent", "#0f766e"}}}}}});
            const std::string draft = text(dig(drafted, "data", "draft"));
            step(cdp::number(at(drafted, "exitCode")) == 0 && !draft.empty(), "an overlay of the theme's tokens drafted for `look`", drafted);
            const json previewed = invoke("generate.preview", {{"draft", draft}});
            step(cdp::number(at(previewed, "exitCode")) == 0 && truthy(dig(previewed, "data", "after")), "previewed: its picture before and after",
                 json{{"exitCode", at(previewed, "exitCode")}, {"errors", at(previewed, "errors")}, {"keys", dig(previewed, "data").is_object() ? json(dig(previewed, "data").size()) : json(nullptr)}});
            step(picture("after-preview") == before, "after the preview the page is as it was");
            const json committed = invoke("generate.commit", {{"draft", draft}});
            step(cdp::number(at(committed, "exitCode")) == 0, "committed", committed);
            step(picture("look") != before, "the page in the new accent");
            const json undone = script.value("window.omui.request({op: 'undo'}).then(r => JSON.stringify(r))");
            step(picture("look-undone") == before, "undone: the page as it was", undone);
            const json again = invoke("generate.draft", {{"surface", "look"}, {"document", {{"colors", {{"accent", "#0f766e"}}}}}});
            step(cdp::number(at(invoke("generate.commit", {{"draft", text(dig(again, "data", "draft"))}}), "exitCode")) == 0, "the look committed again");

            // 3. A view document in a surface.
            const json offer = {{"omui", "0.1"}, {"view", {{"root", {{"type", "text"}, {"id", "offer"}, {"props", {{"text", "Team plan: 20% off this month"}}}}}}}};
            const json card = invoke("generate.draft", {{"surface", "extras"}, {"document", offer}});
            const json shown = invoke("generate.commit", {{"draft", text(dig(card, "data", "draft"))}});
            step(cdp::number(at(shown, "exitCode")) == 0 && shows("Team plan: 20% off this month"), "an offer in `extras`, a view document", shown);
            const std::string customized = picture("customized");

            // 4. Kept across a reload: the page's storage.
            script.call("Page.reload", json::object());
            step(ready(), "reloaded: the page runs again");
            step(shows("Team plan: 20% off this month"), "the offer is there again");
            step(picture("reloaded") == customized, "the same picture after the reload");

            // 5. The layer exported, the storage cleared, the layer imported: the same picture.
            const json exported = invoke("generate.export", json::object());
            const json layer = dig(exported, "data");
            step(text(at(layer, "$format")) == "customization@1" && at(dig(layer, "customization", "surfaces"), "look").is_object() &&
                     at(dig(layer, "customization", "surfaces"), "extras").is_object(),
                 "exported: one layer of data, both surfaces in it", layer);
            (void)script.value("localStorage.clear(), true");
            script.call("Page.reload", json::object());
            step(ready() && shows("Your subscription") && !shows_now("Team plan: 20% off this month"), "the storage cleared: the page as it ships");
            step(picture("cleared") == before, "and its picture as it ships");
            const json imported = invoke("generate.import", {{"document", layer}});
            step(cdp::number(at(imported, "exitCode")) == 0 && shows("Team plan: 20% off this month"), "the layer imported", imported);
            step(picture("imported") == customized, "the same picture as before it was exported");
        },
        [&](const std::string& message) { step(false, "the page's checks ran to the end", message); });
    page.close();
    (*served)->close();
    std::ofstream(root / "target/live-page.json", std::ios::binary) << cdp::pretty(script.report) << '\n';
    std::cout << "the page as it ships (G8):\n";
    for (const auto& one : at(script.report, "checks"))
        std::cout << std::format("  {} {}\n", truthy(at(one, "ok")) ? "ok  " : "FAIL", text(at(one, "what")));
    if (!script.all_ok(true))
        return failed("the page failed its checks (target/live-page.json, pictures in target/live-page/)");
    return 0;
}

}  // namespace omui::devtools::check
