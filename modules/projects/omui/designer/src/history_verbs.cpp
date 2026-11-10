// Live Design's history verbs (omui.designer:history_verbs), over a design's sources.
module omui.designer;

import std;
import omui.core;
import omui.view;

namespace omui::designer {

Value commit_value(const DesignCommit& commit) {
    Value::List parents;
    for (const auto& parent : commit.parents) parents.push_back(Value(parent));
    Value::List paths;
    for (const auto& [path, object] : commit.files) paths.push_back(Value(path));
    return Value{}
        .set("id", commit.id)
        .set("message", commit.message)
        .set("actor", commit.actor)
        .set("branch", commit.branch)
        .set("time", commit.time)
        .set("tree", commit.tree)
        .set("parents", Value(std::move(parents)))
        .set("files", Value(std::move(paths)));
}

// Where two texts differ, by whole lines: the first line and the lines before and after.
Value changed_lines(std::string_view before, std::string_view after) {
    const auto lines = [](std::string_view text) {
        std::vector<std::string_view> out;
        for (const auto line : std::views::split(text, '\n')) out.emplace_back(line.begin(), line.end());
        return out;
    };
    const auto old_lines = lines(before), new_lines = lines(after);
    std::size_t head = 0, tail = 0;
    while (head < old_lines.size() && head < new_lines.size() && old_lines[head] == new_lines[head]) ++head;
    while (tail + head < old_lines.size() && tail + head < new_lines.size() &&
           old_lines[old_lines.size() - 1 - tail] == new_lines[new_lines.size() - 1 - tail])
        ++tail;
    const auto joined = [](std::span<const std::string_view> part) {
        std::string out;
        for (const auto line : part) out += std::format("{}{}", out.empty() ? "" : "\n", line);
        return out;
    };
    return Value{}
        .set("line", static_cast<double>(head + 1))
        .set("before", joined(std::span(old_lines).subspan(head, old_lines.size() - head - tail)))
        .set("after", joined(std::span(new_lines).subspan(head, new_lines.size() - head - tail)));
}

std::string HistoryVerbs::shown_now() const {
    if (!shown_commit.empty()) return shown_commit;
    const auto head = history.resolve("HEAD");
    return head ? *head : std::string();
}

std::string HistoryVerbs::actor() const {
    const auto by = rt.current_invocation();
    return by ? by->actor.text() : std::string("person");
}

void HistoryVerbs::add_to(const std::function<void(std::string_view, VerbFn)>& add) {
    add("design.history", [this](const Value& args) -> VerbResult {
        const std::string branch(args.get("branch") ? args.get("branch")->as_string() : history.head_branch());
        const std::size_t limit = args.get("limit") && args.get("limit")->is_number() ? static_cast<std::size_t>(args.get("limit")->as_double()) : 50;
        Value::List log;
        for (const DesignCommit& commit : history.log(branch, limit)) log.push_back(commit_value(commit));
        Value branches(Value::Map{}), tags(Value::Map{});
        for (const auto& [name, id] : history.branches()) branches.set(name, id);
        for (const auto& [name, id] : history.tags()) tags.set(name, id);
        const auto shown = history.texts_of(shown_now().empty() ? std::string("HEAD") : shown_now());
        return Value{}
            .set("head", history.head_branch())
            .set("branches", std::move(branches))
            .set("tags", std::move(tags))
            .set("log", Value(std::move(log)))
            .set("uncommitted", !shown || *shown != sources.texts());
    });
    add("design.history.commit", [this](const Value& args) -> VerbResult {
        const std::string message(args.get("message") ? args.get("message")->as_string() : "");
        if (message.empty()) return std::unexpected(std::string("a commit says what it is: `message`"));
        auto commit = history.commit(sources.texts(), message, actor(), sources.tree_hash());
        if (!commit) return std::unexpected(commit.error());
        return commit_value(*commit);
    });
    add("design.history.branch", [this](const Value& args) -> VerbResult {
        const std::string name(args.get("name") ? args.get("name")->as_string() : "");
        const Value* at = args.get("at");
        auto made = history.branch(name, at ? std::optional<std::string_view>(at->as_string()) : std::nullopt);
        if (!made) return std::unexpected(made.error());
        return Value{}.set("branch", name).set("at", *made);
    });
    add("design.history.switch", [this](const Value& args) -> VerbResult {
        const std::string to(args.get("to") ? args.get("to")->as_string() : "");
        // Changes not committed: what runs is not the commit shown (the branch's last, or one seen on the timeline).
        const auto shown = history.texts_of(shown_now().empty() ? std::string("HEAD") : shown_now());
        if (shown && *shown != sources.texts() && !(args.get("force") && args.get("force")->as_bool()))
            return std::unexpected(std::string("the design has changes not committed: design.history.commit them, or switch with force (they are dropped)"));
        const auto branches = history.branches();
        auto at = history.resolve(to);
        if (!at) return std::unexpected(std::format("{} (design.history lists the branches and tags)", at.error()));
        auto texts = history.texts_of(*at);
        if (!texts) return std::unexpected(texts.error());
        auto applied = sources.apply(*texts, Runtime::DesignPresentation::Kind::switch_to);
        if (!applied) return std::unexpected(applied.error());
        if (!branches.contains(to)) {  // a commit, seen: the branch stays; a commit keeps it there, design.history.branch makes it one
            shown_commit = *at;
            return applied->set("branch", history.head_branch()).set("at", *at).set("detached", true);
        }
        shown_commit.clear();
        (void)history.switch_to(to);
        return applied->set("branch", to).set("at", branches.at(to));
    });
    add("design.history.revert", [this](const Value& args) -> VerbResult {
        const std::string to(args.get("to") ? args.get("to")->as_string() : "");
        auto id = history.resolve(to);
        if (!id) return std::unexpected(id.error());
        auto texts = history.texts_of(*id);
        if (!texts) return std::unexpected(texts.error());
        auto applied = sources.apply(*texts, Runtime::DesignPresentation::Kind::undo);
        if (!applied) return std::unexpected(applied.error());
        auto commit = history.commit(sources.texts(), std::format("back to {}", id->substr(0, 8)), actor(),
                                     sources.tree_hash());
        shown_commit.clear();
        if (commit) applied->set("commit", commit_value(*commit));
        return applied;
    });
    add("design.history.diff", [this](const Value& args) -> VerbResult {
        const std::string from(args.get("from") ? args.get("from")->as_string() : "HEAD");
        auto before = history.texts_of(from);
        if (!before) return std::unexpected(before.error());
        std::map<std::string, std::string> after;
        if (const Value* to = args.get("to")) {
            auto texts = history.texts_of(to->as_string());
            if (!texts) return std::unexpected(texts.error());
            after = std::move(*texts);
        } else {
            after = sources.texts();  // what runs now
        }
        Value::List files;
        std::set<std::string> paths;
        for (const auto& [path, text] : *before) paths.insert(path);
        for (const auto& [path, text] : after) paths.insert(path);
        for (const auto& path : paths) {
            const std::string old_text = before->contains(path) ? before->at(path) : std::string();
            const std::string new_text = after.contains(path) ? after.at(path) : std::string();
            if (old_text != new_text) files.push_back(changed_lines(old_text, new_text).set("path", path));
        }
        return Value{}.set("from", from).set("to", args.get("to") ? *args.get("to") : Value("running")).set("files", Value(std::move(files)));
    });
    add("design.history.merge", [this](const Value& args) -> VerbResult {
        const std::string from(args.get("from") ? args.get("from")->as_string() : "");
        auto theirs_id = history.resolve(from);
        auto ours_id = history.resolve("HEAD");
        if (!theirs_id) return std::unexpected(theirs_id.error());
        if (!ours_id) return std::unexpected(std::string("nothing committed on this branch to merge into"));
        const auto base_id = history.common_ancestor("HEAD", *theirs_id);
        auto ours = history.texts_of(*ours_id), theirs = history.texts_of(*theirs_id);
        if (!ours || !theirs) return std::unexpected(!ours ? ours.error() : theirs.error());
        std::map<std::string, std::string> base;
        if (base_id)
            if (auto texts = history.texts_of(*base_id)) base = std::move(*texts);
        std::map<std::string, std::string> merged;
        Value::List conflicts;
        std::set<std::string> paths;
        for (const auto& [path, text] : *ours) paths.insert(path);
        for (const auto& [path, text] : *theirs) paths.insert(path);
        for (const auto& path : paths) {
            const std::string base_text = base.contains(path) ? base.at(path) : std::string();
            const std::string our_text = ours->contains(path) ? ours->at(path) : base_text;
            const std::string their_text = theirs->contains(path) ? theirs->at(path) : base_text;
            const TextMerge result = merge_text(base_text, our_text, their_text);
            if (result.conflict) conflicts.push_back(Value(path));
            merged.emplace(path, result.text);
        }
        if (!conflicts.empty())
            return std::unexpected(std::format("both branches changed the same lines of {}: nothing merged — design.edit one side to agree, commit, and merge again",
                                               Value(conflicts).json()));
        auto applied = sources.apply(merged, Runtime::DesignPresentation::Kind::edit);
        if (!applied) return std::unexpected(applied.error());
        auto commit = history.commit(sources.texts(), std::format("merge {}", from), actor(), sources.tree_hash(),
                                     std::vector<std::string>{*ours_id, *theirs_id});
        shown_commit.clear();
        if (commit) applied->set("commit", commit_value(*commit));
        return applied;
    });
    add("design.history.tag", [this](const Value& args) -> VerbResult {
        const std::string name(args.get("name") ? args.get("name")->as_string() : "");
        const Value* at = args.get("at");
        auto tagged = history.tag(name, at ? std::optional<std::string_view>(at->as_string()) : std::nullopt);
        if (!tagged) return std::unexpected(tagged.error());
        return Value{}.set("tag", name).set("at", *tagged);
    });
}

}  // namespace omui::designer
