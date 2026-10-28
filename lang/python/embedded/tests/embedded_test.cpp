// openmui in embedded Python (PocketPy), as a script writes it: the counter
// of component-binding §5.2 is the same tree as the document written by
// hand; expressions are the runtime's; loops, verbs and models; refusals are
// OmuiError and errors say the line; a reload keeps the state; a runaway
// script is stopped, and exit() ends the script, not the program.
import std;
import nlohmann.json;
import openmui.abi;
import openmui.lang;
import openmui.lang.python.embedded;
import openmui.player;
import openmui.ui.base;
import openmui.testing;
import openmui.view;
import openmui.cts;
import openmui.native;
import openmui.native.fuzzy;
import openmui.theme;
import openmui.theme.default_theme;
import openmui.theme.compact;

using namespace omui::testing;
using namespace omui::abi;
namespace lang = omui::lang;
namespace py = omui::lang::python::embedded;
using lang::Json;

namespace {

OmuiString str(std::string_view s) { return {s.data(), s.size()}; }
void collect(void* user, const char* data, std::size_t size) { static_cast<std::string*>(user)->append(data, size); }

struct Runtime {
    OmuiRuntime rt = nullptr;
    double      time = 0;
    int         width = 320, height = 240;
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
        in.width = width;
        in.height = height;
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

struct Python {
    Runtime&                 r;
    lang::Host               host;
    lang::Session            session;
    py::Script               script;
    std::vector<std::string> errors;
    explicit Python(Runtime& rt) : r(rt), host(rt.rt), session(host), script(session) {
        script.on_error([this](std::string_view m) { errors.emplace_back(m); });
    }
    lang::Result<void> run(std::string code, std::string name = "<script>") { return r.wait(host, script.run(std::move(code), std::move(name))); }
};

constexpr std::string_view counter_py = R"py(
import openmui as omui

count = omui.signal(0)

def counter():
    with omui.column(gap=8):
        omui.text(text=omui.fmt("{} clicks", count))
        omui.button(id="inc", text="+1", on_activated=lambda e: count.update(lambda v: v + 1))

omui.mount("main", counter)
)py";

constexpr std::string_view counter_document = R"j({"omui": "0.1", "view": {
  "signals": {"count": {"type": "int", "value": 0}},
  "root": {"type": "column", "props": {"gap": 8}, "children": [
    {"type": "text", "props": {"text": {"expr": "format('{} clicks', count)"}}},
    {"type": "button", "id": "inc", "props": {"text": "+1"}, "on": {"activated": [{"set": "count", "expr": "count + 1"}]}}]}}})j";

// A verb of the script, called as the runtime calls it: its data, or what
// went wrong ({"runner-error": …}).
Json invoke(Runtime& r, std::string_view verb, const Json& args) {
    std::string out;
    const std::string a = args.dump();
    (void)omuiInvoke(r.rt, str(verb), str(a), collect, &out);
    Json data = nullptr;
    for (auto line : out | std::views::split('\n')) {
        Json rec = Json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
        if (!rec.is_object()) continue;
        if (rec.value("kind", "") == "error") return Json{{"runner-error", rec.dump()}};
        if (rec.value("kind", "") == "data" && rec.contains("data")) data = rec["data"];
    }
    return data;
}

