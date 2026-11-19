// AT-SPI, through AccessKit's unix adapter: it speaks D-Bus on threads of its
// own, so every handler is called from another thread.
module omui.accessibility.bridge;

import std;
import mcpplibs.mwrap.accesskit;
import :native;

namespace omui::accessibility::native {

namespace {

// How many of the adapter's steps may wait for its thread before the runtime's thread waits for room: a scroll's
// updates are a node each, and AccessKit takes them as fast as a frame on average — not on every one.
constexpr std::size_t ahead = 4;

}  // namespace

// The adapter, and a thread of the bridge's own that gives it its steps in order. An update's work in AccessKit (its
// tree's difference, the events it raises over D-Bus) waited on AccessKit's own event thread: a frame of a 10,000-row
// list's scroll waited 15.7 ms at p95 for it (C1). The update itself is built on the runtime's thread (what changed is
// the runtime's), and handed over.
struct Service {
    accesskit_unix_adapter*           adapter = nullptr;
    std::mutex                        mutex;
    std::condition_variable_any       given, room;
    std::deque<std::function<void()>> steps;
    std::jthread                      worker;

    void post(std::function<void()> step) {
        {
            std::unique_lock hold(mutex);
            room.wait(hold, [&] { return steps.size() < ahead; });
            steps.push_back(std::move(step));
        }
        given.notify_one();
    }
    // Its steps in order; at its end, those still waiting first.
    void run(std::stop_token stop) {
        for (;;) {
            std::function<void()> next;
            {
                std::unique_lock hold(mutex);
                if (!given.wait(hold, stop, [&] { return !steps.empty(); })) return;
                next = std::move(steps.front());
                steps.pop_front();
            }
            room.notify_all();
            next();
        }
    }
};

namespace {

// An update built before it was handed over: AccessKit takes it when assistive technology listens, else it is freed.
struct Built {
    accesskit_tree_update* update = nullptr;
    bool                   taken = false;
};
accesskit_tree_update* hand_over(void* built) {
    auto* given = static_cast<Built*>(built);
    given->taken = true;
    return given->update;
}

}  // namespace

std::expected<Service*, std::string> open(void*, const Handlers& h) {
    accesskit_unix_adapter* a = accesskit_unix_adapter_new(h.activation, h.userdata, h.action, h.userdata, h.deactivation, h.userdata);
    if (!a) return std::unexpected("AccessKit's unix adapter could not be made");
    auto* s = new Service{.adapter = a};
    s->worker = std::jthread([s](std::stop_token stop) { s->run(stop); });
    return s;
}

void close(Service* s) noexcept {
    if (!s) return;
    s->worker.request_stop();
    s->given.notify_all();
    if (s->worker.joinable()) s->worker.join();  // the steps still waiting are taken first
    accesskit_unix_adapter_free(s->adapter);
    delete s;
}

void update(Service* s, accesskit_tree_update_factory factory, void* userdata) {
    accesskit_tree_update* built = factory(userdata);  // here: the session calls this only while one listens
    s->post([s, built] {
        Built given{built};
        accesskit_unix_adapter_update_if_active(s->adapter, &hand_over, &given);
        if (!given.taken) accesskit_tree_update_free(built);
    });
}

void focus(Service* s, bool focused) {
    s->post([s, focused] { accesskit_unix_adapter_update_window_focus_state(s->adapter, focused); });
}

void bounds(Service* s, accesskit_rect outer, accesskit_rect inner) {
    s->post([s, outer, inner] { accesskit_unix_adapter_set_root_window_bounds(s->adapter, outer, inner); });
}

std::string_view name() noexcept { return "atspi"; }

}  // namespace omui::accessibility::native
