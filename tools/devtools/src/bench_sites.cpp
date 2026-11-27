// `bench sites` (0.1.0 plan ST7): sites of documents side by side — oweb's beside VitePress's, Astro Starlight's
// and mdBook's — one corpus (the minimal documents) built by each side at its pin and measured from outside.
// The sides, the pins, the routes and the visit's conditions are tests/bench/competitors/sites/sites.bench.xdef.json;
// each side's project (its configuration only) is beside it. The harness puts the corpus into a copy of the
// project under target/bench-sites/<side>, in the form the side reads. A round is, for each side in turn, a build
// (the output removed before it, the wall time of the side's own command) and a visit of each route in headless
// Chromium — cold, on the catalogue's connection and processor — with scripts on: the bytes by kind, the requests,
// what the first paint waits for (the document and what the browser marks as blocking the rendering), the first
// and the largest contentful paint, the time long tasks blocked, and when a page's program had drawn it. What was
// written (mlibs.delivery: as stored, as a static host sends it) and each route with scripts off (are the words
// there) are taken in the first round. The report: target/bench-sites/sites.report.xdef.json, each round's sample
// beside it, a table on standard output; `--ledger` judges the samples into the lead ledger (omui.devtools.lead).
module omui.devtools.bench;

import std;
import mcpplibs.os;
import mcpplibs.cmdline;
import nlohmann.json;
import mlibs.delivery;
import omui.devtools.browser;
import omui.devtools.cdp;
import omui.devtools.lead;
import omui.devtools.run;

namespace omui::devtools::bench {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using Clock = std::chrono::steady_clock;

constexpr std::string_view sites_catalogue = "tests/bench/competitors/sites/sites.bench.xdef.json";

std::string utf8_of(const fs::path& path) { return mcpplibs::os::fs::utf8(path); }

std::expected<std::string, std::string> read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot be read", utf8_of(file)));
    return std::string(std::istreambuf_iterator<char>(in), {});
}

bool write_text(const fs::path& file, std::string_view text) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

std::string text_of(const json& value, std::string_view key, std::string_view otherwise = {}) {
    const auto found = value.find(key);
    return found != value.end() && found->is_string() ? found->get<std::string>() : std::string(otherwise);
}

