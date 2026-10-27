// The MCP server as an agent's client sees it: JSON-RPC lines in, lines out,
// against a running application (a headless H3 session on this thread, the
// server on another, over the real endpoint). An irreversible action is put
// to the person — refused to a client that cannot ask, asked by elicitation
// of one that can — and never done on the agent's word alone.
import std;
import openmui.core;
import openmui.mcp;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

std::string request(std::int64_t id, std::string_view method, Value params = Value(Value::Map{})) {
    Value v;
    v.set("jsonrpc", "2.0").set("id", id).set("method", method).set("params", std::move(params));
    return v.json();
}

std::string call(std::int64_t id, std::string_view tool, Value args = Value(Value::Map{})) {
    return request(id, "tools/call", Value{}.set("name", tool).set("arguments", std::move(args)));
}

std::string initialize(std::int64_t id, bool elicitation) {
    Value caps = Value(Value::Map{});
    if (elicitation) caps.set("elicitation", Value(Value::Map{}));
    Value p;
    p.set("protocolVersion", "2025-06-18").set("capabilities", std::move(caps)).set("clientInfo", Value{}.set("name", "Claude Code").set("version", "2"));
    return request(id, "initialize", std::move(p));
}

// A client: lines to send, what came back; a request the server makes (an
// elicitation) answered by `answer`.
struct Client final : mcp::Transport {
    std::deque<std::string>                           in;
    std::vector<Value>                                out;
    std::function<std::optional<Value>(const Value&)> answer;
    std::mutex                                        lock;

    std::optional<std::string> read() override {
        std::scoped_lock g(lock);
        if (in.empty()) return std::nullopt;
        std::string l = std::move(in.front());
        in.pop_front();
        return l;
    }
    void write(std::string_view line) override {
        auto v = parse_json(line);
        check(v.has_value(), std::string(line));
        if (!v) return;
        std::scoped_lock g(lock);
        out.push_back(*v);
        if (v->get("method") && v->get("id") && answer)
            if (auto reply = answer(*v)) {
                Value r;
                r.set("jsonrpc", "2.0").set("id", *v->get("id")).set("result", std::move(*reply));
                in.push_front(r.json());
            }
    }
    // The response to request `id`.
    const Value* reply(std::int64_t id) const {
        for (const Value& v : out)
            if (!v.get("method") && v.get("id") && v.get("id")->as_int(-1) == id) return &v;
        return nullptr;
    }
    std::string text(std::int64_t id) const {
        const Value* r = reply(id);
        const Value* res = r ? r->get("result") : nullptr;
        const Value* c = res ? res->get("content") : nullptr;
        return c && c->list() && !c->list()->empty() ? std::string(c->list()->front().get("text")->as_string()) : std::string();
    }
    bool error(std::int64_t id) const {
        const Value* r = reply(id);
        const Value* res = r ? r->get("result") : nullptr;
        return res && res->get("isError") && res->get("isError")->as_bool();
    }
};

