// omui.agent:broker — background tasks (W8, PPet §2.1–2.2): the work an
// operation starts without waiting for it. A task has a handle the moment it
// is submitted; its work runs on the shared pool, never on the thread that
// draws; what it made is committed by its owner on the UI thread at a frame's
// boundary (the generation's draft, say). Meanwhile the interface draws,
// input is handled and requests are answered — the protocol is full duplex.
// A cancel is taken at once: from then on nothing the task would commit is,
// and the work is asked to stop (a call that cannot be interrupted keeps its
// resources, but no one waits for it). States stay readable after the end.
//
//     agent::TaskBroker tasks(runtime);
//     const auto id = tasks.submit({.name = "draft from code", .work = [](std::stop_token stop, agent::Progress& progress)
//                                       -> agent::TaskWork { …; return [made] { return commit(made); }; }});
export module omui.agent:broker;

import std;
import omui.core;
import omui.view;

export namespace omui::agent {

// What a task's work says of itself while it runs (any thread).
class Progress {
public:
    void set(double fraction, std::string doing = {});
    [[nodiscard]] std::pair<double, std::string> get() const;

private:
    mutable std::mutex lock_;
    double             fraction_ = 0;
    std::string        doing_;
};

// What a task's work answers: the step that commits what it made, run on the
// UI thread (its result the task's), or why it failed.
using TaskCommit = std::function<VerbResult()>;
using TaskWork = std::expected<TaskCommit, std::string>;

struct TaskSpec {
    std::string name;  // what it is: "draft from code for extras"
    std::function<TaskWork(std::stop_token, Progress&)> work;
};

class TaskBroker {
public:
    explicit TaskBroker(Runtime& runtime);
    ~TaskBroker();
    TaskBroker(const TaskBroker&) = delete;
    TaskBroker& operator=(const TaskBroker&) = delete;

    // Its handle at once; the work starts on the pool.
    std::string submit(TaskSpec spec);
    // Taken at once; false when there is no such task or it has ended.
    bool cancel(std::string_view id);
    [[nodiscard]] Value status(std::string_view id) const;
    [[nodiscard]] Value list(bool all) const;
    // How many have not ended.
    [[nodiscard]] std::size_t running() const;

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;  // shared with the work in flight
};

}  // namespace omui::agent
