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
    static Tasks pool;
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
    tf::Taskflow flow;
    flow.for_each_index(std::size_t{0}, chunks, std::size_t{1}, [&](std::size_t c) {
        body(c * grain, std::min(count, (c + 1) * grain));
    });
    // corun: a worker thread that calls parallel_for keeps working instead of blocking.
    if (impl_->executor.this_worker_id() >= 0) impl_->executor.corun(flow);
    else impl_->executor.run(flow).wait();
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
