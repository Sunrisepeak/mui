// openmui.core:task — parallel work and the queue back to the UI thread.
//
// Layout of large subtrees, text shaping, rasterising tiles and rendering
// video frames run on a work-stealing pool (taskflow, D-56). Results come back
// to the UI thread through a lock-free queue drained at the frame boundary.
//
// `parallel_for` partitions a range into chunks that depend only on the range
// and the grain, never on the number of threads or their timing, so work that
// is deterministic per chunk is deterministic as a whole.
export module openmui.core:task;

import std;
import openmui.memory;

export namespace omui {

enum class Priority : std::uint8_t { frame, background };

class Tasks {
public:
    // threads = 0: one per hardware thread, at least 2.
    explicit Tasks(unsigned threads = 0);
    ~Tasks();
    Tasks(const Tasks&) = delete;
    Tasks& operator=(const Tasks&) = delete;

    // The pool shared by a process's runtimes.
    [[nodiscard]] static Tasks& shared();

    [[nodiscard]] unsigned threads() const noexcept;

    // Calls body(begin, end) for consecutive chunks of [0, count) of `grain`
    // elements (the last may be shorter) and returns when all are done. Small
    // ranges run on the calling thread.
    void parallel_for(std::size_t count, std::size_t grain, FunctionRef<void(std::size_t, std::size_t)> body);

    // Runs fn on the pool; the future completes when it has run. `fn` receives
    // a stop token; a cancelled task should return early.
    std::future<void> spawn(std::function<void(std::stop_token)> fn, Priority priority = Priority::background);

    // Asks every task spawned so far to stop.
    void cancel_all() noexcept;

    // Blocks until every spawned task has finished.
    void wait_idle();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Work posted from any thread, run on the thread that drains it.
class UiQueue {
public:
    UiQueue();
    ~UiQueue();
    UiQueue(const UiQueue&) = delete;
    UiQueue& operator=(const UiQueue&) = delete;

    void post(std::function<void()> fn);
    // Runs what is queued now (not what the runs post meanwhile); returns how
    // many ran.
    std::size_t drain();
    [[nodiscard]] std::size_t approximate_size() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui
