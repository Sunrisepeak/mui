// The Inspector's state: what it reads from the application, and when.
module openmui.inspector;

import std;
import openmui.core;
import openmui.view;
import openmui.agent;

namespace omui::inspector {

namespace {

Value request(std::string_view op) {
    Value r;
    r.set("op", op);
    return r;
}

// "main/compose/add" is three levels down.
std::size_t depth_of(std::string_view id) { return static_cast<std::size_t>(std::ranges::count(id, '/')); }

// A model key for a node id (keys are [A-Za-z0-9_.-], ids are paths of such
// names): `_` is `__` and `/` is `_s`, reversibly; one too long for a key is
// its hash, and the Inspector remembers which id it stands for.
std::string key_of(std::string_view id) {
    std::string k;
    for (char c : id) {
        if (c == '_') k += "__";
        else if (c == '/') k += "_s";
        else k += c;
    }
    if (k.size() <= 128) return k;
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (char c : id) h = (h ^ static_cast<unsigned char>(c)) * 0x100000001b3ull;
    return std::format("h{:016x}", h);
}

}  // namespace

Inspector::Inspector(Runtime& ui, Options options) : ui_(ui), options_(std::move(options)), connection_(ui, options_.directory) {
    refresh();
    poll();
}

Inspector::~Inspector() {
    if (timer_) ui_.cancel(timer_);
}

void Inspector::refresh() {
    std::vector<std::string> names;
    for (const agent::EndpointInfo& e : connection_.applications())
        if (e.name != options_.self) names.push_back(e.name);
    std::ranges::sort(names);
    applications.set(names);
    std::string want = !app.peek().empty() ? app.peek() : options_.app;
    if (want.empty() && !names.empty()) want = names.front();
    if (!want.empty() && (want != connection_.app() || !connection_.connected())) choose(want);
    tree_revision_ = -1;  // read the whole tree again
    if (connection_.connected()) read_tree();
}

void Inspector::choose(std::string name) {
    if (name == connection_.app() && connection_.connected()) return;  // already there
    if (auto r = connection_.open(name); !r) {
        status.set(std::format("cannot connect to {}: {}", name, r.error()));
        app.set(name);
        return;
    }
    app.set(name);
    status.set(std::format("connected to {}", name));
    selected.set({});
    details.set({});
    tree_revision_ = -1;
    read_tree();
}

void Inspector::poll() {
    // Frames, clock and pipeline every `poll`, the tree when its revision
    // moved, the selected node's details — one round at a time: a round
    // starts only when the last one has been answered, so a slow or busy
    // application is asked less often instead of falling behind.
    if (connection_.connected() && pending_ == 0) {
        auto answered = [this] { --pending_; };
        pending_ = 4;
        Value s = request("stats");
        s.set("history", std::int64_t{120});
        connection_.call(s, [this, answered](std::vector<Value> r) {
            if (const Value* d = data_of(r)) stats.set(*d);
            else status.set(std::format("{} does not answer: {}", app.peek(), error_of(r)));
            answered();
        });
        connection_.call(request("clock"), [this, answered](std::vector<Value> r) {
            if (const Value* d = data_of(r)) clock.set(*d);
            answered();
        });
        connection_.call(request("pipeline"), [this, answered](std::vector<Value> r) {
            if (const Value* d = data_of(r)) pipeline.set(*d);
            answered();
        });
        Value probe = request("snapshot");
        probe.set("depth", std::int64_t{0});
        connection_.call(probe, [this, answered](std::vector<Value> r) {
            if (const Value* d = data_of(r); d && d->get("revision") && d->get("revision")->as_int() != tree_revision_) read_tree();
            if (!selected.peek().empty()) read_details();
            answered();
        });
    }
    timer_ = ui_.after(options_.poll, [this] { poll(); });
}

void Inspector::read_tree() {
    connection_.call(request("snapshot"), [this](std::vector<Value> r) {
        const Value* d = data_of(r);
        if (!d || !d->get("nodes") || !d->get("nodes")->list()) return;
        tree_revision_ = d->get("revision") ? d->get("revision")->as_int() : -1;
        std::vector<Row> rows;
        ids_.clear();
        for (const Value& n : *d->get("nodes")->list()) {
            const std::string id(n.get("id") ? n.get("id")->as_string() : "");
            if (id.empty()) continue;
            const std::string role(n.get("role") ? n.get("role")->as_string() : "");
            const std::string name(n.get("name") ? n.get("name")->as_string() : "");
            Value fields = Value(Value::Map{});
            fields.set("id", id).set("role", role).set("name", name).set("depth", static_cast<std::int64_t>(depth_of(id)));
            fields.set("label", std::format("{}{}{}{}", std::string(2 * depth_of(id), ' '), role, name.empty() ? "" : "  ", name));
            const std::string key = key_of(id);
            ids_[key] = id;
            rows.push_back({key, std::move(fields)});
        }
        Model& m = ui_.model(tree_model);
        m.clear();
        if (auto pushed = m.push(std::move(rows)); !pushed) status.set(std::format("the tree: {}", pushed.error()));
        tree_size.set(m.size());
    });
}

void Inspector::select(std::string node) {
    selected.set(std::move(node));
    read_details();
}

std::string Inspector::node_of_key(std::string_view key) const {
    auto it = ids_.find(std::string(key));
    return it == ids_.end() ? std::string(key) : it->second;
}

std::string Inspector::key_of_node(std::string_view node) const { return key_of(node); }

void Inspector::read_details() {
    Value r = request("inspect");
    r.set("node", selected.peek());
    connection_.call(r, [this, wanted = selected.peek()](std::vector<Value> rec) {
        if (wanted != selected.peek()) return;  // another node was chosen meanwhile
        if (const Value* d = data_of(rec)) details.set(*d);
        else details.set(Value{}.set("error", error_of(rec)));
    });
}

void Inspector::clock_action(Value r) {
    r.set("op", "clock");
    connection_.call(r, [this](std::vector<Value> rec) {
        if (const Value* d = data_of(rec)) clock.set(*d);
        else status.set(error_of(rec));
    });
}

void Inspector::toggle_pause() {
    const Value& c = clock.peek();
    const bool paused = c.get("paused") && c.get("paused")->as_bool();
    clock_action(Value{}.set("action", paused ? "resume" : "pause"));
}

void Inspector::step(double s) { clock_action(Value{}.set("action", "step").set("seconds", s)); }
void Inspector::set_rate(double r) { clock_action(Value{}.set("action", "rate").set("rate", r)); }
void Inspector::release_time() { clock_action(Value{}.set("action", "release")); }

void Inspector::set_overlay(std::string_view overlay, bool on) {
    Value r = request("debug");
    r.set(overlay, on);
    connection_.call(r, [this](std::vector<Value> rec) {
        if (!succeeded(rec)) status.set(error_of(rec));
    });
}

}  // namespace omui::inspector
