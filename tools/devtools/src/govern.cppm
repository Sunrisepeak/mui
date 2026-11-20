// omui.devtools.govern — a command held within a share of the machine (AGENTS.md
// §5): at most `jobs` compilers, assemblers and linkers run at once among all of
// the command's descendants, whatever the command itself plans (mcpp builds
// workspace groups and host tools side by side, each with its own job count);
// while available memory is under `memory_floor` of the total, fewer. The
// newest beyond the limit are paused and continued as the oldest finish. Test
// programs and the build tools themselves are never paused: a test may measure
// time. Every local build and test runs through it (`omui-devtools govern -- mcpp test -p …`): by default with the
// machine's share as its job count, MCPP_JOBS set to it, at low CPU and I/O priority.
//
// What runs is read from the system's process table (`ps`) and held with `kill`
// (Linux, macOS); on Windows the command runs with the job count it was given
// alone. mcpplibs.os (0.4.4) neither lists nor pauses programs it did not start.
export module omui.devtools.govern;

import std;

export namespace omui::devtools::govern {

// A process as the process table tells it: when it was first seen (an order:
// the governor's own look count), which, its parent, its program's name,
// whether it has ended (a zombie), and whether it is paused (stopped).
struct Process {
    std::uint64_t seen = 0;
    std::int64_t  pid = 0, parent = 0;
    std::string   name;
    bool          ended = false;
    bool          paused = false;  // stopped (a governor's pause, or a debugger's)
};

// Whether a program by this name compiles, assembles or links.
[[nodiscard]] bool heavy(std::string_view name) noexcept;

// The process table as `ps -A -o pid=,ppid=,stat=,comm=` prints it; a program's
// name is its file's (macOS prints the path), a line that is not one is passed over.
[[nodiscard]] std::vector<Process> read_table(std::string_view table);

// The process table now (none where there is no `ps`).
[[nodiscard]] std::vector<Process> processes();

// Of `all`, the heavy ones still running that descend from `root`, oldest first.
[[nodiscard]] std::vector<Process> heavy_under(std::span<const Process> all, std::int64_t root);

// How many may run next: one fewer each look while available memory is under
// the floor (never under one), one more back each look while it is not.
[[nodiscard]] std::int32_t next_allowed(std::int32_t allowed, std::int32_t jobs, std::uint64_t total, std::uint64_t available,
                                        double memory_floor) noexcept;

// Which to pause and which to continue so that the oldest `allowed` run and the
// rest wait; `paused` becomes what is paused afterwards (those that ended leave it).
struct Change {
    std::vector<std::int64_t> pause, resume;
};
[[nodiscard]] Change decide(std::span<const Process> oldest_first, std::set<std::int64_t>& paused, std::int32_t allowed);

struct Options {
    std::int32_t              jobs = 1;
    double                    memory_floor = 0.10;
    std::chrono::milliseconds interval{250};
    std::vector<std::string>  command;  // the program (on PATH or a path) and its arguments
    std::filesystem::path     cwd;      // where it runs (default: here)
    bool                      low_priority = true;  // under `ionice -c 3` and `nice -n 10` where they exist
};

// Runs the command as a unit of its own and holds it until it ends; its status
// as a shell reports it (128 + the signal when one ended it), or why it did not
// start. A request to end this program (an interrupt, a terminate) continues
// what is paused and ends the whole command, and this program then ends as asked.
[[nodiscard]] std::expected<int, std::string> run(const Options& options);

}  // namespace omui::devtools::govern
