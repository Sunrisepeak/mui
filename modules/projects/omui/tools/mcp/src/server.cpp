module omui.mcp;

import std;
import mcpplibs.os;
import omui.core;
import omui.agent;
import openas;

namespace omui::mcp {

namespace {

namespace os = mcpplibs::os;

// ── JSON-RPC and MCP shapes ──────────────────────────────────────────────

// JSON-RPC's own error codes.
constexpr int parse_error = -32700, invalid_request = -32600, method_not_found = -32601, invalid_params = -32602;

Value response(const Value& id, Value result) {
    Value v;
    v.set("jsonrpc", "2.0").set("id", id).set("result", std::move(result));
    return v;
}

Value failure(const Value& id, int code, std::string message) {
    Value e;
    e.set("code", code).set("message", std::move(message));
    Value v;
    v.set("jsonrpc", "2.0").set("id", id).set("error", std::move(e));
    return v;
}

Value text(std::string t) {
    Value v;
    v.set("type", "text").set("text", std::move(t));
    return v;
}

// A tool's result: what the agent reads (text, and an image), whether the
// call failed, and the same as data (structuredContent).
Value tool_result(std::string message, bool error = false, Value structured = {}, Value::List more = {}) {
    Value::List content{text(std::move(message))};
    for (Value& m : more) content.push_back(std::move(m));
    Value r;
    r.set("content", Value(std::move(content)));
    if (!structured.is_null()) r.set("structuredContent", std::move(structured));
    r.set("isError", error);
    return r;
}

// JSON Schema, as tools/list gives each tool's input.
Value prop(std::string_view type, std::string_view description) {
    Value v;
    v.set("type", type).set("description", description);
    return v;
}

Value object_schema(Value::Map properties, std::initializer_list<std::string_view> required = {}) {
    Value s;
    s.set("type", "object").set("properties", Value(std::move(properties)));
    if (required.size()) {
        Value::List r;
        for (std::string_view k : required) r.emplace_back(k);
        s.set("required", Value(std::move(r)));
    }
    s.set("additionalProperties", false);
    return s;
}

const std::pair<std::string, Value> app_prop{"app", prop("string", "default: the one running")};

// A tool's hints from its operation's effect: a read only reads; what can be
// irreversible or external — or runs what it names, whose effect may be — is
// destructive.
Value hints_of(std::string_view effect, bool delegated) {
    const bool read_only = effect == "read" && !delegated;
    Value hints;
    hints.set("readOnlyHint", read_only).set("destructiveHint", effect == "irreversible" || effect == "external");
    hints.set("idempotentHint", read_only).set("openWorldHint", false);
    return hints;
}
std::string_view effect_name(openas::Effect effect) {
    switch (effect) {
    case openas::Effect::read: return "read";
    case openas::Effect::reversible: return "reversible";
    case openas::Effect::irreversible: return "irreversible";
    default: return "external";
    }
}

// A ui_* tool is one of the protocol's operations (omui.agent's catalog, ui.*):
// its hints are that operation's. ui_apps is the server's own, a read.
Value tool(std::string_view name, std::string_view title, std::string_view description, Value schema, std::string_view operation = {}) {
    const auto entry = std::ranges::find(agent::catalog, operation, [](const auto& e) { return e.operation.name; });
    Value t;
    t.set("name", name).set("title", title).set("description", description).set("inputSchema", std::move(schema));
    t.set("annotations", entry == agent::catalog.end() ? hints_of("read", false) : hints_of(effect_name(entry->operation.effect), entry->delegated));
    return t;
}

// An MCP tool's name for a listed operation: cellular.paint → cellular_paint.
std::string tool_name(std::string_view operation) {
    std::string out(operation);
    std::ranges::replace(out, '.', '_');
    std::ranges::replace(out, '-', '_');
    return out;
}

// The protocol's operations an MCP client sees as tools on every turn (W4: what each turn carries); the others stay
// reachable through `omui agent` and ui_describe's answer.
using Operation = std::remove_cvref_t<decltype(agent::catalog.front())>;
constexpr std::array<std::string_view, 19> offered_operations{"ui.describe", "ui.snapshot", "ui.find",     "ui.act",    "ui.invoke",   "ui.undo",  "ui.redo",
                                                              "ui.transaction", "ui.preview", "ui.record", "ui.reload", "ui.wait",  "ui.intend", "ui.drew",
                                                              "ui.capture",  "ui.timeline", "ui.explain", "ui.input", "ui.clock"};
// The operation a ui_* tool is (ui_act → ui.act), when it is one offered.
const Operation* protocol_operation(std::string_view tool) {
    if (!tool.starts_with("ui_")) return nullptr;
    const std::string op = "ui." + std::string(tool.substr(3));
    if (!std::ranges::contains(offered_operations, op)) return nullptr;
    const auto entry = std::ranges::find(agent::catalog, op, [](const auto& e) { return e.operation.name; });
    return entry == agent::catalog.end() ? nullptr : &*entry;
}

std::string_view str(const Value& args, std::string_view key) {
    const Value* v = args.get(key);
    return v ? v->as_string() : std::string_view{};
}

// The actor's name from a client's: lower case, letters, digits and '-'.
std::string actor_name(std::string_view client) {
    std::string out;
    for (char c : client) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("mcp") : out;
}

// ── an application's answer ──────────────────────────────────────────────

struct Answer {
    int         exit = 1;  // 0 success, 1 failure, 2 needs an answer, 3 not ready
    Value       data;
    std::string code, message;
    Value       detail;    // the error record's data (the answer an action wants)
    Value       changes;   // what a step changed (report: "changes"), apart from its data
    bool        delivered = false;  // a result came back
};

// A step's semantic changes, a line each: "+ id role \"name\"", "- id",
// "~ id field: before → after" (at most 24 lines; the rest counted).
std::string changes_text(const Value& changes) {
    if (changes.is_null()) return {};
    std::vector<std::string> lines;
    const auto face = [](const Value& v) {
        const std::string_view name = v.get("name") ? v.get("name")->as_string() : "";
        return std::format("{}{}", v.get("role") ? v.get("role")->as_string() : "", name.empty() ? std::string() : std::format(" \"{}\"", name));
    };
    if (const Value* added = changes.get("added"); added && added->list())
        for (const Value& n : *added->list()) lines.push_back(std::format("+ {} {}", str(n, "id"), n.get("now") ? face(*n.get("now")) : std::string()));
    if (const Value* removed = changes.get("removed"); removed && removed->list())
        for (const Value& n : *removed->list()) lines.push_back(std::format("- {}", str(n, "id")));
    if (const Value* changed = changes.get("changed"); changed && changed->list())
        for (const Value& n : *changed->list())
            if (const Value* fields = n.get("fields"); fields && fields->map())
                for (const auto& [field, was] : *fields->map()) {
                    const auto shown = [](const Value* v) { return !v ? std::string("?") : v->type() == Value::Type::text ? std::format("\"{}\"", v->as_string()) : v->json(); };
                    lines.push_back(std::format("~ {} {}: {} → {}", str(n, "id"), field, shown(was.get("before")), shown(was.get("after"))));
                }
    if (lines.empty()) return "\nno semantic change";
    std::string out = "\nchanged:";
    for (std::size_t index = 0; index < lines.size() && index < 24; ++index) out += "\n  " + lines[index];
    if (lines.size() > 24) out += std::format("\n  … and {} more", lines.size() - 24);
    return out;
}

Answer read_records(const std::vector<Value>& records) {
    Answer a;
    for (const Value& r : records) {
        const std::string_view kind = str(r, "kind");
        if (kind == "data") {
            if (const Value* d = r.get("data")) {
                if (d->map() && d->map()->size() == 1 && d->get("changes"))
                    a.changes = *d->get("changes");
                else
                    a.data = *d;
            }
        } else if (kind == "error") {
            a.code = std::string(str(r, "code"));
            a.message = std::string(str(r, "text"));
            if (const Value* d = r.get("data")) a.detail = *d;
        } else if (kind == "result") {
            a.delivered = true;
            if (const Value* c = r.get("exitCode")) a.exit = static_cast<int>(c->as_int(1));
        }
    }
    return a;
}

}  // namespace

// ── the transport of this program's streams ──────────────────────────────

std::optional<std::string> StdioTransport::read() {
    while (true) {
        if (const auto at = buffer_.find('\n'); at != std::string::npos) {
            std::string line = buffer_.substr(0, at);
            buffer_.erase(0, at + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            return line;
        }
        if (ended_) {
            if (buffer_.empty()) return std::nullopt;
            return std::exchange(buffer_, {});
        }
        auto chunk = os::stdio::read_input();
        if (!chunk || chunk->empty()) ended_ = true;
        else buffer_ += *chunk;
    }
}

void StdioTransport::write(std::string_view line) {
    std::string out(line);
    out += '\n';
    (void)os::stdio::write_output(out);
    (void)os::stdio::flush_output();
}

// ── the server ───────────────────────────────────────────────────────────

struct Server::Impl {
    ServerOptions                          options;
    std::string                            actor;          // kind:name
    bool                                   elicitation = false;
    std::map<std::string, agent::Client>   clients;
    std::int64_t                           next_id = 1;    // the server's own requests
    std::deque<std::string>                later;          // read while waiting for a response
    // The listed operations' tools, and the verb each invokes.
    Value::List                            listed;
    std::map<std::string, std::string>     verbs;
    // The command line's commands as tools (omui_build …): each one's command and effect.
    std::map<std::string, std::pair<std::string, std::string>> commands;

    explicit Impl(ServerOptions o) : options(std::move(o)), actor(options.actor.empty() ? "agent:mcp" : options.actor) {
        std::vector<Value> listings;
        if (options.design)
            if (auto design = parse_json(agent::design::listing)) listings.push_back(std::move(*design));
        if (options.generate)
            if (auto generate = parse_json(agent::generate::listing)) listings.push_back(std::move(*generate));
        // Background tasks (task.*, W8): what a background draft (or any task) is doing, and its cancel.
        if (auto tasks = parse_json(agent::tasks::listing)) listings.push_back(std::move(*tasks));
        listings.insert(listings.end(), options.listings.begin(), options.listings.end());
        for (const Value& listing : listings) {
            const Value* tools = listing.get("tools");
            if (!tools || !tools->list()) continue;
            for (const Value& op : *tools->list()) {
                const std::string verb(str(op, "name")), name = tool_name(verb);
                if (verb.empty() || verbs.contains(name)) continue;
                Value schema = op.get("inputSchema") ? *op.get("inputSchema") : Value{}.set("type", "object");
                Value properties = schema.get("properties") && schema.get("properties")->map() ? *schema.get("properties") : Value(Value::Map{});
                properties.set(app_prop.first, app_prop.second);
                schema.set("properties", std::move(properties)).set("additionalProperties", false);
                std::string description(str(op, "description"));
                description += std::format(" ({} {}: the application's verb {}{})", str(listing, "product"), str(op, "effect"), verb,
                                           str(op, "undo").empty() ? std::string() : std::format("; undo: {}", str(op, "undo")));
                Value t;
                t.set("name", name).set("title", verb).set("description", description).set("inputSchema", std::move(schema));
                t.set("annotations", hints_of(str(op, "effect"), op.get("delegated") && op.get("delegated")->as_bool()));
                listed.push_back(std::move(t));
                verbs.emplace(name, verb);
            }
        }
        list_commands();
    }

    // The command line's commands (no application named: they work on the project the server runs in).
    void list_commands() {
        const Value* tools = options.commands.get("tools");
        if (!tools || !tools->list() || !options.run_command) return;
        for (const Value& op : *tools->list()) {
            const std::string command(str(op, "name")), name = tool_name(command);
            if (command.empty() || commands.contains(name) || verbs.contains(name)) continue;
            if (op.get("terminal") && op.get("terminal")->as_bool()) continue;  // it holds the terminal: a person's command line's
            Value schema = op.get("inputSchema") ? *op.get("inputSchema") : Value{}.set("type", "object");
            schema.set("additionalProperties", false);
            const std::string effect(str(op, "effect"));
            if (effect == "irreversible") {  // the user's answer, for a client that cannot ask them itself
                Value properties = schema.get("properties") && schema.get("properties")->map() ? *schema.get("properties") : Value(Value::Map{});
                properties.set("answer", prop("string", "\"yes\" — only when the user said so, asked by you"));
                schema.set("properties", std::move(properties));
            }
            std::string description(str(op, "description"));
            description += std::format(" (the omui command line, {}{}{})", effect, effect == "irreversible" ? ": the user is asked first" : "",
                                       str(op, "undo").empty() ? std::string() : std::format("; undo: {}", str(op, "undo")));
            Value t;
            t.set("name", name).set("title", command).set("description", description).set("inputSchema", std::move(schema));
            t.set("annotations", hints_of(effect, false));
            listed.push_back(std::move(t));
            commands.emplace(name, std::pair{command, effect});
        }
    }

    // A command run: an irreversible one (it writes something to keep) put to the person first.
    Value run_command(Transport& t, const std::string& command, const std::string& effect, const Value& args) {
        if (effect == "irreversible") {
            const std::string what = std::format("run `{}` {}", command, args.json());
            if (!elicitation)
                if (const Value* answer = args.get("answer"); !answer || answer->as_string() != "yes")
                    return tool_result(std::format("{} writes something to keep — it needs the user's confirmation. Ask the user; only if they agree, call again "
                                                   "with \"answer\": \"yes\". Never answer for them.",
                                                   command),
                                       true, Value{}.set("needs_answer", Value{}.set("question", what).set("answer", "yes")));
            if (elicitation) {
                const std::optional<bool> allowed = ask(t, std::format("{} wants to {}", actor, what));
                if (!allowed) return tool_result(std::format("{} — the user could not be asked; nothing was done", command), true);
                if (!*allowed) return tool_result(std::format("{} — the user declined; nothing was done. Do not try again unless they ask.", command), true);
            }
        }
        Value given = Value(Value::Map{});
        if (args.map())
            for (const auto& [key, value] : *args.map())
                if (key != "answer") given.set(key, value);
        const Value ran = options.run_command(command, given);
        const int exit = static_cast<int>(ran.get("exit") ? ran.get("exit")->as_int() : 1);
        std::string out = std::format("{} exited {}", command, exit);
        if (auto text = str(ran, "out"); !text.empty()) out += std::format("\n{}", text);
        if (auto text = str(ran, "err"); !text.empty()) out += std::format("\n{}", text);
        return tool_result(std::move(out), exit != 0, Value{}.set("data", ran));
    }

    [[nodiscard]] std::filesystem::path directory() const {
        return options.directory.empty() ? agent::default_directory() : options.directory;
    }

    // The endpoints that answer.
    std::vector<agent::EndpointInfo> running() {
        std::vector<agent::EndpointInfo> out;
        for (agent::EndpointInfo& e : agent::list(directory()))
            if (clients.contains(e.name) || agent::Client::connect(e.name, directory())) out.push_back(std::move(e));
        return out;
    }

    // What runs now, said when the client connects (C7; D-186's first level): with one application running, its
    // interactive nodes as an outline and its verbs, so an agent acts on its first turn without asking for the tree
    // (a session's fixed prompt is most of a short task's tokens; the steps are what the interface saves). At most
    // `lines` lines of the outline; the rest is ui_snapshot's.
    std::string at_connect(std::size_t lines = 60) {
        const auto apps = running();
        if (apps.size() != 1) return {};
        const std::string& app = apps.front().name;
        const Answer tree = call(app, Value{}.set("op", "snapshot").set("interactive", true).set("actor", actor), options.timeout);
        const Value* nodes = tree.exit == 0 ? tree.data.get("nodes") : nullptr;
        if (!nodes || !nodes->list() || nodes->list()->empty()) return {};
        const std::string listed = outline(*nodes->list());
        std::size_t end = 0;
        for (std::size_t count = 0; count < lines && end < listed.size(); ++count) {
            const std::size_t line = listed.find('\n', end);
            end = line == std::string::npos ? listed.size() : line + 1;
        }
        std::string out = std::format("\n\n{} ({}) is running. Its interactive nodes now (id role \"name\" = value [states] {{actions}}): act on an id "
                                      "with ui_act, no need to ask for the tree first; what an action changed comes back with it.\n{}",
                                      apps.front().app, apps.front().interface_name, listed.substr(0, end));
        if (end < listed.size()) out += "… the rest: ui_snapshot\n";
        const Answer described = call(app, Value{}.set("op", "describe").set("actor", actor), options.timeout);
        if (const Value* verbs = described.exit == 0 ? described.data.get("verbs") : nullptr; verbs && verbs->list() && !verbs->list()->empty()) {
            out += "Its verbs (ui_invoke):\n";
            for (const Value& verb : *verbs->list()) out += std::format("- {} ({}) — {}\n", str(verb, "name"), str(verb, "effect"), str(verb, "summary"));
        }
        return out;
    }

    // The verbs the running applications offer; none when nothing runs (nothing to tell by).
    std::optional<std::set<std::string>> offered_verbs() {
        const auto apps = running();
        if (apps.empty()) return std::nullopt;
        std::set<std::string> out;
        for (const auto& app : apps) {
            const Answer described = call(app.name, Value{}.set("op", "describe"), options.timeout);
            if (described.exit != 0) return std::nullopt;  // one that cannot say: list everything
            if (const Value* verbs = described.data.get("verbs"); verbs && verbs->list())
                for (const Value& verb : *verbs->list()) out.insert(std::string(str(verb, "name")));
        }
        return out;
    }

    // The application a call means: its `app` (an endpoint's name or an app
    // id), the server's, or the only one running.
    std::expected<std::string, std::string> resolve(const Value& args) {
        std::string wanted(str(args, "app"));
        if (wanted.empty()) wanted = options.app;
        const auto apps = running();
        if (wanted.empty()) {
            if (apps.size() == 1) return apps.front().name;
            if (apps.empty()) return std::unexpected(std::string("no openmui application is running (start one; ui_apps lists them)"));
            std::string names;
            for (const auto& a : apps) names += std::format("{}{}", names.empty() ? "" : ", ", a.name);
            return std::unexpected(std::format("several applications are running ({}): name one as `app`", names));
        }
        for (const auto& a : apps)
            if (a.name == wanted) return a.name;
        for (const auto& a : apps)
            if (a.app == wanted) return a.name;
        return std::unexpected(std::format("no running application is `{}` (ui_apps lists them)", wanted));
    }

    // A request to an application; a broken connection is made again once.
    Answer call(const std::string& app, const Value& request, std::chrono::milliseconds timeout) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto it = clients.find(app);
            if (it == clients.end()) {
                auto c = agent::Client::connect(app, directory());
                if (!c) {
                    Answer a;
                    a.code = "unavailable";
                    a.message = c.error().text();
                    return a;
                }
                it = clients.emplace(app, std::move(*c)).first;
            }
            Answer a = read_records(it->second.call(request, timeout));
            if (a.delivered) return a;
            clients.erase(it);
            if (attempt == 1) return a;
        }
        return {};
    }

