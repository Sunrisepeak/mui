// omui's site, its one page (G5), as it ships, in a real browser: its delivery within its budget
// (site/delivery.budget.xdef.json: a cold visit over the budget's connection, frames timed while the Live Design
// key plays its design), then the visitor's path as the page's sketches have it — the first screen a document before
// the program, at rest still, the Live Design key playing the page's design in place to its end, Copy (a real click)
// putting the prompt with a pairing code on the clipboard and the circle breathing while it waits, the command tab the
// same code, the visitor's agent connecting with that code as the words say (`omui connect <code>`), the page
// connected with the agent's name, omui and platform, the agent reading and describing it, the bridge stopped and
// the page waiting again; reduced motion, no breath. Reports: target/site-perf.json, target/site.json;
// pictures under target/site-pictures/.
module omui.devtools.check;

import std;
import mcpplibs.os;
import omui.devtools.browser;
import omui.devtools.cdp;
import omui.devtools.run;
import :pages;
import :support;

namespace omui::devtools::check {

namespace fs = std::filesystem;
using namespace support;

namespace {

using cdp::json, cdp::at, cdp::dig, cdp::q, cdp::text, cdp::truthy, cdp::number;

// What the page does while its frames are timed: the Live Design key, its design played for five seconds.
constexpr std::string_view replay_moves = R"(
  const nodes = window.omui ? ((await window.omui.snapshot({})).data.nodes || []) : [];
  const live = nodes.find((n) => (n.id || '').endsWith('/live'));
  if (live) await act(live.id);
  await pause(5000);)";

}  // namespace

