// Spans nest per thread, counters and marks land, threads are told apart, a
// full buffer drops and says so, nothing is recorded while off, the Chrome
// JSON is what Perfetto reads, and the summary's percentiles are nearest-rank.
import std;
import openmui.trace;
import openmui.testing;

using namespace omui::testing;
namespace trace = omui::trace;

int main(int argc, char** argv) {
    Suite s{"trace"};

    s.test("off: nothing is recorded", [] {
        (void)trace::stop();
        { trace::Span sp("ignored"); }
        trace::count("ignored", 1);
        trace::start();
        auto t = trace::stop();
        check(t.events.empty());
    });

    s.test("spans nest; counters and marks land; the summary pairs them", [] {
        trace::start();
        trace::name_thread("ui");
        for (int i = 0; i < 10; ++i) {
            trace::Span frame("frame", "frame");
            {
                trace::Span layout("layout", "layout");
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            trace::count("nodes", 100 + i, "frame");
        }
        trace::mark("done");
        auto t = trace::stop();
        check(t.events.size() == 10 * 5 + 1, std::format("{}", t.events.size()));
        auto stats = t.summary();
        check(stats.size() == 2 && stats[0].name == "frame" && stats[0].count == 10);
        auto layout = std::ranges::find(stats, std::string("layout"), &trace::Stat::name);
        check(layout != stats.end() && layout->count == 10 && layout->p50_ms >= 0.2 && layout->max_ms >= layout->p95_ms);
        check(stats[0].total_ms >= layout->total_ms, "a frame contains its layout");
        check(t.threads.size() == 1 && t.threads[0].second == "ui");
        const std::string json = t.chrome_json();
        check(json.starts_with("{\"displayTimeUnit\":\"ms\",\"traceEvents\":["));
        check(json.find("\"ph\":\"B\"") != std::string::npos && json.find("\"ph\":\"C\"") != std::string::npos &&
              json.find("\"ph\":\"i\"") != std::string::npos && json.find("\"thread_name\"") != std::string::npos);
        check(t.table().find("frame/frame") != std::string::npos);
    });

    s.test("threads record into their own buffers", [] {
        trace::start();
        std::vector<std::jthread> workers;
        for (int w = 0; w < 4; ++w)
            workers.emplace_back([w] {
                trace::name_thread(std::format("worker {}", w));
                for (int i = 0; i < 1000; ++i) trace::Span sp("task", "work");
            });
        workers.clear();
        auto t = trace::stop();
        check(t.events.size() == 4 * 2000 && t.threads.size() == 4 && t.dropped == 0);
        auto stats = t.summary();
        check(stats.size() == 1 && stats[0].count == 4000);
        check(std::ranges::is_sorted(t.events, {}, &trace::Event::ns));
    });

    s.test("a full buffer keeps the first events and counts the rest", [] {
        trace::start({.events_per_thread = 100});
        for (int i = 0; i < 80; ++i) trace::Span sp("x");
        auto t = trace::stop();
        check(t.events.size() == 100 && t.dropped == 60);
        check(t.table().find("dropped") != std::string::npos);
    });

    s.test("percentiles are nearest-rank", [] {
        trace::Trace t;
        for (int i = 1; i <= 100; ++i) {
            const std::int64_t at = i * 10'000'000;
            t.events.push_back({trace::Kind::begin, "d", "c", at, 0, 1});
            t.events.push_back({trace::Kind::end, "d", "c", at + i * 1'000'000, 0, 1});
        }
        auto st = t.summary();
        check(st.size() == 1 && st[0].p50_ms == 50 && st[0].p95_ms == 95 && st[0].p99_ms == 99 && st[0].max_ms == 100);
        check_near(st[0].mean_ms, 50.5, 1e-9);
    });

    s.test("interned names outlive the strings they came from", [] {
        std::string name = "stage-made-at-run-time";
        const char* a = omui::trace::intern(name);
        name.assign(name.size(), 'x');  // the original text is gone
        check(std::string_view(a) == "stage-made-at-run-time");
        check(omui::trace::intern("stage-made-at-run-time") == a, "the same text, the same pointer");
        omui::trace::start();
        {
            std::string temporary = "temporary";
            omui::trace::Span span(omui::trace::intern(temporary), omui::trace::intern("cat"));
        }
        const auto t = omui::trace::stop();
        check(t.chrome_json().contains("\"name\":\"temporary\""));
    });

    return s.run(argc, argv);
}