// Serves `client` on a thread while the application runs frames here.
void serve(Headless& h, mcp::Server& server, Client& client) {
    std::atomic<bool> done{false};
    std::jthread t([&] {
        server.serve(client);
        done = true;
    });
    for (int i = 0; i < 10000 && !done; ++i) {
        h.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    t.join();
}

struct Notes {
    Headless h;
    int      deleted = 0;
    bool     pinned = false;

    explicit Notes(const std::filesystem::path& dir) : h([&] {
          HeadlessOptions o;
          o.level = HeadlessLevel::session;
          o.size = {320, 160};
          o.app = "org.openmui.test-notes";
          o.endpoint_directory = dir;
          return o;
      }()) {
        (void)h.runtime().verbs().add({"notes.delete", "Delete every note", Effect::irreversible, "yes", {}}, [this](const Value&) -> VerbResult {
            ++deleted;
            return Value{};
        });
        (void)h.runtime().verbs().add({"notes.pin", "Pin the first note", Effect::reversible, "", {}}, [this](const Value&) -> VerbResult {
            pinned = true;
            return Value{}.set("pinned", 1);
        });
        h.mount("notes", column({.padding = 8, .gap = 4}, {
                                     text({.id = "title", .text = "Notes"}),
                                     box({.id = "delete", .width = 80, .height = 24, .background = "danger", .label = "Delete all"})
                                         .prop("verb", Source{Value("notes.delete"), {}, {}}),
                                 }));
        h.step();
    }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"mcp"};

    s.test("the protocol: initialize (the version asked for, the client named), tools, ping, and errors as JSON-RPC's", [] {
        mcp::Server server({.directory = scratch_dir("mcp-empty")});
        Client c;
        Value p;
        p.set("protocolVersion", "2025-03-26").set("capabilities", Value(Value::Map{})).set("clientInfo", Value{}.set("name", "Claude Code"));
        c.in = {request(1, "initialize", std::move(p)),
                R"({"jsonrpc":"2.0","method":"notifications/initialized"})",
                request(2, "tools/list"),
                request(3, "ping"),
                request(4, "frobnicate"),
                "{not json",
                call(5, "ui_nothing"),
                call(6, "ui_apps"),
                call(7, "ui_snapshot")};
        server.serve(c);
        const Value* init = c.reply(1);
        check(init && init->get("result"), "initialize answered");
        if (init && init->get("result")) {
            const Value& r = *init->get("result");
            check_eq(std::string(r.get("protocolVersion")->as_string()), std::string("2025-03-26"));
            check(r.get("capabilities")->get("tools") != nullptr, "it offers tools");
            check_eq(std::string(r.get("serverInfo")->get("name")->as_string()), std::string("openmui"));
        }
        const Value* tools = c.reply(2) ? c.reply(2)->get("result")->get("tools") : nullptr;
        check(tools && tools->list() && tools->list()->size() == 7, "seven tools");
        if (tools && tools->list()) {
            std::set<std::string> names;
            for (const Value& t : *tools->list()) {
                names.insert(std::string(t.get("name")->as_string()));
                check(t.get("inputSchema") && t.get("inputSchema")->get("type")->as_string() == "object", "an object schema");
            }
            check((names == std::set<std::string>{"ui_apps", "ui_snapshot", "ui_find", "ui_act", "ui_invoke", "ui_wait", "ui_capture"}));
        }
        check(c.reply(3) && c.reply(3)->get("result"), "ping");
        check(c.reply(4) && c.reply(4)->get("error")->get("code")->as_int() == -32601, "an unknown method");
        check(std::ranges::any_of(c.out, [](const Value& v) { return v.get("error") && v.get("error")->get("code")->as_int() == -32700; }), "not JSON");
        check(c.reply(5) && c.reply(5)->get("error")->get("code")->as_int() == -32602, "an unknown tool");
        check(!c.error(6) && c.text(6).contains("no openmui application is running"), c.text(6));
        check(c.error(7) && c.text(7).contains("no openmui application is running"), c.text(7));
        check_eq(c.out.size(), std::size_t{8});  // the notification is not answered
    });

    s.test("against a running application: apps, the tree, find, a verb, wait, capture — and an irreversible action refused to a client that cannot ask", [] {
        const auto dir = scratch_dir("mcp-app");
        Notes notes(dir);
        if (!notes.h.endpoint()) {
            std::println("    skipped: local endpoints are not supported here");
            return;
        }
        std::vector<Value> journal;
        mcp::Server server({.directory = dir, .journal = [&](const Value& e) { journal.push_back(e); }});
        Client c;
        c.in = {initialize(1, false),
                call(2, "ui_apps"),
                call(3, "ui_snapshot", Value{}.set("interactive", true)),
                call(4, "ui_find", Value{}.set("name", "Delete*")),
                call(5, "ui_act", Value{}.set("node", "notes/delete").set("action", "activate")),
                call(6, "ui_invoke", Value{}.set("verb", "notes.pin")),
                call(7, "ui_wait", Value{}.set("settled", true).set("timeout", 2)),
                call(8, "ui_capture", Value{}.set("node", "notes/delete")),
                call(9, "ui_snapshot", Value{}.set("app", "org.openmui.test-notes").set("root", "notes/nowhere")),
                call(10, "ui_act", Value{}.set("node", "notes/delete"))};
        serve(notes.h, server, c);
        check(c.text(2).contains("org.openmui.test-notes"), c.text(2));
        check(c.text(3).contains("notes/delete") && c.text(3).contains("(irreversible)") && !c.text(3).contains("notes/title"), c.text(3));
        check(c.text(4).starts_with("1 found") && c.text(4).contains("\"Delete all\""), c.text(4));
        check(c.error(5) && c.text(5).contains("needs the user's confirmation") && c.text(5).contains("\"answer\": \"yes\""), c.text(5));
        check_eq(notes.deleted, 0);
        check(!c.error(6) && notes.pinned && c.text(6).contains("\"pinned\":1"), c.text(6));
        check(!c.error(7), c.text(7));
        const Value* shot = c.reply(8) ? c.reply(8)->get("result")->get("content") : nullptr;
        check(shot && shot->list() && shot->list()->size() == 2, "a text and an image");
        if (shot && shot->list() && shot->list()->size() == 2) {
            const Value& image = (*shot->list())[1];
            check(image.get("type")->as_string() == "image" && image.get("mimeType")->as_string() == "image/png");
            check(image.get("data")->as_string().starts_with("iVBORw0KGgo"), "a PNG, in base64");
        }
        check(c.error(9) && c.text(9).contains("no node"), c.text(9));
        check(c.error(10) && c.text(10).contains("needs `node` and `action`"), c.text(10));
        // The agent as the client named itself.
        check(!journal.empty() && journal.front().get("actor")->as_string() == "agent:claude-code");
        const auto actions = notes.h.runtime().action_journal();
        check(!actions.empty() && actions.back().actor == "agent:claude-code", actions.empty() ? "" : actions.back().actor);
    });

    s.test("a client that can ask: the person allows — done, with the person in the actor; declines — nothing done", [] {
        const auto dir = scratch_dir("mcp-elicit");
        Notes notes(dir);
        if (!notes.h.endpoint()) return;
        mcp::Server server({.directory = dir});
        Client c;
        std::vector<std::string> asked;
        int answers = 0;
        c.answer = [&](const Value& req) -> std::optional<Value> {
            if (req.get("method")->as_string() != "elicitation/create") return std::nullopt;
            asked.emplace_back(req.get("params")->get("message")->as_string());
            const bool allow = answers++ == 0;  // yes, then no
            return Value{}.set("action", "accept").set("content", Value{}.set("allow", allow));
        };
        c.in = {initialize(1, true),
                call(2, "ui_act", Value{}.set("node", "notes/delete").set("action", "activate")),
                call(3, "ui_act", Value{}.set("node", "notes/delete").set("action", "activate"))};
        serve(notes.h, server, c);
        check_eq(asked.size(), std::size_t{2});
        if (!asked.empty()) check(asked[0].contains("agent:claude-code wants to") && asked[0].contains("irreversible"), asked[0]);
        check(!c.error(2) && c.text(2).starts_with("done"), c.text(2));
        check(c.error(3) && c.text(3).contains("the user declined"), c.text(3));
        check_eq(notes.deleted, 1);
        const auto actions = notes.h.runtime().action_journal();
        const bool by_person = std::ranges::any_of(actions, [](const auto& a) { return a.actor == "agent:claude-code;via=person:user"; });
        check(by_person, "the person's yes is in the actor");
    });

    s.test("the outline: one node a line, children under their parent, states, values, effects and bounds", [] {
        auto node = [](std::string id, std::string role, Value extra = Value(Value::Map{})) {
            Value v = std::move(extra);
            v.set("id", id).set("role", role);
            return v;
        };
        Value::List nodes{
            node("main", "group", Value{}.set("children", Value(Value::List{Value("main/save"), Value("main/dark")}))),
            node("main/save", "button",
                 Value{}.set("name", "Save").set("actions", Value(Value::List{Value{}.set("name", "activate").set("effect", "reversible")}))),
            node("main/dark", "checkbox",
                 Value{}
                     .set("name", "Dark")
                     .set("value", true)
                     .set("state", Value(Value::List{Value("checked")}))
                     .set("actions", Value(Value::List{Value{}.set("name", "toggle").set("effect", "reversible"),
                                                      Value{}.set("name", "erase").set("effect", "irreversible"),
                                                      Value{}.set("name", "approve").set("effect", "reversible").set("person", true)}))
                     .set("bounds", Value(Value::List{Value(10.4), Value(20.0), Value(30.0), Value(12.6)}))),
        };
        check_eq(mcp::outline(nodes), std::string("main group\n"
                                                  "  main/save button \"Save\" {activate}\n"
                                                  "  main/dark checkbox \"Dark\" = true [checked] {toggle, erase (irreversible), approve (person)} @10,20 30x13\n"));
    });

    return s.run(argc, argv);
}