int site(const fs::path& root, const fs::path& built, const fs::path& given_omui, std::vector<std::string> parts) {
    if (parts.empty()) parts = {"delivery", "path"};
    for (const auto& part : parts)
        if (part != "delivery" && part != "path") {
            std::cerr << std::format("omui-devtools check site: no part {} (delivery path)\n", part);
            return 2;
        }
    const bool delivery_asked = std::ranges::contains(parts, std::string("delivery")), path_asked = std::ranges::contains(parts, std::string("path"));
    const auto chrome = browser::chromium();
    if (!chrome)
        return failed("no headless Chromium for the site");
    // The page as `omui build --web` packs it (build.mcpp's dist_toy: <the build's out>/toy/omui-site), the newest.
    fs::path page_dir = built.is_absolute() || built.empty() ? built : root / built;
    if (built.empty()) {
        fs::file_time_type newest{};
        std::error_code walked;
        for (const auto& out : fs::directory_iterator(root / "target/.build-mcpp/out", walked)) {
            const fs::path candidate = out.path() / "omui.site/toy/omui-site";
            std::error_code missing;
            const auto written = fs::last_write_time(candidate / "index.html", missing);
            if (!missing && (page_dir.empty() || written > newest)) page_dir = candidate, newest = written;
        }
    }
    if (!fs::is_regular_file(page_dir / "index.html"))
        return failed(std::format("no page at {} (omui build --web in site/ first)", mcpplibs::os::fs::utf8(page_dir)));
    fs::path omui = given_omui;
    if (omui.empty() && path_asked) {
        const auto artifacts = run::build(root, {"-p", "omui.cli"});
        if (!artifacts)
            return failed(std::format("omui.cli did not build: {}", artifacts.error()));
        for (const auto& artifact : *artifacts)
            if (artifact.name == "omui" && artifact.kind == "bin") omui = artifact.path;
        if (omui.empty())
            return failed("the build of omui.cli reported no omui program");
    }
    const pages::Context context{.root = root, .site = page_dir, .chrome = *chrome};
    int status = 0;

    // 1. The delivery: a cold visit, no HTTP cache, over the budget's connection.
    if (delivery_asked) {
        const fs::path budget = root / "site/delivery.budget.xdef.json";
        const json budgets = json::parse(mcpplibs::os::fs::read(budget).value_or("{}"), nullptr, false);
        const json delivery = pages::perf(context, {.network = text(at(budgets, "network")), .budget = budget, .cache = false,
                                                    .request_log = root / "target/site-requests.report.xdef.json",
                                                    .moves = std::string(replay_moves)});
        std::ofstream(root / "target/site-perf.json", std::ios::binary) << cdp::pretty(delivery) << '\n';
        const auto whole = [&](const json& value) { return value.is_number() ? std::format("{:.0f}", number(value)) : cdp::stringify(value); };
        std::cout << std::format("the page's delivery: {} bytes ({} before its first frame), first frame {} ms, ready {} ms; frames p95 {} ms ({})\n",
                                 whole(dig(delivery, "transfer", "total")), whole(dig(delivery, "transfer", "before_first_frame")),
                                 whole(at(delivery, "first_frame_ms")), whole(at(delivery, "ready_ms")), std::format("{:.1f}", number(dig(delivery, "frames", "p95"))),
                                 text(at(delivery, "browser")));
        for (const auto& one : at(delivery, "budgets"))
            std::cout << std::format("  {} {}: {} (budget {})\n", truthy(at(one, "pass")) ? "ok  " : "OVER", text(at(one, "metric")), whole(at(one, "value")),
                                     whole(at(one, "max")));
        if (!truthy(at(delivery, "ok"))) {
            std::cout << "::error::the site is over its delivery budget (target/site-perf.json)\n";
            status = 1;
        }
    }
    if (!path_asked) return status;

    // 2. The visitor's path.
    const fs::path pictures = root / "target/site-pictures";
    std::error_code error;
    fs::create_directories(pictures, error);
    const fs::path bridge_dir = fs::temp_directory_path() / std::format("omui-site-{:08x}", std::random_device{}());
    fs::create_directories(bridge_dir, error);
    auto served = cdp::Server::start(cdp::pages(page_dir));
    if (!served)
        return failed(served.error());
    auto launched = pages::open(context, 1280, 800);
    if (!launched)
        return failed(launched.error());
    auto& page = **launched;
    cdp::Script script;
    script.page = &page;
    const auto step = [&](bool ok, std::string_view what, std::optional<json> detail = std::nullopt) { return script.check(ok, what, std::move(detail)); };

    // The semantic tree now: each node's id and name.
    const auto nodes = [&]() -> json {
        const json answer = script.value("window.omui.snapshot({}).then(r => JSON.stringify(r.data.nodes || []))");
        const json list = json::parse(answer.is_string() ? answer.get<std::string>() : std::string("[]"), nullptr, false);
        return list.is_array() ? list : json::array();
    };
    // The node whose id ends in /`name` (its id has the layout's own segments before it), or null.
    const auto node = [&](std::string_view name) -> json {
        for (const json& found : nodes())
            if (const std::string id = text(at(found, "id")); id == name || id.ends_with("/" + std::string(name))) return found;
        return nullptr;
    };
    const auto name_of = [&](std::string_view name) { return text(at(node(name), "name")); };
    // A real click at `across` of the node's width (a segmented control's option is a part of it, not a node).
    const auto press_at = [&](std::string_view name, double across) {
        const auto bounds = cdp::box(at(node(name), "bounds"));
        if (!bounds) return false;
        for (const std::string type : {"mouseMoved", "mousePressed", "mouseReleased"})
            script.call("Input.dispatchMouseEvent", {{"type", type}, {"x", (*bounds)[0] + (*bounds)[2] * across}, {"y", (*bounds)[1] + (*bounds)[3] / 2},
                                                     {"button", "left"}, {"clickCount", 1}});
        script.sleep(500);
        return true;
    };
    const auto press = [&](std::string_view name) { return press_at(name, 0.5); };
    const auto ready = [&] {
        for (int index = 0; index < 300 && !script.thrown(); ++index) {
            if (truthy(script.value("!!window.omui && (document.querySelector('[data-omui=mirror]')?.childElementCount ?? 0) > 0")))
                return true;
            script.sleep(100);
        }
        return false;
    };
    // The mark's square, as the page shows it now (its picture's base64): unchanged while it rests, not while it breathes.
    const auto mark = [&](std::string_view name) {
        const auto bounds = cdp::box(at(node("o"), "bounds"));
        if (!bounds) return std::string();
        const double grow = 0.12 * (*bounds)[2];
        return script.screenshot(pictures / std::format("{}.png", name),
                                 {{"format", "png"}, {"clip", {{"x", (*bounds)[0] - grow}, {"y", (*bounds)[1] - grow}, {"width", (*bounds)[2] + 2 * grow},
                                                               {"height", (*bounds)[3] + 2 * grow}, {"scale", 1}}}});
    };
    // Whether the mark changes over a breath (3.2 s): pictures a fifth of a breath apart, any two different.
    const auto breathing = [&](std::string_view name) {
        std::set<std::string> seen;
        for (int index = 0; index < 5; ++index) {
            seen.insert(mark(std::format("{}-{}", name, index)));
            script.sleep(640);
        }
        return seen.size() > 1;
    };
    const auto replay = [&](const json& args) {
        const json answer = script.value(std::format("window.omui.invoke('generate.replay', {}).then(r => JSON.stringify(r))", args.dump()));
        return json::parse(answer.is_string() ? answer.get<std::string>() : std::string("{}"), nullptr, false);
    };
    struct Answer {
        int code = -1;
        std::string out, err;
    };
    // An agent's command, the bridge's directory its own (no person's bridge on this machine is touched).
    const auto agent_command = [&](std::vector<std::string> args) {
        namespace process = mcpplibs::os::process;
        process::spawn_options options;
        options.program = omui;
        options.args = {"page"};
        options.args.insert(options.args.end(), args.begin(), args.end());
        options.args.insert(options.args.end(), {"--dir", mcpplibs::os::fs::utf8(bridge_dir)});
        options.in = process::stdio::null;
        auto done = process::run(std::move(options), process::bounds{.hard = std::chrono::minutes(2)});
        if (!done) return Answer{-1, "", done.error().message()};
        return Answer{done->timed_out ? -1 : done->status.shell_code(), std::move(done->out), std::move(done->err)};
    };
    std::optional<mcpplibs::os::process::process> bridge;
    std::jthread bridge_reader;
    std::string bridge_said;
    const auto stop_bridge = [&] {
        if (bridge) {
            bridge->kill();
            static_cast<void>(bridge->wait());
        }
        if (bridge_reader.joinable()) bridge_reader.join();
        bridge.reset();
    };
    static const std::regex code_form("[A-Z2-9]{4}-[A-Z2-9]{4}");

    // What a copy put on the clipboard, as the page wrote it (the browser's clipboard is the person's).
    script.call("Page.addScriptToEvaluateOnNewDocument", {{"source", R"(
  window.__copied = null;
  if (navigator.clipboard) {
    const write = navigator.clipboard.writeText ? navigator.clipboard.writeText.bind(navigator.clipboard) : null;
    navigator.clipboard.writeText = (t) => { window.__copied = String(t); return write ? write(t).catch(() => {}) : Promise.resolve(); };
  })"}});
    script.attempt(
        [&] {
            // The first screen is a document before the program runs: its heading the page's sentence.
            const std::string document = cdp::get((*served)->url()).value_or("");
            step(document.contains("<h1") && document.contains("one semantic tree drives the pixels"),
                 "before the program: the first screen a document, its h1 the page's sentence");
            script.call("Page.navigate", {{"url", (*served)->url()}});
            step(ready(), "the page runs: window.omui answers, its mirror is there");
            script.sleep(1500);
            step(name_of("o") == "omui" && !node("who").is_object(), "at rest: the mark says omui, no agent beside it");
            step(!breathing("rest"), "at rest the O is still");

            // The Live Design key: the page's design played in place, to its end.
            step(press("live"), "the person presses Live Design");
            const json playing = replay(json::object());
            const std::size_t steps = at(dig(playing, "data"), "steps").is_array() ? at(dig(playing, "data"), "steps").size() : 0;
            step(truthy(dig(playing, "data", "playing")) && steps >= 8, std::format("its design plays in place: {} steps", steps), playing);
            script.sleep(1200);
            (void)mark("design");
            (void)script.screenshot(pictures / "design.png");
            // As long as its show says (each step's beat), and ten seconds more.
            double length = 0;
            if (const json& listed = at(dig(playing, "data"), "steps"); listed.is_array())
                for (const json& one : listed)
                    if (one.is_object() && one.contains("seconds") && one["seconds"].is_number()) length += one["seconds"].get<double>();
            const bool ended = script.until_time([&] { return truthy(dig(replay(json::object()), "data", "ended")); },
                                                 static_cast<int>(std::max(length, steps * 1.25) * 1000) + 10000, 250);
            script.sleep(800);
            step(ended && name_of("line").contains("one semantic tree"), "to its end: the page as it ships, its sentence");

            // Copy (agent tab): the prompt with a pairing code on the clipboard; the page waits, the O breathes.
            step(press("copy"), "the person presses Copy");
            const std::string prompt = text(script.value("window.__copied"));
            std::smatch found;
            const std::string code = std::regex_search(prompt, found, code_form) ? found.str() : std::string();
            step(!code.empty() && prompt.contains("omui connect " + code) && prompt.contains("describe"),
                 "the prompt copied: install, omui connect <code>, describe", prompt.substr(0, 300));
            step(name_of("words").contains(code) && name_of("copy") == "Copied", "the words on the page show the same code; Copy says Copied",
                 name_of("words").substr(0, 300));
            const json waiting = script.value("window.omui.linkState()");
            step(text(at(waiting, "state")) == "waiting" && text(at(waiting, "code")) == code, "the page looks for the agent with that code",
                 cdp::stringify(waiting));
            step(breathing("waiting"), "while it waits, the O breathes");
            (void)script.screenshot(pictures / "waiting.png");

            // The command tab: the same code, as two lines a person types.
            step(press_at("how", 0.75) && name_of("words").contains("\nomui connect " + code), "the command tab: the same code, omui connect <code>",
                 name_of("words").substr(0, 300));
            step(press_at("how", 0.25), "back to the agent tab");

            // The visitor's agent, as the words say: omui connect <code>.
            {
                namespace process = mcpplibs::os::process;
                process::spawn_options options;
                options.program = omui;
                options.args = {"connect", code, "--name", "CI agent", "--dir", mcpplibs::os::fs::utf8(bridge_dir)};
                options.in = process::stdio::null;
                options.out = process::stdio::pipe;
                options.err = process::stdio::output;
                auto started = process::process::spawn(options);
                if (!started) return script.raise(std::format("omui connect did not start: {}", started.error().message()));
                bridge = std::move(*started);
                bridge_reader = std::jthread([&] {
                    for (;;) {
                        auto chunk = bridge->read_output();
                        if (!chunk || chunk->empty()) break;
                        bridge_said += *chunk;
                    }
                });
            }
            const Answer status_answer = agent_command({"status", "--wait"});
            step(status_answer.code == 0 && status_answer.out.contains("\"paired\":true"), "the agent sees the page paired (omui page status --wait)",
                 (status_answer.out + status_answer.err).substr(0, 400));
            const bool paired = script.until_time([&] { return name_of("o") == "Mui"; }, 5000, 100);
            const json link = script.value("window.omui.linkState()");
            const std::string who = name_of("who");
            step(paired && text(at(link, "state")) == "paired", "connected: the mark is Mui", cdp::stringify(link));
            step(who.starts_with("CI agent · omui ") && !text(at(link, "version")).empty() && text(at(link, "platform")).contains('-') &&
                     who.ends_with(" · " + text(at(link, "platform"))),
                 "beside it the agent, its omui and its platform, as its bridge said", who);
            script.sleep(800);
            (void)script.screenshot(pictures / "connected.png");
            const Answer described = agent_command({"describe"});
            step(described.code == 0 && described.out.size() > 40, "the page describes itself to the agent (omui page describe)",
                 (described.out + described.err).substr(0, 300));
            const Answer read = agent_command({"snapshot", "--interactive"});
            step(read.code == 0 && read.out.contains("/copy") && read.out.contains("/live"), "the agent reads the page (omui page snapshot)",
                 read.err.substr(0, 300));

            // Linked, the page is the agent's to design (Live Design, D-149): its designer, which the release links,
            // starts as the link pairs, and the agent edits the page's own TypeScript as it runs — what
            // site/tests/visitor-agent.json says: the project's name, GitHub as its mark, Mui looking at the pointer.
            {
                const json wanted = json::parse(mcpplibs::os::fs::read(root / "site/tests/visitor-agent.json").value_or("{}"), nullptr, false);
                const std::string path = text(at(wanted, "path"));
                const Answer listed = agent_command({"design.sources"});
                std::string source;
                for (const auto line : std::views::split(listed.out, '\n')) {
                    const json record = json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
                    for (const json& script_of : dig(record, "data", "scripts").is_array() ? dig(record, "data", "scripts") : json::array())
                        if (text(at(script_of, "path")) == path) source = text(at(script_of, "text"));
                }
                step(!source.empty(), "paired, the page's source is the agent's to read (omui page design.sources)", (listed.out + listed.err).substr(0, 300));
                bool found = true;
                for (const json& change : at(wanted, "edits").is_array() ? at(wanted, "edits") : json::array()) {
                    const std::string find = text(at(change, "find"));
                    if (const auto where = source.find(find); where != std::string::npos) source.replace(where, find.size(), text(at(change, "replace")));
                    else found = false;
                }
                const Answer edited = agent_command({"design.edit", "--path", path, "--text", source, "--say", text(at(wanted, "say"))});
                step(found && edited.code == 0, "the agent edits it as it runs (omui page design.edit)", (edited.out + edited.err).substr(0, 400));
                const bool designed = script.until_time([&] { return name_of("name") == "omui" && name_of("github") == "GitHub"; }, 5000, 100);
                const auto github = cdp::box(at(node("github"), "bounds"));
                step(designed && github && (*github)[2] == 28 && (*github)[0] > 1100, "the project's name top left, GitHub as its mark top right",
                     node("github"));
                // The edit is presented as one (written in, its code beside it): looked at once it has settled.
                std::string before = mark("settling-0");
                for (int index = 1; index < 40; ++index) {
                    script.sleep(250);
                    const std::string now = mark(std::format("settling-{}", index % 2));
                    if (now == before) break;
                    before = now;
                }
                script.sleep(2500);  // the edit's code beside it goes too
                (void)script.screenshot(pictures / "designed.png");
                // Mui looks where the pointer is: a real mouse moved to her left, then below her right.
                const auto look = [&](double x, double y, std::string_view name) {
                    script.call("Input.dispatchMouseEvent", {{"type", "mouseMoved"}, {"x", x}, {"y", y}});
                    script.sleep(300);
                    return mark(name);
                };
                const auto circle = cdp::box(at(node("o"), "bounds"));
                const double middle_x = circle ? (*circle)[0] + (*circle)[2] / 2 : 640, middle_y = circle ? (*circle)[1] + (*circle)[3] / 2 : 220;
                const std::string left = look(middle_x - 300, middle_y, "looking-left");
                const std::string right = look(middle_x + 300, middle_y + 300, "looking-down-right");
                const std::string again = look(middle_x - 300, middle_y, "looking-left-again");
                step(!left.empty() && left != right && again != right, "Mui looks at the pointer: left, below to the right, and left again");
            }

            // The bridge stops: the page waits again.
            stop_bridge();
            const bool back = script.until_time([&] { return name_of("o") == "omui" && !node("who").is_object(); }, 8000, 200);
            step(back && text(at(script.value("window.omui.linkState()"), "state")) != "paired", "the bridge gone: the page waits again",
                 cdp::stringify(script.value("window.omui.linkState()")));

            // Reduced motion: Copy, and the O waits without breathing.
            script.call("Emulation.setEmulatedMedia", {{"features", json::array({{{"name", "prefers-reduced-motion"}, {"value", "reduce"}}})}});
            script.call("Page.reload", json::object());
            step(ready(), "reloaded, the person asking for reduced motion");
            script.sleep(1000);
            step(press("copy") && text(at(script.value("window.omui.linkState()"), "state")) == "waiting", "Copy: the page waits");
            step(!breathing("reduced"), "and with reduced motion the O is still");
        },
        [&](const std::string& message) { step(false, "the path ran to its end", message); });
    stop_bridge();
    page.close();
    (*served)->close();
    fs::remove_all(bridge_dir, error);
    script.report["bridge"] = bridge_said.substr(0, 2000);
    std::ofstream(root / "target/site.json", std::ios::binary) << cdp::pretty(script.report) << '\n';
    std::cout << "the site, the visitor's path (G5):\n";
    for (const auto& one : at(script.report, "checks"))
        std::cout << std::format("  {} {}{}\n", truthy(at(one, "ok")) ? "ok  " : "FAIL", text(at(one, "what")),
                                 truthy(at(one, "ok")) || !at(one, "detail").is_string() ? std::string() : " — " + text(at(one, "detail")).substr(0, 300));
    if (!script.all_ok(true)) {
        std::cout << "::error::the site failed its path (target/site.json, pictures in target/site-pictures/)\n";
        status = 1;
    }
    return status;
}

}  // namespace omui::devtools::check
