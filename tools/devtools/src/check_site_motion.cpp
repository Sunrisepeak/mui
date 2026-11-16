// Real browser input through the site's places (moved from tools/ci/site-motion.mjs,
// site-presentation.mjs and site-transition.mjs, D-206): motion, the reference pages and the agent;
// first-view density and real input after the preview's handoff; and early painted-pixel input on
// moving primaries. Changes go through Chromium's trusted pointer and keyboard input; semantic
// calls only read mounted controls (and, where the script did, an agent's native input). Every
// threshold and expectation as the scripts had them.
module omui.devtools.check;

import std;
import mcpplibs.os;
import omui.devtools.cdp;
import :pages;
import :support;

namespace omui::devtools::check::pages {

namespace {

using cdp::at, cdp::dig, cdp::q, cdp::text, cdp::truthy, cdp::number, cdp::includes, cdp::js_number;

json failed_report(std::string message) {
    return {{"checks", json::array({{{"ok", false},
                                     {"what", "the browser started"},
                                     {"detail", std::move(message)}}})},
            {"ok", false}};
}

// A snapshot node's scalar value (`v && typeof v === 'object' && 'value' in v ? v.value : v`).
json scalar_of(const json& node) {
    const json& value = at(node, "value");
    if (value.is_object() && value.contains("value"))
        return value["value"];
    return value;
}

bool has_state(const json& node, std::string_view state) {
    return includes(at(node, "state"), state);
}

std::string padded(int value, int width) {
    return std::format("{:0{}}", value, width);
}

void write_text(const fs::path& path, std::string_view text_out) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream(path, std::ios::binary) << text_out;
}

}  // namespace

// --- motion, the reference pages and the agent (site-motion.mjs) ---