    // Asks the person, through the client: true when they allow it.
    std::optional<bool> ask(Transport& t, const std::string& message) {
        const std::int64_t id = next_id++;
        Value allow;
        allow.set("type", "boolean").set("title", "Allow").set("description", "Allow the agent to do this");
        Value schema;
        schema.set("type", "object").set("properties", Value(Value::Map{{"allow", std::move(allow)}})).set("required", Value(Value::List{Value("allow")}));
        Value params;
        params.set("message", message).set("requestedSchema", std::move(schema));
        Value req;
        req.set("jsonrpc", "2.0").set("id", id).set("method", "elicitation/create").set("params", std::move(params));
        t.write(req.json());
        // Until its response: notifications are taken, requests kept for after.
        while (auto line = t.read()) {
            auto msg = parse_json(*line);
            if (!msg) continue;
            const Value* rid = msg->get("id");
            if (!msg->get("method") && rid && rid->as_int(-1) == id) {
                const Value* result = msg->get("result");
                if (!result) return std::nullopt;  // the client could not ask
                if (str(*result, "action") != "accept") return false;
                const Value* content = result->get("content");
                const Value* yes = content ? content->get("allow") : nullptr;
                return yes && yes->as_bool();
            }
            later.push_back(std::move(*line));
        }
        return std::nullopt;
    }

