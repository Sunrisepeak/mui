// The pool over taskflow, in an implementation unit: nothing of it is
// exported (D-40). Taskflow's headers are included here rather than its
// `tf` module imported: the module's partitions each include the profiler's
// header, whose `static inline` helpers clang's Microsoft ABI names alike
// across them, so a unit constructing an Executor (which makes the profiler
// when TF_ENABLE_PROFILER is set) defined them twice on Windows. Included
// once, in this native unit, they are one. The UI queue is ui_queue.cpp's.
module;
#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/for_each.hpp>
module openmui.core;

import std;
import mcpplibs.os;

namespace omui {

struct Tasks::Impl {
    explicit Impl(unsigned n) : executor(n) {}
    tf::Executor      executor;
    std::stop_source  stop;
    std::mutex        mutex;
    std::atomic<int>  pending{0};
    std::condition_variable idle;
};

Tasks::Tasks(unsigned threads) {
    unsigned n = threads ? threads : std::max(2u, std::thread::hardware_concurrency());
    impl_ = std::make_unique<Impl>(n);
}

Tasks::~Tasks() {
    cancel_all();
    wait_idle();
    impl_->executor.wait_for_all();
}

Tasks& Tasks::shared() {
    // OMUI_THREADS caps the program's pool (a CI runner's cores, a host's budget).
    static Tasks pool([] {
        unsigned n = 0;
        if (auto v = mcpplibs::os::env::get("OMUI_THREADS"); v && !v->empty()) {
            const auto r = std::from_chars(v->data(), v->data() + v->size(), n);
            if (r.ec != std::errc{} || n == 0 || n > 1024) n = 0;
        }
        return n;
    }());
    return pool;
}

unsigned Tasks::threads() const noexcept { return static_cast<unsigned>(impl_->executor.num_workers()); }

void Tasks::parallel_for(std::size_t count, std::size_t grain, FunctionRef<void(std::size_t, std::size_t)> body) {
    if (count == 0) return;
    if (grain == 0) grain = 1;
    std::size_t chunks = (count + grain - 1) / grain;
    if (chunks == 1 || impl_->executor.num_workers() <= 1) {
        for (std::size_t c = 0; c < chunks; ++c) body(c * grain, std::min(count, (c + 1) * grain));
        return;
    }
    // corun: a worker thread that calls parallel_for keeps working instead of blocking.
    if (impl_->executor.this_worker_id() >= 0) {
        tf::Taskflow flow;
        flow.for_each_index(std::size_t{0}, chunks, std::size_t{1}, [&](std::size_t c) {
            body(c * grain, std::min(count, (c + 1) * grain));
        });
        impl_->executor.corun(flow);
        return;
    }
    // Another thread: a task a chunk, the calling thread taking the first.
    // Not `run(flow).wait()` on a taskflow of this frame: the executor may
    // still be tearing that run down (locking the taskflow's own mutex) after
    // the wait returns and the taskflow is gone with the frame — which wrote
    // over the caller's stack when several threads drew at once. What the
    // tasks hold is theirs; the body is not touched once the last is counted.
    struct Done {
        std::atomic<std::size_t> left;
        std::promise<void>       all;
        explicit Done(std::size_t n) : left(n) {}
    };
    auto done = std::make_shared<Done>(chunks);
    auto finished = done->all.get_future();
    for (std::size_t c = 1; c < chunks; ++c)
        impl_->executor.silent_async([done, c, grain, count, &body] {
            body(c * grain, std::min(count, (c + 1) * grain));
            if (done->left.fetch_sub(1) == 1) done->all.set_value();
        });
    body(0, std::min(count, grain));
    if (done->left.fetch_sub(1) == 1) done->all.set_value();
    finished.wait();
}

std::future<void> Tasks::spawn(std::function<void(std::stop_token)> fn, Priority) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    impl_->pending.fetch_add(1);
    std::stop_token token = impl_->stop.get_token();
    impl_->executor.silent_async([this, fn = std::move(fn), promise, token]() mutable {
        fn(token);
        promise->set_value();
        if (impl_->pending.fetch_sub(1) == 1) {
            std::lock_guard lock(impl_->mutex);
            impl_->idle.notify_all();
        }
    });
    return future;
}

void Tasks::cancel_all() noexcept {
    impl_->stop.request_stop();
    impl_->stop = std::stop_source{};
}

void Tasks::wait_idle() {
    std::unique_lock lock(impl_->mutex);
    impl_->idle.wait(lock, [&] { return impl_->pending.load() == 0; });
}

}  // namespace omui
