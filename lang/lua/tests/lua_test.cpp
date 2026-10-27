// openmui in Lua, as a script writes it: the counter of component-binding
// §5.2 is the same tree as the document written by hand; expressions are the
// runtime's; loops, verbs and models; errors are values; a reload keeps the
// state.
import std;
import nlohmann.json;
import openmui.abi;
import openmui.lang;
import openmui.lang.lua;
import openmui.ui.base;
import openmui.testing;

using namespace omui::testing;
using namespace omui::abi;
namespace lang = omui::lang;
using lang::Json;

namespace {

OmuiString str(std::string_view s) { return {s.data(), s.size()}; }
void collect(void* user, const char* data, std::size_t size) { static_cast<std::string*>(user)->append(data, size); }

struct Runtime {
    OmuiRuntime rt = nullptr;
    double      time = 0;
    Runtime() {
        OmuiRuntimeCreateInfo info{};
        info.type = OMUI_STRUCTURE_TYPE_RUNTIME_CREATE_INFO;
        info.spec_version = OMUI_SPEC_VERSION;
        info.headless = 1;
        check_eq(omuiCreateRuntime(&info, &rt), OMUI_SUCCESS);
    }
    ~Runtime() { (void)omuiDestroyRuntime(rt); }
    void frame() {
        OmuiFrameInput in{};
        in.type = OMUI_STRUCTURE_TYPE_FRAME_INPUT;
        in.time = time += 1.0 / 60.0;
        in.width = 320;
        in.height = 240;
        in.scale = 1;
        (void)omuiFrame(rt, &in, nullptr);
    }
    void settle(const lang::Host& host) {
        int quiet = 0;
        for (int i = 0; i < 5000 && quiet < 3; ++i) {
            frame();
            quiet = host.idle() ? quiet + 1 : 0;
            if (!quiet) std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    template <class T>
    T wait(const lang::Host& host, std::future<T> f) {
        while (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            frame();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        settle(host);
        return f.get();
    }
    std::string tree(std::string_view slot) {
        std::string out;
        (void)omuiSnapshot(rt, str(std::format(R"j({{"root": "{}"}})j", slot)), collect, &out);
        for (auto line : out | std::views::split('\n')) {
            Json v = Json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
            if (v.is_object() && v.contains("data") && v["data"].contains("nodes")) return v["data"]["nodes"].dump();
        }
        return "<none>";
    }
    std::string name_of(std::string_view id) {
        std::string out;
        (void)omuiSnapshot(rt, str(std::format(R"j({{"root": "{}", "depth": 0}})j", id)), collect, &out);
        for (auto line : out | std::views::split('\n')) {
            Json v = Json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
            if (v.is_object() && v.contains("data") && v["data"].contains("nodes"))
                for (const Json& n : v["data"]["nodes"])
                    if (n["id"] == std::string(id)) return n.value("name", "");
        }
        return "<none>";
    }
    std::int64_t act(std::string_view node, std::string_view action) {
        std::string out;
        const std::string text = Json{{"node", node}, {"action", action}}.dump();
        return omuiAct(rt, str(text), collect, &out);
    }
};

struct Lua {
    Runtime&         r;
    lang::Host       host;
    lang::Session    session;
    lang::lua::Script script;
    std::vector<std::string> errors;
    explicit Lua(Runtime& rt) : r(rt), host(rt.rt), session(host), script(session) {
        script.on_error([this](std::string_view m) { errors.emplace_back(m); });
    }
    lang::Result<void> run(std::string code, std::string name = "=script") { return r.wait(host, script.run(std::move(code), std::move(name))); }
};

constexpr std::string_view counter_lua = R"lua(
local count = omui.signal(0)
local function Counter()
  return omui.Column { gap = 8,
    omui.Text   { text = omui.fmt("{} clicks", count) },
    omui.Button { id = "inc", text = "+1", on_activated = function() count:update(function(v) return v + 1 end) end },
  }
end
assert(omui.mount("main", Counter()))
)lua";

constexpr std::string_view counter_document = R"j({"omui": "0.1", "view": {
  "signals": {"count": {"type": "int", "value": 0}},
  "root": {"type": "column", "props": {"gap": 8}, "children": [
    {"type": "text", "props": {"text": {"expr": "format('{} clicks', count)"}}},
    {"type": "button", "id": "inc", "props": {"text": "+1"}, "on": {"activated": [{"set": "count", "expr": "count + 1"}]}}]}}})j";

}  // namespace