json motion(const Context& context) {
    const fs::path output = context.root / "target/site-motion";
    const std::string output_name = "target/site-motion";
    cdp::Script script(true);
    script.report = {{"checks", json::array()},
                     {"screenshots", json::array()},
                     {"exceptions", json::array()}};
    json& report = script.report;
    const auto check = [&](bool ok, std::string_view what,
                           std::optional<json> detail = std::nullopt) {
        return script.check(ok, what, std::move(detail));
    };
    std::string place = "home";
    const auto mirror = [](std::string_view id) {
        return R"(document.querySelector('[data-omui-id="'+)" + q(id) + R"(+'"]'))";
    };
    const auto snapshot = [&](std::string_view root, int depth = -1) {
        return script.value("window.omui.snapshot({root:" + q(root) + ",depth:" +
                            std::to_string(depth) + "}).then(r=>r.exitCode===0?r.data?.nodes:[])");
    };
    const auto node = [&](const std::string& id) -> json {
        return cdp::find_id(snapshot(id, 0), id);
    };
    const auto scalar = [&](const std::string& id) { return scalar_of(node(id)); };
    const auto label = [&](const std::string& id) {
        const json found = node(id);
        return truthy(at(found, "name")) ? text(at(found, "name")) : std::string();
    };
    const auto exists = [&](const std::string& id) { return truthy(script.value(mirror(id))); };
    const auto checked = [&](const std::string& id) { return has_state(node(id), "checked"); };
    const auto wait = [&](const std::function<bool()>& predicate, int timeout = 12000) {
        return script.until_time(predicate, timeout, 80);
    };
    const auto require_check = [&](const std::function<bool()>& predicate, std::string_view what,
                                   const std::function<json()>& detail = nullptr) {
        const bool ok = wait(predicate);
        if (script.thrown())
            return;
        std::optional<json> shown;
        if (!ok && detail)
            shown = detail();
        if (script.thrown())
            return;
        check(ok, what, shown);
        if (!ok)
            script.raise(std::string(what));
    };
    const auto shot = [&](const std::string& name) {
        script.sleep(200);
        const json answer = script.call("Page.captureScreenshot", {{"format", "png"}});
        if (script.thrown())
            return;
        const std::string data = text(dig(answer, "result", "data"));
        if (data.empty())
            return script.raise("Screenshot failed: " + name);
        cdp::write_base64(output / (name + ".png"), data);
        report["screenshots"].push_back(output_name + "/" + name + ".png");
    };
    const auto scenario = [&](const std::string& name, const std::function<void()>& run) {
        script.attempt(run, [&](const std::string& message) {
            check(false, name + " completed", cdp::error_text(message));
            script.attempt([&] { shot(name + "-failure"); },
                           [](const std::string&) {});  // keep the original failure
        });
    };
    // The browser clipboard's text. A rejected read (a page without focus after pointer input, in
    // some headless builds) is not text: bring the page to the front and focus it, then read again;
    // otherwise say what the browser said.
    const auto clipboard_text = [&]() -> std::string {
        std::string reason = "no answer";
        for (int attempt = 0; attempt < 3 && !script.thrown(); ++attempt) {
            const json read = script.value("navigator.clipboard.readText().then(text => ({ text "
                                           "}), e => ({ error: String((e && e.message) || e) }))");
            if (at(read, "text").is_string())
                return text(at(read, "text"));
            if (truthy(at(read, "error")))
                reason = text(at(read, "error"));
            // Reading back refused: what the page wrote, as it wrote it.
            const json written = script.value("window.__omuiCopied");
            if (written.is_string()) {
                report["clipboard"] = "observed the page's writes (reading back: " + reason + ")";
                return written.get<std::string>();
            }
            script.call("Page.bringToFront");
            script.value("(window.focus(), document.hasFocus())");
        }
        script.raise("the browser clipboard could not be read: " + reason);
        return {};
    };
    const auto dispatch = [&](std::string_view method, json params) {
        script.dispatch(method, std::move(params));
    };
    const auto visible_bounds = [&](const std::string& id) -> std::optional<std::array<double, 4>> {
        for (int attempt = 0; attempt < 24 && !script.thrown(); ++attempt) {
            const json found = node(id);
            if (!at(found, "bounds").is_array() || has_state(found, "hidden")) {
                script.sleep(80);
                continue;
            }
            const auto bounds = cdp::box(at(found, "bounds"));
            if (!bounds || (*bounds)[2] <= 0 || (*bounds)[3] <= 0) {
                script.raise("Invalid bounds: " + id);
                return std::nullopt;
            }
            const auto [x, y, w, h] = *bounds;
            std::optional<std::array<double, 4>> viewport;
            if (id.starts_with("main/bar/"))
                viewport = std::array<double, 4>{0, 0, 1280, 800};
            else {
                const json view = at(node("main/body/view/" + place), "bounds");
                if (view.is_array() && view.size() == 4)
                    viewport = std::array<double, 4>{number(view[0]), number(view[1]),
                                                     number(view[2]), number(view[3])};
            }
            if (!viewport) {
                script.sleep(80);
                continue;
            }
            const auto [vx, vy, vw, vh] = *viewport;
            if (x + w / 2 >= vx && x + w / 2 <= vx + vw && y >= vy + 1 && y + h <= vy + vh - 1)
                return bounds;
            dispatch("Input.dispatchMouseEvent", {{"type", "mouseWheel"},
                                                  {"x", vx + vw * 0.8},
                                                  {"y", vy + vh / 2},
                                                  {"deltaX", 0},
                                                  {"deltaY", y + h / 2 - (vy + vh / 2)}});
            script.sleep(160);
        }
        script.raise("Target did not enter the live viewport: " + id);
        return std::nullopt;
    };
    const auto pointer = [&](const std::string& id, double fraction = 0.5, bool move_only = false) {
        const auto bounds = visible_bounds(id);
        if (!bounds)
            return;
        const auto [x, y, w, h] = *bounds;
        const double px = x + w * fraction, py = y + h / 2;
        dispatch("Input.dispatchMouseEvent", {{"type", "mouseMoved"}, {"x", px}, {"y", py}});
        if (move_only)
            return;
        for (const std::string type : {"mousePressed", "mouseReleased"})
            dispatch("Input.dispatchMouseEvent",
                     {{"type", type}, {"x", px}, {"y", py}, {"button", "left"}, {"clickCount", 1}});
        script.sleep(80);
    };
    const auto key = [&](std::string_view name, std::string_view code, int virtual_key,
                         int modifiers = 0) {
        for (const std::string type : {"keyDown", "keyUp"})
            dispatch("Input.dispatchKeyEvent", {{"type", type},
                                                {"key", name},
                                                {"code", code},
                                                {"windowsVirtualKeyCode", virtual_key},
                                                {"modifiers", modifiers}});
        script.sleep(50);
    };
    const auto type = [&](const std::string& id, const std::string& value) {
        pointer(id);
        require_check(
            [&] {
                return truthy(script.value(
                    R"(document.activeElement?.matches('textarea[data-omui="input"]'))"));
            },
            "real input receives browser focus: " + id);
        key("a", "KeyA", 65, 2);
        dispatch("Input.insertText", {{"text", value}});
        if (value.empty())
            key("Backspace", "Backspace", 8);
        require_check([&] { return scalar(id) == value; },
                      "keyboard input reaches the actual field: " + id, [&] { return scalar(id); });
    };
    const auto segment = [&](const std::string& id, int index, int count,
                             const std::string& expected) {
        pointer(id, (index + 0.5) / count);
        require_check([&] { return scalar(id) == expected; }, "pointer selects " + expected,
                      [&] { return scalar(id); });
    };
    const auto top = [&](const std::string& destination) {
        pointer("main/bar/nav/to_" + destination);
        if (!script.thrown())
            place = destination;
        require_check([&] { return exists("main/body/view/" + destination + "/page"); },
                      "top navigation opens " + destination);
    };
    // One specimen or composition of a wall opened (its address): the page is its own, with its
    // stage and picker.
    const auto open_specimen = [&](const std::string& destination, const std::string& id) {
        const std::string wall = "main/body/view/" + destination + "/page/wall";
        require_check([&] { return exists(wall); },
                      "the wall shows every case at once: " + destination);
        script.value(
            "(location.hash = " +
            q("#/" + std::string(destination == "motion" ? "docs/motion/" : "motion/") + id) +
            ", true)");
        require_check(
            [&] {
                return exists("main/body/view/" + destination + "/page/motion_gallery/stage/scene");
            },
            "the case opened on its own: " + id);
    };
    // A reference page (the motion specimens, styling, the interface), from the docs' list.
    const auto reference = [&](const std::string& destination) {
        top("docs");
        pointer("main/body/view/docs/page/read/results/" + destination + "/open");
        if (!script.thrown())
            place = destination;
        require_check([&] { return exists("main/body/view/" + destination + "/page"); },
                      "the docs open " + destination);
    };
    const auto gallery = [](const std::string& name) {
        return "main/body/view/" + name + "/page/motion_gallery";
    };
    const auto choices = [&](const std::string& root) {
        json out = json::array();
        for (const auto& found : snapshot(root + "/browse/choices"))
            if (text(at(found, "role")) == "button" &&
                text(at(found, "id")).starts_with(root + "/browse/choices/"))
                out.push_back(found);
        return out;
    };
    // A case's address is its own page (E2b–E2d): the case with its details open — the whole
    // transport and its configuration; closing them returns to the wall.
    const auto case_page = [&](const std::string& root, const std::string& description) {
        require_check(
            [&] {
                const json nodes = snapshot(root + "/transport");
                std::vector<std::string> buttons;
                bool seek = false;
                for (const auto& found : nodes) {
                    const std::string id = text(at(found, "id"));
                    if (text(at(found, "role")) == "button" &&
                        id.starts_with(root + "/transport/buttons/"))
                        buttons.push_back(id.substr(id.rfind('/') + 1));
                    if (id == root + "/transport/seek")
                        seek = true;
                }
                std::ranges::sort(buttons);
                return buttons == std::vector<std::string>{"details", "play",    "reset", "reverse",
                                                           "state-a", "state-b", "try"} &&
                       seek;
            },
            description + ": its page exposes play, try, details and the whole transport — seek, "
                          "reverse, reset, both endpoints");
        check(exists(root + "/details/config"), description + ": its page shows its configuration");
        check(!exists(root + "/browse/filters/search") && !exists(root + "/browse/choices"),
              description + ": catalog starts unmounted");
    };
    const auto close_to_wall = [&](const std::string& root, const std::string& destination,
                                   const std::string& address) {
        pointer(root + "/transport/buttons/details");
        require_check(
            [&] {
                return truthy(script.value("location.hash === " + q(address))) &&
                       exists("main/body/view/" + destination + "/page/wall") &&
                       !exists(root + "/details/config");
            },
            "closing Details returns to the wall of every case: " + destination);
    };
    const auto open_picker = [&](const std::string& root) {
        if (!exists(root + "/browse/filters/search"))
            pointer(root + "/choose");
        require_check([&] { return exists(root + "/browse/filters/search"); },
                      "explicit Choose mounts the searchable catalog");
    };
    const auto find_design = [&](const std::string& root, const std::string& id) {
        open_picker(root);
        type(root + "/browse/filters/search", id);
        require_check(
            [&] {
                const json found = choices(root);
                return found.size() == 1 && text(dig(found, 0, "id")).ends_with("/" + id);
            },
            "search locates design: " + id);
        const std::string expected = label(root + "/browse/choices/" + id);
        pointer(root + "/browse/choices/" + id);
        require_check(
            [&] {
                return !expected.empty() &&
                       text(at(node(root + "/stage/scene"), "name")) == expected;
            },
            "the selected scene has the chosen design semantic label: " + id);
        require_check(
            [&] {
                return !exists(root + "/browse/filters/search") &&
                       !exists(root + "/browse/choices");
            },
            "choosing a design closes the catalog and returns to its working stage");
    };
    const auto pause_transport = [&](const std::string& root) {
        visible_bounds(root + "/transport/buttons/play");
        if (label(root + "/transport/buttons/play") == "Pause")
            pointer(root + "/transport/buttons/play");
        require_check([&] { return label(root + "/transport/buttons/play") == "Play"; },
                      "player exposes its paused state");
    };
    const auto bounds_of = [&](const std::string& id) { return at(node(id), "bounds"); };

    std::unique_ptr<cdp::Server> server;
    std::unique_ptr<cdp::Browser> browser;
    script.guard("browser acceptance started and completed", [&] {
        std::error_code error;
        fs::create_directories(output, error);
        auto started = cdp::Server::start(cdp::pages(context.site));
        if (!started)
            return script.raise(started.error());
        server = std::move(*started);
        report["url"] = server->url();
        auto launched = open(context, 1280, 800);
        if (!launched)
            return script.raise(launched.error());
        browser = std::move(*launched);
        script.page = browser.get();
        report["browser"] = browser->version();
        browser->on([&](const std::string& method, const json& params) {
            if (method != "Runtime.exceptionThrown")
                return;
            const json& details = at(params, "exceptionDetails");
            const std::string description = text(dig(details, "exception", "description"));
            const std::string said = text(at(details, "text"));
            report["exceptions"].push_back(!description.empty() ? description
                                           : !said.empty()      ? said
                                                                : "Runtime exception");
        });
        dispatch("Emulation.setEmulatedMedia",
                 {{"features", json::array({{{"name", "prefers-reduced-motion"},
                                             {"value", "no-preference"}}})}});
        dispatch("Browser.grantPermissions",
                 {{"origin", server->url().substr(0, server->url().size() - 1)},
                  {"permissions", {"clipboardReadWrite", "clipboardSanitizedWrite"}}});
        // What the page writes to the clipboard, observed as it writes it (its own promise kept, so
        // a refused write still fails): some headless builds refuse reading the clipboard back
        // whatever is granted.
        script.call("Page.addScriptToEvaluateOnNewDocument", {{"source", R"((() => {
    const c = navigator.clipboard; if (!c || c.__omuiObserved) return;
    const write = c.writeText.bind(c);
    c.writeText = (text) => { window.__omuiCopied = String(text); return write(text); };
    c.__omuiObserved = true;
    window.__omuiClipboardText = () => c.readText().catch(e => typeof window.__omuiCopied === 'string' ? window.__omuiCopied : Promise.reject(e));
  })();)"}});
        dispatch("Page.navigate", {{"url", server->url()}});
        const bool ready = wait(
            [&] {
                return truthy(script.value("window.omuiStaticHandedOff && !!window.omui && !!" +
                                           mirror("main/bar/nav/to_explore")));
            },
            60000);
        check(ready, "the real site hands off to the new live navigation");
        if (!ready)
            return script.raise("The new live site did not become ready");

        scenario("published-agent-policy", [&] {
            const json description = script.value("window.omui.describe()");
            const json policy = dig(description, "data", "agent_policy");
            check(at(description, "exitCode") == 0 && text(at(policy, "mode")) == "dual-user" &&
                      text(at(policy, "source")) == "profile:site-web",
                  "the delivered Web build selects its own published profile", policy);
            check(at(policy, "enabled") == true && at(policy, "input") == true &&
                      text(at(policy, "binding")) == "instance" &&
                      at(policy, "session_valid") == true,
                  "published applications retain an instance-bound Agent endpoint and input");
            check(at(policy, "development_grant") == false &&
                      at(policy, "development_controls") == false,
                  "the Web artifact does not inherit the native generator development session");
            const json forged = script.value("window.omui.request({op:'describe',actor:'person:"
                                             "owner',dev:true,mode:'development'})");
            check(at(forged, "exitCode") == 0 &&
                      text(dig(forged, "data", "agent_policy", "mode")) == "dual-user" &&
                      dig(forged, "data", "agent_policy", "development_grant") == false,
                  "remote actor and development claims do not upgrade the published policy");
            const json input =
                script.value("window.omui.request({op:'input',actor:'agent:deployment-check',"
                             "events:[{type:'click',node:'main/bar/nav/to_docs'}]})");
            check(at(input, "exitCode") == 0,
                  "native Agent input completes through the delivered Wasm frame loop", input);
            const json& actions = dig(input, "data", "actions");
            check(actions.is_array() &&
                      std::ranges::any_of(actions,
                                          [](const json& action) {
                                              return text(at(action, "node")) ==
                                                         "main/bar/nav/to_docs" &&
                                                     at(action, "exitCode") == 0 &&
                                                     text(at(action, "actor")) ==
                                                         "agent:deployment-check;via=service:input";
                                          }),
                  "the real input action records its Agent provenance", actions);
            require_check([&] { return exists("main/body/view/docs/page"); },
                          "Agent input opens the same visible docs");
            pointer("main/bar/nav/to_explore");
            if (!script.thrown())
                place = "explore";
            require_check([&] { return exists("main/body/view/explore/page"); },
                          "a real person pointer goes on from there without replacing the runtime");
        });

        scenario("basic-motion", [&] {
            reference("motion");
            open_specimen("motion", "shape-morph");
            const std::string root = gallery("motion");
            require_check([&] { return std::atoi(label(root + "/heading/count").c_str()) >= 40; },
                          "basic catalog contains at least forty designs");
            case_page(root, "a basic motion case");
            check(text(at(node(root + "/stage/scene"), "name")) == "Shape the journey",
                  "the opening is the public triangle-to-circle path morph");
            check(number(dig(node(root + "/stage/scene"), "bounds", 3)) <= 300.5,
                  "the default morph stays within a compact stage");
            const std::string shape = root + "/stage/scene/composition/shape",
                              change = root + "/stage/scene/composition/change";
            pointer(root + "/stage", .5, true);
            const std::string before_shape = label(shape);
            pointer(change);
            require_check([&] { return label(shape) != before_shape; },
                          "pointer controls the actual morph endpoint");
            key(" ", "Space", 32);
            require_check([&] { return label(shape) == before_shape; },
                          "keyboard controls that same morph endpoint");
            require_check([&] { return exists(root + "/details/bend"); },
                          "the case page exposes the reusable path control");
            pointer(root + "/details/bend");
            key("Home", "Home", 36);
            require_check(
                [&] { return std::abs(number(scalar(root + "/details/bend")) + .3) < .001; },
                "keyboard sets a negative path bend");
            pointer(root + "/details/copy");
            const std::string copied = clipboard_text();
            if (script.thrown())
                return;
            const json decoded = json::parse(copied.empty() ? "{}" : copied, nullptr, false);
            if (decoded.is_discarded())
                return script.raise("SyntaxError: the copied configuration is not JSON");
            check(std::abs(number(dig(decoded, "vertex_path", "bend")) + .3) < .001,
                  "copied public motion configuration contains the real path parameter");
            visible_bounds(root + "/stage");
            shot("shape-morph-opening");
            open_picker(root);
            require_check([&] { return choices(root).size() == 8; },
                          "catalog mounts eight choices per page");
            std::vector<std::string> first;
            for (const auto& found : choices(root))
                first.push_back(text(at(found, "id")));
            pointer(root + "/browse/pagination/next");
            require_check(
                [&] { return label(root + "/browse/pagination/position").starts_with("2 /"); },
                "a real Next click advances the catalog page");
            // Observe the replacement list itself, independently of its page label.
            require_check(
                [&] {
                    const json current = choices(root);
                    return current.size() == 8 &&
                           std::ranges::all_of(current, [&](const json& found) {
                               return !std::ranges::contains(first, text(at(found, "id")));
                           });
                },
                "second page settles to eight different designs",
                [&] {
                    json current = json::array();
                    for (const auto& found : choices(root))
                        current.push_back(at(found, "id"));
                    return json{{"first", first}, {"current", current}};
                });
            pointer(root + "/browse/pagination/previous");
            find_design(root, "toggle-thumb");
            pause_transport(root);
            require_check([&] { return exists(root + "/details/speed"); },
                          "the chosen case keeps its playback parameters open");
            require_check(
                [&] {
                    const json nodes = snapshot(root + "/transport");
                    return std::ranges::
                        all_of(std::array<std::string_view, 5>{"/seek", "/buttons/reverse",
                                                               "/buttons/reset", "/buttons/state-a",
                                                               "/buttons/state-b"},
                               [&](std::string_view suffix) {
                                   return nodes.is_array() &&
                                          std::ranges::any_of(nodes, [&](const json& found) {
                                              return text(at(found, "id")) ==
                                                     root + "/transport" + std::string(suffix);
                                          });
                               });
                },
                "Details exposes seek, reverse, reset and both endpoint controls");
            if (checked(root + "/details/settings/loop"))
                pointer(root + "/details/settings/loop");
            pointer(root + "/details/speed");
            key("Home", "Home", 36);
            require_check([&] { return scalar(root + "/details/speed") == 0.25; },
                          "keyboard changes the real playback speed");
            // Playback intentionally stops offscreen. Bring the canvas back before testing its
            // clock; the nearby transport remains visible with it.
            visible_bounds(root + "/stage");
            const std::string setting = root + "/stage/scene/composition/setting/quiet";
            pointer(root + "/transport/buttons/state-a");
            require_check(
                [&] {
                    return !node(setting).is_null() && !checked(setting) &&
                           scalar(root + "/transport/seek") == 0;
                },
                "A selects the real initial scene state");
            pointer(root + "/transport/buttons/state-b");
            require_check([&] { return checked(setting) && scalar(root + "/transport/seek") == 1; },
                          "B selects the real final scene state");
            pointer(root + "/transport/buttons/reset");
            require_check(
                [&] {
                    return !node(setting).is_null() && !checked(setting) &&
                           scalar(root + "/transport/seek") == 0;
                },
                "Reset restores the real initial state and player position");
            pointer(root + "/transport/seek", 0.75);
            require_check(
                [&] {
                    const double value = number(scalar(root + "/transport/seek"));
                    return value > 0.6 && value < 0.9;
                },
                "pointer seeking selects an intermediate frame");
            const double sought = number(scalar(root + "/transport/seek"));
            script.sleep(200);
            check(std::abs(number(scalar(root + "/transport/seek")) - sought) < 0.01,
                  "the sought frame remains paused");
            pointer(root + "/transport/buttons/reverse");
            pointer(root + "/transport/buttons/play");
            require_check([&] { return number(scalar(root + "/transport/seek")) < sought - 0.02; },
                          "Reverse and Play move toward the previous frame");
            pause_transport(root);
            const double stopped = number(scalar(root + "/transport/seek"));
            script.sleep(250);
            check(std::abs(number(scalar(root + "/transport/seek")) - stopped) < 0.01,
                  "Pause stops the actual player clock");
            close_to_wall(root, "motion", "#/docs/motion");
            open_specimen("motion", "toggle-thumb");
            pointer(root + "/stage", 0.5, true);
            require_check(
                [&] { return label(root + "/transport/hint").contains("real interface"); },
                "moving the pointer into the canvas enters real interaction");
            const std::string toggle = root + "/stage/scene/composition/setting/quiet";
            const bool before = checked(toggle);
            pointer(toggle);
            require_check([&] { return checked(toggle) != before; },
                          "the basic scene toggle changes real semantic state");
            key(" ", "Space", 32);
            require_check([&] { return checked(toggle) == before; },
                          "keyboard Space operates that same scene control");
            shot("basic-motion-interactive");
        });

        scenario("component-motion", [&] {
            reference("motion");
            open_specimen("motion", "shape-morph");
            const std::string root = gallery("motion");
            find_design(root, "component-motion");
            require_check([&] { return exists(root + "/details/selection"); },
                          "a component motion case shows its reusable component animation "
                          "selection");
            pointer(root + "/transport/buttons/state-a");
            const std::string card = root + "/stage/scene/composition/card";
            const auto x_of = [&](const std::string& id) {
                return number(dig(node(id), "bounds", 0));
            };
            const auto y_of = [&](const std::string& id) {
                return number(dig(node(id), "bounds", 1));
            };
            const json at_a = bounds_of(card);
            pointer(root + "/transport/buttons/state-b");
            require_check([&] { return std::abs(x_of(card) - number(at(at_a, 0)) - 24) < .5; },
                          "the actual component group translates by the public recipe");
            const auto invoke_motion = [&](std::string_view verb, json args = json::object()) {
                const json reply =
                    script.value("window.omui.invoke(" + q("motion." + std::string(verb)) + "," +
                                 cdp::stringify(args) + ")");
                script.value("new Promise(resolve => requestAnimationFrame(() => "
                             "requestAnimationFrame(() => resolve(true))))");
                return reply;
            };
            const json discovered = invoke_motion("inspect");
            check(at(discovered, "exitCode") == 0 && truthy(dig(discovered, "data", "available")) &&
                      text(dig(discovered, "data", "selected")) == "component-motion",
                  "delivered Web host exposes the actual public animation through a native "
                  "semantic verb",
                  discovered);
            for (const double progress : {.25, .5, .75}) {
                const json sought = invoke_motion("seek", {{"progress", progress}});
                check(at(sought, "exitCode") == 0 && truthy(dig(sought, "data", "executed")) &&
                          std::abs(number(dig(sought, "data", "progress")) - progress) < 1e-9,
                      "Agent semantic seek samples progress " + js_number(progress), sought);
                require_check(
                    [&] {
                        return std::abs(number(scalar(root + "/transport/seek")) - progress) < .001;
                    },
                    "human progress control observes Agent seek " + js_number(progress));
            }
            const json at_seek = bounds_of(card);
            const json invalid = invoke_motion("seek", {{"progress", 2}});
            check(at(invalid, "exitCode") != 0 &&
                      std::abs(x_of(card) - number(at(at_seek, 0))) < .5,
                  "invalid semantic progress preserves the live browser pose");
            invoke_motion("seek", {{"progress", 1}});
            pointer(root + "/transport/buttons/play");
            require_check(
                [&] {
                    const double progress = number(scalar(root + "/transport/seek"));
                    return progress > .05 && progress < .9;
                },
                "the native Hanim host advances intermediate frames in the actual browser");
            pause_transport(root);
            pointer(root + "/details/selection");
            key("Home", "Home", 36);
            key("ArrowDown", "ArrowDown", 40);
            key("Enter", "Enter", 13);
            require_check([&] { return scalar(root + "/details/selection") == "parts"; },
                          "keyboard selects independently animated parts");
            pointer(root + "/transport/buttons/state-a");
            const json title_a = bounds_of(card + "/title");
            const json part_card_a = bounds_of(card);
            pointer(root + "/transport/buttons/state-b");
            require_check(
                [&] {
                    return std::abs(x_of(card) - number(at(part_card_a, 0))) < .5 &&
                           std::abs(x_of(card + "/title") - number(at(title_a, 0))) > 2;
                },
                "part animation preserves the card layout while moving its selected title");
            pointer(root + "/details/effect");
            key("End", "End", 35);
            key("Enter", "Enter", 13);
            require_check([&] { return scalar(root + "/details/effect") == "layered"; },
                          "keyboard composes nested Hanim image effects on the real note");
            pointer(root + "/transport/buttons/state-b");
            shot("component-motion-nested-effects");
            pointer(root + "/details/copy");
            const std::string source = clipboard_text();
            check(source.contains("hanim::parallel") && source.contains(".part=\"title\"") &&
                      source.contains("playback.play") && source.contains(".rotation_offset=") &&
                      source.contains(".scale_factor=") && source.contains("hanim::composite") &&
                      source.contains("fx::blur") && source.contains("fx::tone"),
                  "the browser clipboard receives the public C++ component recipe with relative "
                  "part transforms");
            type(card + "/editor", "A browser-made note");
            pointer(card + "/pin");
            const bool pinned = checked(card + "/pin");
            pointer(card + "/actions/save");
            require_check(
                [&] {
                    return truthy(script.value(
                        "window.__omuiClipboardText().then(t=>{try{const n=JSON.parse(t);return "
                        "n.title==='A browser-made note'&&n.pinned===" +
                        std::string(pinned ? "true" : "false") +
                        "}catch{return false}}).catch(()=>false)"));
                },
                "real component input copies the edited note JSON to the browser clipboard");
            key("Escape", "Escape", 27);
            require_check(
                [&] { return label(root + "/transport/hint").contains("real interface"); },
                "focus in the animated note keeps the stage in real interaction mode");
            shot("component-motion-live-note");
            pointer(root + "/details/effect");
            key("Home", "Home", 36);
            key("Enter", "Enter", 13);
            require_check(
                [&] {
                    return scalar(root + "/details/effect") == "none" &&
                           scalar(card + "/editor") == "A browser-made note";
                },
                "removing the effect preserves the actual edited component data");
            pointer(root + "/details/selection");
            key("End", "End", 35);
            key("ArrowUp", "ArrowUp", 38);
            key("ArrowUp", "ArrowUp", 38);
            key("ArrowUp", "ArrowUp", 38);
            key("Enter", "Enter", 13);
            require_check([&] { return scalar(root + "/details/selection") == "feedback"; },
                          "keyboard selects public feedback composition");
            pointer(root + "/transport/buttons/state-a");
            const json feedback_a = bounds_of(card);
            pointer(root + "/transport/buttons/state-b");
            require_check([&] { return std::abs(x_of(card) - number(at(feedback_a, 0))) < .5; },
                          "feedback returns the note to its starting position");
            pointer(root + "/details/copy");
            const std::string feedback_source = clipboard_text();
            check(std::ranges::all_of(std::array<std::string_view, 3>{"pulse", "shake", "wobble"},
                                      [&](std::string_view name) {
                                          return feedback_source.contains("hanim::anim::" +
                                                                          std::string(name));
                                      }),
                  "the browser copies the actual public feedback library calls");
            pointer(root + "/transport/buttons/play");
            require_check([&] { return std::abs(x_of(card) - number(at(feedback_a, 0))) > 2; },
                          "public feedback moves through an actual intermediate frame");
            pause_transport(root);
            shot("component-motion-feedback");
            pointer(root + "/details/selection");
            key("Home", "Home", 36);
            for (int index = 0; index < 3; ++index)
                key("ArrowDown", "ArrowDown", 40);
            key("Enter", "Enter", 13);
            require_check([&] { return scalar(root + "/details/selection") == "path"; },
                          "keyboard selects the public timed path recipe");
            invoke_motion("seek", {{"progress", 0}});
            const json path_start = bounds_of(card);
            invoke_motion("seek", {{"progress", .25}});
            require_check(
                [&] {
                    return std::abs(x_of(card) - number(at(path_start, 0)) - 14) < .5 &&
                           std::abs(y_of(card) - number(at(path_start, 1)) + 8) < .5;
                },
                "semantic seek reaches the authored path waypoint");
            invoke_motion("seek", {{"progress", 1}});
            require_check(
                [&] {
                    return std::abs(x_of(card) - number(at(path_start, 0))) < .5 &&
                           std::abs(y_of(card) - number(at(path_start, 1))) < .5;
                },
                "the public path returns to the original presentation");
            pointer(root + "/details/copy");
            const std::string path_source = clipboard_text();
            check(path_source.contains("hanim::anim::move_through") &&
                      path_source.contains("PointPath::bend"),
                  "copied route uses only the public path and animation API");
            visible_bounds(card);
            invoke_motion("seek", {{"progress", .5}});
            shot("component-motion-path");
            pointer(root + "/details/effect");
            key("Home", "Home", 36);
            for (int index = 0; index < 4; ++index)
                key("ArrowDown", "ArrowDown", 40);
            key("Enter", "Enter", 13);
            require_check([&] { return scalar(root + "/details/effect") == "opacity"; },
                          "keyboard selects shared group opacity");
            invoke_motion("seek", {{"progress", .5}});
            check(scalar(card + "/editor") == "A browser-made note",
                  "group opacity preserves the actual note content");
            pointer(root + "/details/copy");
            const std::string faded_source = clipboard_text();
            check(faded_source.contains("hanim::fx::opacity(1, 0.35f)"),
                  "copied composition uses the public opacity effect");
            visible_bounds(card);
            invoke_motion("seek", {{"progress", .5}});
            const json opacity_bounds = bounds_of(card);
            const auto capture_card = [&]() -> std::string {
                if (script.thrown())
                    return {};
                if (!opacity_bounds.is_array()) {
                    script.raise("Cannot read properties of undefined");
                    return {};
                }
                const json answer =
                    script.call("Page.captureScreenshot", {{"format", "png"},
                                                           {"clip",
                                                            {{"x", at(opacity_bounds, 0)},
                                                             {"y", at(opacity_bounds, 1)},
                                                             {"width", at(opacity_bounds, 2)},
                                                             {"height", at(opacity_bounds, 3)},
                                                             {"scale", 1}}}});
                return text(dig(answer, "result", "data"));
            };
            const std::string faded_pixels = capture_card();
            shot("component-motion-opacity");
            const json cleared = script.value("window.omui.act(" + q(root + "/details/effect") +
                                              ",'set_value',{args:{value:'none'}})");
            check(at(cleared, "exitCode") == 0,
                  "Agent clears the opacity effect through the real selector", cleared);
            script.value("new Promise(resolve => requestAnimationFrame(() => "
                         "requestAnimationFrame(() => resolve(true))))");
            invoke_motion("seek", {{"progress", .5}});
            require_check(
                [&] {
                    const json now = bounds_of(card);
                    if (!now.is_array() || !opacity_bounds.is_array())
                        return false;
                    for (std::size_t index = 0; index < now.size(); ++index)
                        if (!(std::abs(number(now[index]) - number(at(opacity_bounds, index))) <
                              .5))
                            return false;
                    return true;
                },
                "clearing opacity retains the same sampled UI geometry");
            const std::string full_pixels = capture_card();
            check(!faded_pixels.empty() && !full_pixels.empty() && faded_pixels != full_pixels,
                  "shared opacity changes actual browser pixels at the same animation pose");
            for (const auto& [name, data] :
                 std::vector<std::pair<std::string, std::string>>{{"opacity-midpoint-crop",
                                                                   faded_pixels},
                                                                  {"opacity-off-crop",
                                                                   full_pixels}})
                if (!data.empty() && !script.thrown()) {
                    cdp::write_base64(output / (name + ".png"), data);
                    report["screenshots"].push_back(output_name + "/" + name + ".png");
                }

            pointer(root + "/details/selection");
            key("End", "End", 35);
            key("ArrowUp", "ArrowUp", 38);
            key("ArrowUp", "ArrowUp", 38);
            key("Enter", "Enter", 13);
            require_check([&] { return scalar(root + "/details/selection") == "rhythm"; },
                          "keyboard selects the public move-pause-return program");
            invoke_motion("seek", {{"progress", 0}});
            const json rhythm_start = bounds_of(card);
            invoke_motion("seek", {{"progress", .5}});
            require_check(
                [&] { return std::abs(x_of(card) - number(at(rhythm_start, 0)) - 18) < .5; },
                "the independent timeline reaches its held position");
            const json held_card = bounds_of(card);
            const json held_title = bounds_of(card + "/title");
            invoke_motion("seek", {{"progress", .6}});
            require_check(
                [&] {
                    const json now_card = bounds_of(card), now_title = bounds_of(card + "/title");
                    if (!now_card.is_array() || !now_title.is_array())
                        return false;
                    bool card_held = true, title_moved = false;
                    for (std::size_t index = 0; index < now_card.size(); ++index)
                        if (!(std::abs(number(now_card[index]) - number(at(held_card, index))) <
                              .5))
                            card_held = false;
                    for (std::size_t index = 0; index < now_title.size(); ++index)
                        if (std::abs(number(now_title[index]) - number(at(held_title, index))) > .1)
                            title_moved = true;
                    return card_held && title_moved;
                },
                "the card holds while its independently delayed title continues animating");
            visible_bounds(card);
            invoke_motion("seek", {{"progress", .6}});
            shot("component-motion-rhythm");
            invoke_motion("seek", {{"progress", 1}});
            require_check([&] { return std::abs(x_of(card) - number(at(rhythm_start, 0))) < .5; },
                          "the rhythm returns after its explicit pause");
            pointer(root + "/details/copy");
            const std::string rhythm_source = clipboard_text();
            check(rhythm_source.contains("hanim::wait(400ms)") &&
                      rhythm_source.contains("hanim::delay(200ms"),
                  "the copied rhythm uses public timeline intervals without a page timer");
            pointer(root + "/details/selection");
            key("End", "End", 35);
            key("ArrowUp", "ArrowUp", 38);
            key("Enter", "Enter", 13);
            require_check([&] { return scalar(root + "/details/selection") == "steps"; },
                          "keyboard selects the finite repeated child program");
            invoke_motion("seek", {{"progress", 0}});
            const json steps_start = bounds_of(card);
            for (const auto& [progress, offset] :
                 std::vector<std::pair<double, double>>{{.25, 8}, {.5, 16}, {.75, 24}, {1, 0}}) {
                invoke_motion("seek", {{"progress", progress}});
                require_check(
                    [&] { return std::abs(x_of(card) - number(at(steps_start, 0)) - offset) < .5; },
                    "finite repeat reaches cumulative step " + js_number(offset) + " at progress " +
                        js_number(progress));
            }
            pointer(root + "/details/copy");
            const std::string steps_source = clipboard_text();
            check(steps_source.contains("hanim::repeat(3, hanim::parallel(") &&
                      steps_source.contains("hanim::wait(100ms)"),
                  "the browser copies one finite child program without manually duplicating its "
                  "iterations");
            visible_bounds(card);
            invoke_motion("seek", {{"progress", .5}});
            shot("component-motion-steps");
            pointer(root + "/details/selection");
            key("End", "End", 35);
            key("Enter", "Enter", 13);
            require_check([&] { return scalar(root + "/details/selection") == "route"; },
                          "keyboard selects the public shared-distance route");
            invoke_motion("seek", {{"progress", 0}});
            const json route_start = bounds_of(card);
            for (const auto& [progress, offset] :
                 std::vector<std::pair<double, double>>{{.25, 21}, {.5, 0}, {.75, -21}, {1, 0}}) {
                invoke_motion("seek", {{"progress", progress}});
                require_check(
                    [&] { return std::abs(x_of(card) - number(at(route_start, 0)) - offset) < .5; },
                    "closed route visits displacement " + js_number(offset) + " at progress " +
                        js_number(progress));
            }
            pointer(root + "/details/copy");
            const std::string route_source = clipboard_text();
            check(route_source.contains("hanim::anim::follow_path") &&
                      route_source.contains("hanim::MotionPath"),
                  "copied code uses the shared path and public animation instead of a page "
                  "sampler");
            visible_bounds(card);
            invoke_motion("seek", {{"progress", .25}});
            shot("component-motion-route");
            pointer(root + "/transport/buttons/details");
            require_check([&] { return !exists(root + "/details/selection"); },
                          "closing Details removes the component recipe controls");
        });

        scenario("creative-demos", [&] {
            top("explore");
            // All the compositions at once on the motion page's canvas, each a stated item. The
            // studio (E2d): the bar's Motion arrives at the works; the region chips fly the camera
            // to the compositions, all of them in view.
            const std::string wall = "main/body/view/explore/page/wall/canvas";
            pointer("main/body/view/explore/page/wall/tools/region/compositions");
            require_check(
                [&] {
                    return std::ranges::count_if(snapshot(wall), [&](const json& found) {
                               return text(at(found, "role")) == "article" &&
                                      text(at(found, "id")).starts_with(wall + "/composition-");
                           }) >= 20;
                },
                "the compositions region shows every composition at once");
            // One opened (its address), the page is that composition's: its stage, its picker.
            script.value("(location.hash = '#/motion/palette-studio', true)");
            const std::string root = gallery("explore");
            require_check([&] { return exists(root + "/stage/scene"); },
                          "a composition opened has its own stage");
            require_check([&] { return std::atoi(label(root + "/heading/count").c_str()) >= 20; },
                          "creative catalog contains at least twenty designs");
            case_page(root, "a creative composition");
            check(!node(root + "/stage/scene/composition/workspace/swatches/swatch-0/hex")
                       .is_null(),
                  "creative demos open the useful palette first");
            check(number(dig(node(root + "/stage/scene"), "bounds", 3)) <= 300.5,
                  "the default palette stage stays within 300 pixels");
            open_picker(root);
            check(choices(root).size() == 8,
                  "creative catalog also limits mounted choices to eight");
            find_design(root, "packing-board");
            const std::string work = root + "/stage/scene/composition/workspace";
            pointer(root + "/stage", 0.5, true);
            pointer(work + "/pack");
            require_check(
                [&] {
                    return std::ranges::count_if(snapshot(work + "/items"), [](const json& found) {
                               return text(at(found, "role")) == "checkbox" &&
                                      has_state(found, "checked");
                           }) == 3;
                },
                "packing-board really packs all three stable items");
            shot("creative-packing");
            find_design(root, "palette-studio");
            pointer(root + "/stage", 0.5, true);
            const auto hexes = [&] {
                std::vector<std::string> out;
                for (int index = 0; index < 4; ++index)
                    out.push_back(
                        label(work + "/swatches/swatch-" + std::to_string(index) + "/hex"));
                return out;
            };
            const auto original_colours = hexes();
            const std::string mix = work + "/mix";
            pointer(mix, 0.8);
            const double before = number(scalar(mix));
            key("ArrowLeft", "ArrowLeft", 37);
            require_check([&] { return number(scalar(mix)) < before; },
                          "palette-studio accepts a real slider keyboard adjustment");
            check(label(work + "/actions/amount").contains("%"),
                  "the palette shows its current real balance");
            require_check(
                [&] {
                    static const std::regex hex(R"(^#[0-9a-f]{6}$)", std::regex::icase);
                    const auto colours = hexes();
                    if (colours.size() != 4)
                        return false;
                    for (std::size_t index = 0; index < 4; ++index)
                        if (!std::regex_match(colours[index], hex) ||
                            colours[index] == original_colours[index])
                            return false;
                    return std::set<std::string>(colours.begin(), colours.end()).size() == 4;
                },
                "contrast changes all four readable, distinct colour outputs");
            const auto colours = hexes();
            pointer(work + "/actions/copy");
            require_check(
                [&] {
                    return truthy(script.value(
                        "window.__omuiClipboardText().then(t=>t.startsWith(':root {')&&" +
                        cdp::stringify(json(colours)) +
                        ".every(hex=>t.includes(hex))&&['light','soft','ink','shade'].every(name=>"
                        "t.includes('--palette-'+name+':'))).catch(()=>false)"));
                },
                "Copy palette puts usable CSS with the current four colours on the actual browser "
                "clipboard");
            shot("creative-palette");
        });

        scenario("text-copy", [&] {
            // Words selected and copied as on any page: dragged from one paragraph of the docs into
            // the next, Ctrl+C.
            reference("features");
            json paragraphs = json::array();
            for (const auto& found : snapshot("main/body/view/features/page"))
                if (text(at(found, "role")) == "paragraph" && truthy(at(found, "bounds")) &&
                    text(at(found, "name")).size() > 40)
                    paragraphs.push_back(found);
            if (script.thrown())
                return;
            check(paragraphs.size() >= 2, "the docs have paragraphs to select", paragraphs.size());
            if (paragraphs.size() < 2)
                return;
            const json first = paragraphs[0], second = paragraphs[1];
            visible_bounds(text(at(first, "id")));
            visible_bounds(text(at(second, "id")));
            const json a = bounds_of(text(at(first, "id"))), b = bounds_of(text(at(second, "id")));
            if (script.thrown())
                return;
            const double from_x = number(at(a, 0)) + 2,
                         from_y = number(at(a, 1)) + std::min(number(at(a, 3)) / 2, 10.0);
            const double to_x = number(at(b, 0)) + 60,
                         to_y = number(at(b, 1)) + std::min(number(at(b, 3)) / 2, 10.0);
            dispatch("Input.dispatchMouseEvent",
                     {{"type", "mouseMoved"}, {"x", from_x}, {"y", from_y}});
            dispatch("Input.dispatchMouseEvent", {{"type", "mousePressed"},
                                                  {"x", from_x},
                                                  {"y", from_y},
                                                  {"button", "left"},
                                                  {"clickCount", 1},
                                                  {"buttons", 1}});
            for (int step = 1; step <= 6; ++step)
                dispatch("Input.dispatchMouseEvent", {{"type", "mouseMoved"},
                                                      {"x", from_x + (to_x - from_x) * step / 6},
                                                      {"y", from_y + (to_y - from_y) * step / 6},
                                                      {"button", "left"},
                                                      {"buttons", 1}});
            dispatch("Input.dispatchMouseEvent", {{"type", "mouseReleased"},
                                                  {"x", to_x},
                                                  {"y", to_y},
                                                  {"button", "left"},
                                                  {"clickCount", 1}});
            script.sleep(150);
            key("c", "KeyC", 67, mcpplibs::os::is_macos ? 4 : 2);
            const std::string first_name = text(at(first, "name"));
            require_check(
                [&] {
                    const std::string copied = clipboard_text();
                    if (script.thrown())
                        return false;
                    const std::string line = copied.substr(0, copied.find('\n'));
                    return copied.contains('\n') && first_name.contains(line.substr(0, 20));
                },
                "Ctrl+C puts the words dragged over, across two paragraphs, on the browser "
                "clipboard",
                [&] { return json(clipboard_text()); });
        });

        scenario("agent-native", [&] {
            top("agent");
            const std::string root = "main/body/view/agent/page/agent_feature";
            require_check([&] { return scalar(root + "/modes") == "use"; },
                          "agent opens the Use path");
            check(label(root + "/use/label").contains("Simulated"),
                  "the demonstration is explicitly identified as simulated");
            pointer(root + "/use/actions/play");
            require_check(
                [&] {
                    const json stop = node(root + "/use/actions/stop");
                    return !stop.is_null() && !has_state(stop, "disabled");
                },
                "the simulation offers a real Stop control");
            pointer(root + "/use/actions/stop");
            script.sleep(1500);
            check(!label(root + "/specimen/receipt").contains("Recorded here"),
                  "Stop cancels the pending local record");
            pointer(root + "/use/actions/play");
            require_check(
                [&] { return label(root + "/specimen/receipt").contains("Recorded here"); },
                "the explicitly started agent completes real task actions");
            check(snapshot(root + "/use/log").size() > 2,
                  "the agent action log contains the actual task records");
            pointer(root + "/history/undo");
            require_check(
                [&] { return !label(root + "/specimen/receipt").contains("Recorded here"); },
                "the real task action can be undone");
            segment(root + "/modes", 1, 3, "develop");
            require_check([&] { return exists(root + "/preview/actions/verify"); },
                          "Develop exposes its stable Verify action");
            check(!exists(root + "/preview/actions/run"),
                  "different task meanings do not share the legacy Run identity");
            pointer(root + "/preview/actions/verify");
            require_check([&] { return label(root + "/status").starts_with("PASS"); },
                          "the real Verify primary runs read, change and check");
            check(!exists(root + "/develop/expected"), "development parameters start unmounted");
            pointer(root + "/method/toggle");
            require_check([&] { return exists(root + "/develop/expected"); },
                          "explicit Agent details mounts protocol controls");
            type(root + "/develop/expected", "Browser verified idea");
            pointer(root + "/develop/requests/assert");
            require_check([&] { return label(root + "/status").starts_with("FAIL"); },
                          "Assert genuinely fails before the requested change");
            pointer(root + "/develop/requests/read");
            require_check([&] { return label(root + "/status").contains("semantic subtree"); },
                          "Read reports the actual runtime snapshot");
            pointer(root + "/develop/requests/change");
            pointer(root + "/develop/requests/assert");
            require_check(
                [&] {
                    return label(root + "/status").starts_with("PASS") &&
                           scalar(root + "/specimen/title") == "Browser verified idea";
                },
                "Change then Assert passes on the actual current value");
            segment(root + "/modes", 2, 3, "design");
            require_check([&] { return exists(root + "/preview/actions/arrange"); },
                          "Design exposes its distinct Arrange action");
            pointer(root + "/specimen/title", 0.5, true);
            const json previous_card = node(root + "/specimen/decision_card");
            const bool before_card =
                !previous_card.is_null() && !has_state(previous_card, "removing");
            pointer(root + "/preview/actions/arrange");
            require_check(
                [&] {
                    const json card = node(root + "/specimen/decision_card");
                    return (!card.is_null() && !has_state(card, "removing")) != before_card;
                },
                "the real Arrange primary changes this task grouping");
            check(!exists(root + "/design/spacing"), "design parameters start unmounted");
            pointer(root + "/method/toggle");
            require_check([&] { return exists(root + "/design/spacing"); },
                          "explicit Agent details mounts design parameters");
            segment(root + "/design/layout", 1, 2, "cards");
            pointer(root + "/design/spacing");
            key("End", "End", 35);
            require_check([&] { return scalar(root + "/design/spacing") == 24; },
                          "Design changes the live specimen spacing");
            check(!node(root + "/specimen/record").is_null(),
                  "design layout preserves its action address");
            pointer(root + "/design/copy_prompt");
            require_check(
                [&] {
                    return truthy(script.value(
                        R"(window.__omuiClipboardText().then(t=>t.includes('"origin":"user_content"')&&t.includes('Browser verified idea')&&t.includes('"spacing":24')&&t.includes('agent-design')).catch(()=>false))"));
                },
                "the real clipboard receives the design prompt with current constraints and quoted "
                "user data");
            shot("agent-design");
        });

        scenario("unified-interface", [&] {
            reference("interface");
            const std::string root = "main/body/view/interface/page/interface_feature",
                              object = root + "/workspace/object";
            check(!exists(root + "/details/language") && !exists(root + "/details/source"),
                  "interface languages and source start unmounted");
            type(object + "/title", "Same object in browser");
            const auto ids = [&] {
                std::vector<std::string> out;
                for (const auto& found : snapshot(object))
                    out.push_back(text(at(found, "id")));
                std::ranges::sort(out);
                return out;
            };
            const auto before_ids = ids();
            require_check(
                [&] {
                    return label(root + "/workspace/meaning/title")
                        .contains("Same object in browser");
                },
                "the live semantic view reads back the edited object");
            pointer(root + "/toggle_details");
            require_check([&] { return exists(root + "/details/language"); },
                          "explicit Details mounts the language picker");
            for (const auto& [index, language, spelling] :
                 std::vector<std::tuple<int, std::string, std::string>>{{0, "cpp", "text_box("},
                                                                        {1, "ts", "textBox("}}) {
                segment(root + "/details/language", index, 2, language);
                require_check(
                    [&] {
                        return label(root + "/details/source").contains(spelling) &&
                               label(root + "/details/source").contains("Same object in browser");
                    },
                    language +
                        " displays its existing builder API expression with the current value");
            }
            check(ids() == before_ids && scalar(object + "/title") == "Same object in browser",
                  "changing source language preserves the same object IDs and model");
            const std::string source = label(root + "/details/source");
            pointer(root + "/details/copy");
            require_check(
                [&] {
                    return truthy(script.value("window.__omuiClipboardText().then(t=>t===" +
                                               q(source) + ").catch(()=>false)"));
                },
                "the browser clipboard receives the displayed current API expressions");
            shot("interface-language-details");
        });

        scenario("scoped-styling", [&] {
            reference("styles");
            const std::string root = "main/body/view/styles/page/styles_feature";
            check(!exists(root + "/details/radius") && !exists(root + "/details/scope"),
                  "style parameters and source start unmounted");
            pointer(root + "/preview_controls/preview_pause");
            require_check(
                [&] { return label(root + "/preview_controls/preview_pause").contains("Resume"); },
                "the actual preview Pause control pauses automatic changes");
            const std::string left = root + "/comparison/default/controls/quiet",
                              right = root + "/comparison/styled/controls/quiet";
            const bool before = checked(left);
            pointer(left);
            require_check(
                [&] { return checked(left) != before && checked(right) == checked(left); },
                "the default and scoped styles operate the same real model");
            check(text(at(node(root + "/comparison/styled/backdrop/chart"), "role")) == "canvas",
                  "the scoped glass controls sit over a real session data chart");
            const bool quiet_before_tabs = checked(left);
            pointer(root + "/comparison/styled/controls/lens/" +
                    (quiet_before_tabs ? "on" : "off"));
            require_check(
                [&] {
                    return checked(left) != quiet_before_tabs && checked(right) == checked(left);
                },
                "the glass notification tabs operate that same quiet setting");
            pointer(root + "/actions/details");
            require_check([&] { return exists(root + "/details/radius"); },
                          "explicit Details mounts style parameters");
            pointer(root + "/details/radius");
            key("End", "End", 35);
            require_check([&] { return scalar(root + "/details/radius") == 28; },
                          "keyboard input changes the real sample corner parameter");
            shot("scoped-styles-details");
        });
        check(report["exceptions"].empty(), "no browser runtime exceptions occurred",
              report["exceptions"]);
    });
    if (browser)
        browser->close();
    if (server)
        server->close();
    script.page = nullptr;
    report["ok"] = script.all_ok(true);
    write_text(output / "report.json", cdp::pretty(report) + "\n");
    return report;
}

