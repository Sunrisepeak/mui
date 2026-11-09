// The MCP server as an agent's client sees it: JSON-RPC lines in, lines out,
// against a running application (a headless H3 session on this thread, the
// server on another, over the real endpoint). An irreversible action is put
// to the person — refused to a client that cannot ask, asked by elicitation
// of one that can — and never done on the agent's word alone.
import std;
import omui.core;
import omui.mcp;
import omui.host.headless;
import omui.ui.base;
import mlibs.testing;
import mlibs.base64;

using namespace mlibs::testing;
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
        check(tools && tools->list() && tools->list()->size() == 50, "nineteen ui_* tools and Live Design's thirty-one");
        if (tools && tools->list()) {
            std::set<std::string> names;
            for (const Value& t : *tools->list()) {
                names.insert(std::string(t.get("name")->as_string()));
                check(t.get("inputSchema") && t.get("inputSchema")->get("type")->as_string() == "object", "an object schema");
            }
            check((names == std::set<std::string>{"ui_apps", "ui_snapshot", "ui_find", "ui_act", "ui_invoke", "ui_undo", "ui_transaction", "ui_preview",
                                                  "ui_wait", "ui_capture", "ui_drew", "ui_intend", "ui_input", "ui_clock", "ui_verbs",
                                                  "ui_timeline", "ui_explain", "ui_reload", "ui_record", "design_begin", "design_sources", "design_api",
                                                  "design_locate", "design_edit", "design_set", "design_insert", "design_remove", "design_preview", "design_preview_accept", "design_preview_reject", "design_variants",
                                                  "design_variants_pick", "design_variants_close", "design_film", "design_timeline", "design_history", "design_history_commit", "design_history_branch",
                                                  "design_history_switch", "design_history_revert", "design_history_diff", "design_history_merge", "design_history_tag", "design_expect", "design_check", "design_handover", "design_save_scenario",
                                                  "design_replay", "design_discard", "design_commit"}));
            // The hints are the catalog's effects (omui.ops.omui.ts): reads, changes, and what runs what it names.
            const auto hint = [&](std::string_view tool, std::string_view key) {
                for (const Value& t : *tools->list())
                    if (t.get("name")->as_string() == tool) return t.get("annotations")->get(key)->as_bool();
                return false;
            };
            check(hint("ui_snapshot", "readOnlyHint") && hint("ui_capture", "readOnlyHint") && hint("ui_verbs", "readOnlyHint") && hint("ui_intend", "readOnlyHint"),
                  "reads");
            check(!hint("ui_clock", "readOnlyHint") && !hint("ui_clock", "destructiveHint") && !hint("ui_input", "readOnlyHint"), "reversible");
            check(!hint("ui_act", "readOnlyHint") && hint("ui_act", "destructiveHint") && hint("ui_invoke", "destructiveHint"), "delegated: at most irreversible");
            // Live Design's, from its catalog (design.ops.omui.ts): an edit takes the script's path and text, and is undone.
            check(hint("design_sources", "readOnlyHint") && !hint("design_edit", "readOnlyHint") && !hint("design_edit", "destructiveHint"), "design: reads and edits");
            for (const Value& t : *tools->list())
                if (t.get("name")->as_string() == "design_edit") {
                    const Value* properties = t.get("inputSchema")->get("properties");
                    check(properties->get("path") && properties->get("text") && properties->get("app"), t.json());
                    check(t.get("description")->as_string().contains("the application's verb design.edit"), t.json());
                }
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
                call(10, "ui_act", Value{}.set("node", "notes/delete")),
                call(11, "ui_verbs")};
        serve(notes.h, server, c);
        check(!c.error(11) && c.text(11).contains("- notes.pin (") , c.text(11));
        check(c.text(2).contains("org.openmui.test-notes"), c.text(2));
        check(c.text(3).contains("notes/delete") && c.text(3).contains("(irreversible)") && !c.text(3).contains("notes/title"), c.text(3));
        check(c.text(4).starts_with("1 found") && c.text(4).contains("\"Delete all\""), c.text(4));
        check(c.error(5) && c.text(5).contains("needs the user's confirmation") && c.text(5).contains("\"answer\": \"yes\""), c.text(5));
        check_eq(notes.deleted, 0);
        check(!c.error(6) && notes.pinned && c.text(6).contains("\"pinned\":1"), c.text(6));
        check(c.text(6).contains("no semantic change"), "what the step changed comes back with it: " + c.text(6));
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

    s.test("input as a person gives it: a click through hit testing, what it hit and did, as the agent's", [] {
        const auto dir = scratch_dir("mcp-input");
        int pressed = 0;
        Headless h([&] {
            HeadlessOptions o;
            o.level = HeadlessLevel::session;
            o.size = {200, 80};
            o.app = "org.openmui.test-input";
            o.endpoint_directory = dir;
            o.agent_input = true;
            return o;
        }());
        if (!h.endpoint()) return;
        h.mount("main", column({.padding = 8}, {box({.id = "go", .width = 80, .height = 24, .label = "Go",
                                                     .on = {{"press", [&](const Value&) { ++pressed; }}}})}));
        h.step();
        mcp::Server server({.directory = dir});
        Client c;
        c.in = {initialize(1, false),
                call(2, "ui_input", Value{}.set("events", Value(Value::List{Value{}.set("type", "click").set("node", "main/go")}))),
                call(3, "ui_input", Value{}.set("events", Value(Value::List{Value{}.set("type", "click").set("x", 190).set("y", 70)}))),
                call(4, "ui_input", Value{}.set("events", Value(Value::List{Value{}.set("type", "fly")}))),
                call(5, "ui_clock", Value{}.set("action", "pause"))};
        serve(h, server, c);
        check(!c.error(2) && c.text(2).contains("pressed at") && c.text(2).contains("main/go button \"Go\"") &&
                  c.text(2).contains("action: activate on main/go by agent:claude-code;via=service:input"),
              c.text(2));
        check_eq(pressed, 1);
        check(!c.error(3) && c.text(3).contains("no action was journaled"), c.text(3));
        check(c.error(4) && c.text(4).contains("not an input event"), c.text(4));
        check(!c.error(5) && c.text(5).contains("\"paused\":true"), c.text(5));
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

    s.test("dry runs, undo and transactions (S-12, S-13): planned, done, taken back", [] {
        const auto dir = scratch_dir("mcp-undo");
        Notes notes(dir);
        if (!notes.h.endpoint()) return;
        auto pinned = signal(false);
        auto volume = signal(30.0);
        notes.h.mount("prefs", column({.gap = 4}, {toggle({.id = "pinned", .checked = pinned, .label = "Pinned"}),
                                                    slider({.id = "volume", .label = "Volume", .step = 5.0, .value = volume, .width = 120})}));
        notes.h.step();
        mcp::Server server({.directory = dir});
        Client c;
        const Value steps = Value(Value::List{Value{}.set("node", "prefs/pinned").set("action", "toggle"),
                                              Value{}.set("node", "prefs/volume").set("action", "set_value").set("args", Value{}.set("value", 60.0)),
                                              Value{}.set("node", "prefs/volume").set("action", "set_value").set("args", Value{}.set("value", "loud"))});
        c.in = {initialize(1, false),
                call(2, "ui_act", Value{}.set("node", "prefs/pinned").set("action", "toggle").set("dry_run", true)),
                call(3, "ui_act", Value{}.set("node", "prefs/pinned").set("action", "toggle")),
                call(4, "ui_undo", Value{}.set("dry_run", true)),
                call(5, "ui_undo"),
                call(6, "ui_undo", Value{}.set("redo", true)),
                call(7, "ui_transaction", Value{}.set("steps", steps).set("dry_run", true)),
                call(8, "ui_transaction", Value{}.set("steps", steps)),
                call(9, "ui_transaction", Value{}.set("steps", Value(Value::List{(*steps.list())[0], (*steps.list())[1]})).set("label", "quiet")),
                call(10, "ui_act", Value{}.set("node", "notes/delete").set("action", "activate").set("dry_run", true))};
        serve(notes.h, server, c);
        check(!c.error(2) && c.text(2).contains("it would run (reversible); value false → true; it can be undone"), c.text(2));
        check(!c.error(3), c.text(3));
        check(!c.error(4) && c.text(4).contains("it would undo toggle on 'prefs/pinned'"), c.text(4));
        check(!c.error(5) && c.text(5).starts_with("undone: toggle on 'prefs/pinned'"), c.text(5));
        check(!c.error(6) && c.text(6).starts_with("redone"), c.text(6));
        check(!c.error(7) && c.text(7).starts_with("dry run: 3 steps — it would fail (reversible)") && c.text(7).contains("- set_value on prefs/volume: it would fail: "), c.text(7));
        check(c.error(8) && c.text(8).contains("step 3") && c.text(8).contains("nothing of it stays"), c.text(8));
        check(!c.error(9) && c.text(9).contains("as one step (quiet)"), c.text(9));
        check(!c.error(10) && c.text(10).contains("confirmation") && notes.deleted == 0, c.text(10));
        check(!pinned.peek() && volume.peek() == 60, std::format("{} {}", pinned.peek(), volume.peek()));
        const History h = notes.h.runtime().history(notes.h.runtime().root());
        check(!h.undo.empty() && h.undo.back().label == "quiet" && h.undo.back().steps == 2);
    });

    s.test("shown before it is allowed (Part 2 A): the picture before and after, the changes, the step that asks the person not done — nothing stays", [] {
        const auto dir = scratch_dir("mcp-preview");
        Notes notes(dir);
        if (!notes.h.endpoint()) return;
        auto pinned = signal(false);
        auto volume = signal(30.0);
        notes.h.mount("prefs", column({.gap = 4}, {toggle({.id = "pinned", .checked = pinned, .label = "Pinned"}),
                                                    slider({.id = "volume", .label = "Volume", .step = 5.0, .value = volume, .width = 120})}));
        notes.h.step();
        const std::size_t journaled = notes.h.runtime().action_journal().size();
        const auto level = notes.h.runtime().motion_level();
        mcp::Server server({.directory = dir});
        Client c;
        const Value steps = Value(Value::List{Value{}.set("node", "prefs/pinned").set("action", "toggle"),
                                              Value{}.set("node", "prefs/volume").set("action", "set_value").set("args", Value{}.set("value", 60.0)),
                                              Value{}.set("node", "notes/delete").set("action", "activate")});
        c.in = {initialize(1, false), call(2, "ui_preview", Value{}.set("steps", steps)),
                call(3, "ui_preview", Value{}.set("steps", Value(Value::List{Value{}.set("node", "prefs/nothing").set("action", "toggle")})))};
        serve(notes.h, server, c);
        const std::string text = c.text(2);
        check(!c.error(2) && text.contains("the interface is as it was"), text);
        check(text.contains("changed prefs/pinned") && text.contains("\"checked\"") && text.contains("changed prefs/volume") && text.contains("60"),
              "the changes the steps make, as a reader reads them: " + text);
        check(text.contains("not done (asks the person): activate on notes/delete (irreversible)"), text);
        const Value* r = c.reply(2);
        const Value* content = r && r->get("result") ? r->get("result")->get("content") : nullptr;
        check(content && content->list() && content->list()->size() == 3, "the words, then the picture before and the picture after");
        if (content && content->list() && content->list()->size() == 3) {
            check((*content->list())[1].get("data")->as_string() != (*content->list())[2].get("data")->as_string(), "the pictures differ: the change is seen");
            // Kept to look at: what the person is shown.
            for (std::size_t i : {1u, 2u})
                if (auto png = mlibs::base64::decode((*content->list())[i].get("data")->as_string()))
                    std::ofstream(dir / (i == 1 ? "before.png" : "after.png"), std::ios::binary)
                        .write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
        }
        // Nothing stays: not the values, not the verb, no journal entry, no history, the motion level as it was.
        check(!pinned.peek() && volume.peek() == 30 && notes.deleted == 0, std::format("{} {} {}", pinned.peek(), volume.peek(), notes.deleted));
        check_eq(notes.h.runtime().action_journal().size(), journaled);
        check(notes.h.runtime().history(notes.h.runtime().root()).undo.empty(), "no step of history");
        check(notes.h.runtime().motion_level() == level && !notes.h.runtime().previewing());
        check(c.error(3) && c.text(3).contains("nothing was done"), c.text(3));
    });

    s.test("what drew a pixel (Part 2 B): the node, the nodes it sits in, and the line of the program that built each", [] {
        const auto dir = scratch_dir("mcp-drew");
        Notes notes(dir);
        if (!notes.h.endpoint()) return;
        const Rect box = notes.h.runtime().screen_bounds(notes.h.runtime().find("notes/delete"));
        mcp::Server server({.directory = dir});
        Client c;
        c.in = {initialize(1, false),
                call(2, "ui_drew", Value{}.set("at", Value(Value::List{Value(box.x + box.width / 2), Value(box.y + box.height / 2)}))),
                call(3, "ui_drew", Value{}.set("at", Value(Value::List{Value(310.0), Value(150.0)})))};
        serve(notes.h, server, c);
        const std::string text = c.text(2);
        check(!c.error(2) && text.starts_with("notes/delete drew"), text);
        check(text.contains("built at") && text.contains("mcp_test.cpp:"), "the line of this test that built the box: " + text);
        check(!c.error(3) && c.text(3).contains("background"), c.text(3));
    });

    s.test("saying what it is about to do (openas §4.4) and handing back (§5.3): the note the person's screen shows, as the agent's; nothing changes", [] {
        const auto dir = scratch_dir("mcp-intend");
        Notes notes(dir);
        if (!notes.h.endpoint()) return;
        const auto journaled = notes.h.runtime().action_journal().size();
        mcp::Server server({.directory = dir, .actor = "agent:guide"});
        Client c;
        c.in = {initialize(1, false), call(2, "ui_intend", Value{}.set("text", "Pin the first note, then delete the old ones").set("node", "notes/pin")),
                call(3, "ui_intend", Value{}), call(4, "ui_intend", Value{}.set("text", "Pinned one; two old notes left: are they yours?").set("hands_back", true)),
                call(5, "ui_intend", Value{}.set("text", ""))};
        serve(notes.h, server, c);
        check(!c.error(2) && c.text(2).contains("said"), c.text(2));
        check(c.error(3), "text is needed");
        check(!c.error(4) && c.text(4).contains("handed back"), c.text(4));
        check(!c.error(5) && c.text(5).contains("withdrawn"), c.text(5));
        check(notes.h.runtime().agent_notes().empty(), "withdrawn at the end");
        check_eq(notes.h.runtime().action_journal().size(), journaled);
    });

    s.test("an application's verbs as tools from its listing (omui-gen ops): schemas, hints, invoked, confirmed by the person", [] {
        const auto dir = scratch_dir("mcp-listing");
        // A listing as omui-gen ops writes one for the notes' verbs.
        const auto file = dir / "notes.ops.json";
        {
            std::ofstream out(file, std::ios::binary);
            out << R"({"kind": "omui.ops", "version": 1, "product": "notes", "doc": "Notes", "tools": [
  {"name": "notes.pin", "description": "Pin the first note", "effect": "reversible", "person": false, "delegated": false, "cost": "instant",
   "undo": "notes.unpin", "undoable": false, "cancellable": false, "result": "", "inputSchema": {"type": "object", "properties": {}, "required": []}},
  {"name": "notes.delete", "description": "Delete every note", "effect": "irreversible", "person": false, "delegated": false, "cost": "instant",
   "undo": "none: gone", "undoable": false, "cancellable": false, "result": "", "inputSchema": {"type": "object", "properties": {}, "required": []}}
], "tiers": []})";
        }
        auto listing = mcp::read_listing(file);
        check(listing.has_value(), listing ? "" : listing.error());
        if (!listing) return;
        {
            std::ofstream out(dir / "ui.ops.json", std::ios::binary);
            out << R"({"kind": "omui.ops", "tools": [{"name": "ui.snapshot"}]})";
        }
        const auto own = mcp::read_listing(dir / "ui.ops.json");
        check(!own && own.error().contains("ui_* tools already"), own ? "accepted" : own.error());
        check(!mcp::read_listing(dir / "nothing.ops.json"), "a file that is not there");
        Notes notes(dir);
        if (!notes.h.endpoint()) return;
        mcp::Server server({.directory = dir, .listings = {*listing}, .design = false});
        Client c;
        c.in = {initialize(1, false), request(2, "tools/list"), call(3, "notes_pin"), call(4, "notes_delete")};
        serve(notes.h, server, c);
        const Value* tools = c.reply(2) ? c.reply(2)->get("result")->get("tools") : nullptr;
        check(tools && tools->list() && tools->list()->size() == 21, "nineteen ui_* tools and the two listed");
        if (tools && tools->list() && tools->list()->size() == 21) {
            const Value& pin = (*tools->list())[19];
            const Value& del = (*tools->list())[20];
            check(pin.get("name")->as_string() == "notes_pin" && pin.get("title")->as_string() == "notes.pin", pin.json());
            check(pin.get("inputSchema")->get("properties")->get("app") != nullptr, "a call may name its application");
            check(!pin.get("annotations")->get("readOnlyHint")->as_bool() && !pin.get("annotations")->get("destructiveHint")->as_bool(), pin.json());
            check(del.get("annotations")->get("destructiveHint")->as_bool() && std::string(del.get("description")->as_string()).contains("irreversible"),
                  del.json());
        }
        check(!c.error(3) && notes.pinned, c.text(3));
        check(c.error(4) && c.text(4).contains("needs the user's confirmation") && notes.deleted == 0, c.text(4));
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
