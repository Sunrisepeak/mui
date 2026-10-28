// The harness measures what it says (iterations, pauses, percentiles), budgets
// read and check as documented, the report is the JSON it promises, and the
// reference scene is the 1000-node dashboard of M1 ⑤.
import std;
import openmui.core;
import openmui.bench;
import openmui.testing;

using namespace omui::testing;
namespace bench = omui::bench;

int main(int argc, char** argv) {
    Suite s{"bench"};

    s.test("the loop: warm-up, enough iterations, and pauses left out", [] {
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
        check_eq(passes, std::size_t{7});
        check_eq(r.iterations, std::size_t{5});
        check(r.max_ms < 1.5, std::format("the paused sleep was measured: max {} ms", r.max_ms));
        check(r.min_ms <= r.p50_ms && r.p50_ms <= r.p95_ms && r.p95_ms <= r.p99_ms && r.p99_ms <= r.max_ms);
        check(r.items == 10 && r.metric("x") == std::optional<double>(4.0));
        check(!r.metric("nothing").has_value());
    });

    s.test("time decides once there are enough iterations", [] {
        bench::Case c{"slow", "", [](bench::State& st) {
                          while (st.next()) std::this_thread::sleep_for(std::chrono::milliseconds(3));
                      }};
        const bench::Result r = bench::run_one(c, {.min_time_ms = 20, .min_iterations = 2, .max_iterations = 1000, .warmup = 0});
        check(r.iterations >= 2 && r.iterations < 20, std::format("{} iterations", r.iterations));
    });

    s.test("budgets: the file, the check, and what is missing", [] {
        auto b = bench::parse_budgets(R"({"format":"openmui.bench.budgets@0.1","budgets":[
            {"name":"a","metric":"p95_ms","max":1,"why":"fast"},
            {"name":"b","max":5,"why":"the default metric is p95_ms"},
            {"name":"gone","max":1,"why":"not run"}]})");
        check(b.has_value() && b->size() == 3 && (*b)[1].metric == "p95_ms", b ? "" : b.error());
        check(!bench::parse_budgets(R"({"format":"openmui.bench.budgets@0.1","budgets":[{"name":"a","max":1}]})").has_value(),
              "a budget must say why");
        check(!bench::parse_budgets(R"({"budgets":[]})").has_value(), "the format is required");
        bench::Report report;
        bench::Result a;
        a.name = "a";
        a.p95_ms = 2;
        bench::Result bb;
        bb.name = "b";
        bb.p95_ms = 4;
        report.results = {a, bb};
        const auto over = report.check(*b);
        check(over.size() == 1 && over[0].budget.name == "a" && over[0].value == 2.0);
        check_eq(report.check(*b, true).size(), std::size_t{2});  // and `gone`, when every budget must be met
    });

    s.test("the report is openmui.bench@0.1 JSON, and a table", [] {
        bench::Report report;
        report.label = "test";
        report.platform = "linux";
        bench::Result r;
        r.name = "x/y";
        r.iterations = 3;
        r.mean_ms = 1.5;
        r.items = 2;
        r.ns_per_item = 750000;
        r.counters = {{"nodes", 1000}};
        report.results.push_back(r);
        auto v = omui::parse_json(report.json());
        check(v.has_value());
        if (!v) return;
        check(v->get("format")->as_string() == "openmui.bench@0.1");
        const omui::Value& first = (*v->get("results")->list())[0];
        check(first.get("name")->as_string() == "x/y" && first.get("counters")->get("nodes")->as_double() == 1000);
        check(report.table().contains("x/y"));
    });

    s.test("the reference scene is M1 ⑤'s: 1000+ nodes at 1920x1080", [] {
        const bench::SceneFacts f = bench::reference_scene_facts();
        check(f.nodes >= 1000 && f.width == 1920 && f.height == 1080, std::format("{} nodes", f.nodes));
    });

    s.test("every benchmark runs (one quick pass of each)", [] {
        bench::add_framework_benchmarks();
        const bench::Report report = bench::run({.min_time_ms = 0, .min_iterations = 1, .max_iterations = 1, .warmup = 0});
        check(report.results.size() == bench::cases().size() && report.results.size() >= 12);
        // A benchmark that cannot run here (no GPU) says why; every other runs once.
        for (const bench::Result& r : report.results)
            check(r.skipped.empty() ? r.iterations == 1 && r.max_ms > 0 : r.iterations == 0, std::format("{} {}", r.name, r.skipped));
        const bench::Result* frame = report.find("ui/frame/one-label");
        check(frame && frame->metric("nodes").value_or(0) >= 1000 && frame->metric("painted").value_or(0) > 0);
    });

    s.test("the budgets file in the tree is valid", [] {
        const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() /
                          "budgets.bench.omui.json";
        std::ifstream in(file);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        auto b = bench::parse_budgets(text);
        check(b.has_value() && !b->empty(), b ? "" : b.error());
        if (!b) return;
        bench::add_framework_benchmarks();
        for (const bench::Budget& x : *b)
            check(std::ranges::any_of(bench::cases(), [&](const bench::Case& c) { return c.name == x.name; }), "no benchmark " + x.name);
    });

    return s.run(argc, argv);
}
