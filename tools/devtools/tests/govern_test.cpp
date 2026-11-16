// omui-devtools govern: which programs are heavy, the process table read, who is
// paused and continued, how many may run as memory runs short — and a real
// command held: two "linkers" asked to run at once run one after the other.
import std;
import mcpplibs.mtest;
import mcpplibs.os;
import omui.devtools.govern;

using namespace mcpplibs::mtest;
namespace held = omui::devtools::govern;

UTest cases{"devtools.govern", [] {
    "compilers, assemblers and linkers are heavy; the build tools and tests are not"_test = [] {
        for (const std::string_view name : {"cc1plus", "clang++", "clang-23", "as", "ld.lld", "wasm-ld", "lld-link"}) expect(held::heavy(name)) << name;
        for (const std::string_view name : {"mcpp", "ninja", "make", "omui-view_test", "sh", "clangd"}) expect(!held::heavy(name)) << name;
    };

    "the process table: pid, parent, state, the program's file name (spaces kept); other lines passed over"_test = [] {
        const auto table = held::read_table("    1     0 Ss   systemd\n"
                                            " 4242  4200 R+   cc1plus\n"
                                            " 4244  4200 Z    my odd ld\n"
                                            " 4245  4200 S    /Library/Developer/usr/bin/clang++\n"
                                            "garbage line\n"
                                            "  12\n");
        expect(fatal(eq(table.size(), std::size_t{4})));
        expect(eq(table[1].pid, std::int64_t{4242}) && eq(table[1].parent, std::int64_t{4200}) && eq(table[1].name, std::string("cc1plus")) && !table[1].ended);
        expect(eq(table[2].name, std::string("my odd ld")) && table[2].ended) << "a name with spaces read whole; a zombie has ended";
        expect(eq(table[3].name, std::string("clang++"))) << "macOS prints the path: the file's name";
    };

    "the heavy ones under the command, at any depth, oldest first; not another's, not one that ended"_test = [] {
        const std::vector<held::Process> all{
            {1, 100, 1, "mcpp"},  {5, 101, 100, "ninja"}, {9, 102, 101, "cc1plus"}, {7, 103, 101, "ld"},
            {8, 104, 100, "test_x"}, {3, 105, 1, "cc1plus"}, {6, 106, 101, "as", true}, {4, 107, 104, "clang++"},
        };
        const auto found = held::heavy_under(all, 100);
        std::vector<std::int64_t> pids;
        for (const auto& process : found) pids.push_back(process.pid);
        expect(pids == std::vector<std::int64_t>{107, 103, 102}) << "105 is another's, 106 ended, 101 and 104 are not heavy";
    };

    "the oldest `allowed` run, the rest wait; one that ends leaves the paused; fewer allowed pauses the newest"_test = [] {
        std::vector<held::Process> processes{{10, 101, 1, "cc1plus"}, {11, 102, 1, "cc1plus"}, {12, 103, 1, "ld"}, {13, 104, 1, "as"}};
        std::set<std::int64_t> paused;
        auto change = held::decide(processes, paused, 2);
        expect(change.pause == std::vector<std::int64_t>{103, 104} && change.resume.empty());
        expect(eq(paused.size(), std::size_t{2}));
        change = held::decide(processes, paused, 2);
        expect(change.pause.empty() && change.resume.empty()) << "nothing to do twice";
        processes.erase(processes.begin());  // the oldest finished
        change = held::decide(processes, paused, 2);
        expect(change.resume == std::vector<std::int64_t>{103} && change.pause.empty());
        change = held::decide(processes, paused, 1);
        expect(change.pause == std::vector<std::int64_t>{103}) << "memory short: the newest running waits again";
        processes.pop_back();  // 104 ended while paused
        (void)held::decide(processes, paused, 1);
        expect(paused == std::set<std::int64_t>{103});
    };

    "short of memory: one fewer each look down to one; with enough again, one more each look up to the jobs"_test = [] {
        expect(eq(held::next_allowed(8, 8, 100, 5, 0.10), 7));
        expect(eq(held::next_allowed(1, 8, 100, 5, 0.10), 1));
        expect(eq(held::next_allowed(3, 8, 100, 50, 0.10), 4));
        expect(eq(held::next_allowed(8, 8, 100, 50, 0.10), 8));
        expect(eq(held::next_allowed(8, 8, 0, 0, 0.10), 8)) << "no memory figure: the jobs alone";
    };

    "a command's status comes back, a missing program says so"_test = [] {
        if constexpr (mcpplibs::os::is_windows) return;
        const auto status = held::run({.jobs = 2, .command = {"sh", "-c", "exit 3"}});
        expect(fatal(status.has_value())) << (status ? "" : status.error());
        expect(eq(*status, 3));
        expect(!held::run({.jobs = 2, .command = {"omui-no-such-program-here"}}).has_value());
    };

    "two linkers started at once with one job: the second waits for the first; with two, they run together"_test = [] {
        if constexpr (mcpplibs::os::is_windows) return;  // held through ps and kill
        // A "linker" that works the processor: the system's shell under a heavy name (its comm is the file's).
        // Not sleep: a stopped process's nanosleep keeps its deadline, so a paused sleep still ends on time.
        expect(fatal(std::filesystem::is_regular_file("/bin/sh"))) << "the system's shell";
        const auto directory = std::filesystem::temp_directory_path() / std::format("omui-devtools-govern-{}", std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(directory);
        const auto linker = directory / "clang-gtest";  // heavy by its name, and no other program's (ps shows 15 characters)
        std::filesystem::copy_file(std::filesystem::canonical("/bin/sh"), linker);
        std::filesystem::permissions(linker, std::filesystem::perms::owner_all);
        const std::string work = std::format("'{}' -c 'i=0; while [ $i -lt 600000 ]; do i=$((i+1)); done'", linker.string());
        // What the governor promises, seen as it holds: the linkers among the command's descendants that run (not
        // paused, not ended), sampled while it runs. Wall-clock ratios are not the promise: they move with the load.
        const auto most_running = [&](std::int32_t jobs) {
            std::atomic<bool> done{false};
            std::vector<int> counts;
            std::jthread sampler([&] {
                while (!done) {
                    int running = 0;
                    for (const auto& process : held::processes())
                        if (process.name == "clang-gtest" && !process.paused && !process.ended) ++running;
                    counts.push_back(running);
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            });
            const auto status = held::run({.jobs = jobs, .interval = std::chrono::milliseconds(20),
                                           .command = {"/bin/sh", "-c", std::format("{0} & {0} & wait", work)}, .low_priority = false});
            done = true;
            sampler.join();
            expect(fatal(status.has_value()));
            expect(eq(*status, 0));
            // The first look comes at most an interval after both start: allow the samples of that moment.
            const auto both = std::ranges::count(counts, 2);
            std::println("    jobs {}: {} samples, {} with both linkers running", jobs, counts.size(), both);
            return std::pair{both, static_cast<std::ptrdiff_t>(counts.size())};
        };
        const auto [held_both, held_samples] = most_running(1);
        const auto [free_both, free_samples] = most_running(2);
        std::filesystem::remove_all(directory);
        expect(held_samples > 20 && held_both * 10 < held_samples) << "one job: the second waits while the first runs";
        expect(free_both * 2 > free_samples) << "two jobs: both at once";
    };
}};