// --- first views (site-presentation.mjs) ---
// First-view density and real input after the preview's handoff: the no-JavaScript catalogue, each
// language at phone and desktop widths, the styles comparison, local effect controls, the agent's
// handoff, reduced motion.

json presentation(const Context& context) {
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    const fs::path output = context.root / "target/site-presentation";
    const std::string output_name = "target/site-presentation";
    std::error_code error;
    fs::create_directories(output, error);
    cdp::Script script(true);
    script.report = {{"checks", json::array()},
                     {"screenshots", json::array()},
                     {"exceptions", json::array()}};
    json& report = script.report;
    const auto check = [&](bool ok, std::string_view what,
                           std::optional<json> detail = std::nullopt) {
        return script.check(ok, what, std::move(detail));
    };
    std::string place;
    int width = 0, height = 0;
    const auto wait = [&](const std::function<bool()>& predicate) {
        return script.until_count(predicate, 200, 75);
    };
    const auto nodes = [&](const std::string& root) {
        return script.value("window.omui.snapshot({root:" + q(root) +
                            ",depth:-1}).then(r=>r.exitCode===0?r.data?.nodes:[])");
    };
    const auto node = [&](const std::string& id) -> json { return cdp::find_id(nodes(id), id); };
    const auto scalar = [&](const std::string& id) { return scalar_of(node(id)); };
    const auto checked = [&](const std::string& id) { return has_state(node(id), "checked"); };
    const auto present = [&](const std::string& id) {
        return truthy(
            script.value(R"(document.querySelector('[data-omui-id="'+)" + q(id) + R"(+'"]'))"));
    };
    const auto require_state = [&](const std::function<bool()>& predicate, std::string_view what) {
        const bool ok = wait(predicate);
        if (script.thrown())
            return;
        check(ok, what);
        if (!ok)
            script.raise(std::string(what));
    };
    const auto pointer = [&](const std::string& id, double fraction = 0.5, bool move_only = false) {
        for (int attempt = 0; attempt < 20 && !script.thrown(); ++attempt) {
            const json found = node(id);
            json view;
            if (id.starts_with("main/bar/"))
                view = {0, 0, width, height};
            else
                view = at(node("main/body/view/" + place), "bounds");
            if (!truthy(at(found, "bounds")) || !truthy(view) || has_state(found, "hidden")) {
                script.sleep(80);
                continue;
            }
            const json& bounds = at(found, "bounds");
            const double x = number(at(bounds, 0)), y = number(at(bounds, 1)),
                         w = number(at(bounds, 2)), h = number(at(bounds, 3));
            const double vx = number(at(view, 0)), vy = number(at(view, 1)),
                         vw = number(at(view, 2)), vh = number(at(view, 3));
            if (y >= vy + 1 && y + h <= vy + vh - 1 && x + w * fraction >= vx &&
                x + w * fraction <= vx + vw) {
                const double px = x + w * fraction, py = y + h / 2;
                script.call("Input.dispatchMouseEvent",
                            {{"type", "mouseMoved"}, {"x", px}, {"y", py}});
                if (!move_only)
                    for (const std::string type : {"mousePressed", "mouseReleased"})
                        script.call("Input.dispatchMouseEvent", {{"type", type},
                                                                 {"x", px},
                                                                 {"y", py},
                                                                 {"button", "left"},
                                                                 {"clickCount", 1}});
                script.sleep(80);
                return;
            }
            script.call("Input.dispatchMouseEvent", {{"type", "mouseWheel"},
                                                     {"x", vx + vw * 0.8},
                                                     {"y", vy + vh / 2},
                                                     {"deltaX", 0},
                                                     {"deltaY", y + h / 2 - (vy + vh / 2)}});
            script.sleep(120);
        }
        script.raise("Control did not enter the visible stage: " + id);
    };
    const auto key = [&](std::string_view name, std::string_view code, int virtual_key,
                         int modifiers = 0) {
        for (const std::string type : {"keyDown", "keyUp"})
            script.call("Input.dispatchKeyEvent", {{"type", type},
                                                   {"key", name},
                                                   {"code", code},
                                                   {"windowsVirtualKeyCode", virtual_key},
                                                   {"modifiers", modifiers}});
    };
    const auto type = [&](const std::string& id, const std::string& value) {
        pointer(id);
        require_state(
            [&] {
                return truthy(script.value(
                    R"(document.activeElement?.matches('textarea[data-omui="input"]'))"));
            },
            "the real editor has browser focus");
        key("a", "KeyA", 65, 2);
        script.call("Input.insertText", {{"text", value}});
        require_state([&] { return scalar(id) == value; },
                      "trusted input reaches the current editor");
    };
    const auto open_route = [&](const std::string& route, const std::string& page_name) {
        place = page_name;
        script.call("Page.navigate", {{"url", url + "#/" + route}});
        require_state(
            [&] {
                return truthy(script.value(
                    R"(window.omuiStaticHandedOff && !!window.omui && !!document.querySelector('[data-omui-id="main/body/view/)" +
                    page_name + R"(/page"]'))"));
            },
            "live page ready: " + route);
    };
    const auto screenshot = [&](const std::string& name) {
        // Reference stills describe the settled state. Continuous transitions have separate native
        // recordings; a short text/layout morph is not a still.
        script.sleep(500);
        const json answer = script.call("Page.captureScreenshot", {{"format", "png"}});
        if (script.thrown())
            return;
        const std::string data = text(dig(answer, "result", "data"));
        if (data.empty())
            return script.raise("Screenshot failed: " + name);
        cdp::write_base64(output / (name + ".png"), data);
        report["screenshots"].push_back(output_name + "/" + name + ".png");
    };
    std::unique_ptr<cdp::Browser> browser;
    const auto launch = [&](int new_width, int new_height) {
        width = new_width;
        height = new_height;
        auto launched = open(context, width, height);
        if (!launched) {
            script.raise(launched.error());
            return false;
        }
        browser = std::move(*launched);
        script.page = browser.get();
        return true;
    };
    const auto close = [&] {
        if (browser)
            browser->close();
        browser.reset();
        script.page = nullptr;
    };
    script.guard("presentation completed", [&] {
        for (const auto& [route, expected] :
             std::vector<std::pair<std::string, std::string>>{{"docs/motion", "shape-morph"},
                                                              {"zh/motion", "palette-studio"}}) {
            if (!launch(390, 844))
                return;
            script.guard("NoScript presentation " + route, [&] {
                script.call("Emulation.setScriptExecutionDisabled", {{"value", true}});
                script.call("Page.navigate", {{"url", url + "#/" + route}});
                require_state(
                    [&] {
                        return truthy(script.value(
                            "Array.from(document.querySelectorAll('[data-gallery-current=" +
                            q(expected) + "]')).some(n=>n.getBoundingClientRect().width>0)"));
                    },
                    "NoScript has one readable selected specimen: " + route);
                const json visible =
                    script.value("Array.from(document.querySelectorAll('[data-gallery-catalog]'))."
                                 "filter(n=>n.getBoundingClientRect().width>0).map(n=>({open:n."
                                 "open,links:n.querySelectorAll('a').length,summary:n."
                                 "querySelector('summary').textContent}))");
                check(visible.is_array() && visible.size() == 1 &&
                          !truthy(dig(visible, 0, "open")) &&
                          number(dig(visible, 0, "links")) >= (route.contains("docs") ? 40 : 20),
                      "NoScript catalogue starts folded with every design link retained", visible);
                const auto click_visible = [&](const std::string& selector) {
                    // Where the control is once it stays there: the point is the control's own (what
                    // elementFromPoint finds there is it, or inside it) — a disclosure opening just
                    // before (a <details> still growing) moves what is below it for a few frames.
                    json point;
                    for (int attempt = 0; attempt < 20 && !script.thrown(); ++attempt) {
                        point = script.value(
                            "(() => {const n=Array.from(document.querySelectorAll(" + q(selector) +
                            ")).find(n=>n.getBoundingClientRect().width>0);if(!n)return "
                            "null;n.scrollIntoView({block:'center'});const "
                            "r=n.getBoundingClientRect();const x=r.x+r.width/2,y=r.y+r.height/2;"
                            "const hit=document.elementFromPoint(x,y);return "
                            "{x,y,own:!!hit&&(hit===n||n.contains(hit))};})()");
                        if (!truthy(point) || truthy(dig(point, "own")))
                            break;
                        script.sleep(50);
                    }
                    if (script.thrown())
                        return;
                    if (!truthy(point))
                        return script.raise("No visible static control: " + selector);
                    if (!truthy(dig(point, "own")))
                        return script.raise("The static control is not what is under its own middle: " + selector);
                    for (const std::string type : {"mouseMoved", "mousePressed", "mouseReleased"})
                        script.call("Input.dispatchMouseEvent", {{"type", type},
                                                                 {"x", at(point, "x")},
                                                                 {"y", at(point, "y")},
                                                                 {"button", "left"},
                                                                 {"clickCount", 1}});
                    script.sleep(100);
                };
                click_visible("[data-gallery-catalog]>summary");
                click_visible("[data-gallery-catalog] [data-gallery-group]>summary");
                click_visible("[data-gallery-catalog] a");
                // A native link's navigation and the static page's own update land a moment after
                // the click.
                const bool linked = wait([&] {
                    return truthy(
                        script.value("location.hash.startsWith(" + q("#/" + route + "/") + ")"));
                });
                check(linked, "native catalogue links open the requested detail without JavaScript",
                      script.value("location.hash"));
                const bool shown = wait([&] {
                    return truthy(script.value(
                        "(() => {const "
                        "current=Array.from(document.querySelectorAll('[data-gallery-current]'))."
                        "filter(n=>n.getBoundingClientRect().width>0);return current.length===1 && "
                        "current[0].dataset.galleryCurrent===location.hash.split('/').pop();})()"));
                });
                check(shown,
                      "NoScript navigation displays the selected design rather than the overview "
                      "default",
                      script.value("({hash: location.hash, current: "
                                   "Array.from(document.querySelectorAll('[data-gallery-current]'))"
                                   ".filter(n=>n.getBoundingClientRect().width>0).map(n=>n.dataset."
                                   "galleryCurrent)})"));
                check(truthy(script.value(
                          "(() => {const "
                          "details=Array.from(document.querySelectorAll('[data-gallery-details]'))."
                          "filter(n=>n.getBoundingClientRect().width>0);return details.length===1 "
                          "&& !details[0].open;})()")),
                      "NoScript detail metadata remains progressively disclosed");
            });
            close();
        }
        for (const auto& [viewport, language] :
             std::vector<std::pair<int, std::string>>{{360, "en"},
                                                      {390, "en"},
                                                      {390, "zh"},
                                                      {1280, "en"},
                                                      {1280, "zh"}}) {
            if (!launch(viewport, viewport < 900 ? 844 : 800))
                return;
            const int run_width = viewport;
            const std::string run_language = language;
            browser->on(
                [&, run_width, run_language](const std::string& method, const json& params) {
                    if (method == "Runtime.exceptionThrown")
                        report["exceptions"].push_back({{"width", run_width},
                                                        {"lang", run_language},
                                                        {"error", at(params, "exceptionDetails")}});
                });
            const std::string label = language + " " + std::to_string(viewport);
            const std::string suffix = language + "-" + std::to_string(viewport);
            script.attempt(
                [&] {
                    const std::string prefix = language == "zh" ? "zh/" : "";
                    open_route(prefix, "home");
                    const std::string root = "main/body/view/home/page/landing";
                    const json action = node(root + "/intro/actions/explore"),
                               visual = node(root + "/scene/visual");
                    const json& action_bounds = at(action, "bounds");
                    check(truthy(action_bounds) && number(at(action_bounds, 1)) >= 60 &&
                              number(at(action_bounds, 1)) + number(at(action_bounds, 3)) <=
                                  height - 44,
                          label + ": primary destination is visible in the initial viewport",
                          action_bounds);
                    if (viewport < 900)
                        check(number(dig(action, "bounds", 1)) < number(dig(visual, "bounds", 1)),
                              label + ": destination precedes the full mobile scene");
                    script.sleep(500);  // Keep geometry checks at first view; capture the brief
                                        // assembly after it settles.
                    screenshot("home-" + suffix);
                    pointer(root + "/intro/actions/explore");
                    if (!script.thrown())
                        place = "components";
                    require_state([&] { return present("main/body/view/components/page"); },
                                  "the visible home CTA opens the real component page");

                    open_route(prefix + "docs/styles", "styles");
                    const std::string style = "main/body/view/styles/page/styles_feature";
                    check(!present(style + "/details/radius") && !present(style + "/previews"),
                          label + ": extra specimens and parameters start unmounted");
                    screenshot("styles-initial-" + suffix);
                    if (viewport < 900) {
                        check(scalar(style + "/comparison/view") == "styled",
                              label + ": phone starts with the styled specimen");
                        check(!present(style + "/comparison/default") &&
                                  present(style + "/comparison/styled"),
                              label + ": exactly one full-width comparison mounts");
                        const json stage = node(style + "/comparison/styled"),
                                   container = node(style);
                        check(truthy(at(stage, "bounds")) && truthy(at(container, "bounds")) &&
                                  std::abs(number(dig(stage, "bounds", 2)) -
                                           number(dig(container, "bounds", 2))) < 1,
                              label + ": the selected specimen uses the full available width",
                              json{{"stage", at(stage, "bounds")},
                                   {"container", at(container, "bounds")}});
                        const std::string quiet = style + "/comparison/styled/controls/quiet";
                        pointer(quiet, 0.5, true);
                        const bool before = checked(quiet);
                        pointer(quiet);
                        require_state([&] { return checked(quiet) != before; },
                                      "the visible styled control changes the shared model");
                        pointer(style + "/comparison/view", 0.25);
                        require_state(
                            [&] { return present(style + "/comparison/default/controls/quiet"); },
                            "a real comparison selection mounts the default view");
                        check(checked(style + "/comparison/default/controls/quiet") != before,
                              "switching material view preserves the edited model");
                        check(!present(style + "/comparison/styled"),
                              "the other comparison is unmounted on phone");
                        screenshot("styles-default-" + suffix);
                        pointer(style + "/comparison/view", 0.75);
                        require_state([&] { return present(quiet); },
                                      "the styled view can be revisited");
                    } else {
                        check(present(style + "/comparison/default") &&
                                  present(style + "/comparison/styled"),
                              label + ": desktop retains the real side-by-side comparison");
                    }
                    screenshot("styles-" + suffix);

                    pointer(style + "/actions/details");
                    require_state([&] { return present(style + "/details/pixel_amount"); },
                                  "local effect controls appear only after Details");
                    pointer(style + "/details/pixel_amount", 0.8);
                    require_state(
                        [&] {
                            const json value = scalar(style + "/details/pixel_amount");
                            const double amount = value.is_number() ? number(value)
                                                  : value.is_string()
                                                      ? std::strtod(text(value).c_str(), nullptr)
                                                      : std::nan("");
                            return amount > 65;
                        },
                        "real pointer changes the chart pixelation strength");
                    pointer(style + "/details/pixel_size", 0.6);
                    pointer(style + "/details/pixel_device");
                    require_state([&] { return checked(style + "/details/pixel_device"); },
                                  "device-pixel units can be selected by a person");
                    const std::string copied_effect =
                        text(at(node(style + "/details/pixel_source"), "name"));
                    check(copied_effect.contains(R"("device_pixels":1)") &&
                              copied_effect.contains(R"("amount":)"),
                          "displayed configuration follows the actual local effect");
                    const json amount_label = node(style + "/details/pixel_amount_label"),
                               size_label = node(style + "/details/pixel_size_label");
                    check(text(at(amount_label, "name")).contains("%") &&
                              text(at(size_label, "name")).contains("px") &&
                              number(dig(amount_label, "bounds", 3)) > 0 &&
                              number(dig(size_label, "bounds", 3)) > 0,
                          "people see the parameter names and current values as rendered text");
                    screenshot("styles-pixel-controls-" + suffix);
                    // Capturing is a native semantic request, not a DOM reconstruction.
                    pointer(style + "/comparison/styled/controls/quiet", 0.5, true);
                    const json capture =
                        script.value("window.omui.request({op:'capture',node:" +
                                     q(style + "/comparison/styled") + ",region:[0,0,120,100]})");
                    check(at(capture, "exitCode") == 0 &&
                              text(dig(capture, "data", "base64")).starts_with("iVBORw0KGgo"),
                          "Agent obtains actual region PNG pixels from the Web renderer");
                    screenshot("styles-local-pixel-" + suffix);

                    open_route(prefix + "agent/use", "agent");
                    const std::string agent = "main/body/view/agent/page/agent_feature",
                                      title = agent + "/specimen/title";
                    const std::string draft =
                        language == "zh" ? "由我接管的想法" : "My own handoff draft";
                    type(title, draft);
                    script.sleep(2100);
                    check(scalar(title) == draft && !checked(agent + "/specimen/reviewed"),
                          label + ": entering and typing cancels pending automatic task steps");
                    pointer(agent + "/modes", 0.5);
                    require_state([&] { return scalar(agent + "/modes") == "develop"; },
                                  "the Develop path opens through real input");
                    check(present(agent + "/preview/actions/verify") &&
                              !present(agent + "/preview/actions/run"),
                          "Develop uses its own stable Verify action identity");
                    check(!present(agent + "/develop/expected") &&
                              !present(agent + "/develop/requests"),
                          "development protocol parameters are absent from the overview");
                    pointer(agent + "/method/toggle");
                    require_state([&] { return present(agent + "/develop/expected"); },
                                  "explicit details mounts development controls");
                    pointer(agent + "/modes", 5.0 / 6);
                    require_state([&] { return scalar(agent + "/modes") == "design"; },
                                  "the Design path opens through real input");
                    check(present(agent + "/preview/actions/arrange") &&
                              !present(agent + "/preview/actions/run"),
                          "Design uses its own stable Arrange action identity");
                    check(!present(agent + "/design/style") && !present(agent + "/design/spacing"),
                          "design parameters are absent from the overview");
                    screenshot("agent-design-" + suffix);
                    pointer(agent + "/method/toggle");
                    require_state([&] { return present(agent + "/design/spacing"); },
                                  "explicit details mounts design parameters");
                },
                [&](const std::string& message) {
                    check(false, "presentation scenario " + label + " completed",
                          cdp::error_text(message));
                    script.attempt([&] { screenshot("failure-" + suffix); },
                                   [](const std::string&) {});  // keep the original failure
                });
            close();
        }
        if (!launch(390, 844))
            return;
        browser->on([&](const std::string& method, const json& params) {
            if (method == "Runtime.exceptionThrown")
                report["exceptions"].push_back(
                    {{"width", 390}, {"reduced", true}, {"error", at(params, "exceptionDetails")}});
        });
        script.guard("reduced-motion manual task completed", [&] {
            script.call("Emulation.setEmulatedMedia",
                        {{"features", json::array({{{"name", "prefers-reduced-motion"},
                                                    {"value", "reduce"}}})}});
            open_route("agent/use", "agent");
            const std::string agent = "main/body/view/agent/page/agent_feature";
            script.sleep(600);
            check(!checked(agent + "/specimen/reviewed"),
                  "system reduced motion does not start an automatic Agent task");
            pointer(agent + "/use/actions/play");
            require_state(
                [&] {
                    return text(at(node(agent + "/specimen/receipt"), "name"))
                        .contains("Recorded here");
                },
                "explicit Play under reduced motion completes real review and record actions");
            check(nodes(agent + "/use/log").size() > 2,
                  "reduced-motion task completion has real actor records");
            screenshot("agent-use-reduced-en-390");
        });
        close();
        check(report["exceptions"].empty(), "no browser runtime exceptions", report["exceptions"]);
    });
    close();
    (*server)->close();
    report["ok"] = script.all_ok();
    return report;
}