    void journal(std::string_view tool_name, const Value& args, const Value& result) {
        if (!options.journal) return;
        Value e;
        e.set("tool", tool_name).set("arguments", args).set("actor", actor);
        e.set("isError", result.get("isError") ? result.get("isError")->as_bool() : false);
        if (const Value* c = result.get("content"); c && c->list() && !c->list()->empty()) e.set("text", c->list()->front().get("text") ? *c->list()->front().get("text") : Value{});
        options.journal(e);
    }

    // An act or invoke: done, refused, or — irreversible or external — put
    // to the person and done again with their answer.
    Value perform(Transport& t, const std::string& app, Value request, std::string_view what) {
        request.set("actor", actor).set("report", "changes");  // what it changed comes back with it: no snapshot to check (W4)
        Answer a = call(app, request, options.timeout);
        if (a.exit == 2) {
            const Value* answers = a.detail.get("answer");
            const std::string answer = answers && answers->list() && !answers->list()->empty() ? std::string(answers->list()->front().as_string()) : "yes";
            if (!elicitation)
                return tool_result(std::format("{} — this needs the user's confirmation. Ask the user; only if they agree, call again with "
                                               "\"answer\": \"{}\". Never answer for them.",
                                               a.message, answer),
                                   true, Value{}.set("needs_answer", Value{}.set("question", a.message).set("answer", answer)));
            const std::optional<bool> allowed = ask(t, std::format("{} wants to: {}", actor, a.message));
            if (!allowed) return tool_result(std::format("{} — the user could not be asked; nothing was done", a.message), true);
            if (!*allowed) return tool_result(std::format("{} — the user declined; nothing was done. Do not try again unless they ask.", a.message), true);
            request.set("answer", answer).set("actor", std::format("{};via=person:user", actor));
            a = call(app, request, options.timeout);
        }
        if (a.exit == 0) {
            std::string out = std::format("done: {}", what);
            if (!a.data.is_null() && !(a.data.type() == Value::Type::boolean)) out += std::format("\n{}", a.data.json());
            out += changes_text(a.changes);
            return tool_result(std::move(out), false, a.data.is_null() ? Value{} : Value{}.set("data", a.data));
        }
        return tool_result(std::format("{}: {}{}", a.code.empty() ? "failed" : a.code, a.message.empty() ? std::string(what) : a.message,
                                       a.exit == 3 ? " (not ready: wait, then try again)" : ""),
                           true);
    }

