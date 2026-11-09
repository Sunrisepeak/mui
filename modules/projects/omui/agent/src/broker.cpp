// Background tasks (omui.agent:broker).
module omui.agent;

import std;
import omui.core;
import omui.view;
import mlibs.control;

namespace omui::agent {

void Progress::set(double fraction, std::string doing) {
    const std::scoped_lock guard(lock_);
    fraction_ = std::clamp(fraction, 0.0, 1.0);
    if (!doing.empty()) doing_ = std::move(doing);
}

std::pair<double, std::string> Progress::get() const {
    const std::scoped_lock guard(lock_);
    return {fraction_, doing_};
}

namespace {

enum class TaskState : std::uint8_t { running, done, failed, cancelled };

std::string_view state_name(TaskState state) {
    switch (state) {
    case TaskState::running:   return "running";
    case TaskState::done:      return "done";
    case TaskState::failed:    return "failed";
    case TaskState::cancelled: return "cancelled";
    }
    return "running";
}

struct Task {
    std::string       id, name, actor;
    TaskState         state = TaskState::running;  // the UI thread's
    Progress          progress;
    std::stop_source  stop;
    Value             result;
    std::string       error;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now(), ended{};
};

}  // namespace

struct TaskBroker::Impl {
    // The runtime while the broker lives: the work posts its end to its queue under this lock,
    // and the broker going takes it away first, so a late end posts nowhere.
    std::mutex                                                  posting;
    Runtime*                                                    runtime;
    std::map<std::string, std::shared_ptr<Task>, std::less<>>   tasks;  // the UI thread's
    std::vector<std::string>                                    order;  // ids, oldest first
    std::uint64_t                                               next = 1;
    std::vector<std::string>                                    added;

    explicit Impl(Runtime& rt) : runtime(&rt) {}

    Value value_of(const Task& task) const {
        const auto [fraction, doing] = task.progress.get();
        const auto until = task.state == TaskState::running ? std::chrono::steady_clock::now() : task.ended;
        Value out = Value{}
                        .set("task", task.id)
                        .set("name", task.name)
                        .set("actor", task.actor)
                        .set("state", std::string(state_name(task.state)))
                        .set("progress", task.state == TaskState::done ? 1.0 : fraction)
                        .set("seconds", std::round(std::chrono::duration<double>(until - task.started).count() * 1000) / 1000);
        if (!doing.empty()) out.set("doing", doing);
        if (task.state == TaskState::done) out.set("result", task.result);
        if (!task.error.empty()) out.set("error", task.error);
        return out;
    }

    // The end of a task's work, on the UI thread: its commit run — unless it was cancelled meanwhile.
    void ended(const std::shared_ptr<Task>& task, TaskWork outcome) {
        if (task->state != TaskState::running) return;  // cancelled: what it made is not committed
        if (!outcome) {
            task->state = TaskState::failed;
            task->error = outcome.error();
        } else if (auto committed = (*outcome)()) {
            task->state = TaskState::done;
            task->result = std::move(*committed);
        } else {
            task->state = TaskState::failed;
            task->error = committed.error();
        }
        task->ended = std::chrono::steady_clock::now();
    }
};

TaskBroker::TaskBroker(Runtime& runtime) : impl_(std::make_shared<Impl>(runtime)) {
    const auto add = [&](std::string_view name, VerbFn fn) {
        const auto entry = std::ranges::find(tasks::catalog, name, [](const auto& op) { return op.operation.name; });
        if (entry == tasks::catalog.end()) return;
        if (runtime.verbs().add(verb_of(*entry), std::move(fn))) impl_->added.emplace_back(name);
    };
    add("task.list", [this](const Value& args) -> VerbResult { return list(args.get("all") && args.get("all")->as_bool()); });
    add("task.status", [this](const Value& args) -> VerbResult {
        const Value* id = args.get("task");
        Value out = status(id ? id->as_string() : std::string_view{});
        if (out.is_null()) return std::unexpected(std::format("no task `{}` (task.list lists them)", id ? id->as_string() : std::string_view{}));
        return out;
    });
    add("task.cancel", [this](const Value& args) -> VerbResult {
        const Value* id = args.get("task");
        const std::string_view name = id ? id->as_string() : std::string_view{};
        if (!cancel(name)) return std::unexpected(std::format("no running task `{}` (task.list lists them)", name));
        return status(name);
    });
}

TaskBroker::~TaskBroker() {
    for (const auto& [id, task] : impl_->tasks)
        if (task->state == TaskState::running) task->stop.request_stop();
    {
        const std::scoped_lock guard(impl_->posting);
        for (const std::string& name : impl_->added) (void)impl_->runtime->verbs().remove(name);
        impl_->runtime = nullptr;  // a work ending from now on posts nowhere
    }
}

std::string TaskBroker::submit(TaskSpec spec) {
    Impl& d = *impl_;
    auto task = std::make_shared<Task>();
    task->id = std::format("t{}", d.next++);
    task->name = std::move(spec.name);
    const auto call = d.runtime->current_invocation();
    task->actor = call ? call->actor.text() : std::string("person:host");
    d.tasks.emplace(task->id, task);
    d.order.push_back(task->id);
    const std::weak_ptr<Impl> broker = impl_;
    (void)Tasks::shared().spawn(
        [broker, task, work = std::move(spec.work)](std::stop_token) mutable {
            TaskWork outcome = task->stop.stop_requested() ? TaskWork(std::unexpected(std::string("cancelled before it began")))
                                                            : work(task->stop.get_token(), task->progress);
            const auto impl = broker.lock();
            if (!impl) return;
            const std::scoped_lock guard(impl->posting);
            if (!impl->runtime) return;
            impl->runtime->queue().post([broker, task, outcome = std::move(outcome)]() mutable {
                if (const auto alive = broker.lock()) alive->ended(task, std::move(outcome));
            });
        });
    return task->id;
}

bool TaskBroker::cancel(std::string_view id) {
    const auto found = impl_->tasks.find(id);
    if (found == impl_->tasks.end() || found->second->state != TaskState::running) return false;
    Task& task = *found->second;
    task.state = TaskState::cancelled;
    task.ended = std::chrono::steady_clock::now();
    task.stop.request_stop();
    return true;
}

Value TaskBroker::status(std::string_view id) const {
    const auto found = impl_->tasks.find(id);
    return found == impl_->tasks.end() ? Value{} : impl_->value_of(*found->second);
}

Value TaskBroker::list(bool all) const {
    Value::List out;
    std::size_t ended_shown = 0;
    for (const std::string& id : impl_->order | std::views::reverse) {
        const Task& task = *impl_->tasks.at(id);
        if (task.state != TaskState::running && !all && ++ended_shown > 10) continue;
        out.push_back(impl_->value_of(task));
    }
    return Value{}.set("tasks", Value(std::move(out))).set("running", static_cast<std::int64_t>(running()));
}

std::size_t TaskBroker::running() const {
    return static_cast<std::size_t>(std::ranges::count_if(impl_->tasks, [](const auto& entry) { return entry.second->state == TaskState::running; }));
}

}  // namespace omui::agent
