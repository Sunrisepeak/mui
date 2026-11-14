// Live Design's history verbs (omui.designer:history_verbs): the agent kernel's
// operations over a design's sources, under the designer's names.
module omui.designer;

import std;
import omui.core;
import omui.view;
import mlibs.agent;
import mlibs.json;

namespace omui::designer {

namespace {

// A commit the kernel answered, as the design's verbs say one: its state is the semantic tree's hash.
Value design_commit(const Value& answered) {
    Value out;
    for (const std::string_view key : {"id", "message", "actor", "branch", "time", "parents", "files"})
        if (const Value* field = answered.get(key)) out.set(key, *field);
    if (const Value* state = answered.get("state")) out.set("tree", *state);
    return out;
}

}  // namespace

Value commit_value(const mlibs::agent::Commit& commit) {
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
        .set("tree", commit.state)
        .set("parents", Value(std::move(parents)))
        .set("files", Value(std::move(paths)));
}

Value changed_lines(std::string_view before, std::string_view after) {
    const mlibs::agent::LineChange change = mlibs::agent::changed_lines(before, after);
    return Value{}.set("line", static_cast<double>(change.line)).set("before", change.before).set("after", change.after);
}

std::expected<std::string, std::string> HistoryVerbs::Bridge::apply(const std::map<std::string, std::string>& texts, Reason reason) {
    using Kind = Runtime::DesignPresentation::Kind;
    auto applied = sources.apply(texts, reason == Reason::switch_to ? Kind::switch_to : reason == Reason::undo ? Kind::undo : Kind::edit);
    if (!applied) return std::unexpected(applied.error());
    return applied->json();
}

std::string HistoryVerbs::actor() const {
    const auto by = rt.current_invocation();
    return by ? by->actor.text() : std::string("person");
}

void HistoryVerbs::add_to(const std::function<void(std::string_view, VerbFn)>& add) {
    // One of the kernel's operations with a verb's arguments, answered as a verb answers.
    const auto run = [this](std::string_view operation, const Value& args) -> VerbResult {
        const std::string text = args.map() ? args.json() : std::string("{}");
        const auto parameters = mlibs::json::Document::parse(text, "arguments");
        if (!parameters) return std::unexpected(parameters.error().text());
        auto answer = operations.run(operation, parameters->root(), actor());
        if (!answer) return std::unexpected(answer.error());
        auto value = parse_json(*answer);
        if (!value) return std::unexpected(value.error().text());
        return std::move(*value);
    };
    add("design.history", [run](const Value& args) -> VerbResult {
        auto listed = run("history", args);
        if (!listed) return listed;
        Value::List log;
        if (const Value* commits = listed->get("log"); commits && commits->list())
            for (const Value& commit : *commits->list()) log.push_back(design_commit(commit));
        listed->set("log", Value(std::move(log)));
        return listed;
    });
    add("design.history.commit", [run](const Value& args) -> VerbResult {
        auto made = run("commit", args);
        if (!made) return made;
        return design_commit(*made);
    });
    add("design.history.branch", [run](const Value& args) { return run("branch", args); });
    add("design.history.switch", [run](const Value& args) { return run("switch", args); });
    add("design.history.diff", [run](const Value& args) { return run("diff", args); });
    add("design.history.tag", [run](const Value& args) { return run("tag", args); });
    // What changed, with the commit that keeps it.
    for (const std::string_view operation : {"revert", "merge"})
        add(std::format("design.history.{}", operation), [run, operation](const Value& args) -> VerbResult {
            auto done = run(operation, args);
            if (!done) return done;
            if (const Value* commit = done->get("commit")) done->set("commit", design_commit(*commit));
            return done;
        });
}

}  // namespace omui::designer
