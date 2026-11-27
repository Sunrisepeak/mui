// A site's four doors, held to one answer (0.1.0 plan AG5; oweb's design §4.10).
//
// doors     a built site whose pages load a program (`oweb site.build --engine`), served as a static host serves
//           it, one of its pages in a real browser. An agent reaches a site by fetching its files, by the `oweb`
//           command over its directory, by the tools the page offers (WebMCP: each operation of the page's
//           subject `site` a tool, from its listing), or connected to the page (`omui connect <code>`, then the
//           page's subjects asked through the bridge: `omui page site.describe`). At each door the site says of itself the same text — its
//           llms.txt — reads a page as the same Markdown — the route's index.md — and finds the same sections
//           for a question. The words searched for through the page are in the page's own search box, with what
//           they found. The report: target/doors.report.xdef.json.
module omui.devtools.check;

import std;
import mcpplibs.os;
import mlibs.delivery;
import mlibs.http;
import omui.devtools.browser;
import omui.devtools.cdp;
import omui.devtools.run;
import :support;

namespace omui::devtools::check {

namespace fs = std::filesystem;
using namespace support;

namespace {

using cdp::json, cdp::at, cdp::text, cdp::truthy;

// Where the browser gives a page no `document.modelContext` (WebMCP is a draft), the check's own stands in its
// place before the page's scripts run: what the page offers is kept, and called as an agent's browser would.
constexpr std::string_view model_context = R"js((() => {
  window.__omuiDoors = { own: !!document.modelContext, tools: [] };
  const keep = (tool) => { window.__omuiDoors.tools.push(tool); };
  if (document.modelContext && typeof document.modelContext.registerTool === 'function') {
    const real = document.modelContext.registerTool.bind(document.modelContext);
    document.modelContext.registerTool = (tool, options) => { keep(tool); try { return real(tool, options); } catch (e) { return undefined; } };
  } else {
    Object.defineProperty(document, 'modelContext', { value: { registerTool: keep }, configurable: true });
  }
})())js";

// What a program said, as records a line: its data record and how it ended.
struct Said {
    int  code = -1;
    json data;
    std::string all;
};
Said records_of(int code, std::string_view out, std::string_view err) {
    Said said{code, json(), std::string(out) + std::string(err)};
    for (const auto line : std::views::split(out, '\n')) {
        const json record = json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
        if (record.is_object() && text(at(record, "kind")) == "data") said.data = at(record, "data");
    }
    return said;
}

std::expected<fs::path, std::string> program(const fs::path& root, const fs::path& given, std::string_view package, std::string_view name) {
    if (!given.empty()) return given;
    const auto artifacts = run::build(root, {"-p", std::string(package)});
    if (!artifacts) return std::unexpected(std::format("{} did not build: {}", package, artifacts.error()));
    for (const auto& artifact : *artifacts)
        if (artifact.name == name && artifact.kind == "bin") return artifact.path;
    return std::unexpected(std::format("the build of {} reported no {} program", package, name));
}

}  // namespace

