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
module omui.core;

import std;
import mcpplibs.os;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::memory::FunctionRef;
}

namespace omui {

namespace {

// The frame crew (red 160): a fan-out's chunks taken by its caller and by the
// helpers it wakes — one fewer than it has chunks, up to the crew's width, the
// same helpers first every time, so threads a frame never needs stay asleep.
// A woken helper takes chunks until none is left, checks out and waits on its
// own word again: it never looks for other work (taskflow 4.1's workers, woken
// a task at a time, try to steal 2 × (queues + 1) times — 44 with 16 workers —
// and yield up to 150 times more before they sleep: the 0.3 ms of each of 16
// threads a frame that red 160 measured). Nothing is allocated
// per fan-out: the job is the crew's, its body the caller's, alive until every
// helper it woke has checked out.
class Crew {
public:
    explicit Crew(unsigned width) : width_(std::clamp(width, 1u, frame_width_limit)) {}
    ~Crew() { stop(); }
    Crew(const Crew&) = delete;
    Crew& operator=(const Crew&) = delete;

    [[nodiscard]] unsigned width() const noexcept { return width_; }

    // Runs body(0) … body(chunks − 1) with the helpers it needs; false, having
    // run nothing, when the crew is busy (another thread's fan-out, or this one's
    // own body asking again): the caller then runs them alone.
    bool run(std::size_t chunks, const FunctionRef<void(std::size_t)>& body) {
        runs_.fetch_add(1, std::memory_order_relaxed);
        if (chunks < 2 || width_ < 2 || busy_.exchange(true, std::memory_order_acquire)) {
            alone_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const auto helpers = static_cast<unsigned>(std::min<std::size_t>(chunks - 1, width_ - 1));
        for (; started_ < helpers; ++started_) {
            Helper& helper = helpers_[started_];
            helper.thread = std::thread([this, &helper] { serve(helper); });
        }
        body_ = &body;
        count_ = chunks;
        next_.store(0, std::memory_order_relaxed);
        inside_.store(helpers, std::memory_order_relaxed);
        // The job is published by each helper's call (release), read after its wait (acquire).
        for (unsigned at = 0; at < helpers; ++at) {
            helpers_[at].call.fetch_add(1, std::memory_order_release);
            helpers_[at].call.notify_one();
        }
        wakes_.fetch_add(helpers, std::memory_order_relaxed);
        chunks_.fetch_add(chunks, std::memory_order_relaxed);
        take();
        // Every helper woken checks out before the body (the caller's) goes away.
        for (std::uint32_t left; (left = inside_.load(std::memory_order_acquire)) != 0;) inside_.wait(left, std::memory_order_acquire);
        body_ = nullptr;
        busy_.store(false, std::memory_order_release);
        return true;
    }

    // A fan-out of one chunk, or one the crew could not take, run by its caller.
    void alone(std::size_t chunks) noexcept { chunks_.fetch_add(chunks, std::memory_order_relaxed); }

    [[nodiscard]] FanOutStats stats() const noexcept {
        return {.runs = runs_.load(std::memory_order_relaxed),
                .alone = alone_.load(std::memory_order_relaxed),
                .wakes = wakes_.load(std::memory_order_relaxed),
                .chunks = chunks_.load(std::memory_order_relaxed),
                .helpers = started_count_.load(std::memory_order_relaxed)};
    }

    void stop() noexcept {
        stopping_.store(true, std::memory_order_release);
        for (unsigned at = 0; at < started_; ++at) {
            helpers_[at].call.fetch_add(1, std::memory_order_release);
            helpers_[at].call.notify_one();
        }
        for (unsigned at = 0; at < started_; ++at)
            if (helpers_[at].thread.joinable()) helpers_[at].thread.join();
        started_ = 0;
    }

private:
    struct alignas(64) Helper {
        std::atomic<std::uint32_t> call{0};  // a fan-out's call: one more each time it is woken
        std::thread                thread;
    };

    void serve(Helper& helper) {
        started_count_.fetch_add(1, std::memory_order_relaxed);
        std::uint32_t seen = 0;
        while (true) {
            helper.call.wait(seen, std::memory_order_acquire);
            seen = helper.call.load(std::memory_order_acquire);
            if (stopping_.load(std::memory_order_acquire)) return;
            take();
            if (inside_.fetch_sub(1, std::memory_order_acq_rel) == 1) inside_.notify_one();
        }
    }

    void take() {
        const FunctionRef<void(std::size_t)>& body = *body_;
        for (std::size_t index; (index = next_.fetch_add(1, std::memory_order_relaxed)) < count_;) body(index);
    }

    const unsigned                                   width_;
    std::array<Helper, frame_width_limit - 1>       helpers_{};
    unsigned                                         started_ = 0;  // by the fan-out holding busy_, and stop()
    std::atomic<bool>                                busy_{false}, stopping_{false};
    const FunctionRef<void(std::size_t)>*            body_ = nullptr;
    std::size_t                                      count_ = 0;
    alignas(64) std::atomic<std::size_t>             next_{0};
    alignas(64) std::atomic<std::uint32_t>           inside_{0};
    std::atomic<std::uint64_t>                       runs_{0}, alone_{0}, wakes_{0}, chunks_{0}, started_count_{0};
};

}  // namespace

// The pool's own threads (taskflow's) are made when work first asks for them:
// a program whose only parallel work is its frames' (a window the CPU draws)
// never starts them.
struct Tasks::Impl {
    explicit Impl(unsigned n) : workers(n), crew(std::min(n, frame_width_limit)) {}
    tf::Executor& executor() {
        std::call_once(made, [&] {
            pool.emplace(workers);
            ready.store(true, std::memory_order_release);
        });
        return *pool;
    }