std::string text_of(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::filesystem::path tests_dir() { return std::filesystem::path(std::source_location::current().file_name()).parent_path(); }

bool has(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

}  // namespace

int main(int argc, char** argv) {
    Suite s{"lang.python.embedded"};
    omui::register_base_components();

    s.test("the counter in Python is the hand-written document's tree, before and after a click (S level)", [] {
        Runtime py_rt, doc_rt;
        Python py(py_rt);
        auto ran = py.run(std::string(counter_py), "counter.py");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(omuiMount(doc_rt.rt, str("main"), str(counter_document)), OMUI_SUCCESS);
        doc_rt.frame();
        check_eq(py_rt.tree("main"), doc_rt.tree("main"));
        for (int i = 0; i < 3; ++i) {
            check_eq(py_rt.act("main/inc", "activate"), std::int64_t{0});
            check_eq(doc_rt.act("main/inc", "activate"), std::int64_t{0});
        }
        py_rt.settle(py.host);
        doc_rt.frame();
        check_eq(py_rt.name_of("main/text:0"), std::string("3 clicks"));
        check_eq(py_rt.tree("main"), doc_rt.tree("main"));
        check(py.errors.empty(), py.errors.empty() ? "" : py.errors[0]);
    });

    s.test("builders as calls and as with blocks make one document; omui.document shows it", [] {
        Runtime r;
        Python py(r);
        auto ran = py.run(R"py(
import openmui as omui
n = omui.signal(1)
def nested():
    with omui.column(gap=4) as c:
        omui.text(id="a", text="A")
        with omui.row():
            omui.text(text=omui.fmt("{}", n))
    return c
called = omui.column(omui.text(id="a", text="A"), omui.row(omui.text(text=omui.fmt("{}", n))), gap=4)
assert omui.document(nested) == omui.document(called)
assert omui.document(called)["view"]["root"]["children"][1]["type"] == "row"
omui.mount("main", nested)
)py");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(r.name_of("main/a"), std::string("A"));
    });

    s.test("expressions: signal arithmetic and the expression language, evaluated by the runtime", [] {
        Runtime r;
        Python py(r);
        auto ran = py.run(R"py(
import openmui as omui
a, b = omui.signal(6), omui.signal(7)
name = omui.signal("ada")
omui.mount("main", omui.column(
    omui.text(id="sum", text=omui.fmt("{}", a * b + 1)),
    omui.text(id="who", text=omui.call("upper", name)),
    omui.text(id="big", text=omui.select(a > 5, "big", "small")),
    omui.text(id="neg", text=omui.fmt("{}", -a % 4)),
    omui.text(id="both", text=omui.select((a >= 1) & ~(b == 0), "yes", "no")),
))
assert repr(a * 2).endswith(" * 2))"), repr(a * 2)
assert a.get() == 6 and a.value == 6
a.set(1)
assert a.get() == 1, "reading never waits: the mirror has it"
try:
    if a == 1:
        pass
    raise AssertionError("an expression has a truth value")
except TypeError:
    pass
)py");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(r.name_of("main/sum"), std::string("8"));
        check_eq(r.name_of("main/who"), std::string("ADA"));
        check_eq(r.name_of("main/big"), std::string("small"));
        check_eq(r.name_of("main/neg"), std::string("-1"));
        check_eq(r.name_of("main/both"), std::string("yes"));
    });

    s.test("tr: a message in the person's language, its arguments by name; a key no catalog has is its own message", [] {
        (void)omui::languages().add_messages(*omui::i18n::Locale::parse("en"), "greeting = \"Hello, {$name}!\"\n", "en.messages.omui.toml");
        omui::languages().request({*omui::i18n::Locale::parse("en")});
        Runtime r;
        Python py(r);
        auto ran = py.run(R"py(
import openmui as omui
name = omui.signal("Ada")
omui.mount("main", omui.column(
    omui.text(id="greeting", text=omui.tr("greeting", name=name)),
    omui.text(id="raw", text=omui.tr("{$n} left", n=3)),
))
name.set("Grace")
)py");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(r.name_of("main/greeting"), std::string("Hello, Grace!"));
        check_eq(r.name_of("main/raw"), std::string("3 left"));
    });

    s.test("loops over a model, handlers with their row, a verb in Python, regions, watches", [] {
        Runtime r;
        Python py(r);
        auto ran = py.run(R"py(
import openmui as omui
archived = []
seen = []

@omui.verb("note.archive", summary="Archive a note")
def archive(args):
    archived.append(args["id"])
    return {"archived": args["id"]}

only_open = omui.signal(False)
only_open.watch(lambda v: seen.append(v))
notes = omui.model("notes")
notes.push([{"key": "n1", "title": "Milk", "done": True}, {"key": "n2", "title": "Bread", "done": False}])
omui.mount("main", omui.column(
    omui.when(only_open, omui.text(id="filter", text="open only")),
    omui.each("notes", lambda note: omui.row(
        omui.text(text=note.title),
        omui.button(id="archive", text="Archive", on_activated=lambda e: omui.invoke("note.archive", {"id": e["rows"]["row"]["key"]})),
    ), where=omui.or_(~only_open, ~omui.call("lookup", None, "x", False))),
))
assert notes.rows()[1]["title"] == "Bread"
def show_filter():
    only_open.set(True)
)py");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(r.name_of("main/n2/text:0"), std::string("Bread"));
        check_eq(r.act("main/n2/archive", "activate"), std::int64_t{0});
        r.settle(py.host);
        check(py.errors.empty(), py.errors.empty() ? "" : py.errors[0]);
        auto archived = py.run("assert archived == ['n2'], archived");
        check(archived.has_value(), archived ? "" : archived.error().text());
        auto shown = py.run("show_filter()\nassert seen == [True], seen");
        check(shown.has_value(), shown ? "" : shown.error().text());
        check_eq(r.name_of("main/filter"), std::string("open only"));
        check_eq(invoke(r, "note.archive", Json{{"id", "n9"}}).dump(), std::string(R"({"archived":"n9"})"));
    });

    s.test("the semantic tree: find, get, set, act and snapshot", [] {
        Runtime r;
        Python py(r);
        auto ran = py.run(R"py(
import openmui as omui
omui.mount("main", omui.column(omui.text(id="t", text="before"), omui.button(id="b", text="Go")))
)py");
        check(ran.has_value(), ran ? "" : ran.error().text());
        auto tree = py.run(R"py(
node = omui.find("main/t")
assert node.get("text") == "before", node.get("text")
node.set(text="after")
nodes = omui.snapshot(root="main")["nodes"]
assert [n["id"] for n in nodes] == ["main/t", "main/b"], [n["id"] for n in nodes]
assert omui.act(node="main/b", action="activate") is not None
try:
    omui.find("main/nope")
    raise AssertionError("found")
except omui.OmuiError as e:
    assert "no node `main/nope`" in str(e), str(e)
)py");
        check(tree.has_value(), tree ? "" : tree.error().text());
        check_eq(r.name_of("main/t"), std::string("after"));
    });

    s.test("native modules: omui.native.<module>.<function>, from openmui.native import …, OmuiError on a refusal", [] {
        if (!omui::native::find("fuzzy")) check(omui::natives::fuzzy::define().has_value(), "the official native module");
        Runtime r;
        Python py(r);
        auto ran = py.run(R"py(
import openmui as omui
from openmui.native import fuzzy
best = fuzzy.rank("mcfg", ["src/main.cpp", "my_config.toml", "docs/mcfg.md"])
assert len(best) == 2 and best[0]["text"] == "docs/mcfg.md" and best[0]["index"] == 2, best[0]
assert best[0]["segments"][1]["text"] == "mcfg" and best[0]["segments"][1]["matched"] is True
assert len(fuzzy.rank("mcfg", ["docs/mcfg.md", "my_config.toml"], 1)) == 1, "the limit"
assert fuzzy.match_("zz", "foo") is None, "nothing is None (match is a keyword of PocketPy's: match_)"
assert getattr(fuzzy, "match") is fuzzy.match_
try:
    fuzzy.rank("q", ["a", 3])
    raise AssertionError("ranked")
except omui.OmuiError as e:
    assert "invalid-argument" in str(e) and "`candidates[1]` is the number 3" in str(e), str(e)
assert omui.native.fuzzy is fuzzy
assert getattr(omui.native, "nope", None) is None
)py");
        check(ran.has_value(), ran ? "" : ran.error().text());
    });

    s.test("refusals are OmuiError with the runtime's message; errors say the file and the line", [] {
        Runtime r;
        Python py(r);
        auto ran = py.run(R"py(
import openmui as omui
try:
    omui.mount("main", omui.element("buton", id="x"))
    raise AssertionError("mounted")
except omui.OmuiError as e:
    assert "did you mean `button`" in str(e), str(e)
try:
    omui.invoke("no.such", {})
    raise AssertionError("invoked")
except omui.OmuiError as e:
    assert "not-found" in str(e), str(e)
try:
    omui.signal(object())
    raise AssertionError("a signal of an object")
except omui.OmuiError as e:
    assert "not data" in str(e), str(e)
)py");
        check(ran.has_value(), ran ? "" : ran.error().text());
        auto syntax = py.run("x = = 1", "tally.py");
        check(!syntax && has(syntax.error().text(), "tally.py") && has(syntax.error().text(), "line 1") && has(syntax.error().text(), "SyntaxError"),
              syntax ? "" : syntax.error().text());
        auto raised = py.run("import openmui as omui\n\nomui.mount('main', 3)", "tally.py");
        check(!raised && has(raised.error().text(), "line 3") && has(raised.error().text(), "OmuiError"), raised ? "" : raised.error().text());
        // A handler that raises: reported, and the runtime goes on.
        auto handler = py.run("omui.mount('main', omui.column(omui.button(id='b', text='x', on_activated=lambda e: 1 / 0)))", "tally.py");
        check(handler.has_value(), handler ? "" : handler.error().text());
        check_eq(r.act("main/b", "activate"), std::int64_t{0});
        r.settle(py.host);
        check(py.errors.size() == 1 && has(py.errors[0], "ZeroDivisionError") && has(py.errors[0], "tally.py"), py.errors.empty() ? "none" : py.errors[0]);
    });

    s.test("a reload keeps the state, the nodes, and replaces the handlers and watches, in under 200 ms", [] {
        Runtime r;
        Python py(r);
        std::string first(counter_py);
        first += "watched = []\ncount.watch(lambda v: watched.append(v))\n";
        check(py.run(first, "counter.py").has_value());
        for (int i = 0; i < 2; ++i) {
            check_eq(r.act("main/inc", "activate"), std::int64_t{0});
            r.settle(py.host);
        }
        std::string next(first);
        next.replace(next.find("{} clicks"), 9, "{} taps");
        next.replace(next.find("v + 1"), 5, "v + 10");
        next.replace(next.find("watched = []\n"), 13, "");  // the list stays: a global of the last run
        const auto start = std::chrono::steady_clock::now();
        auto reloaded = r.wait(py.host, py.script.reload(next, "counter.py"));
        const auto took = std::chrono::steady_clock::now() - start;
        check(reloaded.has_value(), reloaded ? "" : reloaded.error().text());
        check_eq(r.name_of("main/text:0"), std::string("2 taps"));
        check(took < std::chrono::milliseconds(200), std::format("{}", std::chrono::duration_cast<std::chrono::milliseconds>(took)));
        std::println("    reload: {} µs", std::chrono::duration_cast<std::chrono::microseconds>(took).count());
        check_eq(r.act("main/inc", "activate"), std::int64_t{0});
        r.settle(py.host);
        check_eq(r.name_of("main/text:0"), std::string("12 taps"));
        auto once = py.run("assert watched == [1, 2, 12], watched");  // the watch of the last run is gone
        check(once.has_value(), once ? "" : once.error().text());
        check(py.errors.empty(), py.errors.empty() ? "" : py.errors[0]);
    });

    s.test("a reload re-registers its verbs: the new function answers; another effect is refused", [] {
        Runtime r;
        Python py(r);
        constexpr std::string_view v1 = R"py(
import openmui as omui
omui.verb("gift.slow", lambda args: {"slow_percent": 50}, effect="read")
)py";
        check(py.run(std::string(v1)).has_value());
        check_eq(invoke(r, "gift.slow", Json::object()).dump(), std::string(R"({"slow_percent":50})"));
        std::string v2(v1);
        v2.replace(v2.find("50"), 2, "25");
        auto again = r.wait(py.host, py.script.reload(v2));
        check(again.has_value(), again ? "" : again.error().text());
        check_eq(invoke(r, "gift.slow", Json::object()).dump(), std::string(R"({"slow_percent":25})"));
        std::string v3(v2);
        v3.replace(v3.find("\"read\""), 6, "\"reversible\"");
        auto other = r.wait(py.host, py.script.reload(v3));
        check(!other && has(other.error().text(), "another effect"), other ? "reloaded" : other.error().text());
        check_eq(invoke(r, "gift.slow", Json::object()).dump(), std::string(R"({"slow_percent":25})"));
        // A verb that raises is refused, with the exception's line.
        check(py.run("omui.verb('gift.broken', lambda args: {}['x'])").has_value());
        const Json broken = invoke(r, "gift.broken", Json::object());
        check(broken.contains("runner-error") && has(broken["runner-error"].get<std::string>(), "KeyError"), broken.dump());
    });

    s.test("a runaway script is stopped by the watchdog, and the script goes on", [] {
        Runtime r;
        Python py(r);
        py.script.budget(std::chrono::milliseconds(100));
        const auto start = std::chrono::steady_clock::now();
        auto loop = py.run("n = 0\nwhile True:\n    n += 1\n", "spin.py");
        check(!loop && has(loop.error().text(), "TimeoutError") && has(loop.error().text(), "spin.py"), loop ? "ran" : loop.error().text());
        check(std::chrono::steady_clock::now() - start < std::chrono::seconds(5), "stopped soon");
        auto after = py.run("assert n > 0\nimport openmui as omui\nomui.mount('main', omui.column(omui.text(id='t', text='alive')))");
        check(after.has_value(), after ? "" : after.error().text());
        check_eq(r.name_of("main/t"), std::string("alive"));
        // A handler that never returns is stopped too; the runtime is not.
        check(py.run("def spin(e):\n    while True:\n        pass\nomui.mount('main', omui.column(omui.button(id='b', text='x', on_activated=spin)))").has_value());
        check_eq(r.act("main/b", "activate"), std::int64_t{0});
        r.settle(py.host);
        check(!py.errors.empty() && has(py.errors[0], "TimeoutError"), py.errors.empty() ? "none" : py.errors[0]);
    });

    s.test("exit() ends the script, not the program; exit(0) and SystemExit(0) are a clean end", [] {
        Runtime r;
        Python py(r);
        auto three = py.run("x = 1\nexit(3)\nx = 2", "quit.py");
        check(!three && has(three.error().text(), "SystemExit: 3"), three ? "ran" : three.error().text());
        auto clean = py.run("assert x == 1\nexit()");
        check(clean.has_value(), clean ? "" : clean.error().text());
        auto zero = py.run("raise SystemExit(0)");
        check(zero.has_value(), zero ? "" : zero.error().text());
        auto alive = py.run("x = 3");
        check(alive.has_value(), alive ? "" : alive.error().text());
    });

    s.test("two scripts at once: a VM each, their state their own", [] {
        Runtime ra, rb;
        Python a(ra), b(rb);
        check(a.run("import openmui as omui\nn = omui.signal(1)\nomui.mount('main', omui.column(omui.text(id='t', text=omui.fmt('{}', n))))").has_value());
        check(b.run("import openmui as omui\nn = omui.signal(2)\nomui.mount('main', omui.column(omui.text(id='t', text=omui.fmt('{}', n * 10))))").has_value());
        check(a.run("n.set(5)").has_value());
        check(b.run("assert n.get() == 2").has_value());
        check_eq(ra.name_of("main/t"), std::string("5"));
        check_eq(rb.name_of("main/t"), std::string("20"));
    });

    s.test("the player's language: lang = \"py\", *.py; its engine runs and reloads a script", [] {
        const omui::player::Language l = py::language();
        check_eq(l.name, std::string("py"));
        check(l.extensions == std::vector<std::string>{".py"} && !l.load, "*.py, run as it is");
        Runtime r;
        auto engine = l.make(r.rt);
        auto wait = [&](auto f) {  // (a named future of player::Status: an internal error of GCC 16.1)
            while (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                r.frame();
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            for (int i = 0; i < 5000 && !engine->idle(); ++i) r.frame();
            r.frame();
            return f.get();
        };
        constexpr std::string_view app = "import openmui as omui\nn = omui.signal(4)\nomui.mount('main', omui.column(omui.text(id='t', text=omui.fmt('{}', n))))\n";
        auto ran = wait(engine->run(std::string(app), "app.py"));
        check(ran.has_value(), ran ? "" : ran.error());
        check_eq(r.name_of("main/t"), std::string("4"));
        std::string next(app);
        next.replace(next.find("'{}'"), 4, "'{}!'");
        next.replace(next.find("signal(4)"), 9, "signal(9)");  // the signal keeps its value
        auto again = wait(engine->reload(next, "app.py"));
        check(again.has_value(), again ? "" : again.error());
        check_eq(r.name_of("main/t"), std::string("4!"));
        auto bad = wait(engine->reload("x = = 1", "app.py"));
        check(!bad && has(bad.error(), "app.py") && has(bad.error(), "SyntaxError"), bad ? "ran" : bad.error());
    });

    s.test("CTS level S: every scenario records its pinned trace (spec/conformance/S, tests/cts.py)", [] {
        if (!omui::native::find("fuzzy")) check(omui::natives::fuzzy::define().has_value(), "the official native module");
        check(omui::themes::add_default().has_value() && omui::themes::add_compact().has_value(), "the official themes");
        auto all = omui::cts::scenarios();
        check(all.has_value() && all->size() >= 3, all ? "fewer than three scenarios" : all.error());
        if (!all) return;
        const std::string runner = text_of(tests_dir() / "cts.py");
        for (const omui::cts::Scenario& scenario : *all) {
            Runtime r;
            r.height = 480;  // the scenarios' viewport: 320 × 480
            Python py(r);
            auto ran = py.run(runner, "cts.py");
            check(ran.has_value(), ran ? "" : ran.error().text());
            if (!ran) continue;
            const Json trace = omui::cts::run(
                scenario, [&](std::string_view verb, const Json& args) { return invoke(r, verb, args); },
                [&] {
                    // Until the session is idle: an async native's answer is in flight too.
                    int quiet = 0;
                    for (int i = 0; i < 20000 && quiet < 3; ++i) {
                        r.frame();
                        quiet = py.session.idle() ? quiet + 1 : 0;
                        if (!quiet) std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                });
            for (const std::string& d : omui::cts::check(scenario, trace)) fail(scenario.name + ": " + d);
            check(py.errors.empty(), py.errors.empty() ? "" : scenario.name + ": " + py.errors[0]);
        }
    });

    return s.run(argc, argv);
}