int doors(const fs::path& root, const fs::path& directory, std::string_view path, const fs::path& given_oweb, const fs::path& given_omui) {
    namespace process = mcpplibs::os::process;
    std::string route(path);
    if (!route.starts_with('/')) route.insert(route.begin(), '/');
    if (!route.ends_with('/')) route += '/';
    const std::string site = mcpplibs::os::fs::utf8(directory);
    if (!fs::is_regular_file(directory / mcpplibs::os::fs::from_utf8(route.substr(1)) / "index.html") || !fs::is_regular_file(directory / "site.routes.xdef.json"))
        return failed(std::format("no page at {} of a built site in {} (oweb site.build --engine)", route, site));
    const auto chrome = browser::chromium();
    if (!chrome) return failed("no headless Chromium for the doors' check");
    const auto oweb = program(root, given_oweb, "oweb", "oweb");
    if (!oweb) return failed(oweb.error());
    const auto omui = program(root, given_omui, "omui.cli", "omui");
    if (!omui) return failed(omui.error());

    auto server = cdp::Server::start([directory](const cdp::Arrived& request) -> cdp::Reply {
        std::string name = mlibs::http::decoded(request.path).substr(request.path.starts_with("/") ? 1 : 0);
        if (name.empty() || name.ends_with('/')) name += "index.html";
        if (name.contains("..")) return {.status = 400, .body = "", .type = ""};
        const auto bytes = mcpplibs::os::fs::read(directory / mcpplibs::os::fs::from_utf8(name));
        if (!bytes) return {.status = 404, .body = "", .type = ""};
        const std::size_t dot = name.rfind('.');
        const std::string_view type = mlibs::http::media_type(dot == std::string::npos ? std::string_view{} : std::string_view(name).substr(dot));
        return {.status = 200, .body = mlibs::delivery::gzip(*bytes, 6), .type = std::string(type), .headers = {{"Content-Encoding", "gzip"}, {"Cache-Control", "no-store"}}};
    });
    if (!server) return failed(server.error());
    auto launched = cdp::Browser::launch(*chrome, {});
    if (!launched) return failed(launched.error());
    auto& page = **launched;

    // The bridge's directory is the check's own: no person's bridge on this machine is touched.
    std::error_code error;
    const fs::path bridge_dir = fs::temp_directory_path() / std::format("omui-doors-{:08x}", std::random_device{}());
    fs::create_directories(bridge_dir, error);
    std::optional<process::process> bridge;
    std::jthread bridge_reader;
    std::string bridge_said;
    const auto ended = [&] {
        if (bridge) {
            bridge->kill();
            static_cast<void>(bridge->wait());
        }
        if (bridge_reader.joinable()) bridge_reader.join();
        bridge.reset();
        page.close();
        (*server)->close();
        fs::remove_all(bridge_dir, error);
    };
    const auto ran = [&](const fs::path& which, std::vector<std::string> args) {
        process::spawn_options options;
        options.program = which;
        options.args = std::move(args);
        options.in = process::stdio::null;
        auto done = process::run(std::move(options), process::bounds{.hard = std::chrono::minutes(2)});
        if (!done) return Said{-1, json(), done.error().message()};
        return records_of(done->timed_out ? -1 : done->status.shell_code(), done->out, done->err);
    };
    const auto wait = [&](std::string_view ready, int tenths, std::string_view what) -> std::optional<std::string> {
        for (int attempt = 0; attempt < tenths; ++attempt) {
            const auto held = page.value(ready);
            if (!held) return held.error();
            if (truthy(*held)) return std::nullopt;
            page.pump(std::chrono::milliseconds(100));
        }
        return std::format("{} did not come", what);
    };
    json steps = json::array();
    bool all = true;
    const auto step = [&](bool held, std::string_view door, std::string_view what, std::string_view detail = {}) {
        all = all && held;
        steps.push_back({{"door", door}, {"what", what}, {"ok", held}});
        std::cout << std::format("  {} {}: {}{}{}\n", held ? "ok  " : "FAIL", door, what, held || detail.empty() ? "" : " — ", held ? std::string_view{} : detail.substr(0, 400));
        return held;
    };
    const auto stopped = [&](std::string_view why) {
        ended();
        return failed(std::format("the doors' check did not run: {}", why));
    };

    std::string address = (*server)->url();
    if (address.ends_with('/')) address.pop_back();
    if (const auto set = page.call("Page.addScriptToEvaluateOnNewDocument", {{"source", std::string(model_context)}}); !set) return stopped(set.error());
    if (const auto gone = page.call("Page.navigate", {{"url", address + route}}); !gone) return stopped(gone.error());
    if (const auto late = wait("document.readyState === 'complete'", 300, "the page's document")) return stopped(*late);
    // A page that waits for its reader to operate something is told to take its program now.
    if (const auto taken = page.value("(window.omuiLoad && typeof window.omuiLoad.start === 'function' && (window.omuiLoad.start(), true)) || false"); !taken) return stopped(taken.error());
    if (const auto late = wait("window.omuiHasPresented === true && !!window.omui", 900, "the program's first frame")) return stopped(*late);
    std::cout << std::format("a site's four doors ({}): {}{}\n", page.version(), site, route);

    // 1. Fetching: the site's own files, as its host sends them.
    const auto fetched = page.value(std::format(R"js((async () => {{
      const get = async (address) => {{ const r = await fetch(address, {{ cache: 'no-store' }}); return r.ok ? await r.text() : ''; }};
      return {{ llms: await get('/llms.txt'), markdown: await get({} + 'index.md'), routes: await get('/site.routes.xdef.json'), search: await get('/site.search.xdef.json') }};
    }})())js", json(route).dump()));
    if (!fetched) return stopped(fetched.error());
    const std::string llms = text(at(*fetched, "llms")), markdown = text(at(*fetched, "markdown"));
    const json routes = json::parse(text(at(*fetched, "routes")), nullptr, false);
    std::string title;
    for (const json& one : at(routes, "routes").is_array() ? at(routes, "routes") : json::array())
        if (text(at(one, "path")) == route) title = text(at(one, "title"));
    step(llms.starts_with("# ") && llms.contains(route + "index.md"), "fetch", "llms.txt says what the site is and names this page's Markdown", llms);
    step(!markdown.empty() && !title.empty(), "fetch", "the page's index.md, and the routes' table with this route", text(at(*fetched, "routes")));
    step(text(at(*fetched, "search")).contains("\"sections\""), "fetch", "the search's index as data");
    const std::string query = title.empty() ? std::string("start") : title;

    // 2. The command, over the directory the host serves.
    const Said described = ran(*oweb, {"site.describe", "--site", site});
    const Said read = ran(*oweb, {"page.read", "--site", site, "--route", route});
    const Said found = ran(*oweb, {"docs.search", "--site", site, "--query", query});
    step(described.code == 0 && text(at(described.data, "text")) == llms, "command", "oweb site.describe: its text is llms.txt, word for word", described.all);
    step(read.code == 0 && text(at(read.data, "text")) == markdown, "command", "oweb page.read: the page's index.md", read.all);
    step(found.code == 0 && at(found.data, "found").is_array() && !at(found.data, "found").empty(), "command",
         std::format("oweb docs.search `{}`: {} section(s)", query, at(found.data, "found").is_array() ? at(found.data, "found").size() : 0), found.all);

    // 3. The page's tools: each operation of the page's subject offered as the listing says it.
    const auto offered_late = wait("!!window.__omuiDoors && window.__omuiDoors.tools.some((t) => t.name === 'docs_search')", 150, "the site's tools");
    const auto tools = page.value(std::format(R"js((async () => {{
      const tools = window.__omuiDoors.tools;
      const call = async (name, input) => {{
        const tool = tools.find((t) => t.name === name);
        if (!tool) return null;
        const answer = await tool.execute(input, {{}});
        return {{ error: !!answer.isError, text: (answer.content[0] || {{}}).text || '' }};
      }};
      const out = {{ own: window.__omuiDoors.own, names: tools.map((t) => t.name), reads: tools.filter((t) => t.annotations && t.annotations.readOnlyHint).map((t) => t.name),
                    required: (tools.find((t) => t.name === 'docs_search') || {{ inputSchema: {{}} }}).inputSchema.required || [] }};
      out.described = await call('site_describe', {{}});
      out.read = await call('page_read', {{ route: {0} }});
      out.found = await call('docs_search', {{ query: {1} }});
      out.listed = await call('routes_list', {{}});
      await new Promise((done) => setTimeout(done, 600));
      const nodes = ((await window.omui.snapshot({{}})).data || {{}}).nodes || [];
      const box = nodes.find((n) => (n.id || '').endsWith('/search/box'));
      out.box = box ? String(box.value == null ? '' : box.value) : null;
      out.shown = nodes.filter((n) => /\/search\/results\/f\d+\/h\d+$/.test(n.id || '')).length;
      return out;
    }})())js", json(route).dump(), json(query).dump()));
    if (!tools) return stopped(tools.error());
    const auto said = [&](std::string_view name) { return json::parse(text(at(at(*tools, name), "text")), nullptr, false); };
    const auto has = [&](std::string_view list, std::string_view name) {
        const json& names = at(*tools, list);
        return names.is_array() && std::ranges::any_of(names, [&](const json& one) { return text(one) == name; });
    };
    step(!offered_late && has("names", "site_describe") && has("names", "routes_list") && has("names", "page_read") && has("names", "docs_search") && has("names", "ui_snapshot"), "page tools",
         std::format("the site's four operations offered beside the interface's, {}", truthy(at(*tools, "own")) ? "to the browser's document.modelContext" : "to document.modelContext (the check's own: this browser has none)"),
         at(*tools, "names").dump());
    step(has("reads", "site_describe") && has("reads", "docs_search") && at(*tools, "required") == json::array({"query"}), "page tools", "each says it only reads; docs_search requires its query, as its listing does",
         at(*tools, "reads").dump());
    step(!truthy(at(at(*tools, "described"), "error")) && text(at(said("described"), "text")) == llms, "page tools", "site_describe: its text is llms.txt, word for word", text(at(at(*tools, "described"), "text")));
    step(text(at(said("read"), "text")) == markdown, "page tools", "page_read: the page's index.md", text(at(at(*tools, "read"), "text")));
    step(at(said("found"), "found") == at(found.data, "found"), "page tools", "docs_search: the sections the command finds", text(at(at(*tools, "found"), "text")));
    step(at(said("listed"), "routes").is_array() && at(said("listed"), "routes").size() == at(routes, "routes").size(), "page tools", "routes_list: every route of the table");
    step(text(at(*tools, "box")) == query && cdp::number(at(*tools, "shown")) > 0, "page tools",
         std::format("the words searched for are in the page's own search box, {} result(s) under it", cdp::number(at(*tools, "shown"))), tools->dump());

    // 4. Connected: the page's code, the agent's bridge, the page's subjects asked through it.
    const auto code = page.value("window.omui.link()");
    if (!code || text(*code).empty()) return stopped(code ? "the page gave no pairing code" : code.error());
    {
        process::spawn_options options;
        options.program = *omui;
        options.args = {"connect", text(*code), "--name", "CI agent", "--dir", mcpplibs::os::fs::utf8(bridge_dir)};
        options.in = process::stdio::null;
        options.out = process::stdio::pipe;
        options.err = process::stdio::output;
        auto started = process::process::spawn(options);
        if (!started) return stopped(std::format("omui connect did not start: {}", started.error().message()));
        bridge = std::move(*started);
        bridge_reader = std::jthread([&] {
            for (;;) {
                auto chunk = bridge->read_output();
                if (!chunk || chunk->empty()) break;
                bridge_said += *chunk;
            }
        });
    }
    // An operation of the page's subject as a command line: its words are its request (`omui page docs.search --query …`).
    const auto asked = [&](std::vector<std::string> words) {
        words.insert(words.begin(), "page");
        words.insert(words.end(), {"--dir", mcpplibs::os::fs::utf8(bridge_dir)});
        return ran(*omui, std::move(words));
    };
    const Said paired = ran(*omui, {"page", "status", "--wait", "--dir", mcpplibs::os::fs::utf8(bridge_dir)});
    step(paired.code == 0 && paired.all.contains("\"paired\":true"), "connected", "omui connect <the page's code>: paired", paired.all + bridge_said);
    const Said here = asked({"subjects"});
    step(here.code == 0 && here.all.contains(R"("name":"site")") && here.all.contains(R"("name":"ui")"), "connected", "who is here: the interface and the site", here.all);
    const Said told = asked({"site.describe"});
    step(told.code == 0 && text(at(told.data, "text")) == llms, "connected", "site.describe: its text is llms.txt, word for word", told.all);
    const Said given = asked({"page.read", "--route", route});
    step(given.code == 0 && text(at(given.data, "text")) == markdown, "connected", "page.read: the page's index.md", given.all);
    const Said sought = asked({"call", json({{"op", "docs.search"}, {"query", query}}).dump()});
    const Said sought_by_words = asked({"docs.search", "--query", query});
    step(sought.code == 0 && at(sought.data, "found") == at(found.data, "found") && at(sought_by_words.data, "found") == at(found.data, "found"), "connected",
         "docs.search, as words and as a request whole: the sections the command finds", sought.all + sought_by_words.all);
    (void)page.value("window.omui.unlink()");

    const std::string version = page.version();
    ended();
    const json report = {{"$format", "doors-report@1"}, {"site", site}, {"path", route}, {"browser", version}, {"query", query}, {"steps", steps}, {"ok", all}};
    fs::create_directories(root / "target", error);
    std::ofstream(root / "target/doors.report.xdef.json", std::ios::binary) << cdp::pretty(report, 2) << '\n';
    if (!all) return failed("a door of the site answered otherwise than the others (target/doors.report.xdef.json)");
    return 0;
}

}  // namespace omui::devtools::check