// --- transitions (site-transition.mjs) ---
// Live CPU canvas evidence, independent of semantic target coordinates: mutations through trusted
// CDP pointer, keyboard and resize input; the public bridge only reads snapshots and history.
// Correctness coverage, not a performance test.

json transition(const Context& context) {
    auto server = cdp::Server::start(cdp::pages(context.site));
    if (!server)
        return failed_report(server.error());
    const std::string url = (*server)->url();
    const fs::path output = context.root / "target/site-transition";
    const std::string output_name = "target/site-transition";
    std::error_code error;
    fs::create_directories(output, error);
    const double early_window_ms = 250, max_pixel_age_ms = 80;
    cdp::Script script(true);
    script.report =
        {{"checks", json::array()},
         {"scenarios", json::array()},
         {"exceptions", json::array()},
         {"renderer", "cpu"},
         {"pixelSelection",
          "Bottommost connected saturated blue fill below y=200, including the scoped dark "
          "primary; at least 65×12 CSS px and 400 pixels; disabled fills and disconnected text "
          "excluded. Semantic bounds confirm association, never choose the click pixel."},
         {"coverage",
          {{"earlyWindowMs", early_window_ms},
           {"maxPixelAgeMs", max_pixel_age_ms},
           {"note", "An early-motion sampling window; not a product latency budget."}}}};
    json& report = script.report;
    const std::string root = "main/body/view/agent/page/agent_feature";
    const auto check = [&](bool ok, std::string_view what,
                           std::optional<json> detail = std::nullopt,
                           std::string_view kind = "interaction") {
        json row = {{"ok", ok}, {"what", std::string(what)}, {"kind", std::string(kind)}};
        if (detail && !detail->is_discarded())
            row["detail"] = std::move(*detail);
        script.record(std::move(row));
        return ok;
    };
    const auto dispatch = [&](std::string_view method, json params) {
        return script.dispatch(method, std::move(params));
    };
    const auto node = [&](const std::string& id) -> json {
        return cdp::find_id(script.value("window.omui.snapshot({root:" + q(id) +
                                         ",depth:0}).then(r=>r.exitCode===0?r.data?.nodes:[])"),
                            id);
    };
    const auto live = [](const json& found) {
        return !found.is_null() && !has_state(found, "removing") && !has_state(found, "hidden");
    };
    const auto cards = [&] { return live(node(root + "/specimen/decision_card")); };
    const auto until = [&](const std::function<bool()>& predicate, int timeout = 15000) {
        return script.until_time(predicate, timeout, 40);
    };
    const auto click = [&](double x, double y) {
        dispatch("Input.dispatchMouseEvent", {{"type", "mouseMoved"}, {"x", x}, {"y", y}});
        for (const std::string type : {"mousePressed", "mouseReleased"})
            dispatch("Input.dispatchMouseEvent",
                     {{"type", type}, {"x", x}, {"y", y}, {"button", "left"}, {"clickCount", 1}});
    };
    const auto key = [&](std::string_view name, std::string_view code, int virtual_key) {
        for (const std::string type : {"keyDown", "keyUp"})
            dispatch("Input.dispatchKeyEvent", {{"type", type},
                                                {"key", name},
                                                {"code", code},
                                                {"windowsVirtualKeyCode", virtual_key}});
    };
    // Setup controls are stationary, semantic positions are suitable for them. The moving primary
    // below is always selected from painted pixels instead.
    const auto setup_click = [&](const std::string& id) {
        for (int attempt = 0; attempt < 20 && !script.thrown(); ++attempt) {
            const json found = node(id), view = node("main/body/view/agent");
            if (!live(found) || !truthy(at(found, "bounds")) || !truthy(at(view, "bounds"))) {
                script.sleep(40);
                continue;
            }
            const json& bounds = at(found, "bounds");
            const json& viewport = at(view, "bounds");
            const double x = number(at(bounds, 0)), y = number(at(bounds, 1)),
                         w = number(at(bounds, 2)), h = number(at(bounds, 3));
            const double vx = number(at(viewport, 0)), vy = number(at(viewport, 1)),
                         vw = number(at(viewport, 2)), vh = number(at(viewport, 3));
            if (w > 0 && h > 0 && y >= vy + 1 && y + h <= vy + vh - 1)
                return click(x + w / 2, y + h / 2);
            dispatch("Input.dispatchMouseEvent", {{"type", "mouseWheel"},
                                                  {"x", vx + vw * 0.85},
                                                  {"y", vy + vh / 2},
                                                  {"deltaX", 0},
                                                  {"deltaY", y + h / 2 - (vy + vh / 2)}});
            script.sleep(80);
        }
        script.raise("Stationary setup control is not reachable: " + id);
    };
    // Executed in Chromium, using only DOM canvas APIs. Accent detection ignores the header, tiny
    // glyph components, and the muted disabled Record button. Returned points are actual colored
    // pixels, never semantic-box centers.
    const std::string record_canvas = R"(function recordCanvas(duration) {
  const run = { start: performance.now(), frames: [], events: [], done: false, error: null };
  window.__omuiTransitionCapture = run;
  const events = ['pointerdown', 'pointerup', 'keydown', 'keyup', 'resize'];
  const observe = event => run.events.push({ type: event.type, at: performance.now(), trusted: event.isTrusted,
    x: event.clientX, y: event.clientY, key: event.key, width: innerWidth, height: innerHeight });
  events.forEach(type => addEventListener(type, observe, true));
  function sample() {
    try {
      const canvas = document.getElementById('canvas'), context = canvas?.getContext('2d');
      if (!context) throw new Error('The official ?renderer=cpu canvas is not readable');
      const box = canvas.getBoundingClientRect(), w = canvas.width, h = canvas.height;
      const rgba = context.getImageData(0, 0, w, h).data, mask = new Uint8Array(w * h), queue = new Int32Array(w * h);
      const sx = w / box.width, sy = h / box.height, minY = Math.max(0, Math.floor((200 - box.y) * sy));
      const maxY = Math.min(h, Math.ceil((innerHeight - 44 - box.y) * sy));
      for (let y = minY; y < maxY; ++y) for (let x = 0; x < w; ++x) {
        const i = y * w + x, p = i * 4, r = rgba[p], g = rgba[p + 1], blue = rgba[p + 2];
        // The scoped Design style darkens its primary. Keep saturated blue
        // fills, including that actual style; muted disabled fills remain out.
        if (r < 155 && g < 155 && blue > 155 && blue > r + 75 && blue > g + 75 && rgba[p + 3] > 230) mask[i] = 1;
      }
      const components = [];
      for (let y = minY; y < maxY; ++y) for (let x = 0; x < w; ++x) {
        const first = y * w + x; if (!mask[first]) continue;
        let head = 0, tail = 1, left = x, right = x, top = y, bottom = y;
        queue[0] = first; mask[first] = 0;
        while (head < tail) {
          const i = queue[head++], xx = i % w, yy = Math.floor(i / w);
          left = Math.min(left, xx); right = Math.max(right, xx); top = Math.min(top, yy); bottom = Math.max(bottom, yy);
          for (const next of [xx > 0 ? i - 1 : -1, xx + 1 < w ? i + 1 : -1, yy > minY ? i - w : -1, yy + 1 < maxY ? i + w : -1])
            if (next >= 0 && mask[next]) { mask[next] = 0; queue[tail++] = next; }
        }
        const bounds = [box.x + left / sx, box.y + top / sy, (right - left + 1) / sx, (bottom - top + 1) / sy];
        if (bounds[2] < 65 || bounds[3] < 12 || tail / (sx * sy) < 400) continue;
        const cx = (left + right) / 2, cy = (top + bottom) / 2;
        let selected = first, nearest = Infinity;
        for (let i = 0; i < tail; ++i) {
          const px = queue[i] % w, py = Math.floor(queue[i] / w), distance = (px - cx) ** 2 + (py - cy) ** 2;
          if (distance < nearest) { selected = queue[i]; nearest = distance; }
        }
        components.push({ bounds, area: tail / (sx * sy), point: { x: box.x + (selected % w + 0.5) / sx, y: box.y + (Math.floor(selected / w) + 0.5) / sy },
          rgba: Array.from(rgba.slice(selected * 4, selected * 4 + 4)) });
      }
      // The disabled Record is above playback and the task primary. Select
      // the bottommost substantial painted button, even if a disabled fill
      // happens to share its hue; never consult semantic coordinates here.
      components.sort((a, c) => (c.bounds[1] + c.bounds[3]) - (a.bounds[1] + a.bounds[3]) || c.area - a.area);
      run.frames.push({ at: performance.now(), viewport: [innerWidth, innerHeight], canvas: [w, h], canvasBox: [box.x, box.y, box.width, box.height],
        components, png: canvas.toDataURL('image/png') });
    } catch (error) { run.error = String(error.stack || error); }
    if (!run.error && performance.now() - run.start < duration) requestAnimationFrame(sample);
    else { run.done = true; events.forEach(type => removeEventListener(type, observe, true)); }
  }
  requestAnimationFrame(sample);
  return run.start;
})";
    const auto history = [&] {
        return script.value(
            "window.omui.request({op:'history',scope:" + q(root + "/preview/actions/arrange") +
            "}).then(r=>({exitCode:r.exitCode,data:r.data}))");
    };
    struct Phase {
        std::string name;
        std::function<void()> trigger;
        bool require_moving = true;
    };
    const auto save_phase = [&](const json& scenario, const Phase& phase, json& record) {
        const bool done =
            until([&] { return truthy(script.value("window.__omuiTransitionCapture?.done")); },
                  5000);
        check(done,
              text(at(scenario, "name")) + "/" + phase.name + ": continuous capture completes",
              std::nullopt, "coverage");
        json run = script.value("window.__omuiTransitionCapture");
        if (script.thrown())
            return;
        if (!truthy(run))
            return script.raise("No canvas recording");
        check(!truthy(at(run, "error")),
              text(at(scenario, "name")) + "/" + phase.name +
                  ": all captured frames come from the real CPU canvas",
              at(run, "error"), "coverage");
        const std::string directory =
            output_name + "/" + text(at(scenario, "name")) + "/" + phase.name;
        fs::create_directories(context.root / directory, error);
        if (run["frames"].is_array())
            for (std::size_t index = 0; index < run["frames"].size(); ++index) {
                json& frame = run["frames"][index];
                const std::string file =
                    directory + "/frame-" + padded(static_cast<int>(index), 3) + ".png";
                const std::string png = text(at(frame, "png"));
                cdp::write_base64(context.root / file, png.substr(png.find(',') + 1));
                frame.erase("png");
                frame["file"] = file;
            }
        record["capture"] = run;
        const json screenshot = dispatch("Page.captureScreenshot", {{"format", "png"}});
        if (script.thrown())
            return;
        cdp::write_base64(context.root / directory / "browser.png",
                          text(dig(screenshot, "result", "data")));
        record["screenshot"] = directory + "/browser.png";
        record["trace"] = directory + "/frames.motion.xdef.json";
    };
    std::unique_ptr<cdp::Browser> browser;
    const auto run_phase = [&](json& scenario, const Phase& spec) {
        json record = {{"name", spec.name}, {"beforeCards", cards()}, {"beforeHistory", history()}};
        if (script.thrown())
            return;
        const std::size_t slot = scenario["phases"].size();
        scenario["phases"].push_back(record);
        script.value("(" + record_canvas + ")(650)");
        script.attempt(
            [&] {
                if (script.thrown())
                    return;
                spec.trigger();
                json probe;
                bool associated = false;
                for (int index = 0; index < 20 && !script.thrown(); ++index) {
                    probe = script.value(
                        "(async()=>{const c=window.__omuiTransitionCapture;const [r,v]=await "
                        "Promise.all([window.omui.snapshot({root:" +
                        q(root + "/preview/actions/arrange") +
                        ",depth:0}),window.omui.snapshot({root:'main/body/view/"
                        "agent',depth:0})]);const f=c?.frames.at(-1),p=c?.frames.at(-2);return "
                        "{now:performance.now(),start:c?.start,viewport:innerWidth,runtimeWidth:v."
                        "data?.nodes?.find(n=>n.id==='main/body/view/"
                        "agent')?.bounds?.[2],previous:p?{at:p.at,components:p.components}:null,"
                        "frame:f?{at:f.at,components:f.components}:null,node:r.data?.nodes?.find(n="
                        ">n.id===" +
                        q(root + "/preview/actions/arrange") + ")}})()");
                    // During insertion the new semantic node exists before its pixels do. Do not
                    // mistake an outgoing Verify button for the incoming Arrange. Bounds only
                    // confirm the identity of the detected painted component; the click point still
                    // comes exclusively from the pixel mask.
                    const json& bounds = dig(probe, "node", "bounds");
                    const json& painted = dig(probe, "frame", "components", 0);
                    const double point_x = number(dig(painted, "point", "x")),
                                 point_y = number(dig(painted, "point", "y"));
                    associated = at(probe, "runtimeWidth") == at(probe, "viewport") &&
                                 live(at(probe, "node")) &&
                                 !has_state(at(probe, "node"), "offscreen") &&
                                 number(at(bounds, 2)) > 0 && number(at(bounds, 3)) > 0 &&
                                 truthy(painted) && point_x >= number(at(bounds, 0)) &&
                                 point_x <= number(at(bounds, 0)) + number(at(bounds, 2)) &&
                                 point_y >= number(at(bounds, 1)) &&
                                 point_y <= number(at(bounds, 1)) + number(at(bounds, 3));
                    if (!spec.require_moving) {
                        // A resize can expose the old child allocation for one frame while its
                        // responsive region rebuilds. Observe two matching painted allocations,
                        // rather than click an obsolete pre-resize rectangle.
                        const json& previous = dig(probe, "previous", "components", 0);
                        bool same = truthy(previous) && at(painted, "bounds").is_array();
                        if (same)
                            for (std::size_t corner = 0; corner < at(painted, "bounds").size();
                                 ++corner)
                                if (!(std::abs(number(dig(painted, "bounds", corner)) -
                                               number(dig(previous, "bounds", corner))) <= 0.5))
                                    same = false;
                        associated = associated && same;
                    }
                    if (associated)
                        break;
                    if (truthy(probe) &&
                        number(at(probe, "now")) - number(at(probe, "start")) > early_window_ms)
                        break;
                    script.sleep(8);
                }
                if (script.thrown())
                    return;
                record["probe"] = probe;
                const json painted = dig(probe, "frame", "components", 0);
                record["pixelAssociatedWithPrimary"] = associated;
                const bool early =
                    associated && truthy(painted) &&
                    number(at(probe, "now")) - number(at(probe, "start")) <= early_window_ms &&
                    number(at(probe, "now")) - number(dig(probe, "frame", "at")) < max_pixel_age_ms;
                const std::string name = text(at(scenario, "name")) + "/" + spec.name;
                check(early,
                      name + ": a fresh painted primary is covered in the early motion window",
                      probe, "coverage");
                if (!early)
                    return script.raise("CoverageGap: Coverage gap: sampling/CDP missed the early "
                                        "window; this does not establish a product timing failure");
                if (spec.require_moving)
                    check(has_state(at(probe, "node"), "animating"),
                          name + ": the real primary is still moving at the pixel probe",
                          dig(probe, "node", "state"), "coverage");
                // Record semantic bounds as a comparison, never as the source of this point.
                record["pixelPoint"] = at(painted, "point");
                click(number(dig(painted, "point", "x")), number(dig(painted, "point", "y")));
                record["dispatchedAt"] = script.value("performance.now()");
                record["deliveredInput"] = script.value(
                    "(()=>{const p=" + cdp::stringify(at(painted, "point")) +
                    ";return "
                    "[...window.__omuiTransitionCapture.events].reverse().find(e=>e.type==='"
                    "pointerup'&&Math.abs(e.x-p.x)<=1&&Math.abs(e.y-p.y)<=1)})()");
                const json& received = record["deliveredInput"];
                record["coveredInput"] =
                    truthy(at(received, "trusted")) &&
                    number(at(received, "at")) - number(at(probe, "start")) <= early_window_ms &&
                    number(at(received, "at")) - number(dig(probe, "frame", "at")) <=
                        max_pixel_age_ms;
                if (!truthy(record["coveredInput"])) {
                    record["afterCards"] = cards();
                    record["afterHistory"] = history();
                    return script.raise(
                        "CoverageGap: Coverage gap: trusted input arrived after the sampled pixels "
                        "became stale or after the early window");
                }
                const bool before_cards = truthy(record["beforeCards"]);
                const bool changed = until([&] { return cards() != before_cards; }, 3000);
                record["afterCards"] = cards();
                record["afterHistory"] = history();
                record["afterPause"] = node(root + "/preview/actions/pause");
                record["afterOwnership"] = node(root + "/preview/state");
                static const std::regex yours("Yours to use|现在由你操作");
                check(has_state(record["afterPause"], "disabled") &&
                          std::regex_search(text(at(record["afterOwnership"], "name")), yours),
                      name + ": the pointer took ownership instead of starting a Replay simulation",
                      json{{"pause", record["afterPause"]},
                           {"ownership", record["afterOwnership"]}});
                check(changed,
                      name + ": clicking actual painted pixels changes the real task grouping",
                      record["afterCards"]);
                const json& undo = dig(record["afterHistory"], "data", "undo");
                const std::size_t undone = undo.is_array() ? undo.size() : 0;
                const json& before_undo = dig(record["beforeHistory"], "data", "undo");
                check(at(record["afterHistory"], "exitCode") == 0 &&
                          undone > (before_undo.is_array() ? before_undo.size() : 0) &&
                          text(dig(undo, undone ? undone - 1 : 0, "label"))
                              .contains(root + "/preview/actions/arrange"),
                      name + ": public history records the new Arrange action",
                      record["afterHistory"]);
            },
            [&](const std::string& message) { script.raise(message); });
        script.finally([&] {
            save_phase(scenario, spec, record);
            const json& events = dig(record, "capture", "events");
            json up;
            if (events.is_array() && record.contains("pixelPoint"))
                for (auto event = events.rbegin(); event != events.rend(); ++event)
                    if (text(at(*event, "type")) == "pointerup" &&
                        std::abs(number(at(*event, "x")) -
                                 number(dig(record, "pixelPoint", "x"))) <= 1 &&
                        std::abs(number(at(*event, "y")) -
                                 number(dig(record, "pixelPoint", "y"))) <= 1) {
                        up = *event;
                        break;
                    }
            if (!up.is_null())
                record["receivedInput"] = up;
            const std::string name = text(at(scenario, "name")) + "/" + spec.name;
            check(truthy(at(up, "trusted")),
                  name + ": Chromium received a trusted pointer at the sampled ink",
                  up.is_null() ? std::nullopt : std::optional<json>(up), "coverage");
            const bool fresh = !up.is_null() && truthy(dig(record, "probe", "frame")) &&
                               number(at(up, "at")) - number(dig(record, "probe", "frame", "at")) <=
                                   max_pixel_age_ms;
            check(fresh, name + ": input refers to freshly sampled painted pixels",
                  up.is_null() || !truthy(dig(record, "probe", "frame"))
                      ? std::nullopt
                      : std::optional<json>(number(at(up, "at")) -
                                            number(dig(record, "probe", "frame", "at"))),
                  "coverage");
            check(!up.is_null() && number(at(up, "at")) - number(dig(record, "capture", "start")) <=
                                       early_window_ms,
                  name + ": trusted input actually arrived in the early coverage window",
                  up.is_null() ? std::nullopt
                               : std::optional<json>(number(at(up, "at")) -
                                                     number(dig(record, "capture", "start"))),
                  "coverage");
            const std::size_t frames = dig(record, "capture", "frames").is_array()
                                           ? dig(record, "capture", "frames").size()
                                           : 0;
            check(frames >= 8, name + ": continuous frames remain available for review", frames,
                  "coverage");
            if (record.contains("trace"))
                write_text(context.root / text(record["trace"]), cdp::pretty(record) + "\n");
            scenario["phases"][slot] = record;
        });
    };
    script.guard("transition completed", [&] {
        for (const int width : {390, 1280})
            for (const std::string language : {"en", "zh"}) {
                json scenario = {{"name", language + "-" + std::to_string(width)},
                                 {"width", width},
                                 {"language", language},
                                 {"phases", json::array()}};
                auto launched = open(context, width, 844);
                if (!launched)
                    return script.raise(launched.error());
                browser = std::move(*launched);
                script.page = browser.get();
                const std::string scenario_name = text(scenario["name"]);
                browser->on([&, scenario_name](const std::string& method, const json& params) {
                    if (method == "Runtime.exceptionThrown")
                        report["exceptions"].push_back(
                            {{"scenario", scenario_name},
                             {"detail", at(params, "exceptionDetails")}});
                });
                script.attempt(
                    [&] {
                        dispatch("Emulation.setEmulatedMedia",
                                 {{"features", json::array({{{"name", "prefers-reduced-motion"},
                                                             {"value", "no-preference"}}})}});
                        dispatch("Page.navigate",
                                 {{"url", url + "?renderer=cpu#/" +
                                              (language == "zh" ? "zh/" : "") + "agent/develop"}});
                        const bool ready = until([&] {
                            return truthy(
                                script.value("window.omuiStaticHandedOff && !!window.omui && "
                                             "!!document.querySelector('[data-omui-id=" +
                                             q(root + "/preview/actions/verify") + "]')"));
                        });
                        check(ready, scenario_name +
                                         ": the final candidate exposes the real Verify primary");
                        if (!ready)
                            return script.raise("Live Agent page did not open");
                        setup_click(root + "/preview/actions/pause");
                        const bool paused = until([&] {
                            return has_state(node(root + "/preview/actions/pause"), "disabled");
                        });
                        check(paused,
                              scenario_name + ": a real Pause click stops the scripted preview");
                        if (!paused)
                            return script.raise(
                                "Could not pause the scripted preview through real input");
                        check(has_state(node(root + "/specimen/record"), "disabled"),
                              scenario_name + ": the draft Record stays disabled above the "
                                              "playback/task region");
                        run_phase(scenario,
                                  {"mode-switch", [&] { setup_click(root + "/modes/design"); }});
                        setup_click(root + "/modes/design");
                        key("ArrowLeft", "ArrowLeft", 37);
                        check(until(
                                  [&] { return at(node(root + "/modes"), "value") == "develop"; }),
                              scenario_name + ": real keyboard input selects Develop");
                        run_phase(scenario, {"mode-and-narrow", [&] {
                                                 key("ArrowRight", "ArrowRight", 39);
                                                 script.sleep(30);
                                                 dispatch("Emulation.setDeviceMetricsOverride",
                                                          {{"width", 360},
                                                           {"height", 844},
                                                           {"deviceScaleFactor", 1},
                                                           {"mobile", false}});
                                             }});
                        // Pure external constraints follow the new layout by contract; they need
                        // not animate. Still require early, fresh-pixel input after the Runtime has
                        // actually adopted the browser's restored width.
                        run_phase(scenario, {"restore-width",
                                             [&] {
                                                 dispatch("Emulation.setDeviceMetricsOverride",
                                                          {{"width", width},
                                                           {"height", 844},
                                                           {"deviceScaleFactor", 1},
                                                           {"mobile", false}});
                                             },
                                             false});
                    },
                    [&](const std::string& message) {
                        const bool gap = message.starts_with("CoverageGap: ");
                        check(false, scenario_name + ": transition scenario completes",
                              gap ? message : cdp::error_text(message),
                              gap ? "coverage" : "interaction");
                    });
                report["scenarios"].push_back(scenario);
                browser->close();
                browser.reset();
                script.page = nullptr;
            }
        check(report["exceptions"].empty(), "no unexpected browser runtime exceptions",
              report["exceptions"]);
    });
    if (browser)
        browser->close();
    (*server)->close();
    report["ok"] = script.all_ok();
    write_text(output / "report.motion.xdef.json", cdp::pretty(report) + "\n");
    return report;
}

}  // namespace omui::devtools::check::pages
