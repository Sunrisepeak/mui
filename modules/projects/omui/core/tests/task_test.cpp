import std;
import openmui.core;
import mlibs.testing;

using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"core.task"};

    s.test("parallel_for covers every index once, in fixed chunks", [] {
        omui::Tasks tasks(4);
        std::vector<int> hits(10007, 0);
        std::mutex m;
        std::set<std::pair<std::size_t, std::size_t>> chunks;
        tasks.parallel_for(hits.size(), 1000, [&](std::size_t b, std::size_t e) {
            for (std::size_t i = b; i < e; ++i) ++hits[i];
            std::lock_guard lock(m);
            chunks.emplace(b, e);
        });
        check(std::ranges::all_of(hits, [](int h) { return h == 1; }));
        check(chunks.size() == 11);
        check(chunks.rbegin()->second == 10007);
    });

    s.test("nested parallel_for from a worker", [] {
        omui::Tasks tasks(4);
        std::atomic<int> sum = 0;
        tasks.parallel_for(8, 1, [&](std::size_t, std::size_t) {
            tasks.parallel_for(100, 10, [&](std::size_t b, std::size_t e) { sum += static_cast<int>(e - b); });
        });
        check_eq(sum.load(), 800);
    });

    s.test("spawn, cancel, wait", [] {
        omui::Tasks tasks(2);
        std::atomic<int> done = 0;
        auto f = tasks.spawn([&](std::stop_token) { done += 1; });
        f.wait();
        check_eq(done.load(), 1);
        std::atomic<bool> stopped = false;
        if (tasks.threads() == 1) {
            // The Web host executes spawn synchronously. Cancellation has to
            // be requested during that call; a later statement cannot run yet.
            auto g = tasks.spawn([&](std::stop_token token) {
                tasks.cancel_all();
                stopped = token.stop_requested();
            });
            g.wait();
            check(stopped.load());
            auto next = tasks.spawn([](std::stop_token token) { check(!token.stop_requested()); });
            next.wait();
            tasks.wait_idle();
            return;
        }
        auto g = tasks.spawn([&](std::stop_token t) {
            while (!t.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            stopped = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        tasks.cancel_all();
        g.wait();
        check(stopped.load());
        tasks.wait_idle();
    });

    s.test("the UI queue takes posts from any thread", [] {
        omui::UiQueue q;
        omui::Tasks tasks(4);
        tasks.parallel_for(4, 1, [&](std::size_t begin, std::size_t end) {
            for (auto worker = begin; worker < end; ++worker)
                for (int i = 0; i < 250; ++i) q.post([] {});
        });
        std::size_t ran = 0;
        while (q.approximate_size() > 0) ran += q.drain();
        check_eq(ran, std::size_t{1000});
        int order = 0;
        q.post([&] { order = order * 10 + 1; });
        q.post([&] { order = order * 10 + 2; q.post([&] { order = order * 10 + 3; }); });
        check_eq(q.drain(), std::size_t{2}, std::source_location::current());
        check_eq(order, 12);
        check_eq(q.drain(), std::size_t{1});
        check_eq(order, 123);
    });

    return s.run(argc, argv);
}
