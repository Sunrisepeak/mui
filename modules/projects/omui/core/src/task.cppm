// omui.core:task — parallel work and the queue back to the UI thread.
//
// Layout of large subtrees, text shaping, rasterising tiles and rendering
// video frames run on a work-stealing pool (taskflow, D-56). Results come back
// to the UI thread through a lock-free queue drained at the frame boundary.
//
// `parallel_for` partitions a range into chunks that depend only on the range
// and the grain, never on the number of threads or their timing, so work that
// is deterministic per chunk is deterministic as a whole.
//
// A frame's work (Priority::frame: a frame's raster bands, its effects' rows)
// is not the pool's: a crew of its own runs it, at most `frame_width()` threads
// with the caller, never behind a background task. A fan-out wakes one helper
// fewer than it has chunks, up to the width, and no more: a frame of one chunk
// wakes none (the caller draws it), and a woken helper takes chunks until none
// is left and sleeps again at once — it does not look for other work. The crew's
// threads start when a fan-out first needs them; a fan-out allocates nothing
// (red 160; .agents/docs/2026-11-27-raster-pool-fan-out-design.md).
export module omui.core:task;

import std;
import mlibs.memory;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::memory::FunctionRef;
}

export namespace omui {

enum class Priority : std::uint8_t { frame, background };

// The frame crew's fan-outs since the pool was made (Tasks::frame_stats): what a
// bench and a test read to hold how many threads a frame wakes.
struct FanOutStats {
    std::uint64_t runs = 0;     // frame fan-outs asked for
    std::uint64_t alone = 0;    // of them, run by the caller alone: one chunk, or the crew busy with another
    std::uint64_t wakes = 0;    // helpers woken, all fan-outs together
    std::uint64_t chunks = 0;   // chunks run
    std::uint64_t helpers = 0;  // the crew's threads started (at most frame_width() − 1)
};

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
    // How many threads run a frame's chunks at most, the caller one of them:
    // min(threads(), frame_width_limit).
    [[nodiscard]] unsigned frame_width() const noexcept;
    [[nodiscard]] FanOutStats frame_stats() const noexcept;

    // Calls body(begin, end) for consecutive chunks of [0, count) of `grain`
    // elements (the last may be shorter) and returns when all are done. Small
    // ranges run on the calling thread. At Priority::frame the chunks are the
    // frame crew's (above); a fan-out from inside one — or while another thread's
    // runs — is run by its caller alone.
    void parallel_for(std::size_t count, std::size_t grain, FunctionRef<void(std::size_t, std::size_t)> body,
                      Priority priority = Priority::background);

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

// The most threads a frame's fan-out runs on, the caller counted: enough for
// a heavy frame's bands (the 1080p dashboard's whole frame is 10), while a
// light one wakes only those it needs (tests/bench `ui/raster/*`,
// `core/list-scroll/raster*`; the design's §4).
inline constexpr unsigned frame_width_limit = 16;

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
