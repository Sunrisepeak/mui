module omui.devtools.browser;

import std;
import mcpplibs.os;
import mlibs.http;
import nlohmann.json;

namespace omui::devtools::browser {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// The first of the answers, whoever gives it: the program's exit, the browser's, the clock's.
class Finish {
public:
    void with(int code) {
        const std::scoped_lock hold(lock_);
        if (!code_) code_ = code;
        changed_.notify_all();
    }
    std::optional<int> wait(std::chrono::seconds most) {
        std::unique_lock hold(lock_);
        changed_.wait_for(hold, most, [&] { return code_.has_value(); });
        return code_;
    }

private:
    std::mutex lock_;
    std::condition_variable changed_;
    std::optional<int> code_;
};

std::string page_of(const Options& options, const nlohmann::json& preloaded) {
    const std::string script = os::fs::utf8(options.program.filename());
    return std::format(R"(<!doctype html>
<meta charset="utf-8">
<title>{0}</title>
<canvas id="canvas" width="320" height="240" style="width: 320px; height: 240px"></canvas>
<script>
  // Every line in order: one request after another.
  var queue = Promise.resolve();
  var send = function (path, body) {{ queue = queue.then(function () {{ return fetch(path, {{ method: 'POST', body: String(body) }}); }}).catch(function () {{}}); }};
  var Module = {{
    arguments: {1},
    print: function (t) {{ send('/out', t); }},
    printErr: function (t) {{ send('/err', t); }},
    onExit: function (code) {{ send('/exit', code); }},
    onAbort: function (what) {{ send('/err', 'aborted: ' + what); send('/exit', 1); }},
    // The preloads: the files in the program's file system before main().
    preRun: [function () {{
      {2}.forEach(function (f) {{
        addRunDependency(f[1]);
        fetch(f[0]).then(function (r) {{ return r.arrayBuffer(); }}).then(function (b) {{
          FS.mkdirTree(f[1].slice(0, f[1].lastIndexOf('/')));
          FS.writeFile(f[1], new Uint8Array(b));
        }}).catch(function (e) {{ send('/err', 'preload ' + f[0] + ': ' + e); }}).then(function () {{ removeRunDependency(f[1]); }});
      }});
    }}],
  }};
  window.addEventListener('error', function (e) {{ send('/err', 'page error: ' + e.message); send('/exit', 1); }});
  window.addEventListener('unhandledrejection', function (e) {{ send('/err', 'unhandled rejection: ' + (e.reason && e.reason.stack || e.reason)); }});
</script>
<script src="{0}"></script>
)",
                       script, nlohmann::json(options.arguments).dump(), preloaded.dump());
}

}  // namespace

std::optional<int> summary_failures(std::string_view line) {
    static const std::regex summary(R"(^(?:\S+: )?(\d+) passed, (\d+) failed(?:, \d+ skipped)?$)");
    std::match_results<std::string_view::const_iterator> found;
    if (!std::regex_match(line.begin(), line.end(), found, summary)) return std::nullopt;
    return std::stoi(found[2].str());
}

std::optional<fs::path> chromium() {
    if (const auto chosen = os::env::get("OMUI_CHROME"); chosen && !chosen->empty()) return os::fs::from_utf8(*chosen);
    // Playwright's headless shell, the newest.
    std::vector<fs::path> shells;
    std::error_code error;
    const fs::path cache = fs::path(os::env::get("HOME").value_or("")) / ".cache" / "ms-playwright";
    for (const auto& entry : fs::directory_iterator(cache, error)) {
        if (!os::fs::utf8(entry.path().filename()).starts_with("chromium_headless_shell-")) continue;
        for (const auto& inner : fs::directory_iterator(entry.path(), error))
            if (fs::exists(inner.path() / "chrome-headless-shell")) shells.push_back(inner.path() / "chrome-headless-shell");
    }
    if (!shells.empty()) return *std::ranges::max_element(shells);
    for (const std::string_view name : {"chrome-headless-shell", "google-chrome", "google-chrome-stable", "chromium", "chromium-browser"})
        if (auto found = os::env::find_executable(name)) return *found;
    return std::nullopt;
}

