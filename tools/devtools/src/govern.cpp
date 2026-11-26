module omui.devtools.govern;

import std;
import mcpplibs.os;

namespace omui::devtools::govern {
namespace {

namespace os = mcpplibs::os;

// Pauses or continues these processes. mcpplibs.os (0.4.4) starts and ends programs but neither pauses nor
// continues another's descendants, so the system's own `kill` does it: one run for a look's whole change.
void signal(std::string_view which, const std::vector<std::int64_t>& pids) {
    if (pids.empty()) return;
    os::process::spawn_options spawn;
    spawn.program = std::filesystem::is_regular_file("/bin/kill") ? std::filesystem::path("/bin/kill") : std::filesystem::path("kill");
    spawn.args.emplace_back(std::format("-{}", which));
    for (const std::int64_t pid : pids) spawn.args.push_back(std::to_string(pid));
    (void)os::process::run(std::move(spawn));  // one that ended meanwhile is no matter
}

}  // namespace

bool heavy(std::string_view name) noexcept {
    constexpr std::array<std::string_view, 19> programs{"cc1plus", "cc1",      "cc1obj",  "clang",  "clang++",  "as",       "collect2",
                                                        "lto1",    "lto-wrapper", "ld",   "ld.lld", "ld.bfd",   "ld.gold",  "lld",
                                                        "mold",    "wasm-ld",  "wasm-opt", "link.exe", "lld-link"};
    return std::ranges::contains(programs, name) || name.starts_with("clang-") || name.starts_with("clang++-");
}

std::vector<Process> read_table(std::string_view table) {
    std::vector<Process> found;
    const auto number = [](std::string_view text, std::int64_t& value) {
        const auto [end, failed] = std::from_chars(text.data(), text.data() + text.size(), value);
        return failed == std::errc{} && end == text.data() + text.size();
    };
    for (const auto part : std::views::split(table, '\n')) {
        std::string_view line(part.begin(), part.end());
        std::array<std::string_view, 3> fields;  // pid, ppid, state; the rest of the line is the name (it may hold spaces)
        bool whole = true;
        for (auto& field : fields) {
            line.remove_prefix(std::min(line.find_first_not_of(" \t"), line.size()));
            const auto end = std::min(line.find_first_of(" \t"), line.size());
            field = line.substr(0, end);
            line.remove_prefix(end);
            whole = whole && !field.empty();
        }
        line.remove_prefix(std::min(line.find_first_not_of(" \t"), line.size()));
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.remove_suffix(1);
        Process process;
        if (!whole || line.empty() || !number(fields[0], process.pid) || !number(fields[1], process.parent)) continue;
        if (const auto slash = line.rfind('/'); slash != std::string_view::npos && slash + 1 < line.size()) line.remove_prefix(slash + 1);
        process.name = std::string(line);
        process.ended = fields[2].starts_with('Z') || fields[2].starts_with('X');
        process.paused = fields[2].starts_with('T');
        found.push_back(std::move(process));
    }
    return found;
}

std::vector<Process> processes() {
    os::process::spawn_options spawn;
    spawn.program = std::filesystem::is_regular_file("/bin/ps") ? std::filesystem::path("/bin/ps") : std::filesystem::path("ps");
    spawn.args = {"-A", "-o", "pid=,ppid=,stat=,comm="};
    const auto ran = os::process::run(std::move(spawn));
    if (!ran || !ran->status.success()) return {};
    return read_table(ran->out);
}

std::vector<Process> heavy_under(std::span<const Process> all, std::int64_t root) {
    std::multimap<std::int64_t, const Process*> children;
    for (const Process& process : all) children.emplace(process.parent, &process);
    std::vector<Process> found;
    std::vector<std::int64_t> next{root};
    std::set<std::int64_t> seen{root};
    while (!next.empty()) {
        const std::int64_t parent = next.back();
        next.pop_back();
        for (auto [at, end] = children.equal_range(parent); at != end; ++at) {
            const Process& child = *at->second;
            if (!seen.insert(child.pid).second) continue;
            next.push_back(child.pid);
            if (!child.ended && heavy(child.name)) found.push_back(child);
        }
    }
    // A heavy program that started another only waits for it (collect2 for ld, lto-wrapper for lto1): the one at
    // work is counted, not the one waiting. Counted both, as many links at once as half the jobs would fill every
    // place with those waiting, and what they wait for would be held for ever.
    std::map<std::int64_t, std::int64_t> parent_of;
    for (const Process& process : all) parent_of.emplace(process.pid, process.parent);
    std::set<std::int64_t> heavy_ones, waiting;
    for (const Process& process : found) heavy_ones.insert(process.pid);
    for (const Process& process : found) {
        std::set<std::int64_t> walked;
        for (std::int64_t above = process.parent; above != root && walked.insert(above).second;) {
            if (heavy_ones.contains(above)) waiting.insert(above);
            const auto next = parent_of.find(above);
            if (next == parent_of.end()) break;
            above = next->second;
        }
    }
    std::erase_if(found, [&](const Process& process) { return waiting.contains(process.pid); });
    std::ranges::sort(found, {}, [](const Process& process) { return std::pair{process.seen, process.pid}; });
    return found;
}

std::int32_t next_allowed(std::int32_t allowed, std::int32_t jobs, std::uint64_t total, std::uint64_t available, double memory_floor) noexcept {
    if (total > 0 && static_cast<double>(available) < memory_floor * static_cast<double>(total)) return std::max(1, allowed - 1);
    return allowed < jobs ? allowed + 1 : jobs;
}

Change decide(std::span<const Process> oldest_first, std::set<std::int64_t>& paused, std::int32_t allowed) {
    Change change;
    std::set<std::int64_t> live;
    for (const Process& process : oldest_first) live.insert(process.pid);
    std::erase_if(paused, [&](std::int64_t pid) { return !live.contains(pid); });
    for (std::size_t index = 0; index < oldest_first.size(); ++index) {
        const std::int64_t pid = oldest_first[index].pid;
        const bool within = index < static_cast<std::size_t>(std::max(allowed, 0));
        if (within && paused.contains(pid)) change.resume.push_back(pid), paused.erase(pid);
        else if (!within && !paused.contains(pid)) change.pause.push_back(pid), paused.insert(pid);
    }
    return change;
}

std::expected<int, std::string> run(const Options& options) {
    if (options.command.empty()) return std::unexpected("no command to run");
    const bool listening = os::interrupt::available() && !os::interrupt::requested();  // a request is ours from here on
    os::process::spawn_options spawn;
    // At low CPU and I/O priority where the platform has the programs for it (the command keeps its own arguments).
    std::vector<std::string> line;
    const std::filesystem::path program = options.command.front();
    std::error_code error;
    if (!(program.has_parent_path() ? std::filesystem::is_regular_file(program, error) : os::env::find_executable(options.command.front()).has_value()))
        return std::unexpected(std::format("`{}` did not start: no such program", options.command.front()));
    if (options.low_priority && !os::is_windows) {
        if (os::env::find_executable("ionice")) line.insert(line.end(), {"ionice", "-c", "3"});
        if (os::env::find_executable("nice")) line.insert(line.end(), {"nice", "-n", "10"});
    }
    line.insert(line.end(), options.command.begin(), options.command.end());
    spawn.program = line.front();
    spawn.args.assign(line.begin() + 1, line.end());
    if (!options.cwd.empty()) spawn.cwd = options.cwd;
    spawn.env.emplace_back("MCPP_JOBS", std::to_string(std::max(options.jobs, 1)));  // mcpp plans that many; the holding below keeps the whole tree to it
    spawn.in = os::process::stdio::inherit;
    spawn.out = os::process::stdio::inherit;
    spawn.err = os::process::stdio::inherit;
    spawn.own_unit = true;  // everything it starts, ended with it
    auto process = os::process::process::spawn(spawn);
    if (!process) return std::unexpected(std::format("`{}` did not start: {}", options.command.front(), process.error().message()));
    const auto root = static_cast<std::int64_t>(process->id());
    const bool holding = !os::is_windows;
    std::set<std::int64_t> paused;
    std::map<std::int64_t, std::uint64_t> first_seen;  // the look a process was first seen at: the older, the earlier
    std::uint64_t look = 0;
    std::int32_t allowed = std::max(options.jobs, 1);
    for (;;) {
        auto ended = process->wait_for(options.interval);
        if (!ended) return std::unexpected(std::format("waiting for `{}`: {}", options.command.front(), ended.error().message()));
        if (*ended) {
            signal("CONT", {paused.begin(), paused.end()});
            return (*ended)->shell_code();
        }
        if (listening && os::interrupt::requested()) {
            signal("CONT", {paused.begin(), paused.end()});
            process->kill();  // the whole unit
            (void)process->wait();
            os::interrupt::end_as_requested();
            return 130;
        }
        if (!holding) continue;
        allowed = next_allowed(allowed, std::max(options.jobs, 1), os::machine::memory().value_or(0), os::machine::memory_available().value_or(0),
                               options.memory_floor);
        ++look;
        auto all = processes();
        std::map<std::int64_t, std::uint64_t> still;
        for (Process& each : all) each.seen = still[each.pid] = first_seen.contains(each.pid) ? first_seen[each.pid] : look;
        first_seen = std::move(still);
        const Change change = decide(heavy_under(all, root), paused, allowed);
        signal("CONT", change.resume);
        signal("STOP", change.pause);
    }
}

}  // namespace omui::devtools::govern
