// The Script Binding Profile from C++, as a language binding drives it: the
// script's work on its own thread, a view as a document (the same tree as the
// document written by hand), mirrored signals, handlers with their rows, and
// a verb the script implements while the runtime waits.
import std;
import nlohmann.json;
import omui.abi;
import omui.lang;
import omui.ui.base;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
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
        expect(eq(omuiCreateRuntime(&info, &rt), OMUI_SUCCESS));
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
    // Frames until the script has nothing left to do, twice in a row.
    void settle(const lang::Host& host) {
        int quiet = 0;
        for (int i = 0; i < 5000 && quiet < 3; ++i) {
            frame();
            quiet = host.idle() ? quiet + 1 : 0;
            if (!quiet) std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    // The semantic tree under a slot (its nodes, bounds included).
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
    std::int64_t act(std::string_view node, std::string_view action, std::string_view answer = {}) {
        std::string out;
        Json req{{"node", node}, {"action", action}};
        if (!answer.empty()) req["answer"] = answer;
        const std::string text = req.dump();
        return omuiAct(rt, str(text), collect, &out);
    }
};

constexpr std::string_view counter_document = R"j({"omui": "0.1", "view": {
  "signals": {"count": {"type": "int", "value": 0}},
  "root": {"type": "column", "props": {"gap": 8}, "children": [
    {"type": "text", "props": {"text": {"expr": "format('{} clicks', count)"}}},
    {"type": "button", "id": "inc", "props": {"text": "+1"}, "on": {"activated": [{"set": "count", "expr": "count + 1"}]}}]}}})j";

}  // namespace

