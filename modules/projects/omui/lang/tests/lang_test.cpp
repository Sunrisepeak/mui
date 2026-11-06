// The Script Binding Profile from C++, as a language binding drives it: the
// script's work on its own thread, a view as a document (the same tree as the
// document written by hand), mirrored signals, handlers with their rows, and
// a verb the script implements while the runtime waits.
import std;
import nlohmann.json;
import openmui.abi;
import openmui.lang;
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

int main(int argc, char** argv) {
    Suite s{"lang"};
    omui::register_base_components();

    s.test("a script's counter is the hand-written document's tree, before and after a click", [] {
        Runtime script_rt, doc_rt;
        lang::Host host(script_rt.rt);
        lang::Session session(host);
        host.post([&] {
            auto count = session.signal(0);
            check(count.has_value());
            lang::Element text{.type = "Text"};
            text.props.emplace_back("text", lang::Prop{.kind = lang::Prop::Kind::expr, .expr = lang::fmt("{} clicks", {lang::signal_ref(*count)})});
            lang::Element button{.type = "Button", .id = "inc"};
            button.props.emplace_back("text", lang::Prop{.value = "+1"});
            button.on.emplace_back("activated", [&session, c = *count](const Json&) { session.set(c, session.get(c).get<int>() + 1); });
            lang::Element column{.type = "Column"};
            column.props.emplace_back("gap", lang::Prop{.value = 8});
            column.children = {text, button};
            auto m = session.mount("main", column);
            check(m.has_value(), m ? "" : m.error().text());
        });
        script_rt.settle(host);
        check_eq(omuiMount(doc_rt.rt, str("main"), str(counter_document)), OMUI_SUCCESS);
        doc_rt.frame();
        check_eq(script_rt.tree("main"), doc_rt.tree("main"));
        check_eq(script_rt.act("main/inc", "activate"), std::int64_t{0});
        check_eq(doc_rt.act("main/inc", "activate"), std::int64_t{0});
        script_rt.settle(host);
        doc_rt.frame();
        check_eq(script_rt.name_of("main/text:0"), std::string("1 clicks"));
        check_eq(script_rt.tree("main"), doc_rt.tree("main"));
    });

    s.test("signals are mirrored: reading never waits; both sides' writes arrive", [] {
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
        check_eq(omuiSignalSet(r.rt, sig, &v), OMUI_SUCCESS);
        r.settle(host);
        Json read;
        host.post([&] { read = session.get(sig); });
        r.settle(host);
        check_eq(read.dump(), std::string(R"j({"a":2})j"));
        // The script writes it: the runtime has it by the next frame.
        host.post([&] { session.set(sig, Json{{"a", 3}}); });
        r.settle(host);
        std::string out;
        check_eq(omuiSignalGet(r.rt, sig, collect, &out), OMUI_SUCCESS);
        check_eq(out, std::string(R"j({"a":3})j"));
        check(seen.size() == 2 && seen[0]["a"] == 2 && seen[1]["a"] == 3, std::to_string(seen.size()));
    });

    s.test("a loop's handlers receive their row; a verb the script implements answers through the runtime", [] {
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
            check(v.has_value(), v ? "" : v.error().text());
            lang::Element title{.type = "text"};
            title.props.emplace_back("text", lang::Prop{.kind = lang::Prop::Kind::expr, .expr = lang::member(lang::name_ref("note"), "title")});
            lang::Element archive{.type = "button", .id = "archive"};
            archive.props.emplace_back("text", lang::Prop{.value = "Archive"});
            archive.on.emplace_back("activated", [&session](const Json& e) {
                auto out = session.invoke("note.archive", Json{{"id", e["rows"]["note"]["key"]}});
                check(out.has_value());
            });
            lang::Element row{.type = "row", .children = {title, archive}};
            lang::Element list{.type = "column"};
            list.children.push_back(lang::Element{.region = std::make_shared<lang::Region>(
                                                      lang::Region{.kind = lang::Region::Kind::each, .then = {row}, .model = "notes", .variable = "note"})});
            check(session.document(list)["view"]["models"].contains("notes"), "the document declares the model its loop reads");
            auto m = session.mount("main", list);
            check(m.has_value(), m ? "" : m.error().text());
            check(session.splice("notes", 0, 0, Json::array({{{"key", "n1"}, {"title", "Milk"}}, {{"key", "n2"}, {"title", "Bread"}}})).has_value());
        });
        r.settle(host);
        check_eq(r.name_of("main/n2/text:0"), std::string("Bread"));
        check_eq(r.act("main/n2/archive", "activate"), std::int64_t{0});
        r.settle(host);
        check(archived == std::vector<std::string>{"n2"}, archived.empty() ? "" : archived[0]);
        // An agent invokes the script's verb directly: the runtime waits for the script.
        std::string out;
        check_eq(omuiInvoke(r.rt, str("note.archive"), str(R"j({"id": "n1"})j"), collect, &out), OMUI_SUCCESS);
        check(out.find(R"j("archived":"n1")j") != std::string::npos && out.find(R"j("of":2)j") != std::string::npos, out);
        r.settle(host);
    });

    return s.run(argc, argv);
}