    // A dry run (S-13): what it would do, said plainly; nothing is asked or done.
    Value plan(const std::string& app, Value request, std::string_view what) {
        request.set("actor", actor).set("dry_run", true);
        Answer a = call(app, request, options.timeout);
        if (a.exit != 0) return tool_result(std::format("{}: {}", a.code.empty() ? "failed" : a.code, a.message), true);
        return tool_result(std::format("dry run: {} — {}", what, outcome_text(a.data)), false, Value{}.set("data", a.data));
    }

    static std::string outcome_text(const Value& d) {
        const std::string_view outcome = str(d, "outcome");
        std::string out;
        if (outcome == "run") out = "it would run";
        else if (outcome == "needs-answer") out = std::format("it would need the user's confirmation (answer \"{}\")", str(d, "answer"));
        else out = outcome == "refused" ? "it would be refused" : "it would fail";
        if (const Value* e = d.get("error")) out += std::format(": {}", str(*e, "text"));
        if (auto effect = str(d, "effect"); !effect.empty()) out += std::format(" ({})", effect);
        if (const Value* v = d.get("value")) out += std::format("; value {} → {}", v->get("before") ? v->get("before")->json() : "null", v->get("after") ? v->get("after")->json() : "null");
        if (const Value* u = d.get("undoable")) out += u->as_bool() ? "; it can be undone" : "; it cannot be undone";
        for (std::string_view k : {"undo", "redo"})
            if (auto l = str(d, k); !l.empty()) out += std::format("; it would {} {}", k, l);
        if (const Value* st = d.get("steps"); st && st->list())
            for (const Value& x : *st->list()) out += std::format("\n- {} on {}: {}", str(x, "action"), str(x, "id"), outcome_text(x));
        return out;
    }

    // ── the other ways (D-142): the same application, another interface ──

    std::expected<std::string, std::string> only_application() {
        const auto apps = running();
        if (apps.empty()) return std::unexpected(std::string("no application listens (omui agent ls)"));
        return apps.front().name;
    }

    // A person's devices: the events, then two frames to let them land; "done" or why not.
    Value devices(const std::string& app, Value::List events) {
        events.push_back(Value{}.set("type", "wait").set("frames", 2));
        const auto count = static_cast<std::int64_t>(events.size());
        Value req;
        req.set("op", "input").set("events", Value(std::move(events))).set("actor", actor);
        const Answer a = call(app, req, std::chrono::milliseconds(count * 2000) + options.timeout);
        return tool_result(a.exit == 0 ? std::string("done") : std::format("{}: {}", a.code.empty() ? "failed" : a.code, a.message), a.exit != 0);
    }

    // The window's accessibility tree: what a platform tree carries of a node (ref, role, name, value, states,
    // bounds), nested; its actions and their effects, and the application's verbs, are what the semantic way adds.
    Value accessibility_tree(const std::string& app) {
        const Answer a = call(app, Value{}.set("op", "snapshot"), options.timeout);
        if (a.exit != 0) return tool_result(std::format("{}: {}", a.code.empty() ? "failed" : a.code, a.message), true);
        const Value* listed = a.data.get("nodes");
        if (!listed || !listed->list()) return tool_result("[]");
        std::map<std::string_view, const Value*> by_id;
        std::set<std::string_view> children;
        for (const Value& node : *listed->list()) {
            by_id.emplace(str(node, "id"), &node);
            if (const Value* kids = node.get("children"); kids && kids->list())
                for (const Value& kid : *kids->list()) children.insert(kid.as_string());
        }
        std::set<std::string_view> seen;
        const std::function<Value(const Value&)> kept = [&](const Value& node) {
            seen.insert(str(node, "id"));
            Value out;
            out.set("ref", str(node, "id"));
            const auto present = [](const Value* value) {
                return value && !value->is_null() && !(value->type() == Value::Type::text && value->as_string().empty()) &&
                       !(value->list() && value->list()->empty()) && !(value->map() && value->map()->empty());
            };
            for (const auto& [from, to] : {std::pair<std::string_view, std::string_view>{"role", "role"}, {"name", "name"}, {"value", "value"},
                                           {"state", "states"}, {"bounds", "bounds"}})
                if (const Value* value = node.get(from); present(value)) out.set(to, *value);
            Value::List kids;
            if (const Value* listed_kids = node.get("children"); listed_kids && listed_kids->list())
                for (const Value& kid : *listed_kids->list())
                    if (const auto found = by_id.find(kid.as_string()); found != by_id.end() && !seen.contains(found->first)) kids.push_back(kept(*found->second));
            if (!kids.empty()) out.set("children", Value(std::move(kids)));
            return out;
        };
        Value::List roots;
        for (const Value& node : *listed->list())
            if (!children.contains(str(node, "id")) && !seen.contains(str(node, "id"))) roots.push_back(kept(node));
        return tool_result(Value(std::move(roots)).json());
    }

