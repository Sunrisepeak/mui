module openmui.lang;

import std;
import nlohmann.json;
import openmui.abi;
import :host;

namespace omui::lang {

std::string Error::text() const {
    return message.empty() ? std::format("openmui: result {}", static_cast<int>(result)) : message;
}

// What the script thread asks of the runtime's thread.
struct Host::Inbox {
    std::mutex                        mutex;
    std::deque<std::function<void()>> tasks;
    std::atomic<int>                  in_flight{0};  // queued and not yet done

    void push(std::function<void()> fn) {
        in_flight.fetch_add(1);
        std::scoped_lock lock(mutex);
        tasks.push_back(std::move(fn));
    }
    void drain() {
        while (true) {
            std::function<void()> fn;
            {
                std::scoped_lock lock(mutex);
                if (tasks.empty()) return;
                fn = std::move(tasks.front());
                tasks.pop_front();
            }
            fn();
            in_flight.fetch_sub(1);
        }
    }
};

namespace {

// The inboxes posts may still reach, by id. A post carries the id, not a
// pointer it owns: one the runtime drops unrun (it was torn down first)
// leaks nothing, and one that runs after its Host is gone finds no inbox.
std::mutex                                           inboxes_mutex;
std::unordered_map<std::uint64_t, std::weak_ptr<Host::Inbox>> inboxes;
std::uint64_t                                        next_inbox = 1;

std::uint64_t enrol(const std::shared_ptr<Host::Inbox>& inbox) {
    std::scoped_lock lock(inboxes_mutex);
    inboxes.emplace(next_inbox, inbox);
    return next_inbox++;
}

void withdraw(std::uint64_t id) {
    std::scoped_lock lock(inboxes_mutex);
    inboxes.erase(id);
}

// The post that wakes the runtime's thread: it drains the inbox.
void drain_posted(void* p) {
    std::shared_ptr<Host::Inbox> inbox;
    {
        std::scoped_lock lock(inboxes_mutex);
        if (auto it = inboxes.find(reinterpret_cast<std::uintptr_t>(p)); it != inboxes.end()) inbox = it->second.lock();
    }
    if (inbox) inbox->drain();
}

}  // namespace

Host::Host(abi::OmuiRuntime runtime)
    : rt_(runtime), runtime_thread_(std::this_thread::get_id()), inbox_(std::make_shared<Inbox>()), inbox_id_(enrol(inbox_)) {
    std::promise<std::thread::id> started;
    auto id = started.get_future();
    thread_ = std::jthread([this, &started](std::stop_token stop) {
        started.set_value(std::this_thread::get_id());
        loop(stop);
    });
    script_thread_id_ = id.get();
}

Host::~Host() {
    thread_.request_stop();
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    inbox_->drain();  // nothing the script asked is left undone
    withdraw(inbox_id_);
}

void Host::post(std::function<void()> fn) {
    {
        std::scoped_lock lock(mutex_);
        queue_.push_back(std::move(fn));
    }
    wake_.notify_all();
}

void Host::loop(std::stop_token stop) {
    std::unique_lock lock(mutex_);
    while (true) {
        if (!wake_.wait(lock, stop, [&] { return !queue_.empty() || !urgent_.empty(); })) return;
        auto& from = urgent_.empty() ? queue_ : urgent_;
        auto fn = std::move(from.front());
        from.pop_front();
        running_ = true;
        lock.unlock();
        fn();
        lock.lock();
        running_ = false;
        wake_.notify_all();
    }
}

void Host::drain() { inbox_->drain(); }

void Host::send(std::function<void()> fn) {
    if (on_runtime_thread()) {
        fn();
        return;
    }
    inbox_->push(std::move(fn));
    if (abi::omuiPost(rt_, drain_posted, reinterpret_cast<void*>(static_cast<std::uintptr_t>(inbox_id_))) != abi::OMUI_SUCCESS)
        inbox_->drain();
}

void Host::run_on_ui(std::function<void()> fn) {
    bool done = false;
    send([&] {
        fn();
        std::scoped_lock lock(mutex_);
        done = true;
        wake_.notify_all();
    });
    // While the script waits, what the runtime waits for from the script (a
    // verb the script implements, invoked by this very call) runs here.
    std::unique_lock lock(mutex_);
    while (!done) {
        wake_.wait(lock, [&] { return done || !urgent_.empty(); });
        while (!done && !urgent_.empty()) {
            auto task = std::move(urgent_.front());
            urgent_.pop_front();
            lock.unlock();
            task();
            lock.lock();
        }
    }
}

void Host::run_on_script(std::function<void()> fn) {
    std::atomic<bool> finished{false};
    {
        std::scoped_lock lock(mutex_);
        urgent_.push_back([&] {
            fn();
            finished.store(true);
        });
    }
    wake_.notify_all();
    // Meanwhile, what the script asks of this thread is done here.
    while (!finished.load()) {
        drain();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    drain();
}

void Host::retire(const std::function<bool()>& down) {
    if (on_script_thread()) {
        (void)down();
        return;
    }
    // Each time `down` answers false, the script's code goes on (its wait for
    // this thread answered by run_on_script's draining) toward its return.
    while (!script([&] { return down(); })) {
    }
}

bool Host::idle() const noexcept {
    std::scoped_lock lock(mutex_);
    return queue_.empty() && urgent_.empty() && !running_ && inbox_->in_flight.load() == 0;
}

}  // namespace omui::lang
