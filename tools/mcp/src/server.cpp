module openmui.mcp;

import std;
import mcpplibs.os;
import openmui.core;
import openmui.agent;

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

const std::pair<std::string, Value> app_prop{"app", prop("string", "The application's endpoint name or app id (ui_apps); none: the one running")};

Value tool(std::string_view name, std::string_view title, std::string_view description, Value schema, bool read_only, bool destructive) {
    Value hints;
    hints.set("readOnlyHint", read_only).set("destructiveHint", destructive).set("idempotentHint", read_only).set("openWorldHint", false);
    Value t;
    t.set("name", name).set("title", title).set("description", description).set("inputSchema", std::move(schema)).set("annotations", std::move(hints));
    return t;
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
    bool        delivered = false;  // a result came back
};

Answer read_records(const std::vector<Value>& records) {
    Answer a;
    for (const Value& r : records) {
        const std::string_view kind = str(r, "kind");
        if (kind == "data") {
            if (const Value* d = r.get("data")) a.data = *d;
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

    explicit Impl(ServerOptions o) : options(std::move(o)), actor(options.actor.empty() ? "agent:mcp" : options.actor) {}

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
                    a.message = c.error();
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
        request.set("actor", actor);
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

    Value run_tool(std::string_view name, const Value& args, Transport& t) {
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
        if (name == "ui_snapshot") {
            Value req;
            req.set("op", "snapshot");
            if (auto r = str(args, "root"); !r.empty()) req.set("root", r);
            for (std::string_view k : {"depth", "offset", "limit"})
                if (const Value* v = args.get(k)) req.set(k, v->as_int());
            if (const Value* v = args.get("interactive")) req.set("interactive", v->as_bool());
            Answer a = call(*app, req, options.timeout);
            if (a.exit != 0) return tool_result(std::format("{}: {}", a.code, a.message), true);
            const Value* nodes = a.data.get("nodes");
            const Value::List none;
            const Value::List& list = nodes && nodes->list() ? *nodes->list() : none;
            return tool_result(std::format("{}: {} nodes\n{}", *app, list.size(), outline(list)), false, a.data);
        }
        if (name == "ui_find") {
            Value q = Value(Value::Map{});
            if (const Value* raw = args.get("query"); raw && raw->map()) q = *raw;
            for (std::string_view k : {"role", "name", "id", "text"})
                if (auto v = str(args, k); !v.empty()) q.set(k, v);
            if (const Value* s = args.get("state")) q.set("state", s->list() ? *s : Value(Value::List{*s}));
            if (const Value* l = args.get("limit")) q.set("limit", l->as_int());
            Answer a = call(*app, Value{}.set("op", "find").set("q", q), options.timeout);
            if (a.exit != 0) return tool_result(std::format("{}: {}", a.code, a.message), true);
            const Value* nodes = a.data.get("nodes");
            const Value::List none;
            const Value::List& list = nodes && nodes->list() ? *nodes->list() : none;
            return tool_result(list.empty() ? std::string("no node matches") : std::format("{} found\n{}", list.size(), outline(list)), false, a.data);
        }
        if (name == "ui_act") {
            const std::string_view node = str(args, "node"), action = str(args, "action");
            if (node.empty() || action.empty()) return tool_result("ui_act needs `node` and `action`", true);
            Value req;
            req.set("op", "act").set("node", node).set("action", action);
            if (const Value* v = args.get("args")) req.set("args", *v);
            if (auto v = str(args, "answer"); !v.empty()) req.set("answer", v);
            if (const Value* d = args.get("dry_run"); d && d->as_bool()) return plan(*app, std::move(req), std::format("{} on {}", action, node));
            return perform(t, *app, std::move(req), std::format("{} on {}", action, node));
        }
        if (name == "ui_invoke") {
            const std::string_view verb = str(args, "verb");
            if (verb.empty()) return tool_result("ui_invoke needs `verb`", true);
            Value req;
            req.set("op", "invoke").set("verb", verb);
            if (const Value* v = args.get("args")) req.set("args", *v);
            if (auto v = str(args, "answer"); !v.empty()) req.set("answer", v);
            if (const Value* d = args.get("dry_run"); d && d->as_bool()) return plan(*app, std::move(req), verb);
            return perform(t, *app, std::move(req), verb);
        }
        if (name == "ui_undo") {
            const bool again = args.get("redo") && args.get("redo")->as_bool();
            Value req;
            req.set("op", again ? "redo" : "undo");
            if (auto v = str(args, "scope"); !v.empty()) req.set("scope", v);
            if (const Value* d = args.get("dry_run"); d && d->as_bool()) return plan(*app, std::move(req), again ? "redo" : "undo");
            req.set("actor", actor);
            Answer a = call(*app, req, options.timeout);
            if (a.exit != 0) return tool_result(std::format("{}: {}", a.code.empty() ? "failed" : a.code, a.message), true);
            return tool_result(std::format("{}: {}", again ? "redone" : "undone", str(a.data, again ? "redone" : "undone")), false, Value{}.set("data", a.data));
        }
        if (name == "ui_transaction") {
            const Value* steps = args.get("steps");
            if (!steps || !steps->list() || steps->list()->empty()) return tool_result("ui_transaction needs `steps`: [{node, action, args}]", true);
            Value req;
            req.set("op", "transaction").set("steps", *steps);
            if (auto v = str(args, "label"); !v.empty()) req.set("label", v);
            const std::string what = std::format("{} steps", steps->list()->size());
            if (const Value* d = args.get("dry_run"); d && d->as_bool()) return plan(*app, std::move(req), what);
            req.set("actor", actor);
            Answer a = call(*app, req, options.timeout);
            if (a.exit != 0) return tool_result(std::format("{}: {} — nothing of it stays", a.code.empty() ? "failed" : a.code, a.message), true);
            return tool_result(std::format("done: {} as one step ({})", what, str(a.data, "committed")), false, Value{}.set("data", a.data));
        }
        if (name == "ui_wait") {
            Value req;
            req.set("op", "wait");
            if (const Value* q = args.get("query")) req.set("q", *q);
            if (const Value* v = args.get("absent")) req.set("absent", v->as_bool());
            if (const Value* v = args.get("settled")) req.set("settled", v->as_bool());
            const double seconds = args.get("timeout") ? args.get("timeout")->as_double(5) : 5.0;
            req.set("timeout", seconds);
            const auto budget = std::chrono::milliseconds(static_cast<std::int64_t>(seconds * 1000)) + options.timeout;
            Answer a = call(*app, req, budget);
            if (a.exit != 0) return tool_result(std::format("{}: {}", a.code.empty() ? "timeout" : a.code, a.message), true);
            return tool_result("the condition holds", false, a.data);
        }
        if (name == "ui_capture") {
            Value req;
            req.set("op", "capture");
            if (auto n = str(args, "node"); !n.empty()) req.set("node", n);
            Answer a = call(*app, req, options.timeout);
            if (a.exit != 0) return tool_result(std::format("{}: {}", a.code, a.message), true);
            Value image;
            image.set("type", "image").set("data", str(a.data, "base64")).set("mimeType", "image/png");
            const Value* b = a.data.get("bounds");
            return tool_result(std::format("{} at {}", str(args, "node").empty() ? std::string_view("the window") : str(args, "node"),
                                           b ? b->json() : std::string("?")),
                               false, {}, Value::List{std::move(image)});
        }
        if (name == "ui_input") {
            const Value* events = args.get("events");
            if (!events || !events->list() || events->list()->empty()) return tool_result("ui_input needs `events`: [{\"type\": \"click\", \"x\": …, \"y\": …}, …]", true);
            Value req;
            req.set("op", "input").set("events", *events).set("actor", actor);
            const auto budget = std::chrono::milliseconds(static_cast<std::int64_t>(events->list()->size()) * 2000) + options.timeout;
            Answer a = call(*app, req, budget);
            if (a.exit != 0) return tool_result(std::format("{}: {}", a.code.empty() ? "failed" : a.code, a.message), true);
            std::string out = std::format("{} frames", a.data.get("frames") ? a.data.get("frames")->as_int() : 0);
            if (const Value* p = a.data.get("pressed"); p && p->list())
                for (const Value& h : *p->list()) {
                    const Value* at = h.get("at");
                    std::string where = at && at->list() && at->list()->size() == 2
                                            ? std::format("{:.0f},{:.0f}", (*at->list())[0].as_double(), (*at->list())[1].as_double())
                                            : std::string("?");
                    if (str(h, "node").empty()) out += std::format("\npressed at {}: nothing there", where);
                    else
                        out += std::format("\npressed at {}: {} {}{}", where, str(h, "semantic").empty() ? str(h, "node") : str(h, "semantic"),
                                           str(h, "role"), str(h, "name").empty() ? std::string() : std::format(" \"{}\"", str(h, "name")));
                }
            const Value* acts = a.data.get("actions");
            if (!acts || !acts->list() || acts->list()->empty())
                out += "\nno action was journaled (input a component takes itself — keys a game reads, a pointer's moves — journals none; look at the picture or the tree)";
            else
                for (const Value& x : *acts->list())
                    out += std::format("\naction: {} on {} by {}{}", str(x, "action"), str(x, "node"), str(x, "actor"),
                                       x.get("exitCode") && x.get("exitCode")->as_int() != 0 ? std::format(" (exit {})", x.get("exitCode")->as_int()) : "");
            if (auto f = str(a.data, "focused"); !f.empty()) out += std::format("\nfocused: {}", f);
            Value::List content;
            if (const Value* c = args.get("capture"); c && c->as_bool()) {
                Answer pic = call(*app, Value{}.set("op", "capture"), options.timeout);
                if (pic.exit == 0) content.push_back(Value{}.set("type", "image").set("data", str(pic.data, "base64")).set("mimeType", "image/png"));
            }
            return tool_result(std::move(out), false, Value{}.set("data", a.data), std::move(content));
        }
        if (name == "ui_clock") {
            const std::string_view action = str(args, "action");
            Value req;
            req.set("op", "clock").set("action", action.empty() ? std::string_view("show") : action);
            for (std::string_view k : {"seconds", "rate"})
                if (const Value* v = args.get(k)) req.set(k, v->as_double());
            Answer a = call(*app, req, options.timeout);
            if (a.exit != 0) return tool_result(std::format("{}: {}", a.code.empty() ? "failed" : a.code, a.message), true);
            return tool_result(a.data.json(), false, Value{}.set("data", a.data));
        }
        return tool_result(std::format("no tool `{}`", name), true);
    }
};

Server::Server(ServerOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
Server::~Server() = default;

Value::List Server::tools() {
    const auto n = [](std::string_view d) { return prop("integer", d); };
    const auto s = [](std::string_view d) { return prop("string", d); };
    const auto b = [](std::string_view d) { return prop("boolean", d); };
    const auto o = [](std::string_view d) { return prop("object", d); };
    return {
        tool("ui_apps", "Running applications", "The openmui applications running on this machine, by endpoint name and app id.",
             object_schema({}), true, false),
        tool("ui_snapshot", "Semantic tree",
             "An application's semantic tree as an outline: one node a line — id, role, name, value, states and the actions it offers "
             "(with their effect: irreversible and external ones need the user's confirmation; one marked person is the user's own, which "
             "only they do at the machine — do not try it). Ids are stable paths; use them with ui_act.",
             object_schema({app_prop,
                            {"root", s("a subtree's id")},
                            {"depth", n("levels below the root")},
                            {"interactive", b("only nodes that can be operated")},
                            {"offset", n("skip this many nodes (paging)")},
                            {"limit", n("at most this many nodes")}}),
             true, false),
        tool("ui_find", "Find nodes", "Nodes matching a query: a role, a name glob (Save*), a state, or a full query object (spatial: within).",
             object_schema({app_prop,
                            {"role", s("button, text_box, checkbox, link, list_item …")},
                            {"name", s("a name glob: Save*, *TODO*")},
                            {"state", s("a state the nodes have: checked, disabled, focused, selected …")},
                            {"query", o("a whole openmui.tree query, e.g. {\"role\":\"button\",\"within\":{\"id\":\"main/toolbar\"}}")},
                            {"limit", n("at most this many")}}),
             true, false),
        tool("ui_act", "Act on a node",
             "Performs an action a node offers (activate, toggle, set_value with args {\"value\": …}, focus, scroll …), as a person "
             "would. An irreversible or external action is put to the user first; never pass `answer` unless the user gave it. "
             "dry_run: only say what it would do (its effect, whether it can be undone, the value after) and change nothing.",
             object_schema({app_prop,
                            {"node", s("the node's id")},
                            {"action", s("an action the node lists")},
                            {"args", o("the action's arguments")},
                            {"answer", s("the user's answer to a confirmation they were asked for")},
                            {"dry_run", b("only what would happen; nothing is done")}},
                           {"node", "action"}),
             false, true),
        tool("ui_invoke", "Invoke a verb", "Invokes an application verb (its describe lists them), e.g. notes.new with {\"title\": …}.",
             object_schema({app_prop,
                            {"verb", s("the verb's name")},
                            {"args", o("its arguments")},
                            {"answer", s("the user's answer to a confirmation they were asked for")},
                            {"dry_run", b("only what would happen; nothing is done")}},
                           {"verb"}),
             false, true),
        tool("ui_undo", "Undo or redo",
             "Takes back the last step of the application's history (the window's, or a scope's), or with redo puts it forward again. "
             "Reversible actions that say how are in the history; an irreversible or external one ends it — undo stops there.",
             object_schema({app_prop,
                            {"redo", b("redo instead of undo")},
                            {"scope", s("a node with its own history (default: the window)")},
                            {"dry_run", b("only say what it would undo")}}),
             false, false),
        tool("ui_transaction", "Several actions as one",
             "Performs actions in order as one step of history: if one fails, those done are taken back and nothing stays. Only "
             "reversible and read actions may be in it. dry_run checks every step first.",
             object_schema({app_prop,
                            {"steps", prop("array", "[{\"node\": id, \"action\": name, \"args\": {…}}, …]").set("items", prop("object", "a step"))},
                            {"label", s("what the step is called in the history")},
                            {"dry_run", b("only what each step would do")}},
                           {"steps"}),
             false, false),
        tool("ui_wait", "Wait for the interface",
             "Waits until nodes matching a query appear (or, absent, are gone) and/or the interface has settled (nothing animating).",
             object_schema({app_prop,
                            {"query", o("a query that must match, e.g. {\"id\":\"main/toast\"}")},
                            {"absent", b("the query must not match")},
                            {"settled", b("nothing animates or is being removed")},
                            {"timeout", prop("number", "seconds (default 5)")}}),
             true, false),
        tool("ui_capture", "Capture pixels", "The pixels of the window, or of one node, as a PNG image.",
             object_schema({app_prop, {"node", s("a node's id (default: the window)")}}), true, false),
        tool("ui_input", "Input as a person gives it",
             "Pointer, wheel, keys and text, delivered frame by frame through the application's real input path (hit testing, focus, "
             "gestures), as a person's devices would give them — to use the interface as a person does and find what does not work. Only "
             "what it does is the agent's, decided as its requests are (the user's own actions refused; an irreversible one needs their answer); "
             "an application may turn it off. Events: {type: move|click|double_click|down|up, x, y | node, button?, "
             "mods?}, {type: drag, from: [x,y] | from_node, to: [x,y] | to_node, steps?}, {type: wheel, x, y, dx?, dy?}, {type: key|key_down|"
             "key_up, key: A|Enter|Escape|Tab|Space|Left|PageDown…, mods?: [shift, ctrl, alt, meta], hold?: frames}, {type: keys, keys: [U, J], hold?} "
             "(pressed together), {type: text, text}, "
             "{type: wait, frames}. Coordinates are logical pixels, as ui_snapshot's @x,y. The answer says what each press hit, the actions "
             "performed (by whom) and where focus is; capture: true adds the window's pixels after.",
             object_schema({app_prop,
                            {"events", prop("array", "the events, in order").set("items", prop("object", "an event"))},
                            {"capture", b("the window's pixels after the input")}},
                           {"events"}),
             false, false),
        tool("ui_clock", "The application's time",
             "Shows or controls the application's clock: pause, resume, step {seconds}, seek {seconds}, rate {rate}, release.",
             object_schema({app_prop,
                            {"action", s("show, pause, resume, step, seek, rate, release")},
                            {"seconds", prop("number", "for step and seek")},
                            {"rate", prop("number", "for rate: 1 is real time")}}),
             false, false),
    };
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
        r.set("instructions",
              "Operate running openmui applications through their semantic trees: ui_apps to see what runs, ui_snapshot or ui_find "
              "to read, ui_act or ui_invoke to change, ui_wait to let the interface settle, ui_capture to see pixels, ui_input to point, "
              "click, drag, scroll and type as a person does (when the application allows it), ui_clock for its time. Ids are stable "
              "paths. Actions marked irreversible or external are confirmed by the user; never answer for them. Actions marked person are "
              "the user's own and are refused to agents.");
        t.write(response(*id, std::move(r)).json());
        return;
    }
    if (method == "ping") {
        t.write(response(*id, Value(Value::Map{})).json());
        return;
    }
    if (method == "tools/list") {
        t.write(response(*id, Value{}.set("tools", Value(tools()))).json());
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
        if (const Value* c = n.get("children"); c && c->list())
            for (const Value& k : *c->list())
                if (auto it = by_id.find(k.as_string()); it != by_id.end()) line(*it->second, depth + 1);
    };
    for (const Value& n : nodes)
        if (!children.contains(str(n, "id"))) line(n, 0);
    for (const Value& n : nodes) line(n, 0);  // anything a cycle or a page left out
    return out;
}

}  // namespace omui::mcp