int main(int argc, char** argv) {
    Suite s{"lang.lua"};
    omui::register_base_components();

    s.test("the counter in Lua is the hand-written document's tree, before and after a click (S level)", [] {
        Runtime lua_rt, doc_rt;
        Lua lua(lua_rt);
        auto ran = lua.run(std::string(counter_lua));
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(omuiMount(doc_rt.rt, str("main"), str(counter_document)), OMUI_SUCCESS);
        doc_rt.frame();
        check_eq(lua_rt.tree("main"), doc_rt.tree("main"));
        for (int i = 0; i < 3; ++i) {
            check_eq(lua_rt.act("main/inc", "activate"), std::int64_t{0});
            check_eq(doc_rt.act("main/inc", "activate"), std::int64_t{0});
        }
        lua_rt.settle(lua.host);
        doc_rt.frame();
        check_eq(lua_rt.name_of("main/text:0"), std::string("3 clicks"));
        check_eq(lua_rt.tree("main"), doc_rt.tree("main"));
        check(lua.errors.empty(), lua.errors.empty() ? "" : lua.errors[0]);
    });

    s.test("expressions: signal arithmetic and the expression language, evaluated by the runtime", [] {
        Runtime r;
        Lua lua(r);
        auto ran = lua.run(R"lua(
local a, b = omui.signal(6), omui.signal(7)
local name = omui.signal("ada")
assert(omui.mount("main", omui.Column {
  omui.Text { id = "sum", text = omui.fmt("{}", a * b + 1) },
  omui.Text { id = "who", text = omui.call("upper", name) },
  omui.Text { id = "big", text = omui.select(omui.gt(a, 5), "big", "small") },
  omui.Text { id = "neg", text = omui.fmt("{}", -a % 4) },
}))
assert(tostring(a * 2):find("s%d+ %* 2"))
assert(a:get() == 6)
a:set(1)
assert(a:get() == 1, "reading never waits: the mirror has it")
)lua");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(r.name_of("main/sum"), std::string("8"));
        check_eq(r.name_of("main/who"), std::string("ADA"));
        check_eq(r.name_of("main/big"), std::string("small"));
        check_eq(r.name_of("main/neg"), std::string("-1"));
    });

    s.test("loops over a model, handlers with their row, a verb in Lua, regions", [] {
        Runtime r;
        Lua lua(r);
        auto ran = lua.run(R"lua(
archived = {}
assert(omui.verb("note.archive", { summary = "Archive a note" }, function(args)
  table.insert(archived, args.id)
  return { archived = args.id }
end))
local only_open = omui.signal(false)
local notes = omui.model("notes")
assert(notes:push({ { key = "n1", title = "Milk", done = true }, { key = "n2", title = "Bread", done = false } }))
assert(omui.mount("main", omui.Column {
  omui.when(only_open, { omui.Text { id = "filter", text = "open only" } }),
  omui.each("notes", function(note)
    return omui.Row {
      omui.Text { text = note.title },
      omui.Button { id = "archive", text = "Archive", on_activated = function(e)
        assert(omui.invoke("note.archive", { id = e.rows.row.key }))
      end },
    }
  end, omui.or_(omui.not_(only_open), omui.not_(omui.call("lookup", omui.null, "x", false)))),
}))
show_filter = function() only_open:set(true) end
)lua");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(r.name_of("main/n2/text:0"), std::string("Bread"));
        check_eq(r.act("main/n2/archive", "activate"), std::int64_t{0});
        r.settle(lua.host);
        check(lua.errors.empty(), lua.errors.empty() ? "" : lua.errors[0]);
        auto archived = lua.run("assert(archived[1] == 'n2', archived[1])");
        check(archived.has_value(), archived ? "" : archived.error().text());
        auto shown = lua.run("show_filter()");
        check(shown.has_value());
        check_eq(r.name_of("main/filter"), std::string("open only"));
    });

    s.test("errors are values, and the line says where", [] {
        Runtime r;
        Lua lua(r);
        auto ran = lua.run(R"lua(
local ok, err = omui.mount("main", omui.Buton { id = "x" })
assert(ok == nil and err:find("did you mean `button`"), err)
local ok2, err2 = omui.invoke("no.such", {})
assert(ok2 == nil and err2:find("not%-found"), err2)
local ok3, err3 = pcall(omui.fmt, 3)
assert(not ok3 and err3:find("pattern"), err3)
)lua");
        check(ran.has_value(), ran ? "" : ran.error().text());
        auto syntax = lua.run("local x = = 1", "=tally.lua");
        check(!syntax && syntax.error().text().find("tally.lua:1:") != std::string::npos, syntax ? "" : syntax.error().text());
        auto raised = lua.run("omui.mount('main', 3)", "=tally.lua");
        check(!raised && raised.error().text().find("omui.mount(slot, element)") != std::string::npos, raised ? "" : raised.error().text());
    });

    s.test("a reload keeps the state, the nodes, and replaces the handlers", [] {
        Runtime r;
        Lua lua(r);
        check(lua.run(std::string(counter_lua)).has_value());
        check_eq(r.act("main/inc", "activate"), std::int64_t{0});
        check_eq(r.act("main/inc", "activate"), std::int64_t{0});
        r.settle(lua.host);
        std::string next(counter_lua);
        next.replace(next.find("{} clicks"), 9, "{} taps");
        next.replace(next.find("v + 1"), 5, "v + 10");
        const auto start = std::chrono::steady_clock::now();
        auto reloaded = r.wait(lua.host, lua.script.reload(next));
        const auto took = std::chrono::steady_clock::now() - start;
        check(reloaded.has_value(), reloaded ? "" : reloaded.error().text());
        check_eq(r.name_of("main/text:0"), std::string("2 taps"));
        check(took < std::chrono::milliseconds(200), std::format("{}", std::chrono::duration_cast<std::chrono::milliseconds>(took)));
        check_eq(r.act("main/inc", "activate"), std::int64_t{0});
        r.settle(lua.host);
        check_eq(r.name_of("main/text:0"), std::string("12 taps"));
    });

    return s.run(argc, argv);
}
