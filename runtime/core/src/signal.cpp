module openmui.core;

import std;

namespace omui {

namespace detail {

namespace {

struct Graph {
    ReactiveNode*              current = nullptr;  // the node being evaluated
    std::uint64_t              epoch   = 1;        // bumped on every change
    int                        batch   = 0;
    bool                       flushing = false;
    std::deque<ReactiveNode*>  queue;              // effects to run
};

thread_local Graph graph;

void unlink_deps(ReactiveNode& n) {
    for (const auto& d : n.deps) std::erase(d.node->observers, &n);
    n.deps.clear();
}

bool any_dep_changed(ReactiveNode& n) {
    for (const auto& d : n.deps) {
        if (d.node->evaluate && !d.node->effect) refresh(*d.node);
        if (d.node->version != d.version) return true;
    }
    return false;
}

void flush() {
    if (graph.flushing) return;
    graph.flushing = true;
    int guard = 0;
    while (!graph.queue.empty()) {
        if (++guard > 100000) {
            error("signal effects did not settle after 100000 runs: an effect keeps writing what it reads");
            for (ReactiveNode* e : graph.queue) e->queued = false;
            graph.queue.clear();
            break;
        }
        ReactiveNode* e = graph.queue.front();
        graph.queue.pop_front();
        e->queued = false;
        if (!e->stale) continue;
        if (e->checked == 0 || any_dep_changed(*e)) evaluate_tracked(*e, e->evaluate);
        e->stale = false;
        e->checked = graph.epoch;
    }
    graph.flushing = false;
}

}  // namespace

ReactiveNode::~ReactiveNode() {
    unlink_deps(*this);
    for (ReactiveNode* o : observers) std::erase_if(o->deps, [&](const Dep& d) { return d.node == this; });
    if (queued) std::erase(graph.queue, this);
    if (graph.current == this) graph.current = nullptr;
}

void track(ReactiveNode& source) {
    ReactiveNode* reader = graph.current;
    if (!reader || reader == &source) return;
    for (auto& d : reader->deps)
        if (d.node == &source) {
            d.version = source.version;
            return;
        }
    reader->deps.push_back({&source, source.version});
    source.observers.push_back(reader);
}

void schedule(ReactiveNode& effect) {
    if (effect.queued) return;
    effect.queued = true;
    graph.queue.push_back(&effect);
}

namespace {

void mark(ReactiveNode& n) {
    for (ReactiveNode* o : n.observers) {
        if (o->stale) {
            if (o->effect) schedule(*o);
            continue;
        }
        o->stale = true;
        if (o->effect) schedule(*o);
        else mark(*o);
    }
}

}  // namespace

void changed(ReactiveNode& source) {
    ++graph.epoch;
    mark(source);
    if (graph.batch == 0) flush();
}

void refresh(ReactiveNode& n) {
    if (!n.evaluate) return;
    if (n.checked == graph.epoch) return;
    if (n.running) {
        error("a computed value reads itself");
        return;
    }
    if (n.checked == 0 || (n.stale && any_dep_changed(n))) evaluate_tracked(n, n.evaluate);
    n.stale = false;
    n.checked = graph.epoch;
}

void evaluate_tracked(ReactiveNode& n, void (*body)(ReactiveNode*)) {
    unlink_deps(n);
    ReactiveNode* saved = graph.current;
    graph.current = &n;
    n.running = true;
    body(&n);
    n.running = false;
    graph.current = saved;
}

void begin_batch() noexcept { ++graph.batch; }

void end_batch() {
    if (--graph.batch == 0) flush();
}

void dispose(ReactiveNode& n) {
    unlink_deps(n);
    if (n.queued) {
        std::erase(graph.queue, &n);
        n.queued = false;
    }
}

UntrackedScope::UntrackedScope() : saved_(graph.current) { graph.current = nullptr; }
UntrackedScope::~UntrackedScope() { graph.current = saved_; }

}  // namespace detail

struct Watch::State : detail::ReactiveNode {
    explicit State(std::function<void()> f) : fn(std::move(f)) {
        effect = true;
        evaluate = &State::run;
    }
    static void run(detail::ReactiveNode* n) { static_cast<State*>(n)->fn(); }
    std::function<void()> fn;
};

Watch::Watch(std::function<void()> fn) : state_(std::make_unique<State>(std::move(fn))) {
    detail::evaluate_tracked(*state_, &State::run);
    state_->stale = false;
    state_->checked = 1;
}

Watch::Watch() noexcept = default;
Watch::Watch(Watch&& o) noexcept : state_(std::move(o.state_)) {}

Watch& Watch::operator=(Watch&& o) noexcept {
    if (this != &o) {
        dispose();
        state_ = std::move(o.state_);
    }
    return *this;
}

Watch::~Watch() { dispose(); }

void Watch::dispose() {
    if (!state_) return;
    detail::dispose(*state_);
    state_.reset();
}

}  // namespace omui