std::expected<int, std::string> run(const Options& options) {
    const fs::path chrome = options.chrome.empty() ? chromium().value_or(fs::path{}) : options.chrome;
    if (chrome.empty()) return std::unexpected("no headless Chromium (OMUI_CHROME)");
    if (!fs::exists(options.program)) return std::unexpected(std::format("{} does not exist", os::fs::utf8(options.program)));
    const fs::path directory = fs::absolute(options.program).parent_path();
    // What the preloads asked for: [url, destination path] for each file.
    nlohmann::json preloaded = nlohmann::json::array();
    std::error_code error;
    for (std::size_t index = 0; index < options.preloads.size(); ++index)
        for (const auto& entry : fs::directory_iterator(options.preloads[index].first, error))
            if (entry.is_regular_file(error)) {
                const std::string name = os::fs::utf8(entry.path().filename());
                preloaded.push_back({std::format("/preload/{}/{}", index, name), std::format("{}/{}", options.preloads[index].second, name)});
            }
    const std::string page = page_of(options, preloaded);

    auto listener = os::net::loopback_listener::listen(0);
    if (!listener) return std::unexpected(std::format("no server on the loopback: {}", listener.error().message()));
    const std::uint16_t port = listener->port();
    Finish finish;
    std::mutex said;
    std::vector<int> failures;  // each suite's
    const auto serve = [&](os::ipc::connection connection) {
        std::string arrived;
        std::optional<mlibs::http::Request> request;
        while (!request) {
            auto chunk = connection.read_for(std::chrono::seconds(10));
            if (!chunk || !*chunk || (*chunk)->empty()) return;
            arrived += **chunk;
            auto parsed = mlibs::http::parse(arrived);
            if (!parsed) return;
            request = std::move(*parsed);
        }
        while (arrived.size() < request->head_size + request->body_length) {
            auto chunk = connection.read_for(std::chrono::seconds(10));
            if (!chunk || !*chunk || (*chunk)->empty()) return;
            arrived += **chunk;
        }
        const std::string body = arrived.substr(request->head_size, request->body_length);
        const auto answer = [&](int status, std::string_view content, std::string_view type) {
            static_cast<void>(connection.write(mlibs::http::response(status, content, {.type = type})));
            connection.shutdown_write();
        };
        if (request->method == "POST") {
            {
                const std::scoped_lock hold(said);
                if (request->path == "/out") {
                    std::cout << body << '\n' << std::flush;
                    if (const auto failed = summary_failures(body)) failures.push_back(*failed);
                } else if (request->path == "/err") {
                    std::cerr << body << '\n' << std::flush;
                }
            }
            if (request->path == "/exit") {
                int code = 0;
                std::from_chars(body.data(), body.data() + body.size(), code);
                finish.with(code);
            }
            answer(200, "", "text/plain");
            return;
        }
        const std::string path = mlibs::http::decoded(request->path);
        if (path == "/" || path == "/index.html") return answer(200, page, "text/html");
        if (path.starts_with("/preload/")) {
            const std::string rest = path.substr(9);
            const std::size_t slash = rest.find('/');
            std::size_t index = options.preloads.size();
            std::from_chars(rest.data(), rest.data() + std::min(slash, rest.size()), index);
            if (index < options.preloads.size() && slash != std::string::npos)
                if (const auto bytes = read_text(options.preloads[index].first / fs::path(os::fs::from_utf8(rest.substr(slash + 1))).filename()))
                    return answer(200, *bytes, "application/octet-stream");
            return answer(404, "", "text/plain");
        }
        const fs::path file = directory / fs::path(os::fs::from_utf8(path)).filename();
        if (const auto bytes = read_text(file)) return answer(200, *bytes, mlibs::http::media_type(os::fs::utf8(file.extension())));
        answer(404, "", "text/plain");
    };
    std::atomic<bool> stopping{false};
    std::jthread server([&] {
        std::vector<std::jthread> connections;
        while (!stopping) {
            auto accepted = listener->accept();
            if (!accepted || stopping) break;
            connections.emplace_back(serve, std::move(*accepted));
        }
    });

    const fs::path profile = fs::temp_directory_path() / std::format("omui-chrome-{}", std::random_device{}());
    fs::create_directories(profile, error);
    os::process::spawn_options launch;
    launch.program = chrome;
    if (!options.headed) launch.args.push_back("--headless");
    launch.args.insert(launch.args.end(), {"--no-sandbox", "--no-first-run", "--disable-gpu-sandbox", std::format("--user-data-dir={}", os::fs::utf8(profile)),
                                           "--enable-unsafe-webgpu", "--enable-unsafe-swiftshader"});
    launch.args.insert(launch.args.end(), options.flags.begin(), options.flags.end());
    launch.args.push_back(std::format("http://localhost:{}/", port));
    launch.in = os::process::stdio::null;
    launch.out = os::process::stdio::null;
    launch.err = os::process::stdio::pipe;
    auto browser = os::process::process::spawn(launch);
    std::string log;
    std::jthread watcher;
    if (browser) {
        watcher = std::jthread([&] {
            for (;;) {
                auto chunk = browser->read_error();
                if (!chunk || chunk->empty()) break;
                log += *chunk;
            }
            const auto ended = browser->wait();
            finish.with(ended && ended->shell_code() != 0 ? ended->shell_code() : 1);  // the browser gone before the program said
        });
    }
    int code = 1;
    if (!browser) {
        std::cerr << std::format("omui-devtools browser: Chromium did not start: {}\n", browser.error().message());
    } else if (const auto given = finish.wait(options.timeout)) {
        code = *given;
    } else {
        std::cerr << std::format("omui-devtools browser: {} did not finish in {} s\n", os::fs::utf8(options.program.filename()), options.timeout.count());
    }
    {
        const std::scoped_lock hold(said);
        if (code == 0 && (failures.empty() || std::ranges::any_of(failures, [](int failed) { return failed > 0; }))) code = 1;
    }
    if (browser) {
        browser->kill();
        if (watcher.joinable()) watcher.join();
    }
    stopping = true;
    static_cast<void>(os::net::connect_loopback(port));  // wakes the accept
    if (server.joinable()) server.join();
    for (int attempt = 0; attempt < 5 && fs::exists(profile); ++attempt) {
        fs::remove_all(profile, error);
        if (fs::exists(profile)) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (code != 0 && os::env::get("OMUI_BROWSER_LOG")) std::cerr << log;
    return code;
}

}  // namespace omui::devtools::browser
