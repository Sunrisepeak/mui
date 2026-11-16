module omui.devtools.run;

import std;
import mcpplibs.os;
import nlohmann.json;

namespace omui::devtools::run {

namespace fs = std::filesystem;
namespace process = mcpplibs::os::process;

std::expected<Ran, std::string> program(const fs::path& root, std::string name, std::vector<std::string> args, const How& how) {
    process::spawn_options options;
    options.program = name;
    options.args = std::move(args);
    options.cwd = how.cwd.empty() ? root : how.cwd;
    // spawn, not process::run: run pipes both streams whatever the options say, and these choices are the point.
    options.in = how.input.empty() ? process::stdio::null : process::stdio::pipe;
    options.out = how.shown ? process::stdio::inherit : process::stdio::pipe;
    options.err = how.merged && !how.shown ? process::stdio::output : process::stdio::inherit;
    for (const auto& [variable, value] : how.variables) options.env.emplace_back(variable, value);
    auto started = process::process::spawn(options);
    if (!started) return std::unexpected(std::format("{} did not start: {}", name, started.error().message()));
    Ran out;
    std::jthread feeder;
    if (!how.input.empty())
        feeder = std::jthread([&started, &how] {
            static_cast<void>(started->write(how.input));
            started->close_input();
        });
    if (!how.shown)
        for (;;) {
            auto chunk = started->read_output();
            if (!chunk || chunk->empty()) break;
            out.out += *chunk;
        }
    if (feeder.joinable()) feeder.join();
    const auto status = started->wait();
    if (!status) return std::unexpected(std::format("{}: {}", name, status.error().message()));
    out.status = status->shell_code();
    return out;
}

std::expected<std::string, std::string> output(const fs::path& root, std::string name, std::vector<std::string> args, const How& how) {
    const std::string shown = name;
    auto done = program(root, std::move(name), std::move(args), how);
    if (!done) return std::unexpected(done.error());
    if (done->status != 0) return std::unexpected(std::format("{} exited {}", shown, done->status));
    while (!done->out.empty() && (done->out.back() == '\n' || done->out.back() == '\r')) done->out.pop_back();
    return std::move(done->out);
}

int share_of_machine() {
    const auto& processors = mcpplibs::os::machine::processors();
    int jobs = std::max(1, static_cast<int>(processors.logical) * 9 / 10);
    if (const auto memory = mcpplibs::os::machine::memory())
        jobs = std::min(jobs, static_cast<int>(*memory / (1024 * 1024) * 9 / 10 / 2250));
    jobs = std::max(jobs, 1);
    if (const auto asked = mcpplibs::os::env::get("MCPP_JOBS")) {
        int given = 0;
        const auto [end, failed] = std::from_chars(asked->data(), asked->data() + asked->size(), given);
        if (failed == std::errc{} && end == asked->data() + asked->size() && given >= 1) jobs = std::min(jobs, given);
    }
    return jobs;
}

std::expected<std::vector<Artifact>, std::string> build(const fs::path& root, std::vector<std::string> args) {
    args.insert(args.begin(), "build");
    args.insert(args.end(), {"--message-format", "json"});
    std::string said = "mcpp";
    for (const auto& arg : args) said += " " + arg;
    auto done = program(root, "mcpp", std::move(args), {.variables = {{"MCPP_JOBS", std::to_string(share_of_machine())}}});
    if (!done) return std::unexpected(done.error());
    if (done->status != 0) return std::unexpected(std::format("{} failed ({})", said, done->status));
    std::vector<Artifact> out;
    for (auto part : std::views::split(std::string_view(done->out), '\n')) {
        const auto line = nlohmann::json::parse(std::string_view(part.begin(), part.end()), nullptr, false);
        if (line.is_discarded() || line.value("event", "") != "artifact" || !line.contains("artifact")) continue;
        const auto& artifact = line["artifact"];
        out.push_back({artifact.value("name", ""), artifact.value("kind", ""), fs::path(artifact.value("path", ""))});
    }
    return out;
}

}  // namespace omui::devtools::run
