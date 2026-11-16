module omui.devtools.incremental;

import std;
import mcpplibs.os;
import nlohmann.json;

namespace omui::devtools::incremental {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

struct Built {
    std::set<fs::path> directories;
    double             seconds = 0;
};

// One build, its events and log kept beside the report (`<stem>.events.xdef.jsonl`, `<stem>.log`).
std::expected<Built, std::string> build(const Options& options, const fs::path& stem) {
    mcpplibs::os::process::spawn_options spawn;
    spawn.program = options.mcpp;
    spawn.args = arguments(options);
    const auto started = std::chrono::steady_clock::now();
    auto ran = mcpplibs::os::process::run(std::move(spawn));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!ran) return std::unexpected(std::format("mcpp did not start: {}", ran.error().message()));
    const fs::path events = stem.parent_path() / mcpplibs::os::fs::from_utf8(mcpplibs::os::fs::utf8(stem.filename()) + ".events.xdef.jsonl");
    const fs::path log = stem.parent_path() / mcpplibs::os::fs::from_utf8(mcpplibs::os::fs::utf8(stem.filename()) + ".log");
    std::ofstream(events, std::ios::binary) << ran->out;
    std::ofstream(log, std::ios::binary) << ran->err;
    if (ran->status.shell_code() != 0)
        return std::unexpected(std::format("mcpp failed ({}); see {}", ran->status.shell_code(), mcpplibs::os::fs::generic_utf8(log)));
    return Built{configurations(ran->out), seconds};
}

}  // namespace

std::vector<std::string> arguments(const Options& options) {
    std::vector<std::string> args{"build", "--profile", options.profile, "--message-format", "json"};
    if (options.workspace) args.emplace_back("--workspace");
    for (const auto& package : options.packages) args.insert(args.end(), {"-p", package});
    for (const auto& package : options.excluded) args.insert(args.end(), {"--exclude", package});
    if (!options.toolchain.empty()) args.insert(args.end(), {"--toolchain", options.toolchain});
    if (options.locked) args.emplace_back("--locked");
    return args;
}

std::set<fs::path> configurations(std::string_view events) {
    std::set<fs::path> directories;
    for (const auto line : std::views::split(events, '\n')) {
        const json event = json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
        if (!event.is_object() || event.value("event", "") != "plan") continue;
        if (const auto at = event.find("configuration"); at != event.end() && at->is_object() && at->contains("outputDir"))
            directories.insert(mcpplibs::os::fs::from_utf8((*at)["outputDir"].get<std::string>()));
    }
    return directories;
}

std::expected<Outputs, std::string> outputs(const std::set<fs::path>& directories) {
    Outputs out;
    for (const fs::path& directory : directories) {
        const fs::path database = directory / "compile_commands.json";
        const auto text = mcpplibs::os::fs::read(database);
        if (!text) return std::unexpected(std::format("{}: {}", mcpplibs::os::fs::generic_utf8(database), text.error().message()));
        const json entries = json::parse(*text, nullptr, false);
        if (!entries.is_array()) return std::unexpected(std::format("{} is not a compilation database", mcpplibs::os::fs::generic_utf8(database)));
        for (const json& entry : entries) {
            if (!entry.is_object() || !entry.contains("output"))
                return std::unexpected(std::format("a compilation has no output in {}", mcpplibs::os::fs::generic_utf8(database)));
            fs::path path = mcpplibs::os::fs::from_utf8(entry["output"].get<std::string>());
            if (path.is_relative()) path = mcpplibs::os::fs::from_utf8(entry.value("directory", "")) / path;
            std::error_code failed;
            const fs::path real = fs::weakly_canonical(path, failed);
            const std::string name = mcpplibs::os::fs::generic_utf8(failed ? path : real);
            const auto size = fs::file_size(path, failed);
            const auto modified = failed ? fs::file_time_type{} : fs::last_write_time(path, failed);
            if (failed) {
                out.missing.insert(name);
                continue;
            }
            out.found[name] = Stamp{std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count(), size};
        }
    }
    if (out.found.empty()) return std::unexpected("the compilation databases name no existing output");
    return out;
}

std::vector<std::string> changed(const Outputs& before, const Outputs& after) {
    std::vector<std::string> paths;
    for (const auto& [path, stamp] : before.found) {
        const auto found = after.found.find(path);
        if (found == after.found.end() || found->second != stamp) paths.push_back(path);
    }
    for (const auto& [path, stamp] : after.found)
        if (!before.found.contains(path)) paths.push_back(path);
    std::ranges::sort(paths);
    return paths;
}

bool check(const Options& options, std::ostream& said, std::ostream& errors) {
    std::vector<std::string> command{mcpplibs::os::fs::generic_utf8(options.mcpp)};
    for (auto& arg : arguments(options)) command.push_back(std::move(arg));
    json report{{"ok", false}, {"command", command}};
    std::error_code ignored;
    fs::create_directories(options.report.parent_path(), ignored);
    std::string prefix = mcpplibs::os::fs::utf8(options.report.filename());
    if (prefix.ends_with(".report.xdef.json")) prefix.resize(prefix.size() - std::string_view(".report.xdef.json").size());
    const fs::path directory = options.report.parent_path();
    const auto attempt = [&]() -> std::expected<void, std::string> {
        auto warm = build(options, directory / mcpplibs::os::fs::from_utf8(prefix + "-warm"));
        if (!warm) return std::unexpected(warm.error());
        report["warm_seconds"] = warm->seconds;
        if (warm->directories.empty()) return std::unexpected("mcpp did not name its build configuration");
        auto before = outputs(warm->directories);
        if (!before) return std::unexpected(before.error());
        auto repeat = build(options, directory / mcpplibs::os::fs::from_utf8(prefix + "-repeat"));
        if (!repeat) return std::unexpected(repeat.error());
        report["repeat_seconds"] = repeat->seconds;
        if (!repeat->directories.empty() && repeat->directories != warm->directories)
            return std::unexpected("an unchanged command chose another build configuration");
        auto after = outputs(warm->directories);
        if (!after) return std::unexpected(after.error());
        const auto paths = changed(*before, *after);
        report["compilation_outputs"] = before->found.size();
        report["missing_outputs"] = before->missing.size();
        report["changed_outputs"] = paths;
        report["coverage_unchanged"] = before->missing == after->missing;
        report["ok"] = paths.empty() && before->missing == after->missing;
        said << std::format("incremental: {} / {} compilation outputs changed; repeat {:.2f}s (warm {:.2f}s)\n", paths.size(),
                            before->found.size(), repeat->seconds, warm->seconds);
        return {};
    };
    if (auto done = attempt(); !done) {
        report["error"] = done.error();
        errors << "incremental: " << done.error() << '\n';
    }
    std::ofstream(options.report, std::ios::binary) << report.dump(2) << '\n';
    const bool ok = report["ok"].get<bool>();
    if (!ok) errors << std::format("::error::unchanged compilation check failed ({})\n", mcpplibs::os::fs::generic_utf8(options.report));
    return ok;
}

}  // namespace omui::devtools::incremental
