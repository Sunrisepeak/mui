// The UI queue over moodycamel's ConcurrentQueue (imported as a module,
// mcpplibs.mwrap.concurrentqueue), in an implementation unit: nothing of it
// is exported (D-40).
module openmui.core;

import std;
import mcpplibs.mwrap.concurrentqueue;

namespace omui {

struct UiQueue::Impl {
    moodycamel::ConcurrentQueue<std::function<void()>> queue;
};

UiQueue::UiQueue() : impl_(std::make_unique<Impl>()) {}
UiQueue::~UiQueue() = default;

void UiQueue::post(std::function<void()> fn) { impl_->queue.enqueue(std::move(fn)); }

std::size_t UiQueue::drain() {
    std::size_t limit = impl_->queue.size_approx();
    std::size_t ran = 0;
    std::function<void()> fn;
    while (ran < limit && impl_->queue.try_dequeue(fn)) {
        fn();
        ++ran;
    }
    return ran;
}

std::size_t UiQueue::approximate_size() const noexcept { return impl_->queue.size_approx(); }

}  // namespace omui
