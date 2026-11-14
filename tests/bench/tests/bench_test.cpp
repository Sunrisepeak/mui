// A scene's loop is measured as it says (iterations, pauses, percentiles — mtest's laps
// under it), budgets read as documented, every benchmark runs, and the reference scene is
// the 1000-node dashboard of M1 ⑤.
import std;
import omui.core;
import omui.bench;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace bench = omui::bench;

UTest cases{"bench", [] {

    "the loop: warm-up, enough iterations, and pauses left out"_test = [] {
        std::size_t passes = 0;
        bench::Case c{"loop", "", [&](bench::State& st) {
                          while (st.next()) {
                              ++passes;
                              st.pause();
                              std::this_thread::sleep_for(std::chrono::milliseconds(2));
                              st.resume();
                              st.items(10);
                              st.counter("x", 4);
                          }
                      }};
        const bench::Result r = bench::run_one(c, {.min_time_ms = 0, .min_iterations = 5, .max_iterations = 5, .warmup = 2});
        expect(eq(passes, std::size_t{7}));
        expect(eq(r.iterations, std::size_t{5}));
        expect(r.max_ms < 1.5) << std::format("the paused sleep was measured: max {} ms", r.max_ms);
        expect(r.min_ms <= r.p50_ms && r.p50_ms <= r.p95_ms && r.p95_ms <= r.p99_ms && r.p99_ms <= r.max_ms);
        expect(r.items == 10 && r.metric("x") == std::optional<double>(4.0));
        expect(!r.metric("nothing").has_value());
    };

    "time decides once there are enough iterations"_test = [] {
        bench::Case c{"slow", "", [](bench::State& st) {
                          while (st.next()) std::this_thread::sleep_for(std::chrono::milliseconds(3));
                      }};
        const bench::Result r = bench::run_one(c, {.min_time_ms = 20, .min_iterations = 2, .max_iterations = 1000, .warmup = 0});
        expect(r.iterations >= 2 && r.iterations < 20) << std::format("{} iterations", r.iterations);
    };

    "budgets: the file's form, and what a budget must say"_test = [] {
        auto b = bench::parse_budgets(R"({"format":"openmui.bench.budgets@0.1","budgets":[
            {"name":"a","max":1,"why":"x"},{"name":"b","metric":"p95_ms","max":2,"why":"y"},{"name":"gone","max":1,"why":"z","unless":"software"}]})");
        expect(b.has_value() && b->size() == 3 && (*b)[1].metric == "p95_ms" && (*b)[2].unless == "software") << (b ? "" : b.error());
        expect(!bench::parse_budgets(R"({"format":"openmui.bench.budgets@0.1","budgets":[{"name":"a","max":1}]})").has_value()) << "a budget must say why";
        expect(!bench::parse_budgets(R"({"budgets":[]})").has_value()) << "the format is required";
    };

    "a body that skips measures nothing; one that fails says why"_test = [] {
        bench::Case skipping{"skips", "", [](bench::State& st) {
                                 st.skip("no device here");
                                 while (st.next()) {}
                             }};
        const bench::Result skipped = bench::run_one(skipping, {});
        expect(skipped.skipped == "no device here" && skipped.iterations == 0 && !skipped.metric("p95_ms").has_value());
        bench::Case failing{"fails", "", [](bench::State& st) {
                                while (st.next()) {
                                    st.fail("the frame did not draw");
                                    return;
                                }
                            }};
        const bench::Result failed = bench::run_one(failing, {.min_time_ms = 0, .min_iterations = 3, .max_iterations = 3, .warmup = 0});
        expect(failed.failed == "the frame did not draw" && !failed.metric("p95_ms").has_value());
    };

    "the reference scene is M1 ⑤'s: 1000+ nodes at 1920x1080"_test = [] {
        const bench::SceneFacts f = bench::reference_scene_facts();
        expect(f.nodes >= 1000 && f.width == 1920 && f.height == 1080) << std::format("{} nodes", f.nodes);
    };

    "every benchmark runs (one quick pass of each)"_test = [] {
        bench::add_framework_benchmarks();
        expect(bench::cases().size() >= 12);
        // A benchmark that cannot run here (no GPU) says why; every other runs once.
        for (const bench::Case& one : bench::cases()) {
            const bench::Result r = bench::run_one(one, {.min_time_ms = 0, .min_iterations = 1, .max_iterations = 1, .warmup = 0});
            expect(r.failed.empty()) << std::format("{}: {}", r.name, r.failed);
            expect(r.skipped.empty() ? r.iterations == 1 && r.max_ms > 0 : r.iterations == 0) << std::format("{} {}", r.name, r.skipped);
            if (r.name == "ui/frame/one-label") expect(r.metric("nodes").value_or(0) >= 1000 && r.metric("painted").value_or(0) > 0);
        }
    };

    "the budgets file in the tree is valid"_test = [] {
        const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() /
                          "budgets.bench.omui.json";
        std::ifstream in(file);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        auto b = bench::parse_budgets(text);
        expect(b.has_value() && !b->empty()) << (b ? "" : b.error());
        if (!b) return;
        bench::add_framework_benchmarks();
        for (const bench::Budget& x : *b)
            expect(std::ranges::any_of(bench::cases(), [&](const bench::Case& c) { return c.name == x.name; })) << "no benchmark " + x.name;
    };
}};