    Value run_way_tool(std::string_view name, const Value& args) {
        std::string app = options.app;
        if (app.empty()) {
            auto only = only_application();
            if (!only) return tool_result(only.error(), true);
            app = std::move(*only);
        }
        const auto number = [&](std::string_view key) -> std::optional<double> {
            const Value* value = args.get(key);
            if (!value || (value->type() != Value::Type::real && value->type() != Value::Type::integer)) return std::nullopt;
            return value->as_double();
        };
        const auto needs = [&](std::initializer_list<std::string_view> keys) -> std::optional<Value> {
            for (std::string_view key : keys)
                if (!number(key)) return tool_result(std::format("{} needs `{}` (a number)", name, key), true);
            return std::nullopt;
        };
        const auto at = [&](std::string_view type, std::string_view x, std::string_view y) {
            return Value{}.set("type", type).set("x", *number(x)).set("y", *number(y));
        };
        if (name == "screenshot") {
            const Answer a = call(app, Value{}.set("op", "capture"), options.timeout);
            if (a.exit != 0 || str(a.data, "base64").empty()) return tool_result("no picture", true);
            return tool_result("the window", false, {}, Value::List{Value{}.set("type", "image").set("data", str(a.data, "base64")).set("mimeType", "image/png")});
        }
        if (name == "accessibility_tree") return accessibility_tree(app);
        if (name == "click") {
            if (auto missing = needs({"x", "y"})) return *missing;
            Value event = at("click", "x", "y");
            if (auto button = str(args, "button"); !button.empty()) event.set("button", button);
            return devices(app, {std::move(event)});
        }
        if (name == "double_click") {
            if (auto missing = needs({"x", "y"})) return *missing;
            return devices(app, {at("click", "x", "y"), at("click", "x", "y")});
        }
        if (name == "drag") {
            if (auto missing = needs({"from_x", "from_y", "to_x", "to_y"})) return *missing;
            Value event;
            event.set("type", "drag").set("from", Value(Value::List{Value(*number("from_x")), Value(*number("from_y"))}))
                .set("to", Value(Value::List{Value(*number("to_x")), Value(*number("to_y"))}));
            return devices(app, {std::move(event)});
        }
        if (name == "type") {
            const Value* typed = args.get("text");
            if (!typed || typed->type() != Value::Type::text) return tool_result("type needs `text`", true);
            return devices(app, {Value{}.set("type", "text").set("text", *typed)});
        }
        if (name == "key") {
            if (str(args, "key").empty()) return tool_result("key needs `key`", true);
            return devices(app, {Value{}.set("type", "key").set("key", str(args, "key"))});
        }
        if (name == "scroll") {
            if (auto missing = needs({"x", "y", "dy"})) return *missing;
            return devices(app, {at("wheel", "x", "y").set("dy", *number("dy"))});
        }
        if (name == "click_ref") {
            if (str(args, "ref").empty()) return tool_result("click_ref needs `ref`", true);
            return devices(app, {Value{}.set("type", "click").set("node", str(args, "ref"))});
        }
        return tool_result(std::format("no tool `{}`", name), true);
    }

    // A ui_* tool is one of the protocol's operations (omui.agent's catalog): its arguments are the request's, the
    // application named by `app` (resolved apart). What changes something goes through the person's decision
    // (perform) and a dry run is said plainly (plan); every answer is shown as an agent reads it best.
    Value run_operation(Transport& t, const Operation& entry, const Value& args, const std::string& app) {
        const std::string_view op = entry.operation.name.substr(3);  // ui.act → act, as the protocol has it
        Value request;
        request.set("op", op);
        if (args.map())
            for (const auto& [key, value] : *args.map())
                if (key != "app" && key != "dry_run" && !(op == "input" && key == "capture")) request.set(key, value);
        const bool dry = args.get("dry_run") && args.get("dry_run")->as_bool();
        const std::string what = op == "act" ? std::format("{} on {}", str(args, "action"), str(args, "node"))
                               : op == "invoke" ? std::string(str(args, "verb"))
                               : op == "transaction" && args.get("steps") && args.get("steps")->list() ? std::format("{} steps", args.get("steps")->list()->size())
                                                                                                         : std::string(op);
        if (dry) return plan(app, std::move(request), what);
        if (op == "act" || op == "invoke") return perform(t, app, std::move(request), what);
        request.set("actor", actor);
        std::chrono::milliseconds budget = options.timeout;
        if (op == "wait") {  // waiting is the application's: the answer may take that long
            const double seconds = args.get("timeout") ? args.get("timeout")->as_double(5) : 5.0;
            request.set("timeout", seconds);
            budget += std::chrono::milliseconds(static_cast<std::int64_t>(seconds * 1000));
        }
        if (op == "input" && args.get("events") && args.get("events")->list())
            budget += std::chrono::milliseconds(static_cast<std::int64_t>(args.get("events")->list()->size()) * 2000);
        const Answer a = call(app, request, budget);
        if (a.exit != 0) {
            // What stays of a transaction is the answer's to say (a step that gave no way back stays done); else nothing does.
            const bool nothing_stays = op == "transaction" && a.message.find("stay done") == std::string::npos;
            return tool_result(std::format("{}: {}{}", a.code.empty() ? "failed" : a.code, a.message, nothing_stays ? " — nothing of it stays" : ""), true);
        }
        return shown(op, args, app, a);
    }

