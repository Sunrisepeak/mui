// openmui.lang:host — a script's thread beside a runtime (component-binding
// §6.2): the runtime's thread holds the tree; the script runs on its own.
//
//   script thread                       runtime thread
//   ─────────────                       ──────────────
//   host.ui(fn)  ── omuiPost ──────────▶ fn() at the next frame boundary
//                ◀── the result ───────
//   handler(args) ◀── host.post ──────── an event (omuiOn)
//   signal cache  ◀── host.post ──────── a signal changed (omuiSignalWatch)
//
// Calls into the runtime are coarse and few (a view is one mount); signals are
// mirrored, so reading one never waits; writing one is sent and not waited for
// (the runtime has it by the next frame). Nothing of a script runs in a frame.
export module openmui.lang:host;

import std;
import nlohmann.json;
import openmui.abi;

export namespace omui::lang {

using Json = nlohmann::json;

// A failed call of the C ABI: its result and what the sink said.
struct Error {
    abi::OmuiResult result = abi::OMUI_ERROR_INTERNAL;
    std::string     message;

    [[nodiscard]] std::string text() const;
};

template <class T>
using Result = std::expected<T, Error>;

class Host {
public:
    // Attaches to a runtime whose thread is the caller's, and starts the
    // script thread.
    explicit Host(abi::OmuiRuntime runtime);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    [[nodiscard]] abi::OmuiRuntime runtime() const noexcept { return rt_; }

    // Work for the script thread, from any thread.
    void post(std::function<void()> fn);
    // Runs fn on the runtime's thread and waits for it (from the script
    // thread; on the runtime's thread it runs at once).
    template <class F>
    auto ui(F fn) -> std::invoke_result_t<F&> {
        using R = std::invoke_result_t<F&>;
        if (on_runtime_thread()) return fn();
        if constexpr (std::is_void_v<R>) {
            run_on_ui([&] { fn(); });
        } else {
            std::optional<R> out;
            run_on_ui([&] { out.emplace(fn()); });
            return std::move(*out);
        }
    }
    // Sends work to the runtime's thread without waiting.
    void send(std::function<void()> fn);
    // Runs fn on the script thread and waits for it (from the runtime's
    // thread: a verb the script implements). While it waits, the runtime's
    // thread keeps doing what the script asks of it, so neither blocks the
    // other.
    template <class F>
    auto script(F fn) -> std::invoke_result_t<F&> {
        using R = std::invoke_result_t<F&>;
        if (on_script_thread()) return fn();
        if constexpr (std::is_void_v<R>) {
            run_on_script([&] { fn(); });
        } else {
            std::optional<R> out;
            run_on_script([&] { out.emplace(fn()); });
            return std::move(*out);
        }
    }

    // Nothing queued or running on the script thread and nothing in flight
    // to the runtime: what a test (or a frame loop) waits for.
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] bool on_runtime_thread() const noexcept { return std::this_thread::get_id() == runtime_thread_; }
    [[nodiscard]] bool on_script_thread() const noexcept { return std::this_thread::get_id() == script_thread_id_; }

    // What the script thread asked of the runtime's: runs it now (the
    // runtime's thread, between frames or while it waits for the script).
    void drain();

    struct Inbox;  // the runtime thread's queue; posts reach it by id

private:
    void run_on_ui(std::function<void()> fn);
    void run_on_script(std::function<void()> fn);
    void loop(std::stop_token stop);

    abi::OmuiRuntime                  rt_;
    std::thread::id                   runtime_thread_;
    std::thread::id                   script_thread_id_;
    mutable std::mutex                mutex_;
    std::condition_variable_any       wake_;
    std::deque<std::function<void()>> queue_;
    std::deque<std::function<void()>> urgent_;  // the runtime waits for these (a verb), even inside a script's call
    bool                              running_ = false;
    std::shared_ptr<Inbox>            inbox_;
    std::uint64_t                     inbox_id_ = 0;  // what a post carries: no allocation to leak
    std::jthread                      thread_;
};

}  // namespace omui::lang