    unsigned                   workers;
    Crew                       crew;
    std::once_flag             made;
    std::atomic<bool>          ready{false};
    std::optional<tf::Executor> pool;
    std::stop_source           stop;
    std::mutex                 mutex;
    std::atomic<int>           pending{0};
    std::condition_variable    idle;
};

Tasks::Tasks(unsigned threads) {
    unsigned n = threads ? threads : std::max(2u, std::thread::hardware_concurrency());
    impl_ = std::make_unique<Impl>(n);
}

Tasks::~Tasks() {
    cancel_all();
    wait_idle();
    if (impl_->ready.load(std::memory_order_acquire)) impl_->pool->wait_for_all();
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

unsigned Tasks::threads() const noexcept { return impl_->workers; }

unsigned Tasks::frame_width() const noexcept { return impl_->crew.width(); }

FanOutStats Tasks::frame_stats() const noexcept { return impl_->crew.stats(); }

void Tasks::parallel_for(std::size_t count, std::size_t grain, FunctionRef<void(std::size_t, std::size_t)> body, Priority priority) {
    if (count == 0) return;
    if (grain == 0) grain = 1;
    std::size_t chunks = (count + grain - 1) / grain;
    if (priority == Priority::frame) {
        const auto chunk = [&](std::size_t c) { body(c * grain, std::min(count, (c + 1) * grain)); };
        if (impl_->crew.run(chunks, chunk)) return;
        impl_->crew.alone(chunks);
        for (std::size_t c = 0; c < chunks; ++c) chunk(c);
        return;
    }
    if (chunks == 1 || impl_->workers <= 1) {
        for (std::size_t c = 0; c < chunks; ++c) body(c * grain, std::min(count, (c + 1) * grain));
        return;
    }
    tf::Executor& executor = impl_->executor();
    // corun: a worker thread that calls parallel_for keeps working instead of blocking.
    if (executor.this_worker_id() >= 0) {
        tf::Taskflow flow;
        flow.for_each_index(std::size_t{0}, chunks, std::size_t{1}, [&](std::size_t c) {
            body(c * grain, std::min(count, (c + 1) * grain));
        });
        executor.corun(flow);
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
        executor.silent_async([done, c, grain, count, &body] {
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
    impl_->executor().silent_async([this, fn = std::move(fn), promise, token]() mutable {
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