double median_of(std::vector<double> values) {
    if (values.empty()) return 0;
    std::ranges::sort(values);
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

// One route of the corpus: where it is served, its page in the corpus, and the words a reader came for.
struct SiteRoute {
    std::string path, source, says;
};

// A page of the corpus in the form `routes`: its first heading as the title in front matter (the side draws it),
// its links to other pages as their routes — the words unchanged.
std::string as_routes(std::string_view text, std::string_view source, std::span<const SiteRoute> routes) {
    std::string title, body(text);
    if (text.starts_with("# ")) {
        const std::size_t end = text.find('\n');
        title = std::string(text.substr(2, end == std::string_view::npos ? end : end - 2));
        body = end == std::string_view::npos ? std::string() : std::string(text.substr(end + 1));
    }
    const fs::path directory = fs::path(std::u8string(source.begin(), source.end())).parent_path();
    std::string out;
    for (std::size_t at = 0; at < body.size();) {
        const std::size_t open = body.find("](", at);
        if (open == std::string::npos) {
            out += body.substr(at);
            break;
        }
        const std::size_t close = body.find(')', open);
        out += body.substr(at, open + 2 - at);
        if (close == std::string::npos) {
            at = open + 2;
            continue;
        }
        const std::string inner = body.substr(open + 2, close - open - 2);
        const std::size_t space = inner.find(' ');
        const std::string href = inner.substr(0, space), rest = space == std::string::npos ? std::string() : inner.substr(space);
        const std::size_t hash = href.find('#');
        const std::string file = href.substr(0, hash), anchor = hash == std::string::npos ? std::string() : href.substr(hash);
        std::string written = href;
        if (file.ends_with(".md") && !file.contains("://") && !file.starts_with('/')) {
            const std::u8string normal = (directory / fs::path(std::u8string(file.begin(), file.end()))).lexically_normal().generic_u8string();
            const std::string target(normal.begin(), normal.end());
            if (const auto route = std::ranges::find(routes, target, &SiteRoute::source); route != routes.end()) written = route->path + anchor;
        }
        out += written + rest + ")";
        at = close + 1;
    }
    std::string quoted;
    for (const char letter : title) {
        if (letter == '"' || letter == '\\') quoted += '\\';
        quoted += letter;
    }
    return std::format("---\ntitle: \"{}\"\n---\n{}", quoted, out);
}

// The corpus put where a side reads it: its pages (and the generated reference) as they are or as routes, what
// stands beside them (pictures) copied. The page for a lost address is the side's own.
std::expected<void, std::string> place_corpus(const fs::path& root, const json& catalogue, std::span<const SiteRoute> routes, const fs::path& content, std::string_view form) {
    const fs::path corpus = root / text_of(catalogue, "corpus");
    std::error_code error;
    for (const SiteRoute& route : routes) {
        const fs::path from = route.source == "reference/ops/index.md" ? root / text_of(catalogue, "reference") : corpus / route.source;
        const auto text = read_text(from);
        if (!text) return std::unexpected(text.error());
        if (!write_text(content / route.source, form == "routes" ? as_routes(*text, route.source, routes) : *text))
            return std::unexpected(std::format("{}: cannot be written", utf8_of(content / route.source)));
    }
    for (const auto& entry : fs::recursive_directory_iterator(corpus, error)) {
        if (!entry.is_regular_file() || entry.path().extension() == ".md") continue;
        const fs::path to = content / entry.path().lexically_relative(corpus);
        fs::create_directories(to.parent_path(), error);
        fs::copy_file(entry.path(), to, fs::copy_options::overwrite_existing, error);
        if (error) return std::unexpected(std::format("{}: {}", utf8_of(to), error.message()));
    }
    return {};
}

// A side made ready and how it is built: the program and its arguments, where it runs, what it writes.
struct Ready {
    std::string program;
    std::vector<std::string> args;
    fs::path cwd, out;
    json versions = json::object();
};

std::expected<Ready, std::string> ready(const fs::path& root, const json& catalogue, const json& side, std::span<const SiteRoute> routes, const fs::path& work, bool install) {
    const std::string series = text_of(side, "series"), build = text_of(side, "build");
    const fs::path directory = work / series;
    std::error_code error;
    Ready made;
    if (build == "oweb") {
        // Our side: the builder as a release builds it, run as the command it is.
        const auto built = run::build(root, {"-p", "modules/projects/oweb", "--profile", "release"});
        if (!built) return std::unexpected(built.error());
        const auto program = std::ranges::find(*built, std::string("oweb"), &run::Artifact::name);
        if (program == built->end()) return std::unexpected("the build of modules/projects/oweb gave no program `oweb`");
        made.program = utf8_of(program->path);
        made.out = directory / "site";
        made.cwd = root;
        made.args = {"site.build", "--site", utf8_of(root / fs::path(text_of(catalogue, "corpus")).parent_path()), "--out", utf8_of(made.out)};
        if (side.value("engine", false)) {
            // The program its pages load: the newest world packed here (oweb.reader: `mcpp pack --target
            // wasm32-emscripten --profile release --pkg-format dist-toy` in modules/projects/oweb/reader).
            fs::path world;
            fs::file_time_type newest{};
            for (const auto& entry : fs::directory_iterator(root / "target/.build-mcpp/out", error)) {
                const fs::path page = entry.path() / "oweb.reader/toy/oweb-reader/index.html";
                if (!fs::is_regular_file(page, error)) continue;
                if (const auto written = fs::last_write_time(page, error); world.empty() || written > newest) {
                    world = page.parent_path();
                    newest = written;
                }
            }
            if (world.empty()) return std::unexpected("no world of oweb.reader is packed here (in modules/projects/oweb/reader: mcpp pack --target wasm32-emscripten --profile release --pkg-format dist-toy)");
            made.args.insert(made.args.end(), {"--engine", utf8_of(world)});
        }
        made.versions["oweb"] = run::commit(root);
        return made;
    }
    // Their side: the project's configuration copied, the corpus put in, its tools at their pins.
    const fs::path project = root / "tests/bench/competitors/sites" / text_of(side, "project");
    const fs::path content = directory / text_of(side, "content");
    made.out = directory / text_of(side, "out");
    made.cwd = directory;
    fs::remove_all(content, error);
    fs::remove_all(made.out, error);
    fs::create_directories(directory, error);
    fs::copy(project, directory, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    if (error) return std::unexpected(std::format("{}: {}", utf8_of(directory), error.message()));
    if (auto placed = place_corpus(root, catalogue, routes, content, text_of(side, "form", "pages")); !placed) return std::unexpected(placed.error());
    const json& pins = catalogue.at("pins");
    if (build == "npm") {
        if (install) {
            const auto installed = run::program(root, "npm", {"install", "--no-audit", "--no-fund"}, {.cwd = directory, .merged = true, .timeout = std::chrono::seconds(900)});
            if (!installed) return std::unexpected(installed.error());
            if (installed->status != 0) return std::unexpected(std::format("npm install in {} failed:\n{}", utf8_of(directory), installed->out));
        }
        for (const auto& name : side.value("pins", json::array())) {
            const json& pin = pins.at(name.get<std::string>());
            const auto package = read_text(directory / "node_modules" / text_of(pin, "package") / "package.json");
            if (!package) return std::unexpected(std::format("{} is not installed in {} (run without --no-install)", text_of(pin, "package"), utf8_of(directory)));
            const std::string found = text_of(json::parse(*package, nullptr, false), "version");
            if (found != text_of(pin, "version")) return std::unexpected(std::format("{} is {} here, pinned {}", text_of(pin, "package"), found, text_of(pin, "version")));
            made.versions[text_of(pin, "package")] = found;
        }
        made.program = "npm";
        made.args = {"run", "--silent", "build"};
        return made;
    }
    if (build == "mdbook") {
        const json& pin = pins.at("mdbook");
        const auto version = run::output(root, "mdbook", {"--version"});
        if (!version) return std::unexpected(std::format("mdbook is not on PATH (pinned {}): {}", text_of(pin, "version"), version.error()));
        if (!version->ends_with(text_of(pin, "version"))) return std::unexpected(std::format("`{}` here, pinned {}", *version, text_of(pin, "version")));
        made.versions["mdbook"] = text_of(pin, "version");
        made.program = "mdbook";
        made.args = {"build"};
        return made;
    }
    return std::unexpected(std::format("the side {} is built by `{}`: oweb, npm or mdbook", series, build));
}

// Where a side serves a route: a directory's page (`/start/`), or a file of the page's name (`/start.html`; a
// directory's own page is the directory).
std::string address_of(const json& side, const SiteRoute& route) {
    if (text_of(side, "address", "directory") == "directory" || route.source.ends_with("index.md")) return route.path.substr(1);
    return route.source.substr(0, route.source.size() - 3) + ".html";
}

// What the page records of its own visit, from before its first byte is read.
constexpr std::string_view watching = R"(
  window.__site = { fcp: null, lcp: null, blocked: 0, loaded: null };
  try {
    new PerformanceObserver((list) => { for (const e of list.getEntries()) if (e.name === 'first-contentful-paint') window.__site.fcp = e.startTime; }).observe({ type: 'paint', buffered: true });
    new PerformanceObserver((list) => { for (const e of list.getEntries()) window.__site.lcp = e.startTime; }).observe({ type: 'largest-contentful-paint', buffered: true });
    new PerformanceObserver((list) => { for (const e of list.getEntries()) if (e.duration > 50) window.__site.blocked += e.duration - 50; }).observe({ type: 'longtask', buffered: true });
  } catch (e) {}
  addEventListener('load', () => { window.__site.loaded = performance.now(); });
)";

struct Visiting {
    fs::path chrome;
    int width = 412, height = 823, quiet_ms = 2000, timeout_ms = 90000;
    double mbit = 0, rtt = 0, cpu = 1;
};

// One route visited cold: a browser of its own (no cache, no profile kept), the connection and the processor as
// the catalogue says. With scripts: the bytes by kind, the requests, the paints, the blocking. Without: whether
// the words are in the page.
json visit(const Visiting& how, const std::string& url, const SiteRoute& route, bool scripts) {
    const fs::path profile = fs::temp_directory_path() / std::format("omui-bench-sites-{:08x}", std::random_device{}());
    std::error_code error;
    fs::create_directories(profile, error);
    json row = json::object();
    {
        auto launched = cdp::Browser::launch(how.chrome, {.width = how.width, .height = how.height, .flags = {"--user-data-dir=" + utf8_of(profile)}});
        if (!launched) {
            fs::remove_all(profile, error);
            return json{{"error", launched.error()}};
        }
        auto& page = **launched;
        std::map<std::string, std::string> kinds;
        std::set<std::string> open;                      // asked for, not yet ended
        json bytes = json::object();
        double last = 0, total = 0;
        std::size_t requests = 0, failed = 0;
        page.on([&](const std::string& method, const json& params) {
            const std::string id = cdp::text(cdp::at(params, "requestId"));
            const double when = cdp::number(cdp::at(params, "timestamp"));
            if (method == "Network.requestWillBeSent") {
                ++requests;
                open.insert(id);
                last = when;
            }
            if (method == "Network.dataReceived") last = when;
            if (method == "Network.responseReceived") {
                kinds[id] = cdp::text(cdp::at(params, "type"));
                if (cdp::number(cdp::dig(params, "response", "status")) >= 400) ++failed;
            }
            if (method == "Network.loadingFinished") {
                std::string kind = kinds.contains(id) && !kinds[id].empty() ? kinds[id] : "Other";
                for (char& letter : kind) letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
                const double size = cdp::number(cdp::at(params, "encodedDataLength"));
                bytes[kind] = (bytes.contains(kind) ? bytes[kind].get<double>() : 0.0) + size;
                total += size;
                open.erase(id);
                last = when;
            }
            if (method == "Network.loadingFailed") {
                open.erase(id);
                last = when;
            }
        });
        cdp::Script script;
        script.page = &page;
        script.call("Network.enable");
        script.call("Network.setCacheDisabled", {{"cacheDisabled", true}});
        script.call("Network.setBypassServiceWorker", {{"bypass", true}});
        if (how.mbit > 0)
            script.call("Network.emulateNetworkConditions", {{"offline", false}, {"latency", how.rtt}, {"downloadThroughput", how.mbit * 125000}, {"uploadThroughput", how.mbit * 125000 * 15 / 32}});
        if (how.cpu > 1) script.call("Emulation.setCPUThrottlingRate", {{"rate", how.cpu}});
        if (!scripts) script.call("Emulation.setScriptExecutionDisabled", {{"value", true}});
        script.call("Page.enable");
        if (scripts) script.call("Page.addScriptToEvaluateOnNewDocument", {{"source", std::string(watching)}});
        const auto begun = Clock::now();
        script.call("Page.navigate", {{"url", url}});
        // Until the page has loaded, nothing it asked for is still coming, nothing has been asked for or has arrived
        // for `quiet_ms` — and, where the page takes a program, the program has drawn it.
        const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - begun).count(); };
        double quiet_since = 0, seen_last = -1, presented = 0;
        bool loaded = false, program = false;
        while (elapsed() < how.timeout_ms && !script.thrown()) {
            script.sleep(100);
            if (!loaded) loaded = cdp::truthy(script.value("document.readyState === 'complete'"));
            if (scripts && presented == 0) {
                const json state = script.value("typeof window.omuiHasPresented === 'undefined' ? null : (window.omuiHasPresented === true ? performance.now() : 0)");
                program = !state.is_null();
                presented = program ? cdp::number(state) : 0;
            }
            if (last != seen_last || !open.empty()) {
                seen_last = last;
                quiet_since = elapsed();
            }
            if (loaded && open.empty() && elapsed() - quiet_since >= how.quiet_ms && (!program || presented > 0)) break;
        }
        const std::string says = route.says;
        row["address"] = url;
        // The words in their order, however the page breaks its lines; and whether the browser's own find reaches
        // them (a phrase that wraps onto a second line is one a reader still searches for whole).
        row["says"] = cdp::truthy(script.value(std::format("document.body && document.body.innerText.replace(/\\s+/g, ' ').includes({})", cdp::q(says))));
        row["found"] = cdp::truthy(script.value(std::format("(getSelection().removeAllRanges(), window.find({}))", cdp::q(says))));
        if (scripts) {
            const json recorded = script.value("window.__site");
            // What the first paint waits for, as the browser says it: the document and each resource it marks as
            // blocking the rendering (a sheet in the head, a script that is neither deferred nor a module).
            const json waited = script.value(
                "(() => { const nav = performance.getEntriesByType('navigation')[0]; const blocking = performance.getEntriesByType('resource').filter((e) => e.renderBlockingStatus === 'blocking');"
                " return { document: nav ? nav.transferSize : 0, blocking: blocking.reduce((sum, e) => sum + e.transferSize, 0), count: blocking.length }; })()");
            const double before_paint = cdp::number(cdp::at(waited, "document")) + cdp::number(cdp::at(waited, "blocking"));
            row["requests"] = requests;
            row["failed"] = failed;
            row["transfer"] = total;
            row["before_first_paint"] = before_paint;
            row["blocking_resources"] = cdp::at(waited, "count");
            row["by_kind"] = bytes;
            row["fcp_ms"] = cdp::at(recorded, "fcp");
            row["lcp_ms"] = cdp::at(recorded, "lcp");
            row["blocked_ms"] = cdp::at(recorded, "blocked");
            row["loaded_ms"] = cdp::at(recorded, "loaded");
            // A page that takes a program: when the program had drawn it (seen within a tenth of a second).
            if (program) row["presented_ms"] = presented > 0 ? json(presented) : json(nullptr);
        }
        if (const auto thrown = script.take()) row["error"] = *thrown;
        page.close();
    }
    fs::remove_all(profile, error);
    return row;
}

