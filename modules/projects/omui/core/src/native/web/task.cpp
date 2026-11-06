// The pool on the Web (D-84): one thread, the page's. The interface is the
// threaded pool's (pool/task.cpp); the work runs on the calling thread, in
// the same chunks, so what is deterministic per chunk gives the same result.
// A spawned task runs at once and its future is ready when spawn returns.
module openmui.core;

import std;

namespace omui {

struct Tasks::Impl {
    std::stop_source stop;
};

Tasks::Tasks(unsigned) : impl_(std::make_unique<Impl>()) {}

Tasks::~Tasks() = default;

Tasks& Tasks::shared() {
    static Tasks pool(1);
    return pool;
}

unsigned Tasks::threads() const noexcept { return 1; }

void Tasks::parallel_for(std::size_t count, std::size_t grain, FunctionRef<void(std::size_t, std::size_t)> body) {
    if (count == 0) return;
    if (grain == 0) grain = 1;
    const std::size_t chunks = (count + grain - 1) / grain;
    for (std::size_t c = 0; c < chunks; ++c) body(c * grain, std::min(count, (c + 1) * grain));
}

std::future<void> Tasks::spawn(std::function<void(std::stop_token)> fn, Priority) {
    std::promise<void> done;
    fn(impl_->stop.get_token());
    done.set_value();
    return done.get_future();
}

void Tasks::cancel_all() noexcept {
    impl_->stop.request_stop();
    impl_->stop = std::stop_source{};
}

void Tasks::wait_idle() {}

}  // namespace omui
