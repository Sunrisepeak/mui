import std;
import openmui.core;
import openmui.testing;

using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"core.signal"};

    s.test("computed values follow and are lazy", [] {
        auto a = omui::signal(1);
        int runs = 0;
        auto doubled = omui::computed([=, &runs] { ++runs; return a() * 2; });
        check_eq(runs, 0);
        check_eq(doubled(), 2);
        check_eq(doubled(), 2);
        check_eq(runs, 1, std::source_location::current());
        a.set(5);
        check_eq(runs, 1);  // lazy: not until read
        check_eq(doubled(), 10);
        check_eq(runs, 2);
    });

    s.test("a diamond updates once and never shows a mixed state", [] {
        auto a = omui::signal(1);
        auto b = omui::computed([=] { return a() + 1; });
        auto c = omui::computed([=] { return a() * 10; });
        std::vector<std::pair<int, int>> seen;
        omui::Watch w([=, &seen] { seen.emplace_back(b(), c()); });
        a.set(2);
        a.set(3);
        check(seen.size() == 3);
        check(seen[1] == std::pair{3, 20});
        check(seen[2] == std::pair{4, 30});
    });

    s.test("an equal value stops propagation", [] {
        auto a = omui::signal(3);
        auto parity = omui::computed([=] { return a() % 2; });
        int runs = 0;
        omui::Watch w([=, &runs] { (void)parity(); ++runs; });
        a.set(5);  // parity unchanged
        check_eq(runs, 1);
        a.set(6);
        check_eq(runs, 2);
        a.set(6);  // same value: nothing at all
        check_eq(runs, 2);
    });

    s.test("batches notify once", [] {
        auto x = omui::signal(0);
        auto y = omui::signal(0);
        int runs = 0;
        omui::Watch w([=, &runs] { (void)(x() + y()); ++runs; });
        omui::batch([&] {
            x.set(1);
            y.set(2);
            x.set(3);
        });
        check_eq(runs, 2);
    });

    s.test("after_batch: once the batch's effects ran, in order; now outside a batch", [] {
        auto x = omui::signal(0);
        std::vector<std::string> log;
        omui::Watch w([=, &log] { log.push_back(std::format("watch {}", x())); });
        omui::batch([&] {
            x.set(1);
            omui::after_batch([&] { log.push_back(std::format("told {}", x.peek())); });
            omui::batch([&] { omui::after_batch([&] { log.push_back("second"); }); });
            x.set(2);
        });
        check(log == std::vector<std::string>{"watch 0", "watch 2", "told 2", "second"}, std::format("{}", log));
        // What it writes notifies before what waits after it.
        log.clear();
        omui::batch([&] {
            omui::after_batch([&] {
                x.set(3);
                omui::after_batch([&] { log.push_back("then"); });
            });
        });
        check(log == std::vector<std::string>{"watch 3", "then"}, std::format("{}", log));
        bool now = false;
        omui::after_batch([&] { now = true; });
        check(now, "outside a batch, at once");
    });

    s.test("dynamic dependencies", [] {
        auto use_a = omui::signal(true);
        auto a = omui::signal(std::string("a"));
        auto b = omui::signal(std::string("b"));
        int runs = 0;
        omui::Watch w([=, &runs] { (void)(use_a() ? a() : b()); ++runs; });
        b.set("B");  // not read: no run
        check_eq(runs, 1);
        use_a.set(false);
        check_eq(runs, 2);
        a.set("A");  // no longer read
        check_eq(runs, 2);
        b.set("BB");
        check_eq(runs, 3);
    });

    s.test("untracked reads and disposal", [] {
        auto a = omui::signal(1);
        auto b = omui::signal(1);
        int runs = 0;
        omui::Watch w([=, &runs] { (void)a(); (void)omui::untracked([&] { return b(); }); ++runs; });
        b.set(2);
        check_eq(runs, 1);
        a.set(2);
        check_eq(runs, 2);
        w.dispose();
        a.set(3);
        check_eq(runs, 2);
        check(!w.active());
    });

    s.test("a watch that writes converges", [] {
        auto a = omui::signal(0);
        auto clamped = omui::signal(0);
        omui::Watch w([=] { clamped.set(std::min(a(), 10)); });
        a.set(42);
        check_eq(clamped.peek(), 10);
    });

    s.test("signals are shared handles", [] {
        auto a = omui::signal(1);
        auto copy = a;
        copy.set(7);
        check_eq(a.peek(), 7);
        check(copy.same_as(a));
        auto name = omui::signal("ok");
        check_eq(name.peek(), std::string("ok"));
        a.update([](int v) { return v + 1; });
        check_eq(a.peek(), 8);
    });

    s.test("destroying a signal leaves dependants safe", [] {
        std::optional<omui::Signal<int>> a(omui::signal(1));
        auto value = omui::signal(0);
        omui::Watch w([sig = *a, value] { value.set(sig()); });
        a.reset();  // the watch still holds a copy
        check_eq(value.peek(), 1);
    });

    return s.run(argc, argv);
}