UTest cases{"lang", [] {
    omui::register_base_components();

    "a script's counter is the hand-written document's tree, before and after a click"_test = [] {
        Runtime script_rt, doc_rt;
        lang::Host host(script_rt.rt);
        lang::Session session(host);
        host.post([&] {
            auto count = session.signal(0);
            expect(count.has_value());
            lang::Element text{.type = "Text"};
            text.props.emplace_back("text", lang::Prop{.kind = lang::Prop::Kind::expr, .expr = lang::fmt("{} clicks", {lang::signal_ref(*count)})});
            lang::Element button{.type = "Button", .id = "inc"};
            button.props.emplace_back("text", lang::Prop{.value = "+1"});
            button.on.emplace_back("activated", [&session, c = *count](const Json&) { session.set(c, session.get(c).get<int>() + 1); });
            lang::Element column{.type = "Column"};
            column.props.emplace_back("gap", lang::Prop{.value = 8});
            column.children = {text, button};
            auto m = session.mount("main", column);
            expect(m.has_value()) << (m ? "" : m.error().text());
        });
        script_rt.settle(host);
        expect(eq(omuiMount(doc_rt.rt, str("main"), str(counter_document)), OMUI_SUCCESS));
        doc_rt.frame();
        expect(eq(script_rt.tree("main"), doc_rt.tree("main")));
        expect(eq(script_rt.act("main/inc", "activate"), std::int64_t{0}));
        expect(eq(doc_rt.act("main/inc", "activate"), std::int64_t{0}));
        script_rt.settle(host);
        doc_rt.frame();
        expect(eq(script_rt.name_of("main/text:0"), std::string("1 clicks")));
        expect(eq(script_rt.tree("main"), doc_rt.tree("main")));
    };

    "signals are mirrored: reading never waits; both sides' writes arrive"_test = [] {
        Runtime r;
        lang::Host host(r.rt);
        lang::Session session(host);
        std::atomic<OmuiSignal> sig{0};
        std::vector<Json> seen;
        host.post([&] {
            sig = *session.signal(Json{{"a", 1}});
            session.watch(sig, [&](const Json& v) { seen.push_back(v); });
        });
        r.settle(host);
        // The runtime's side writes it (an agent, a document's `set`).
        OmuiValue v{};
        v.type = OMUI_VALUE_TYPE_JSON;
        v.s = str(R"j({"a": 2})j");
        expect(eq(omuiSignalSet(r.rt, sig, &v), OMUI_SUCCESS));
        r.settle(host);
        Json read;
        host.post([&] { read = session.get(sig); });
        r.settle(host);
        expect(eq(read.dump(), std::string(R"j({"a":2})j")));
        // The script writes it: the runtime has it by the next frame.
        host.post([&] { session.set(sig, Json{{"a", 3}}); });
        r.settle(host);
        std::string out;
        expect(eq(omuiSignalGet(r.rt, sig, collect, &out), OMUI_SUCCESS));
        expect(eq(out, std::string(R"j({"a":3})j")));
        expect(seen.size() == 2 && seen[0]["a"] == 2 && seen[1]["a"] == 3) << std::to_string(seen.size());
    };

    "a handler's echo of what it was told is no write: it never puts back a value the runtime has moved on from"_test = [] {
        Runtime r;
        lang::Host host(r.rt);
        lang::Session session(host);
        std::atomic<OmuiSignal> sig{0};
        // `onChanged: (e) => body.set(e.text)`: the script sets the signal to what it is told it now is.
        host.post([&] {
            sig = *session.signal(Json("one"));
            session.watch(sig, [&](const Json& told) { session.set(sig, told); });
        });
        r.settle(host);
        // Every value the runtime's signal takes, in order.
        std::vector<std::string> taken;
        OmuiSubscription watching = 0;
        expect(eq(omuiSignalWatch(r.rt, sig, [](void* user, OmuiSignal, OmuiString value) { static_cast<std::vector<std::string>*>(user)->emplace_back(value.data, value.size); },
                                  &taken, &watching),
                  OMUI_SUCCESS));
        // The script's thread is busy while a component changes the value twice (a task ticked, then ticked back).
        std::promise<void> busy;
        host.post([&] { busy.get_future().wait(); });
        for (const std::string_view next : {R"j("two")j", R"j("three")j"}) {
            OmuiValue v{};
            v.type = OMUI_VALUE_TYPE_JSON;
            v.s = str(next);
            expect(eq(omuiSignalSet(r.rt, sig, &v), OMUI_SUCCESS));
            r.frame();
        }
        busy.set_value();
        r.settle(host);
        std::string out;
        expect(eq(omuiSignalGet(r.rt, sig, collect, &out), OMUI_SUCCESS));
        expect(eq(out, std::string(R"j("three")j")));
        std::string history;
        for (const std::string& value : taken) history += value + " ";
        expect(taken == std::vector<std::string>{R"j("two")j", R"j("three")j"}) << "the runtime's value never went back: " << history;
    };

    "a loop's handlers receive their row; a verb the script implements answers through the runtime"_test = [] {
        Runtime r;
        lang::Host host(r.rt);
        lang::Session session(host);
        std::vector<std::string> archived;
        host.post([&] {
            auto v = session.verb({"note.archive", "Archive a note", OMUI_EFFECT_REVERSIBLE, ""}, [&](const Json& args) -> lang::Result<Json> {
                archived.push_back(args.value("id", ""));
                // A verb may ask the runtime things while the runtime waits for it.
                auto rows = session.rows("notes");
                return Json{{"archived", args.value("id", "")}, {"of", rows ? rows->size() : 0}};
            });
            expect(v.has_value()) << (v ? "" : v.error().text());
            lang::Element title{.type = "text"};
            title.props.emplace_back("text", lang::Prop{.kind = lang::Prop::Kind::expr, .expr = lang::member(lang::name_ref("note"), "title")});
            lang::Element archive{.type = "button", .id = "archive"};
            archive.props.emplace_back("text", lang::Prop{.value = "Archive"});
            archive.on.emplace_back("activated", [&session](const Json& e) {
                auto out = session.invoke("note.archive", Json{{"id", e["rows"]["note"]["key"]}});
                expect(out.has_value());
            });
            lang::Element row{.type = "row", .children = {title, archive}};
            lang::Element list{.type = "column"};
            list.children.push_back(lang::Element{.region = std::make_shared<lang::Region>(
                                                      lang::Region{.kind = lang::Region::Kind::each, .then = {row}, .model = "notes", .variable = "note"})});
            expect(session.document(list)["view"]["models"].contains("notes")) << "the document declares the model its loop reads";
            auto m = session.mount("main", list);
            expect(m.has_value()) << (m ? "" : m.error().text());
            expect(session.splice("notes", 0, 0, Json::array({{{"key", "n1"}, {"title", "Milk"}}, {{"key", "n2"}, {"title", "Bread"}}})).has_value());
        });
        r.settle(host);
        expect(eq(r.name_of("main/n2/text:0"), std::string("Bread")));
        expect(eq(r.act("main/n2/archive", "activate"), std::int64_t{0}));
        r.settle(host);
        expect(archived == std::vector<std::string>{"n2"}) << (archived.empty() ? "" : archived[0]);
        // An agent invokes the script's verb directly: the runtime waits for the script.
        std::string out;
        expect(eq(omuiInvoke(r.rt, str("note.archive"), str(R"j({"id": "n1"})j"), collect, &out), OMUI_SUCCESS));
        expect(out.find(R"j("archived":"n1")j") != std::string::npos && out.find(R"j("of":2)j") != std::string::npos) << out;
        r.settle(host);
    };
}};
