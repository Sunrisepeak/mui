// omui agent sweep / omui page sweep (the strategy's interaction sweep, a
// hard gate, D-91): every node an agent can operate, every action of it,
// really performed — the way to know that nothing a person or an agent can
// press is dead. Each action is asked about first (a dry run). What cannot
// be undone, or reaches outside, is only checked to ask before it acts (its
// answer is a person's). What can be undone is performed, must succeed, and
// is undone: the node's value and states as they were. A read is performed
// and must succeed. Arguments are made up from their types (and the node's
// value). Every action's verdict a JSON line; a failure, the exit code 1.
module omui.cli;

import std;
import mcpplibs.cmdline;
import omui.core;

namespace omui::cli {

namespace cmd = mcpplibs::cmdline;

namespace {

// A sample value of an argument's type, near the node's own value where it has one: "f64 | min | max" takes its
// first word ("min"), a list an empty one, an integer 0.
Value sample(std::string_view type, const Value* now) {
    if (const auto bar = type.find(" | "); bar != std::string_view::npos) {
        const std::string_view rest = type.substr(bar + 3);
        return Value(std::string(rest.substr(0, rest.find(' '))));
    }
    if (type == "string") return Value(std::string(now && now->type() == Value::Type::text ? now->as_string() : "") + " ·");
    if (type == "bool") return Value(!(now && now->as_bool()));
    if (type == "i64" || type == "i32" || type == "u32" || type == "u64") return Value(static_cast<std::int64_t>(0));
    if (type == "f64" || type == "f32") return Value(now && now->is_number() ? now->as_double() : 0.5);
    if (type.starts_with("list")) return Value(Value::List{});
    return Value{};
}

// The node's value and lasting states, as a snapshot gives them: what an undo must give back (where the focus
// is, what the pointer is over, what is pressed or still moving are not the history's).
std::string seen(const Value& node) {
    Value v;
    if (const Value* x = node.get("value")) v.set("value", *x);
    if (const Value* x = node.get("state"); x && x->list()) {
        Value::List lasting;
        for (const Value& st : *x->list())
            if (const std::string_view k = st.as_string(); k != "focused" && k != "hovered" && k != "pressed" && k != "animating" && k != "offscreen")
                lasting.push_back(st);
        v.set("state", Value(std::move(lasting)));
    }
    return v.json();
}

const Value* data_of(const std::vector<Value>& records) {
    for (const Value& r : records)
        if (r.get("kind") && r.get("kind")->as_string() == "data") return r.get("data");
    return nullptr;
}
int exit_of(const std::vector<Value>& records) {
    for (const Value& r : records)
        if (r.get("kind") && r.get("kind")->as_string() == "result") return static_cast<int>(r.get("exitCode") ? r.get("exitCode")->as_int(1) : 1);
    return 1;
}
std::string code_of(const std::vector<Value>& records) {
    for (const Value& r : records)
        if (r.get("kind") && r.get("kind")->as_string() == "error" && r.get("code")) return std::string(r.get("code")->as_string());
    return {};
}
std::string errors_of(const std::vector<Value>& records) {
    std::string out;
    for (const Value& r : records)
        if (r.get("kind") && r.get("kind")->as_string() == "error")
            out += std::format("{}{}", out.empty() ? "" : "; ", r.get("text") ? r.get("text")->as_string() : r.json());
    return out;
}

}  // namespace

int sweep(const std::function<std::vector<Value>(const Value&)>& call, const cmd::ParsedArgs& a, std::ostream& out, std::ostream& err) {
    const std::string actor = a.value("actor").value_or("agent:omui-sweep");
    // Every node an agent can operate, a page at a time.
    std::vector<Value> nodes;
    for (std::int64_t offset = 0;;) {
        Value req;
        req.set("op", "snapshot").set("interactive", true).set("offset", offset).set("limit", static_cast<std::int64_t>(200));
        if (auto root = a.value("root")) req.set("root", *root);
        const auto records = call(req);
        const Value* d = data_of(records);
        if (exit_of(records) != 0 || !d || !d->get("nodes") || !d->get("nodes")->list()) {
            err << "omui sweep: the snapshot failed: " << errors_of(records) << '\n';
            return 1;
        }
        for (const Value& n : *d->get("nodes")->list()) nodes.push_back(n);
        offset += static_cast<std::int64_t>(d->get("nodes")->list()->size());
        if (d->get("nodes")->list()->empty() || !d->get("total") || offset >= d->get("total")->as_int()) break;
    }
    std::size_t ok = 0, asks = 0, failed = 0, unchanged = 0, arguments = 0, disabled = 0, live = 0, gone = 0, held = 0, unrecorded = 0, person = 0;
    // A node as it is now (its actions, value and states), by its id.
    auto now_of = [&](const std::string& id) -> std::optional<Value> {
        const auto r = call(Value{}.set("op", "find").set("q", Value{}.set("id", id)));
        const Value* d = data_of(r);
        if (d && d->get("nodes") && d->get("nodes")->list() && !d->get("nodes")->list()->empty()) return d->get("nodes")->list()->front();
        return std::nullopt;
    };
    auto say = [&](const Value& node, std::string_view action, std::string_view effect, std::string_view verdict, std::string_view detail) {
        Value v;
        v.set("kind", "data").set("data", Value{}.set("node", node.get("id") ? node.get("id")->as_string() : "").set("action", action).set("effect", effect)
                                              .set("verdict", verdict).set("detail", detail));
        out << v.json() << '\n';
    };
    // Leaves first: a container's actions (a canvas's pan and zoom, a view's
    // scroll) move what is inside it out of view, and out of the tree.
    for (const Value& listed : nodes | std::views::reverse) {
        const std::string id(listed.get("id") ? listed.get("id")->as_string() : "");
        const Value* actions = listed.get("actions");
        if (!actions || !actions->list()) continue;
        for (const Value& act : *actions->list()) {
            const std::string name(act.get("name") ? act.get("name")->as_string() : "");
            const std::string effect(act.get("effect") ? act.get("effect")->as_string() : "read");
            // As it is now (an action before may have changed it): gone, disabled or no longer offering this, said so.
            const auto current = now_of(id);
            if (!current) {
                ++gone, say(listed, name, effect, "gone", "not performed: the node is no longer there (an action before removed it)");
                continue;
            }
            const Value& node = *current;
            const Value* st = node.get("state");
            if (st && st->list() && std::ranges::any_of(*st->list(), [](const Value& x) { return x.as_string() == "disabled"; })) {
                ++disabled, say(node, name, effect, "disabled", "not performed: disabled now");
                continue;
            }
            const Value* offered = node.get("actions");
            if (!offered || !offered->list() ||
                std::ranges::none_of(*offered->list(), [&](const Value& x) { return x.get("name") && x.get("name")->as_string() == name; })) {
                ++gone, say(node, name, effect, "gone", "not performed: no longer offered (an action before changed the node)");
                continue;
            }
            // Its value changing on its own (a clock, a meter): not held to coming back the same.
            const auto again = now_of(id);
            const bool by_itself = !again || seen(*again) != seen(node);
            Value req;
            req.set("op", "act").set("node", id).set("action", name).set("actor", actor);
            if (const Value* args = act.get("args"); args && args->map()) {
                Value made;
                for (const auto& [k, type] : *args->map()) made.set(k, sample(type.as_string(), node.get("value")));
                req.set("args", made);
            }
            const auto own = std::ranges::find_if(*offered->list(), [&](const Value& x) { return x.get("name") && x.get("name")->as_string() == name; });
            if (own != offered->list()->end() && own->get("person") && own->get("person")->as_bool()) {
                // S-10 is a successful boundary only if the real request is refused and its state stays put.
                const auto denied = call(req);
                const auto after = now_of(id);
                if (exit_of(denied) == 1 && code_of(denied) == "refused" && after && seen(*after) == seen(node))
                    ++person, say(node, name, effect, "person", "refused to the agent, with the node unchanged (S-10)");
                else ++failed, say(node, name, effect, "failed", std::format("person-only action was not refused unchanged: {}", errors_of(denied)));
                continue;
            }
            Value dry = req;
            dry.set("dry_run", true);
            const auto asked = call(dry);
            const int dry_exit = exit_of(asked);
            if (effect == "irreversible" || effect == "external") {
                // Its answer is a person's: it must say it asks (or would run with the answer), never act on a dry run.
                if (dry_exit == 0 || dry_exit == 2) ++asks, say(node, name, effect, "asks", "not performed: its answer is a person's");
                else ++failed, say(node, name, effect, "failed", std::format("the dry run: {}", errors_of(asked)));
                continue;
            }
            // Refused with the arguments made up (a view, a key, an anchor no one has): it needs real ones, which only
            // the program knows — said, not counted as a dead control.
            const bool made_up = req.get("args") != nullptr;
            const bool readonly = st && st->list() && std::ranges::any_of(*st->list(), [](const Value& x) { return x.as_string() == "readonly"; });
            auto refused = [&](const std::vector<Value>& records, std::string_view when) {
                if (readonly && effect != "read") {  // stated: it is shown, not operated (the ball's bricks)
                    ++held, say(node, name, effect, "readonly", std::format("refused, as its state says ({}): {}", when, errors_of(records)));
                } else if (made_up) {
                    ++arguments;
                    say(node, name, effect, "arguments", std::format("made-up arguments refused ({}, {}): {}", when, code_of(records), errors_of(records)));
                } else {
                    ++failed;
                    say(node, name, effect, "failed", std::format("{}: {}", when, errors_of(records)));
                }
            };
            if (dry_exit != 0) {
                refused(asked, "the dry run");
                continue;
            }
            // Its history (the nearest scope around it; a canvas's item, the window's) before and after: undone only when
            // it was recorded, never an older step in its place.
            auto history = [&] {
                auto h = call(Value{}.set("op", "history").set("scope", id));
                if (exit_of(h) != 0) h = call(Value{}.set("op", "history"));
                const Value* d = data_of(h);
                const Value* steps = d ? d->get("undo") : nullptr;
                return std::pair{std::string(d && d->get("scope") ? d->get("scope")->as_string() : "app"),
                                 steps && steps->list() ? steps->list()->size() : std::size_t{0}};
            };
            const auto [scope, recorded] = history();
            const auto done = call(req);
            if (exit_of(done) != 0) {
                refused(done, "performed");
                continue;
            }
            if (effect != "reversible") {
                ++ok, say(node, name, effect, "ok", "performed");
                continue;
            }
            const bool in_history = history().second > recorded;
            const Value undo = Value{}.set("op", "undo").set("scope", scope).set("actor", actor);
            if (by_itself) {
                ++live;
                if (in_history) (void)call(undo);
                say(node, name, effect, "ok", "performed (its value changes on its own: not compared after the undo)");
                continue;
            }
            // Performed; then undone, and the node as it was.
            const auto after = call(Value{}.set("op", "find").set("q", Value{}.set("id", id)));
            const Value* found = data_of(after);
            const Value* now = found && found->get("nodes") && found->get("nodes")->list() && !found->get("nodes")->list()->empty()
                                   ? &found->get("nodes")->list()->front()
                                   : nullptr;
            const bool changed = !now || seen(*now) != seen(node);
            if (!in_history) {
                // S-12: an inverse is the action's to give (MAY), so said, not failed: what an undo cannot take back.
                if (changed)
                    ++unrecorded, say(node, name, effect, "unrecorded", std::format("performed; it gave no inverse, so an undo passes it by: {} then {}",
                                                                                    seen(node), now ? seen(*now) : std::string("(gone)")));
                else ++unchanged, say(node, name, effect, "ok", "performed; nothing changed, nothing recorded");
                continue;
            }
            const auto undone = call(undo);
            const auto back = call(Value{}.set("op", "find").set("q", Value{}.set("id", id)));
            const Value* returned = data_of(back);
            const Value* restored = returned && returned->get("nodes") && returned->get("nodes")->list() && !returned->get("nodes")->list()->empty()
                                        ? &returned->get("nodes")->list()->front()
                                        : nullptr;
            if (changed && exit_of(undone) != 0) {
                ++failed;
                say(node, name, effect, "failed", std::format("reversible, but its undo was refused: {}", errors_of(undone)));
                continue;
            }
            if (changed && restored && seen(*restored) != seen(node)) {
                ++failed;
                say(node, name, effect, "failed", std::format("undone, but not as it was: {} then {}", seen(node), seen(*restored)));
                continue;
            }
            if (!changed) ++unchanged, say(node, name, effect, "ok", "performed; its node looks the same (its effect may be elsewhere)");
            else ++ok, say(node, name, effect, "ok", "performed and undone");
        }
    }
    Value summary;
    summary.set("kind", "data").set("data", Value{}.set("nodes", static_cast<std::int64_t>(nodes.size())).set("ok", static_cast<std::int64_t>(ok))
                                                 .set("unchanged", static_cast<std::int64_t>(unchanged)).set("live", static_cast<std::int64_t>(live))
                                                 .set("asks", static_cast<std::int64_t>(asks)).set("person", static_cast<std::int64_t>(person)).set("arguments", static_cast<std::int64_t>(arguments))
                                                 .set("disabled", static_cast<std::int64_t>(disabled)).set("gone", static_cast<std::int64_t>(gone)).set("readonly", static_cast<std::int64_t>(held)).set("unrecorded", static_cast<std::int64_t>(unrecorded)).set("failed", static_cast<std::int64_t>(failed)));
    out << summary.json() << '\n';
    out << Value{}.set("kind", "result").set("exitCode", failed ? 1 : 0).json() << '\n';
    return failed ? 1 : 0;
}

}  // namespace omui::cli