std::string kilobytes(double bytes) { return std::format("{:.1f}", bytes / 1000.0); }

}  // namespace

mcpplibs::cmdline::App sites_command_line() {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Option;
    return App("sites")
        .description("Sites of documents side by side (ST7): oweb's beside VitePress's, Starlight's and mdBook's — one corpus built by each at its pin, "
                     "the build timed, what is written measured, each route visited cold in headless Chromium with scripts on and off; rounds "
                     "alternating the sides, judged into the lead ledger")
        .option(Option("only").takes_value().value_name("SIDES").help("series, comma-separated (default: every side)"))
        .option(Option("rounds").takes_value().value_name("N").help("a build and a pass of visits a side, the sides alternating (default: the catalogue's)"))
        .option(Option("no-install").help("their tools as they are installed in target/bench-sites (no npm install)"))
        .option(Option("no-visit").help("build and measure what is written; visit no page"))
        .option(Option("ledger").help("judge the rounds' samples into the lead ledger and write its pages (with every side, visited)"))
        .option(Option("dry-run").help("with --ledger: print the verdicts; write the samples, not the ledger"));
}

int sites(const fs::path& root, const mcpplibs::cmdline::ParsedArgs& given) {
    const auto failed = [](std::string_view why) {
        std::cerr << "omui-devtools bench sites: " << why << '\n';
        return 2;
    };
    const auto catalogue_text = read_text(root / sites_catalogue);
    if (!catalogue_text) return failed(catalogue_text.error());
    const json catalogue = json::parse(*catalogue_text, nullptr, false);
    if (!catalogue.is_object() || !catalogue.contains("sides")) return failed(std::format("{} is no catalogue of sides", sites_catalogue));
    std::vector<SiteRoute> routes;
    for (const json& route : catalogue.at("routes")) routes.push_back({text_of(route, "path"), text_of(route, "source"), text_of(route, "says")});
    std::vector<std::string> only;
    if (const auto named = given.value("only"))
        for (const auto part : std::views::split(*named, ','))
            if (!std::ranges::empty(part)) only.emplace_back(part.begin(), part.end());
    int rounds = catalogue.value("rounds", 5);
    const int builds = std::max(1, catalogue.value("builds", 1));
    if (const auto asked = given.value("rounds")) {
        if (std::from_chars(asked->data(), asked->data() + asked->size(), rounds).ec != std::errc{} || rounds < 1) return failed("--rounds takes a count of at least 1");
    }
    Visiting how;
    const bool visits = !given.is_flag_set("no-visit"), ledger = given.is_flag_set("ledger");
    if (ledger && (!visits || !only.empty())) return failed("--ledger judges every side, visited: without --only and --no-visit");
    if (visits) {
        const auto chrome = browser::chromium();
        if (!chrome) return failed("no headless Chromium (OMUI_CHROME); --no-visit measures the builds and what they write alone");
        how.chrome = *chrome;
        how.width = catalogue.at("viewport").value("width", 412);
        how.height = catalogue.at("viewport").value("height", 823);
        how.quiet_ms = catalogue.value("quiet_ms", 2000);
        how.timeout_ms = catalogue.value("timeout_ms", 90000);
        how.cpu = catalogue.value("cpu", 1.0);
        const std::string network = text_of(catalogue, "network");
        if (const std::size_t slash = network.find('/'); slash != std::string::npos) {
            how.mbit = std::stod(network.substr(0, slash));
            how.rtt = std::stod(network.substr(slash + 1));
        }
    }
    const fs::path work = root / "target/bench-sites";
    std::error_code error;

    // Every side made ready before any is measured: its tools at their pins, the corpus in.
    struct Running {
        const json* side = nullptr;
        std::string series;
        Ready made;
        std::vector<double> build_ms, transfer, before, requests, fcp, lcp, blocked, presented;  // a round each
        mlibs::delivery::Manifest written;
        json agent = json::object(), routes = json::array();
        std::size_t says_with = 0, says_without = 0, found_with = 0;
    };
    std::vector<Running> sides;
    for (const json& side : catalogue.at("sides")) {
        const std::string series = text_of(side, "series");
        if (!only.empty() && !std::ranges::contains(only, series)) continue;
        auto made = ready(root, catalogue, side, routes, work, !given.is_flag_set("no-install"));
        if (!made) return failed(std::format("{}: {}", series, made.error()));
        sides.push_back({.side = &side, .series = series, .made = std::move(*made)});
    }
    if (sides.empty()) return failed("no side of the catalogue is named so");

    // The machine's state, said on the ledger's records: other work on it moves the times, not the bytes.
    const auto load_now = [&] {
        // As uptime says it ("load average: 1.33, 2.29, 1.76"): the first.
        const std::string said = run::output(root, "uptime", {}).value_or("");
        const std::size_t at = said.find("average:");
        if (at == std::string::npos) return std::string("unknown");
        const std::size_t from = said.find_first_not_of(' ', at + 8), to = said.find(',', at);
        return from == std::string::npos ? std::string("unknown") : said.substr(from, to == std::string::npos ? to : to - from);
    };
    const std::string load_at_start = load_now();
    const fs::path samples_path = work / "sites.samples.xdef.jsonl";
    fs::remove(samples_path, error);
    for (int round = 1; round <= rounds; ++round) {
        std::cout << std::format("round {} of {}\n", round, rounds) << std::flush;
        for (Running& side : sides) {
            // The build, from nothing written: the wall time of the side's own command — the median of the
            // catalogue's `builds` in a row (a build of tens of milliseconds is otherwise the machine's moment).
            std::vector<double> timed;
            for (int again = 0; again < builds; ++again) {
                fs::remove_all(side.made.out, error);
                const auto begun = Clock::now();
                const auto built = run::program(root, side.made.program, side.made.args, {.cwd = side.made.cwd, .merged = true, .timeout = std::chrono::seconds(600)});
                timed.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begun).count());
                if (!built) return failed(std::format("{}: {}", side.series, built.error()));
                if (built->status != 0) return failed(std::format("{}: its build failed ({}):\n{}", side.series, built->status, built->out));
            }
            const double took = median_of(timed);
            side.build_ms.push_back(took);
            json sample = {{"series", side.series}, {"round", round}, {"key", "docs"}, {"build_ms", took}};
            if (round == 1) {
                auto written = mlibs::delivery::measure(side.made.out);
                if (!written) return failed(std::format("{}: {}", side.series, written.error()));
                side.written = std::move(*written);
                // What an agent that only fetches finds, as files of the site.
                const auto there = [&](std::string_view path) { return fs::is_regular_file(side.made.out / fs::path(std::u8string(path.begin(), path.end())), error); };
                std::size_t markdown = 0;
                for (const SiteRoute& route : routes) markdown += there(route.path.substr(1) + "index.md") ? 1 : 0;
                side.agent = {{"markdown_routes", markdown}, {"llms_txt", there("llms.txt")}, {"routes_table", there("site.routes.xdef.json")}, {"search_index_as_data", there("site.search.xdef.json")}};
            }
            sample["written_bytes"] = side.written.bytes;
            sample["written_sent_bytes"] = side.written.sent;
            std::cout << std::format("  {:<12} built in {:>6.0f} ms; {} files, {} kB stored, {} kB sent\n", side.series, took, side.written.files.size(),
                                     kilobytes(static_cast<double>(side.written.bytes)), kilobytes(static_cast<double>(side.written.sent)))
                      << std::flush;
            if (visits) {
                auto server = cdp::Server::start(cdp::pages(side.made.out));
                if (!server) return failed(server.error());
                std::vector<double> transfer, before, requests, fcp, lcp, blocked, presented;
                json visited = json::array();
                for (const SiteRoute& route : routes) {
                    const std::string url = (*server)->url() + address_of(*side.side, route);
                    json with = visit(how, url, route, true);
                    if (with.contains("error")) return failed(std::format("{} {}: {}", side.series, route.path, text_of(with, "error")));
                    transfer.push_back(with.value("transfer", 0.0));
                    before.push_back(with.value("before_first_paint", 0.0));
                    requests.push_back(with.value("requests", 0.0));
                    fcp.push_back(cdp::number(with["fcp_ms"]));
                    lcp.push_back(cdp::number(with["lcp_ms"]));
                    blocked.push_back(cdp::number(with["blocked_ms"]));
                    if (with.contains("presented_ms") && with["presented_ms"].is_number()) presented.push_back(with["presented_ms"].get<double>());
                    json row = {{"path", route.path}, {"scripts", with}};
                    if (round == 1) {
                        // With scripts off the page is what it is: asked once.
                        json without = visit(how, url, route, false);
                        if (without.contains("error")) return failed(std::format("{} {}: {}", side.series, route.path, text_of(without, "error")));
                        side.says_with += with.value("says", false) ? 1 : 0;
                        side.found_with += with.value("found", false) ? 1 : 0;
                        side.says_without += without.value("says", false) ? 1 : 0;
                        std::cout << std::format("    {:<16} {:>8} kB in {:>2} requests ({:>6} kB the first paint waits for), first paint {:>5.0f} ms, largest {:>5.0f} ms, blocked {:>4.0f} ms{}; its words with scripts {} (the browser's find {}), without {}\n",
                                                 route.path, kilobytes(with.value("transfer", 0.0)), with.value("requests", 0), kilobytes(with.value("before_first_paint", 0.0)), cdp::number(with["fcp_ms"]),
                                                 cdp::number(with["lcp_ms"]), cdp::number(with["blocked_ms"]),
                                                 with.contains("presented_ms") && with["presented_ms"].is_number() ? std::format(", its program had drawn at {:.0f} ms", with["presented_ms"].get<double>()) : std::string(),
                                                 with.value("says", false) ? "yes" : "NO", with.value("found", false) ? "reaches them" : "DOES NOT", without.value("says", false) ? "yes" : "NO")
                                  << std::flush;
                        row["no_scripts"] = std::move(without);
                        visited.push_back(std::move(row));
                    }
                }
                if (round == 1) side.routes = std::move(visited);
                // A round's sample: the median over the routes.
                sample["route_transfer_bytes"] = median_of(transfer);
                sample["route_blocking_bytes"] = median_of(before);
                sample["route_requests"] = median_of(requests);
                sample["fcp_ms"] = median_of(fcp);
                sample["lcp_ms"] = median_of(lcp);
                sample["blocked_ms"] = median_of(blocked);
                if (!presented.empty()) sample["presented_ms"] = median_of(presented);
                side.transfer.push_back(median_of(transfer));
                side.before.push_back(median_of(before));
                side.requests.push_back(median_of(requests));
                side.fcp.push_back(median_of(fcp));
                side.lcp.push_back(median_of(lcp));
                side.blocked.push_back(median_of(blocked));
                if (!presented.empty()) side.presented.push_back(median_of(presented));
                if (round > 1)
                    std::cout << std::format("    a route: {} kB, first paint {:.0f} ms, largest {:.0f} ms\n", kilobytes(median_of(transfer)), median_of(fcp), median_of(lcp)) << std::flush;
            }
            if (!lead::append(samples_path, std::vector{sample.dump()})) return failed("the samples cannot be written");
        }
    }

    json reported = json::array();
    for (const Running& side : sides) {
        json record = {{"series", side.series},
                       {"about", text_of(*side.side, "about")},
                       {"versions", side.made.versions},
                       {"build_ms", {{"median", median_of(side.build_ms)}, {"least", std::ranges::min(side.build_ms)}, {"most", std::ranges::max(side.build_ms)}, {"rounds", rounds}}},
                       {"written", {{"files", side.written.files.size()}, {"bytes", side.written.bytes}, {"sent", side.written.sent}}},
                       {"agent", side.agent}};
        if (visits) {
            record["routes"] = side.routes;
            record["visit"] = {{"transfer_median", median_of(side.transfer)},
                               {"before_first_paint_median", median_of(side.before)},
                               {"requests_median", median_of(side.requests)},
                               {"fcp_ms_median", median_of(side.fcp)},
                               {"lcp_ms_median", median_of(side.lcp)},
                               {"blocked_ms_median", median_of(side.blocked)},
                               {"presented_ms_median", side.presented.empty() ? json(nullptr) : json(median_of(side.presented))},
                               {"says_with_scripts", side.says_with},
                               {"found_with_scripts", side.found_with},
                               {"says_without_scripts", side.says_without},
                               {"routes", routes.size()}};
        }
        reported.push_back(std::move(record));
    }
    const json report = {{"$format", "report@1"},
                         {"report", "bench-sites"},
                         {"date", run::today()},
                         {"commit", run::commit(root)},
                         {"conditions", {{"network", text_of(catalogue, "network")}, {"cpu", catalogue.value("cpu", 1.0)}, {"viewport", catalogue.at("viewport")}, {"rounds", rounds}, {"builds", builds}}},
                         {"sides", reported}};
    write_text(work / "sites.report.xdef.json", cdp::pretty(report, 2) + "\n");
    // The table: a side a row, each number the median of the rounds (a round's: the median of its routes).
    std::cout << "\n| side | build (ms) | files | stored (kB) | a route transfers (kB) | its first paint waits for (kB) | requests | first paint (ms) | largest paint (ms) | blocked (ms) | its program has drawn (ms) | reads with no script | the browser's find reaches its words | Markdown routes | llms.txt |\n"
                 "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|\n";
    for (const json& side : reported) {
        const json& visited = side.contains("visit") ? side["visit"] : json::object();
        const auto shown = [&](std::string_view key, bool kilo) {
            return visited.contains(key) && visited[key].is_number() ? (kilo ? kilobytes(visited[key].get<double>()) : std::format("{:.0f}", visited[key].get<double>())) : std::string("—");
        };
        const auto of_routes = [&](std::string_view key) {
            return visited.contains(key) ? std::format("{} of {}", visited[key].get<std::size_t>(), visited["routes"].get<std::size_t>()) : std::string("—");
        };
        std::cout << std::format("| {} | {:.0f} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |\n", text_of(side, "series"), side["build_ms"]["median"].get<double>(),
                                 side["written"]["files"].get<std::size_t>(), kilobytes(side["written"]["bytes"].get<double>()), shown("transfer_median", true), shown("before_first_paint_median", true),
                                 shown("requests_median", false), shown("fcp_ms_median", false), shown("lcp_ms_median", false), shown("blocked_ms_median", false), shown("presented_ms_median", false),
                                 of_routes("says_without_scripts"), of_routes("found_with_scripts"),
                                 side["agent"]["markdown_routes"].get<std::size_t>(), side["agent"]["llms_txt"].get<bool>() ? "yes" : "no");
    }
    std::cout << std::format("\nthe report: {}\n", utf8_of(work / "sites.report.xdef.json"));
    if (!ledger) return 0;

    // The lead ledger (D-135): each measured metric of these sides judged by the ratio a round.
    lead::Context context = lead::context_here(root);
    context.display = std::format("headless Chromium, {} × {}", how.width, how.height);
    context.conditions = std::format("the connection {} (megabits a second / round trip in ms), the processor slowed {}×; a cold visit a route, a browser of its own each; 1-minute load {} at the start, {} at the end",
                                     text_of(catalogue, "network"), catalogue.value("cpu", 1.0), load_at_start, load_now());
    std::vector<std::string> skipped;
    const auto judged = lead::verdicts(mcpplibs::os::fs::read(root / lead::metrics_path).value_or(""), mcpplibs::os::fs::read(samples_path).value_or(""),
                                       mcpplibs::os::fs::read(root / lead::ledger_path).value_or(""), context, skipped);
    if (!judged) return failed(judged.error());
    std::vector<std::string> records;
    for (const auto& verdict : *judged) {
        const auto record = json::parse(verdict.record, nullptr, false);
        if (record.is_discarded() || !record.contains("ours")) continue;
        // Only this run's metrics: those whose series these sides are.
        const std::string ours = record["ours"].value("name", "");
        if (std::ranges::none_of(sides, [&](const Running& side) { return side.series == ours; })) continue;
        std::cout << verdict.said << '\n';
        records.push_back(verdict.record);
    }
    if (given.is_flag_set("dry-run")) return 0;
    if (!lead::append(root / lead::ledger_path, records)) return failed("the ledger cannot be written");
    const auto pages_written = lead::write_pages(root);
    if (!pages_written) return failed(pages_written.error());
    for (const auto& path : *pages_written) std::cout << std::format("wrote {}\n", path);
    return 0;
}

}  // namespace omui::devtools::bench
