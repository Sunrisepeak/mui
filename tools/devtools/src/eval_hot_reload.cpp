// eval hot-reload (W2, save to screen ≤ 100 ms at p95; moved from tools/eval/hot_reload.py): a TypeScript application
// run in development mode with a real clock, its script saved again and again with a word of its title changed;
// how long until the semantic tree shows the new title, read through the agent protocol (`omui agent find`) as fast
// as it answers, so the figure includes that read.
module omui.devtools.eval;

import std;
import mcpplibs.os;
import nlohmann.json;
import omui.devtools.run;

namespace omui::devtools::eval {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;

// The application's text node's name, as `omui agent find APP --name Text*` reports it.
std::string shown_title(const fs::path& omui, const fs::path& endpoints, const std::string& app) {
    const auto done = run::program(endpoints, os::fs::utf8(omui), {"--dir", os::fs::utf8(endpoints), "agent", "find", app, "--name", "Text*"},
                                   {.timeout = std::chrono::seconds(10)});
    if (!done) return {};
    for (const auto line : std::views::split(std::string_view(done->out), '\n')) {
        const auto record = nlohmann::json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
        if (record.is_discarded() || !record.is_object() || !record.contains("data") || !record["data"].is_object()) continue;
        for (const auto& node : record["data"].value("nodes", nlohmann::json::array()))
            if (node.value("role", "") == "text") return node.value("name", "");
    }
    return {};
}

}  // namespace

int hot_reload(const HotReload& options) {
    const auto stamp = std::random_device{}();
    const fs::path work = fs::temp_directory_path() / std::format("omui-hot-{}", stamp);
    const fs::path endpoints = fs::temp_directory_path() / std::format("omui-hot-ep-{}", stamp);
    std::error_code error;
    fs::create_directories(work, error);
    fs::create_directories(endpoints, error);
    fs::copy_file(options.app_dir / "mcpp.toml", work / "mcpp.toml", error);
    fs::copy(options.app_dir / "logic", work / "logic", fs::copy_options::recursive, error);
    const fs::path script = work / "logic" / "main.ts";
    std::string original;
    {
        std::ifstream in(script, std::ios::binary);
        original.assign(std::istreambuf_iterator<char>(in), {});
    }
    const auto finish = [&](int code) {
        fs::remove_all(work, error);
        fs::remove_all(endpoints, error);
        return code;
    };
    if (original.find("Text statistics") == std::string::npos) {
        std::cerr << std::format("omui-devtools eval hot-reload: {} has no \"Text statistics\" to change\n", os::fs::utf8(script));
        return finish(2);
    }
    os::process::spawn_options launch;
    launch.program = options.program;
    launch.args = {"--dev", "--serve", "--agent-dir", os::fs::utf8(endpoints)};
    launch.cwd = work;
    launch.in = launch.out = launch.err = os::process::stdio::null;
    launch.own_unit = true;
    auto app = os::process::process::spawn(launch);
    if (!app) {
        std::cerr << std::format("omui-devtools eval hot-reload: {} did not start: {}\n", os::fs::utf8(options.program), app.error().message());
        return finish(2);
    }
    const auto stop = [&](int code) {
        app->kill();
        static_cast<void>(app->wait());
        return finish(code);
    };
    std::string name;
    for (int attempt = 0; attempt < 100 && name.empty(); ++attempt) {
        const auto listed = run::program(endpoints, os::fs::utf8(options.omui), {"--dir", os::fs::utf8(endpoints), "agent", "ls"});
        if (listed) name = listed->out.substr(0, listed->out.find_first_of("\t\n"));
        if (name.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (name.empty()) {
        std::cerr << "omui-devtools eval hot-reload: the application did not start\n";
        return stop(1);
    }
    std::vector<double> took;
    for (int round = 0; round < options.rounds; ++round) {
        const std::string word = std::format("Text statistics {}", round);
        std::string changed = original;
        for (std::size_t at = 0; (at = changed.find("Text statistics", at)) != std::string::npos; at += word.size()) changed.replace(at, 15, word);
        std::ofstream(script, std::ios::binary | std::ios::trunc) << changed;
        const auto start = std::chrono::steady_clock::now();
        while (shown_title(options.omui, endpoints, name) != word) {
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
                std::cerr << std::format("omui-devtools eval hot-reload: round {} did not show within 5 s\n", round);
                return stop(1);
            }
        }
        took.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::ranges::sort(took);
    const auto at = [&](double share) { return took[std::min(took.size() - 1, static_cast<std::size_t>(std::max(1.0, std::round(share * took.size()))) - 1)]; };
    const double median = took.size() % 2 ? took[took.size() / 2] : (took[took.size() / 2 - 1] + took[took.size() / 2]) / 2;
    const auto tenth = [](double value) { return std::round(value * 10) / 10; };
    std::cout << nlohmann::ordered_json{{"rounds", took.size()}, {"median_ms", tenth(median)}, {"p95_ms", tenth(at(0.95))}, {"max_ms", tenth(took.back())}}.dump()
              << '\n';
    return stop(0);
}

}  // namespace omui::devtools::eval
