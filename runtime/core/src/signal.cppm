// openmui.core:signal — state that the interface follows by itself.
//
//     auto count = omui::signal(0);
//     auto label = omui::computed([=] { return std::format("clicked {} times", count()); });
//     omui::Watch log([=] { omui::info("{}", label()); });
//     count.set(1);          // label recomputes, the effect runs once
//
// Writes push a "stale" mark through the graph; reads pull: a computed value
// is recomputed only when one of the values it read last time has changed,
// and an equal result stops the propagation. Effects run after the write (or
// after the outermost batch), each at most once per change, in the order they
// were scheduled. Dependencies are tracked automatically from what is read.
//
// The graph belongs to the thread that uses it (the UI thread). Signals are
// handles: copies refer to the same state, which is what lets a builder's
// lambda capture a signal by value.
export module openmui.core:signal;

import std;
import :diag;

export namespace omui {

namespace detail {

struct ReactiveNode {
    struct Dep {
        ReactiveNode* node;
        std::uint64_t version;
    };

    std::uint64_t              version = 0;  // bumped when the value changes
    std::uint64_t              checked = 0;  // the epoch this node was last known fresh
    bool                       stale   = true;
    bool                       queued  = false;
    bool                       effect  = false;
    bool                       running = false;
    std::vector<ReactiveNode*> observers;
    std::vector<Dep>           deps;
    void (*evaluate)(ReactiveNode*) = nullptr;

    ReactiveNode() = default;
    ReactiveNode(const ReactiveNode&) = delete;
    ReactiveNode& operator=(const ReactiveNode&) = delete;
    ~ReactiveNode();
};

// A source is being read: record it as a dependency of the node evaluating.
void track(ReactiveNode& source);
// A source's value changed: mark what depends on it, schedule effects.
void changed(ReactiveNode& source);
// Bring a computed node up to date (re-evaluating only if needed).
void refresh(ReactiveNode& node);
// Evaluate `node` with dependency tracking, replacing its dependencies.
void evaluate_tracked(ReactiveNode& node, void (*body)(ReactiveNode*));
void schedule(ReactiveNode& effect);
void begin_batch() noexcept;
void end_batch();
void dispose(ReactiveNode& node);

struct UntrackedScope {
    UntrackedScope();
    ~UntrackedScope();
    UntrackedScope(const UntrackedScope&) = delete;
    UntrackedScope& operator=(const UntrackedScope&) = delete;

private:
    ReactiveNode* saved_;
};

template <class T>
bool same(const T& a, const T& b) {
    if constexpr (std::equality_comparable<T>) return a == b;
    else return false;
}

}  // namespace detail

template <class T>
class Signal {
public:
    using value_type = T;

    explicit Signal(T initial = T{}) : state_(std::make_shared<State>(std::move(initial))) {}

    // The value; inside a computed or an effect, also a dependency.
    [[nodiscard]] const T& get() const {
        detail::track(*state_);
        return state_->value;
    }
    [[nodiscard]] const T& operator()() const { return get(); }

    // The value without becoming a dependency.
    [[nodiscard]] const T& peek() const noexcept { return state_->value; }

    // An equal value changes nothing and notifies no one.
    void set(T value) const {
        if (detail::same(state_->value, value)) return;
        state_->value = std::move(value);
        ++state_->version;
        detail::changed(*state_);
    }

    template <class F>
        requires std::invocable<F&, const T&>
    void update(F&& f) const {
        set(std::invoke(f, state_->value));
    }

    [[nodiscard]] std::uint64_t version() const noexcept { return state_->version; }
    [[nodiscard]] bool same_as(const Signal& o) const noexcept { return state_ == o.state_; }
    // Whether anything reads it now (a computed value, an effect, a region).
    [[nodiscard]] bool observed() const noexcept { return !state_->observers.empty(); }

private:
    struct State : detail::ReactiveNode {
        explicit State(T v) : value(std::move(v)) {
            stale = false;
            version = 1;
        }
        T value;
    };
    std::shared_ptr<State> state_;
};

template <class T>
[[nodiscard]] Signal<T> signal(T initial) {
    return Signal<T>(std::move(initial));
}

[[nodiscard]] inline Signal<std::string> signal(const char* initial) { return Signal<std::string>(initial); }

template <class T>
class Computed {
public:
    using value_type = T;

    template <class F>
        requires std::invocable<F&> && std::convertible_to<std::invoke_result_t<F&>, T>
    explicit Computed(F f) : state_(std::make_shared<State>(std::function<T()>(std::move(f)))) {}

    [[nodiscard]] const T& get() const {
        detail::refresh(*state_);
        detail::track(*state_);
        return *state_->value;
    }
    [[nodiscard]] const T& operator()() const { return get(); }

    [[nodiscard]] const T& peek() const {
        detail::refresh(*state_);
        return *state_->value;
    }

private:
    struct State : detail::ReactiveNode {
        explicit State(std::function<T()> f) : fn(std::move(f)) { evaluate = &State::run; }
        static void run(detail::ReactiveNode* n) {
            auto* self = static_cast<State*>(n);
            T next = self->fn();
            if (!self->value || !detail::same(*self->value, next)) {
                self->value.emplace(std::move(next));
                ++self->version;
            }
        }
        std::function<T()> fn;
        std::optional<T>   value;
    };
    std::shared_ptr<State> state_;
};

template <class F>
[[nodiscard]] auto computed(F f) -> Computed<std::invoke_result_t<F&>> {
    return Computed<std::invoke_result_t<F&>>(std::move(f));
}

// Runs `fn` now and again whenever something it read changes, until the
// Watch is destroyed (or `dispose` is called).
class Watch {
public:
    Watch() noexcept;
    explicit Watch(std::function<void()> fn);
    Watch(Watch&&) noexcept;  // out of line: State is complete only there
    Watch& operator=(Watch&&) noexcept;
    Watch(const Watch&) = delete;
    Watch& operator=(const Watch&) = delete;
    ~Watch();

    void dispose();
    [[nodiscard]] bool active() const noexcept { return state_ != nullptr; }

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Writes inside `f` notify once, when the outermost batch ends.
template <class F>
decltype(auto) batch(F&& f) {
    detail::begin_batch();
    struct End {
        ~End() { detail::end_batch(); }
    } end;
    return std::invoke(std::forward<F>(f));
}

// Reads inside `f` do not become dependencies.
template <class F>
decltype(auto) untracked(F&& f) {
    detail::UntrackedScope scope;
    return std::invoke(std::forward<F>(f));
}

}  // namespace omui
