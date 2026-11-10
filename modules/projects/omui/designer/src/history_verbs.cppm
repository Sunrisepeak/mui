// omui.designer:history_verbs — Live Design's history verbs (D-151:
// design.history, .commit, .branch, .switch, .revert, .diff, .merge, .tag)
// over any design's sources: the player's TypeScript, which an edit runs at
// once, or a C++ application's files under the development host (W16), which
// a build runs. The history goes on in whoever holds it, whatever restarts.
export module omui.designer:history_verbs;

import std;
import omui.core;
import omui.view;
import :history;

export namespace omui::designer {

// What the history's verbs work on: the design's sources as they run now, and a way to run others.
class DesignSources {
public:
    virtual ~DesignSources() = default;
    // Every source's text as it runs now (path → text): a state of the design.
    [[nodiscard]] virtual std::map<std::string, std::string> texts() = 0;
    // These texts put into what runs (each that differs), presented as `kind` says; what changed.
    virtual std::expected<Value, std::string> apply(const std::map<std::string, std::string>& texts, Runtime::DesignPresentation::Kind kind) = 0;
    // The semantic tree's hash of what runs now ("" when it is not known here).
    [[nodiscard]] virtual std::string tree_hash() = 0;
};

// A commit as the verbs answer it.
[[nodiscard]] Value commit_value(const DesignCommit& commit);
// Where two texts differ, by whole lines: the first line and the lines before and after.
[[nodiscard]] Value changed_lines(std::string_view before, std::string_view after);

class HistoryVerbs {
public:
    HistoryVerbs(Runtime& runtime, DesignHistory& history, DesignSources& sources) : rt(runtime), history(history), sources(sources) {}
    // Each verb given to `add` (which declares it as the catalog does).
    void add_to(const std::function<void(std::string_view, VerbFn)>& add);
    // The commit shown: one seen, else the branch's last ("" when none).
    [[nodiscard]] std::string shown_now() const;
    std::string shown_commit;  // a commit seen, not a branch's last (design.history.switch to it, the timeline)

private:
    [[nodiscard]] std::string actor() const;  // who asked: the invocation's actor, else the person
    Runtime&       rt;
    DesignHistory& history;
    DesignSources& sources;
};

}  // namespace omui::designer
