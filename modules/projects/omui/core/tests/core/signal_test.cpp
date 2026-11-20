import std;
import omui.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

static UTest cases{"core.signal", [] {

    "computed values follow and are lazy"_test = [] {
        auto a = omui::signal(1);
        int runs = 0;
        auto doubled = omui::computed([=, &runs] { ++runs; return a() * 2; });
        expect(eq(runs, 0));
        expect(eq(doubled(), 2));
        expect(eq(doubled(), 2));
        expect(eq(runs, 1), std::source_location::current());
        a.set(5);
        expect(eq(runs, 1));  // lazy: not until read
        expect(eq(doubled(), 10));
        expect(eq(runs, 2));
    };

    "a diamond updates once and never shows a mixed state"_test = [] {
        auto a = omui::signal(1);
        auto b = omui::computed([=] { return a() + 1; });
        auto c = omui::computed([=] { return a() * 10; });
        std::vector<std::pair<int, int>> seen;
        omui::Watch w([=, &seen] { seen.emplace_back(b(), c()); });
        a.set(2);
        a.set(3);
        expect(seen.size() == 3);
        expect(seen[1] == std::pair{3, 20});
        expect(seen[2] == std::pair{4, 30});
    };

    "an equal value stops propagation"_test = [] {
        auto a = omui::signal(3);
        auto parity = omui::computed([=] { return a() % 2; });
        int runs = 0;
        omui::Watch w([=, &runs] { (void)parity(); ++runs; });
        a.set(5);  // parity unchanged
        expect(eq(runs, 1));
        a.set(6);
        expect(eq(runs, 2));
        a.set(6);  // same value: nothing at all
        expect(eq(runs, 2));
    };

    "batches notify once"_test = [] {
        auto x = omui::signal(0);
        auto y = omui::signal(0);
        int runs = 0;
        omui::Watch w([=, &runs] { (void)(x() + y()); ++runs; });
        omui::batch([&] {
            x.set(1);
            y.set(2);
            x.set(3);
        });
        expect(eq(runs, 2));
    };

    "after_batch: once the batch's effects ran, in order; now outside a batch"_test = [] {
        auto x = omui::signal(0);
        std::vector<std::string> log;
        omui::Watch w([=, &log] { log.push_back(std::format("watch {}", x())); });
        omui::batch([&] {
            x.set(1);
            omui::after_batch([&] { log.push_back(std::format("told {}", x.peek())); });
            omui::batch([&] { omui::after_batch([&] { log.push_back("second"); }); });
            x.set(2);
        });
        expect(log == std::vector<std::string>{"watch 0", "watch 2", "told 2", "second"}) << std::format("{}", log);
        // What it writes notifies before what waits after it.
        log.clear();
        omui::batch([&] {
            omui::after_batch([&] {
                x.set(3);
                omui::after_batch([&] { log.push_back("then"); });
            });
        });
        expect(log == std::vector<std::string>{"watch 3", "then"}) << std::format("{}", log);
        bool now = false;
        omui::after_batch([&] { now = true; });
        expect(now) << "outside a batch, at once";
    };

    "dynamic dependencies"_test = [] {
        auto use_a = omui::signal(true);
        auto a = omui::signal(std::string("a"));
        auto b = omui::signal(std::string("b"));
        int runs = 0;
        omui::Watch w([=, &runs] { (void)(use_a() ? a() : b()); ++runs; });
        b.set("B");  // not read: no run
        expect(eq(runs, 1));
        use_a.set(false);
        expect(eq(runs, 2));
        a.set("A");  // no longer read
        expect(eq(runs, 2));
        b.set("BB");
        expect(eq(runs, 3));
    };

    "untracked reads and disposal"_test = [] {
        auto a = omui::signal(1);
        auto b = omui::signal(1);
        int runs = 0;
        omui::Watch w([=, &runs] { (void)a(); (void)omui::untracked([&] { return b(); }); ++runs; });
        b.set(2);
        expect(eq(runs, 1));
        a.set(2);
        expect(eq(runs, 2));
        w.dispose();
        a.set(3);
        expect(eq(runs, 2));
        expect(!w.active());
    };

    "a watch that writes converges"_test = [] {
        auto a = omui::signal(0);
        auto clamped = omui::signal(0);
        omui::Watch w([=] { clamped.set(std::min(a(), 10)); });
        a.set(42);
        expect(eq(clamped.peek(), 10));
    };

    "signals are shared handles"_test = [] {
        auto a = omui::signal(1);
        auto copy = a;
        copy.set(7);
        expect(eq(a.peek(), 7));
        expect(copy.same_as(a));
        auto name = omui::signal("ok");
        expect(eq(name.peek(), std::string("ok")));
        a.update([](int v) { return v + 1; });
        expect(eq(a.peek(), 8));
    };

    "destroying a signal leaves dependants safe"_test = [] {
        std::optional<omui::Signal<int>> a(omui::signal(1));
        auto value = omui::signal(0);
        omui::Watch w([sig = *a, value] { value.set(sig()); });
        a.reset();  // the watch still holds a copy
        expect(eq(value.peek(), 1));
    };
}};
