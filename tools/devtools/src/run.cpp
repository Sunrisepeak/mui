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
    const bool bounded = how.timeout.count() > 0;
    options.own_unit = bounded;  // the timeout ends what it started too
    auto started = process::process::spawn(options);
    if (!started) return std::unexpected(std::format("{} did not start: {}", name, started.error().message()));
    Ran out;
    std::jthread feeder;
    if (!how.input.empty())
        feeder = std::jthread([&started, &how] {
            static_cast<void>(started->write(how.input));
            started->close_input();
        });
    const auto deadline = std::chrono::steady_clock::now() + how.timeout;
    const auto left = [&] { return std::max(std::chrono::milliseconds(1), std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())); };
    if (!how.shown)
        for (;;) {
            if (!bounded) {
                auto chunk = started->read_output();
                if (!chunk || chunk->empty()) break;
                out.out += *chunk;
                continue;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                out.timed_out = true;
                break;
            }
            auto chunk = started->read_output_for(std::min(left(), std::chrono::milliseconds(500)));
            if (!chunk) break;
            if (!*chunk) continue;  // nothing yet
            if ((*chunk)->empty()) break;
            out.out += **chunk;
        }
    if (bounded && !out.timed_out) {
        auto ended = started->wait_for(left());
        if (ended && !*ended) out.timed_out = true;
    }
    if (out.timed_out) {
        started->kill();
        static_cast<void>(started->wait());
        if (feeder.joinable()) feeder.join();
        out.status = 124;
        return out;
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

std::string commit(const fs::path& root) { return output(root, "git", {"-C", mcpplibs::os::fs::utf8(root), "rev-parse", "--short=8", "HEAD"}).value_or("unknown"); }

std::string today() { return std::format("{:%F}", std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())); }

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

std::expected<std::map<std::string, fs::path>, std::string> tests(const fs::path& root, std::vector<std::string> args) {
    args.insert(args.begin(), "test");
    args.insert(args.end(), {"--no-run", "--message-format", "json"});
    std::string said = "mcpp";
    for (const auto& arg : args) said += " " + arg;
    auto done = program(root, "mcpp", std::move(args), {.variables = {{"MCPP_JOBS", std::to_string(share_of_machine())}}});
    if (!done) return std::unexpected(done.error());
    if (done->status != 0) return std::unexpected(std::format("{} failed ({})", said, done->status));
    std::map<std::string, fs::path> out;
    for (auto part : std::views::split(std::string_view(done->out), '\n')) {
        const auto line = nlohmann::json::parse(std::string_view(part.begin(), part.end()), nullptr, false);
        if (line.is_discarded() || !line.contains("program") || !line["program"].is_string()) continue;
        out[line.value("test", "")] = fs::path(line["program"].get<std::string>());
    }
    return out;
}

std::optional<std::filesystem::path> packed(const std::filesystem::path& root, std::vector<std::string> args, std::string_view suffix) {
    args.insert(args.begin(), "pack");
    args.insert(args.end(), {"--message-format", "json"});
    const auto ran = program(root, "mcpp", std::move(args), {.merged = true});
    if (!ran || ran->status != 0) return std::nullopt;
    std::optional<std::filesystem::path> found;
    for (const auto part : std::views::split(std::string_view(ran->out), '\n')) {
        std::string_view text(part.begin(), part.end());
        while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) text.remove_suffix(1);
        const auto line = nlohmann::json::parse(text, nullptr, false);
        if (!line.is_discarded() && line.contains("data") && line["data"].contains("artifacts")) {
            for (const auto& artifact : line["data"]["artifacts"])
                if (artifact.value("path", "").ends_with(suffix)) found = std::filesystem::path(artifact.value("path", ""));
            continue;
        }
        const auto at = text.find("Packed ");
        if (at == std::string_view::npos || !text.ends_with(suffix)) continue;
        std::string path(text.substr(at + 7));
        if (path.starts_with("~/"))
            if (const auto home = mcpplibs::os::env::get("HOME")) path = *home + path.substr(1);
        found = std::filesystem::path(path);
    }
    if (found && !std::filesystem::exists(*found)) return std::nullopt;
    return found;
}

}  // namespace omui::devtools::run