    // An answer as an agent reads it best: the tree as an outline, pictures as images, what changed a line each.
    Value shown(std::string_view op, const Value& args, const std::string& app, const Answer& a) {
        const Value data_of = Value{}.set("data", a.data);
        const auto pictures_of = [&](const Value& holder) {
            Value::List pictures;
            for (std::string_view when : {"before", "after"})
                if (const Value* picture = holder.get(when); picture && picture->get("base64"))
                    pictures.push_back(Value{}.set("type", "image").set("data", str(*picture, "base64")).set("mimeType", "image/png"));
            return pictures;
        };
        const auto changes_lines = [&](const Value* changes, bool with_fields) {
            std::string text;
            if (changes)
                for (std::string_view kind : {"changed", "added", "removed"})
                    if (const Value* listed_changes = changes->get(kind); listed_changes && listed_changes->list())
                        for (const Value& one : *listed_changes->list()) {
                            text += std::format("\n  {} {}", kind, str(one, "id"));
                            if (with_fields) text += std::format(": {}", (one.get("fields") ? *one.get("fields") : one.get("now") ? *one.get("now") : *one.get("was")).json());
                        }
            return text;
        };
        if (op == "snapshot" || op == "find") {
            const Value* nodes = a.data.get("nodes");
            const Value::List none;
            const Value::List& list = nodes && nodes->list() ? *nodes->list() : none;
            if (op == "snapshot") return tool_result(std::format("{}: {} nodes\n{}", app, list.size(), outline(list)), false, a.data);
            return tool_result(list.empty() ? std::string("no node matches") : std::format("{} found\n{}", list.size(), outline(list)), false, a.data);
        }
        if (op == "describe") {  // the application's verbs, for ui_invoke
            const Value* verbs_listed = a.data.get("verbs");
            std::string out = std::format("{}: {}, verbs\n", app, str(a.data, "interface"));
            Value::List list;
            if (verbs_listed && verbs_listed->list())
                for (const Value& v : *verbs_listed->list()) {
                    std::string takes;
                    if (const Value* given = v.get("args"); given && given->map())
                        for (const auto& [key, type] : *given->map()) takes += std::format("{}{}: {}", takes.empty() ? "" : ", ", key, type.as_string());
                    out += std::format("- {} ({}{}){}{} — {}\n", str(v, "name"), str(v, "effect"), v.get("answer") ? std::format(", answer \"{}\"", str(v, "answer")) : std::string(),
                                       takes.empty() ? "" : std::format(" {{{}}}", takes), v.get("person") ? " [person only]" : "", str(v, "summary"));
                    list.push_back(v);
                }
            if (list.empty()) out += "(none)\n";
            return tool_result(std::move(out), false, Value{}.set("verbs", Value(std::move(list))));
        }
        if (op == "undo" || op == "redo") return tool_result(std::format("{}: {}", op == "redo" ? "redone" : "undone", str(a.data, op == "redo" ? "redone" : "undone")), false, data_of);
        if (op == "transaction") {
            const std::size_t steps = args.get("steps") && args.get("steps")->list() ? args.get("steps")->list()->size() : 0;
            return tool_result(std::format("done: {} steps as one step ({})", steps, str(a.data, "committed")), false, data_of);
        }
        if (op == "wait") return tool_result("the condition holds", false, a.data);
        if (op == "preview") {
            // Shown before it is allowed (Part 2 §4.1, A): for the person to look at before saying yes to ui_act / ui_transaction.
            std::string text = "preview (nothing was done";
            const Value* restored = a.data.get("restored");
            text += restored && restored->as_bool() ? ", the interface is as it was):" : ", BUT the interface is not as it was):";
            text += changes_lines(a.data.get("changes"), true);
            if (const Value* not_done = a.data.get("not_done"); not_done && not_done->list())
                for (const Value& one : *not_done->list()) text += std::format("\n  not done (asks the person): {} on {} ({})", str(one, "action"), str(one, "id"), str(one, "effect"));
            Value::List pictures = pictures_of(a.data);
            if (pictures.size() == 2) text += "\n(the first picture is before, the second after)";
            return tool_result(text, false, {}, std::move(pictures));
        }
        if (op == "record") {  // a recording a fresh instance replays to the same bytes (W12)
            const std::size_t frames = a.data.get("frames") && a.data.get("frames")->list() ? a.data.get("frames")->list()->size() : 0;
            const Value expected = a.data.get("expect") ? *a.data.get("expect") : Value{};
            return tool_result(std::format("a recording of {} frames, ending as tree {} and pixels {}; save the JSON below and run `omui replay FILE` "
                                           "to play it in a fresh instance and check it ends the same\n{}",
                                           frames, str(expected, "tree"), str(expected, "pixels"), a.data.json()),
                               false, data_of);
        }
        if (op == "reload") {
            // Agent hot reload (W2): the semantic changes, where the pixels changed (that region before and after) and the
            // motion it started, sampled — no screenshot to compare.
            std::string reloaded;
            if (const Value* list = a.data.get("reloaded"); list && list->list())
                for (const Value& path : *list->list()) reloaded += std::format("{}{}", reloaded.empty() ? "" : ", ", path.as_string());
            std::string text = reloaded.empty() ? std::string("nothing changed on disk")
                                                : std::format("reloaded {} in {:.1f} ms", reloaded, a.data.get("took_ms") ? a.data.get("took_ms")->as_double() : 0.0);
            text += changes_lines(a.data.get("changes"), false);
            Value::List pictures;
            if (const Value* pixels = a.data.get("pixels")) {
                const Value* region = pixels->get("changed");
                text += region && !region->is_null() ? std::format("\npixels changed in {}", region->json()) : std::string("\nno pixel changed");
                pictures = pictures_of(*pixels);
                if (pictures.size() == 2) text += " (the first picture is that region before, the second after)";
            }
            if (const Value* motion = a.data.get("motion"); motion && motion->get("samples") && motion->get("samples")->list()) {
                std::size_t moving = 0;
                for (const Value& sample : *motion->get("samples")->list())
                    if (sample.get("changed") && !sample.get("changed")->is_null()) ++moving;
                text += std::format("\nmotion: {} samples every {} s, {} moving; {}", motion->get("samples")->list()->size(), motion->get("every") ? motion->get("every")->as_double() : 0.0,
                                    moving, motion->get("settled") && motion->get("settled")->as_bool() ? "settled" : "still moving at the last");
            }
            return tool_result(text, false, data_of, std::move(pictures));
        }
        if (op == "intend") {
            const bool said = a.data.get("said") && a.data.get("said")->as_bool();
            return tool_result(!said                                                              ? "withdrawn"
                               : (a.data.get("hands_back") && a.data.get("hands_back")->as_bool()) ? "handed back: the person sees your summary"
                                                                                                   : "said: the person sees it",
                               false, data_of);
        }
        if (op == "drew") {
            const std::string at = args.get("at") ? args.get("at")->json() : std::string("?");
            if (a.data.get("node") && a.data.get("node")->is_null()) return tool_result(std::format("{}: {}", at, str(a.data, "why")), false, data_of);
            std::string text = std::format("{} drew {}", str(a.data, "node"), at);
            if (const Value* chain = a.data.get("chain"); chain && chain->list())
                for (const Value& one : *chain->list())
                    text += std::format("\n  {}  built at {}", str(one, "id"), one.get("source") && !one.get("source")->is_null() ? one.get("source")->as_string() : std::string_view("?"));
            return tool_result(text, false, data_of);
        }
        if (op == "timeline" || op == "explain") {
            if (op == "explain" && a.data.get("found") && !a.data.get("found")->as_bool()) return tool_result(std::string(str(a.data, "why")), false, data_of);
            std::string text = op == "timeline" ? std::format("recorder: {}", str(a.data, "level")) : std::string(str(a.data, "summary"));
            if (const Value* list = a.data.get("records"); list && list->list())
                for (const Value& record : *list->list()) {
                    const std::string node = str(record, "node").empty() ? std::string() : std::string(str(record, "node")) + ": ";
                    text += op == "timeline" ? std::format("\n  #{} {} {}{}", record.get("cause") ? record.get("cause")->as_int() : 0, str(record, "kind"), node, str(record, "text"))
                                             : std::format("\n  {} {}{}", str(record, "kind"), node, str(record, "text"));
                }
            return tool_result(text, false, data_of);
        }
        if (op == "capture") {
            if (str(args, "format") == "svg") {
                // Vectors: exact geometry, colours and text, and which node drew what; what SVG cannot say is an island of pixels.
                const Value* islands = a.data.get("islands");
                return tool_result(std::format("{}\n({} island(s) kept as pixels)", str(a.data, "svg"), islands && islands->list() ? islands->list()->size() : 0), false, data_of);
            }
            const Value* bounds = a.data.get("bounds");
            return tool_result(std::format("{} at {}", str(args, "node").empty() ? std::string_view("the window") : str(args, "node"), bounds ? bounds->json() : std::string("?")), false, {},
                               Value::List{Value{}.set("type", "image").set("data", str(a.data, "base64")).set("mimeType", "image/png")});
        }
        if (op == "input") {
            std::string out = std::format("{} frames", a.data.get("frames") ? a.data.get("frames")->as_int() : 0);
            if (const Value* pressed = a.data.get("pressed"); pressed && pressed->list())
                for (const Value& hit : *pressed->list()) {
                    const Value* at = hit.get("at");
                    const std::string where = at && at->list() && at->list()->size() == 2 ? std::format("{:.0f},{:.0f}", (*at->list())[0].as_double(), (*at->list())[1].as_double()) : std::string("?");
                    if (str(hit, "node").empty()) out += std::format("\npressed at {}: nothing there", where);
                    else
                        out += std::format("\npressed at {}: {} {}{}", where, str(hit, "semantic").empty() ? str(hit, "node") : str(hit, "semantic"), str(hit, "role"),
                                           str(hit, "name").empty() ? std::string() : std::format(" \"{}\"", str(hit, "name")));
                }
            const Value* acts = a.data.get("actions");
            if (!acts || !acts->list() || acts->list()->empty())
                out += "\nno action was journaled (input a component takes itself — keys a game reads, a pointer's moves — journals none; look at the picture or the tree)";
            else
                for (const Value& action : *acts->list())
                    out += std::format("\naction: {} on {} by {}{}", str(action, "action"), str(action, "node"), str(action, "actor"),
                                       action.get("exitCode") && action.get("exitCode")->as_int() != 0 ? std::format(" (exit {})", action.get("exitCode")->as_int()) : "");
            if (auto focused = str(a.data, "focused"); !focused.empty()) out += std::format("\nfocused: {}", focused);
            Value::List content;
            if (const Value* capture = args.get("capture"); capture && capture->as_bool()) {
                const Answer picture = call(app, Value{}.set("op", "capture"), options.timeout);
                if (picture.exit == 0) content.push_back(Value{}.set("type", "image").set("data", str(picture.data, "base64")).set("mimeType", "image/png"));
            }
            return tool_result(std::move(out), false, data_of, std::move(content));
        }
        return tool_result(a.data.json(), false, data_of);  // clock and the rest: the data as it is
    }

    Value run_tool(std::string_view name, const Value& args, Transport& t) {
        if (options.way != Way::semantic) return run_way_tool(name, args);
        if (const auto command = commands.find(std::string(name)); command != commands.end())
            return run_command(t, command->second.first, command->second.second, args);
        if (name == "ui_apps") {
            const auto apps = running();
            if (apps.empty()) return tool_result("no openmui application is running");
            std::string out;
            Value::List list;
            for (const auto& a : apps) {
                out += std::format("- {} (app {}, {})\n", a.name, a.app, a.interface_name);
                list.push_back(Value{}.set("name", a.name).set("app", a.app).set("interface", a.interface_name));
            }
            return tool_result(std::move(out), false, Value{}.set("apps", Value(std::move(list))));
        }
        auto app = resolve(args);
        if (!app) return tool_result(app.error(), true);
        if (const auto operation = protocol_operation(name)) return run_operation(t, *operation, args, *app);
        if (const auto listed_verb = verbs.find(std::string(name)); listed_verb != verbs.end()) {
            Value given = Value(Value::Map{});
            if (args.map())
                for (const auto& [key, value] : *args.map())
                    if (key != "app") given.set(key, value);
            Value req;
            req.set("op", "invoke").set("verb", listed_verb->second).set("args", std::move(given));
            return perform(t, *app, std::move(req), listed_verb->second);
        }
        return tool_result(std::format("no tool `{}`", name), true);
    }
};

std::optional<Way> way_named(std::string_view name) {
    if (name == "semantic") return Way::semantic;
    if (name == "a11y-tree") return Way::accessibility_tree;
    if (name == "screenshot") return Way::screenshot;
    return std::nullopt;
}

namespace {

// What the other ways offer, worded as they word it: the devices at coordinates, the picture; the tree by ref.
Value::List way_tools(Way way) {
    const auto n = [](std::string_view description) { return prop("number", description); };
    const auto devices = [](Value tool_value) {
        Value hints;
        hints.set("readOnlyHint", false).set("destructiveHint", false).set("idempotentHint", false).set("openWorldHint", false);
        return tool_value.set("annotations", std::move(hints));
    };
    const auto offered = [](std::string_view name, std::string_view description, Value schema) {
        Value out;
        out.set("name", name).set("description", description).set("inputSchema", std::move(schema));
        return out;
    };
    Value::List out{
        offered("screenshot", "A picture of the application's window (PNG).", object_schema({})).set("annotations", hints_of("read", false)),
        devices(offered("click", "Clicks at a point of the window, in its pixels.",
                        object_schema({{"x", n("x")}, {"y", n("y")}, {"button", prop("string", "left, right or middle")}}, {"x", "y"}))),
        devices(offered("double_click", "Clicks twice at a point of the window.", object_schema({{"x", n("x")}, {"y", n("y")}}, {"x", "y"}))),
        devices(offered("drag", "Presses at one point, moves to another, releases.",
                        object_schema({{"from_x", n("from x")}, {"from_y", n("from y")}, {"to_x", n("to x")}, {"to_y", n("to y")}},
                                      {"from_x", "from_y", "to_x", "to_y"}))),
        devices(offered("type", "Types text where the keyboard focus is.", object_schema({{"text", prop("string", "the text")}}, {"text"}))),
        devices(offered("key", "Presses a key (Enter, Escape, Tab, Backspace, ArrowDown, Ctrl+A …).", object_schema({{"key", prop("string", "the key")}}, {"key"}))),
        devices(offered("scroll", "Turns the wheel at a point (dy < 0 scrolls up).", object_schema({{"x", n("x")}, {"y", n("y")}, {"dy", n("how far")}}, {"x", "y", "dy"}))),
    };
    if (way == Way::accessibility_tree) {
        out.push_back(offered("accessibility_tree", "The window's accessibility tree: each node's ref, role, name, value, states and bounds (x, y, width, height), nested.",
                              object_schema({}))
                          .set("annotations", hints_of("read", false)));
        out.push_back(devices(offered("click_ref", "Clicks the middle of the node with this ref.", object_schema({{"ref", prop("string", "the node's ref")}}, {"ref"}))));
    }
    return out;
}

}  // namespace

Server::Server(ServerOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
Server::~Server() = default;

Value::List Server::tools() const {
    if (impl_->options.way != Way::semantic) return way_tools(impl_->options.way);
    // ui_apps is the server's own; the protocol's operations are their catalog's, schema and all (D-145: what a tool
    // is for in depth is describe's and the skill's), each with the application it is asked of.
    Value::List out{tool("ui_apps", "Running applications", "The openmui applications running here.", object_schema({}))};
    static const auto protocol = parse_json(agent::listing);
    if (protocol && protocol->get("tools") && protocol->get("tools")->list())
        for (const Value& listed_operation : *protocol->get("tools")->list()) {
            const std::string_view operation = str(listed_operation, "name");
            if (!std::ranges::contains(offered_operations, operation)) continue;
            Value schema = listed_operation.get("inputSchema") ? *listed_operation.get("inputSchema") : Value{}.set("type", "object");
            Value properties = schema.get("properties") && schema.get("properties")->map() ? *schema.get("properties") : Value(Value::Map{});
            properties.set(app_prop.first, app_prop.second);
            if (operation == "ui.input") properties.set("capture", prop("boolean", "the window's picture after (this server's)"));
            schema.set("properties", std::move(properties)).set("additionalProperties", false);
            out.push_back(tool(tool_name(operation), operation, str(listed_operation, "description"), std::move(schema), operation));
        }
    out.insert(out.end(), impl_->listed.begin(), impl_->listed.end());
    return out;
}

Value::List Server::offered_tools() const {
    Value::List out = tools();
    const auto offered = impl_->offered_verbs();
    if (!offered) return out;
    std::erase_if(out, [&](const Value& listed) {
        const auto verb = impl_->verbs.find(std::string(str(listed, "name")));
        return verb != impl_->verbs.end() && !offered->contains(verb->second);
    });
    return out;
}

void Server::serve(Transport& transport) {
    while (true) {
        std::string line;
        if (!impl_->later.empty()) {
            line = std::move(impl_->later.front());
            impl_->later.pop_front();
        } else if (auto next = transport.read()) {
            line = std::move(*next);
        } else {
            return;
        }
        handle(line, transport);
    }
}

void Server::handle(std::string_view line, Transport& t) {
    Impl& d = *impl_;
    auto msg = parse_json(line);
    if (!msg) {
        t.write(failure(Value{}, parse_error, std::format("not JSON: {}", msg.error().text())).json());
        return;
    }
    if (!msg->map()) {
        t.write(failure(Value{}, invalid_request, "a message is a JSON object").json());
        return;
    }
    const Value* id = msg->get("id");
    const std::string_view method = str(*msg, "method");
    if (method.empty()) return;  // a response to nothing pending, or a stray one: nothing to do
    if (!id) return;             // a notification (initialized, cancelled): nothing to answer
    const Value params = msg->get("params") ? *msg->get("params") : Value(Value::Map{});
    if (method == "initialize") {
        const std::string_view asked = str(params, "protocolVersion");
        std::string_view version = protocol_versions.front();
        for (std::string_view v : protocol_versions)
            if (v == asked) version = v;
        if (const Value* caps = params.get("capabilities")) d.elicitation = caps->get("elicitation") != nullptr;
        if (d.options.actor.empty())
            if (const Value* info = params.get("clientInfo")) d.actor = std::format("agent:{}", actor_name(str(*info, "name")));
        Value caps;
        caps.set("tools", Value{}.set("listChanged", false));
        Value info;
        info.set("name", "openmui").set("title", "openmui applications").set("version", "0.1.0");
        Value r;
        r.set("protocolVersion", version).set("capabilities", std::move(caps)).set("serverInfo", std::move(info));
        if (d.options.way != Way::semantic) {
            // Another way's arm says only what that way's own server says: nothing about the application.
            t.write(response(*id, std::move(r)).json());
            return;
        }
        r.set("instructions",
              std::string("Operate running openmui applications through their semantic trees: ui_apps to see what runs, ui_snapshot or ui_find "
              "to read, ui_describe for the application's verbs, ui_act or ui_invoke to change, ui_wait to let the interface settle, ui_capture to see pixels, ui_input to point, "
              "click, drag, scroll and type as a person does (when the application allows it), ui_clock for its time. Ids are stable "
              "paths. Actions marked irreversible or external are confirmed by the user; never answer for them. Actions marked person are "
              "the user's own and are refused to agents.") +
                  d.at_connect());
        t.write(response(*id, std::move(r)).json());
        return;
    }
    if (method == "ping") {
        t.write(response(*id, Value(Value::Map{})).json());
        return;
    }
    if (method == "tools/list") {
        t.write(response(*id, Value{}.set("tools", Value(offered_tools()))).json());
        return;
    }
    if (method == "tools/call") {
        const std::string name(str(params, "name"));
        const Value args = params.get("arguments") && params.get("arguments")->map() ? *params.get("arguments") : Value(Value::Map{});
        const bool known = std::ranges::any_of(tools(), [&](const Value& tl) { return str(tl, "name") == name; });
        if (!known) {
            t.write(failure(*id, invalid_params, std::format("no tool `{}`", name)).json());
            return;
        }
        Value result = d.run_tool(name, args, t);
        d.journal(name, args, result);
        t.write(response(*id, std::move(result)).json());
        return;
    }
    t.write(failure(*id, method_not_found, std::format("no method `{}`", method)).json());
}

std::expected<Value, std::string> read_listing(const std::filesystem::path& file) {
    auto text = os::fs::read(file);
    if (!text) return std::unexpected(std::format("cannot read {}: {}", utf8(file), text.error().message()));
    auto listing = parse_json(*text);
    if (!listing || str(*listing, "kind") != "omui.ops" || !listing->get("tools") || !listing->get("tools")->list())
        return std::unexpected(std::format("{} is not an operation listing (ops-gen makes <product>.ops.json)", utf8(file)));
    for (const Value& op : *listing->get("tools")->list())
        if (str(op, "name").starts_with("ui."))
            return std::unexpected(std::format("{}: the protocol's ui.* operations are the ui_* tools already", utf8(file)));
    return std::move(*listing);
}

// ── the outline ──────────────────────────────────────────────────────────

std::string outline(const Value::List& nodes) {
    std::map<std::string_view, const Value*> by_id;
    std::set<std::string_view> children;
    for (const Value& n : nodes) {
        by_id.emplace(str(n, "id"), &n);
        if (const Value* c = n.get("children"); c && c->list())
            for (const Value& k : *c->list()) children.insert(k.as_string());
    }
    std::string out;
    std::set<std::string_view> done;
    std::function<void(const Value&, int)> line = [&](const Value& n, int depth) {
        const std::string_view id = str(n, "id");
        if (!done.insert(id).second) return;
        std::string l(static_cast<std::size_t>(depth) * 2, ' ');
        l += std::format("{} {}", id, str(n, "role"));
        if (auto name = str(n, "name"); !name.empty()) l += std::format(" \"{}\"", name);
        if (const Value* v = n.get("value"); v && !v->is_null()) l += std::format(" = {}", v->json());
        if (const Value* s = n.get("state"); s && s->list() && !s->list()->empty()) {
            std::string states;
            for (const Value& x : *s->list()) states += std::format("{}{}", states.empty() ? "" : " ", x.as_string());
            l += std::format(" [{}]", states);
        }
        if (const Value* a = n.get("actions"); a && a->list() && !a->list()->empty()) {
            std::string acts;
            for (const Value& x : *a->list()) {
                acts += std::format("{}{}", acts.empty() ? "" : ", ", str(x, "name"));
                if (auto effect = str(x, "effect"); effect == "irreversible" || effect == "external") acts += std::format(" ({})", effect);
                if (const Value* p = x.get("person"); p && p->as_bool()) acts += " (person)";
            }
            l += std::format(" {{{}}}", acts);
        }
        if (const Value* b = n.get("bounds"); b && b->list() && b->list()->size() == 4) {
            const auto& r = *b->list();
            l += std::format(" @{},{} {}x{}", std::lround(r[0].as_double()), std::lround(r[1].as_double()), std::lround(r[2].as_double()),
                             std::lround(r[3].as_double()));
        }
        out += l;
        out += '\n';
        if (const Value* c = n.get("children"); c && c->list()) {
            for (const Value& k : *c->list())
                if (auto it = by_id.find(k.as_string()); it != by_id.end()) line(*it->second, depth + 1);
            if (const Value* total = n.get("children_total"); total && total->as_int() > static_cast<std::int64_t>(c->list()->size()))
                out += std::format("{}… {} more children (ui_snapshot with root {} and offset)\n", std::string(static_cast<std::size_t>(depth + 1) * 2, ' '),
                                   total->as_int() - static_cast<std::int64_t>(c->list()->size()), id);
        }
    };
    for (const Value& n : nodes)
        if (!children.contains(str(n, "id"))) line(n, 0);
    for (const Value& n : nodes) line(n, 0);  // anything a cycle or a page left out
    return out;
}

}  // namespace omui::mcp
