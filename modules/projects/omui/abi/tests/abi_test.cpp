// openmui.h as a language binding uses it: a runtime driven frame by frame,
// a view document mounted, nodes found, properties read and set, events,
// signals (the document's own and created ones), verbs with their answers,
// the bus interface (snapshot, act, wait), pixels, and every way to misuse a
// call answered by its result rather than a crash.
import std;
import openmui.abi;
import openmui.core;
import openmui.ui.base;
import openmui.native.fuzzy;
import openmui.testing;

using namespace omui::testing;
using namespace omui::abi;

namespace {

OmuiString str(std::string_view s) { return {s.data(), s.size()}; }

void collect(void* user, const char* data, std::size_t size) { static_cast<std::string*>(user)->append(data, size); }

struct Rt {
    OmuiRuntime rt = nullptr;
    double      time = 0;

    explicit Rt(bool headless = true) {
        OmuiRuntimeCreateInfo info{};
        info.type = OMUI_STRUCTURE_TYPE_RUNTIME_CREATE_INFO;
        info.spec_version = OMUI_SPEC_VERSION;
        info.app_id = str("org.openmui.abi-test");
        info.headless = headless ? 1 : 0;
        check_eq(omuiCreateRuntime(&info, &rt), OMUI_SUCCESS);
    }
    ~Rt() { (void)omuiDestroyRuntime(rt); }

    OmuiResult frame(std::uint32_t w = 320, std::uint32_t h = 200, std::string_view events = {}, OmuiFrameOutput* out = nullptr) {
        OmuiFrameInput in{};
        in.type = OMUI_STRUCTURE_TYPE_FRAME_INPUT;
        in.time = time += 1.0 / 60.0;
        in.width = w;
        in.height = h;
        in.scale = 1;
        in.events_json = str(events);
        return omuiFrame(rt, &in, out);
    }

    OmuiNode find(std::string_view selector) {
        OmuiNode n = 0;
        check_eq(omuiFind(rt, str(selector), &n), OMUI_SUCCESS);
        return n;
    }

    std::string get(OmuiNode n, std::string_view prop) {
        std::string out;
        check_eq(omuiGet(rt, n, str(prop), collect, &out), OMUI_SUCCESS);
        return out;
    }

    // The node's semantic name, from the bus interface's snapshot.
    std::string name_of(std::string_view id) {
        std::string out;
        const std::string q = std::format(R"j({{"root": "{}", "depth": 0}})j", id);
        (void)omuiSnapshot(rt, str(q), collect, &out);
        for (std::string_view line : out | std::views::split('\n') | std::views::transform([](auto r) { return std::string_view(r); })) {
            auto v = omui::parse_json(line);
            if (!v || !v->get("data")) continue;
            if (const omui::Value* nodes = v->get("data")->get("nodes"); nodes && nodes->list())
                for (const omui::Value& n : *nodes->list())
                    if (n.get("id") && n.get("id")->as_string() == id) return std::string(n.get("name") ? n.get("name")->as_string() : "");
        }
        return "<none>";
    }
};

constexpr std::string_view counter = R"j({"omui": "0.1", "view": {
  "signals": {"count": {"type": "int", "value": 0}},
  "root": {"type": "column", "props": {"gap": 8}, "children": [
    {"type": "text", "id": "label", "props": {"text": {"expr": "format('{} clicks', count)"}}},
    {"type": "button", "id": "inc", "props": {"text": "+1"}, "on": {"activated": [{"set": "count", "expr": "count + 1"}]}}]}}})j";

struct Store {
    std::vector<std::string> deleted;
};

// A verb implemented in C: it reads its arguments and answers through the sink.
OmuiResult delete_note(void* user, OmuiString args, PFN_omuiSink sink, void* sink_user) {
    auto v = omui::parse_json(std::string_view(args.data, args.size));
    static_cast<Store*>(user)->deleted.emplace_back(v->get("id")->as_string());
    constexpr std::string_view answer = R"j({"deleted": true})j";
    sink(sink_user, answer.data(), answer.size());
    return OMUI_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"abi"};
    omui::register_base_components();

    s.test("creating a runtime: versions, extensions, profiles", [] {
        OmuiRuntimeCreateInfo info{};
        OmuiRuntime rt = nullptr;
        info.type = OMUI_STRUCTURE_TYPE_FRAME_INPUT;
        check_eq(omuiCreateRuntime(&info, &rt), OMUI_ERROR_INVALID_ARGUMENT);  // not a create info
        info.type = OMUI_STRUCTURE_TYPE_RUNTIME_CREATE_INFO;
        info.headless = 1;
        info.spec_version = (OMUI_SPEC_VERSION_MAJOR << 16) | (OMUI_SPEC_VERSION_MINOR + 1);
        check_eq(omuiCreateRuntime(&info, &rt), OMUI_ERROR_INCOMPATIBLE_VERSION);
        info.spec_version = OMUI_SPEC_VERSION;
        OmuiString ext = str("OMUI_EXT_nothing");
        info.extension_count = 1;
        info.extensions = &ext;
        check_eq(omuiCreateRuntime(&info, &rt), OMUI_ERROR_EXTENSION_NOT_PRESENT);
        info.extension_count = 0;
        check_eq(omuiCreateRuntime(&info, &rt), OMUI_SUCCESS);
        std::uint32_t count = 7;
        check_eq(omuiEnumerateExtensions(rt, &count, nullptr), OMUI_SUCCESS);
        check_eq(count, 0u);
        check_eq(omuiRun(rt), OMUI_ERROR_INVALID_ARGUMENT);  // headless: frames come from omuiFrame
        check_eq(omuiDestroyRuntime(rt), OMUI_SUCCESS);
        Rt windowed(false);
        check_eq(omuiRun(windowed.rt), OMUI_ERROR_EXTENSION_NOT_PRESENT);  // no desktop host linked here
    });

    s.test("a view document mounted, found, read, operated by the bus interface", [] {
        Rt r;
        check_eq(omuiMount(r.rt, str("main"), str(counter)), OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        const OmuiNode inc = r.find("main/inc");
        check_eq(r.get(inc, "text"), std::string("\"+1\""));
        check_eq(r.find(R"j({"role": "button", "name": "+1"})j"), inc);
        OmuiNode none = 1;
        check_eq(omuiFind(r.rt, str("main/nothing"), &none), OMUI_ERROR_NOT_FOUND);
        std::string out;
        check_eq(omuiAct(r.rt, str(R"j({"node": "main/inc", "action": "activate"})j"), collect, &out), OMUI_SUCCESS);
        check(out.find("\"exitCode\":0") != std::string::npos, out);
        check_eq(r.frame(), OMUI_SUCCESS);
        check_eq(r.name_of("main/label"), std::string("1 clicks"));
        // The document's signal, as the binding's own signal object refers to it.
        OmuiSignal count = 0;
        check_eq(omuiSignalFind(r.rt, str("main"), str("count"), &count), OMUI_SUCCESS);
        std::string value;
        check_eq(omuiSignalGet(r.rt, count, collect, &value), OMUI_SUCCESS);
        check_eq(value, std::string("1"));
        OmuiValue ten{};
        ten.type = OMUI_VALUE_TYPE_INT;
        ten.i = 10;
        check_eq(omuiSignalSet(r.rt, count, &ten), OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        check_eq(r.name_of("main/label"), std::string("10 clicks"));
        // Mounted again: reconciled, the state kept.
        std::string next(counter);
        next.replace(next.find("clicks"), 6, "taps");
        check_eq(omuiMount(r.rt, str("main"), str(next)), OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        check_eq(r.name_of("main/label"), std::string("10 taps"));
        check_eq(r.find("main/inc"), inc);
        check_eq(omuiMount(r.rt, str("main"), str(R"j({"omui": "0.1", "view": {"root": {"type": "nope"}}})j")), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiUnmount(r.rt, str("main")), OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        std::string stale;
        check_eq(omuiGet(r.rt, inc, str("text"), collect, &stale), OMUI_ERROR_STALE_HANDLE);
        check_eq(omuiUnmount(r.rt, str("main")), OMUI_ERROR_NOT_FOUND);
    });

    s.test("properties set as a batch at the next frame; events; created signals bound", [] {
        Rt r;
        check_eq(omuiMount(r.rt, str("main"), str(counter)), OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        const OmuiNode inc = r.find("main/inc");
        OmuiProp props[2]{};
        props[0].name = str("text");
        props[0].value.type = OMUI_VALUE_TYPE_STRING;
        props[0].value.s = str("Add");
        props[1].name = str("disabled");
        props[1].value.type = OMUI_VALUE_TYPE_BOOL;
        props[1].value.b = 0;
        check_eq(omuiSet(r.rt, inc, props, 2), OMUI_SUCCESS);
        check_eq(r.get(inc, "text"), std::string("\"+1\""));  // not before the frame
        check_eq(r.frame(), OMUI_SUCCESS);
        check_eq(r.get(inc, "text"), std::string("\"Add\""));
        props[0].value.type = OMUI_VALUE_TYPE_JSON;
        props[0].value.s = str("{bad");
        check_eq(omuiSet(r.rt, inc, props, 1), OMUI_ERROR_INVALID_ARGUMENT);

        struct Seen {
            int         n = 0;
            std::string event, args;
            OmuiNode    node = 0;
        } seen;
        OmuiSubscription sub = 0;
        check_eq(omuiOn(r.rt, inc, str("activated"),
                        [](void* u, OmuiNode node, OmuiString event, OmuiString args) {
                            auto* s = static_cast<Seen*>(u);
                            ++s->n;
                            s->node = node;
                            s->event.assign(event.data, event.size);
                            s->args.assign(args.data, args.size);
                        },
                        &seen, &sub),
                 OMUI_SUCCESS);
        std::string out;
        (void)omuiAct(r.rt, str(R"j({"node": "main/inc", "action": "activate"})j"), collect, &out);
        check(seen.n == 1 && seen.node == inc && seen.event == "activated", seen.event);
        check_eq(omuiOff(r.rt, sub), OMUI_SUCCESS);
        (void)omuiAct(r.rt, str(R"j({"node": "main/inc", "action": "activate"})j"), collect, &out);
        check_eq(seen.n, 1);
        check_eq(omuiOff(r.rt, sub), OMUI_ERROR_NOT_FOUND);

        // A signal the binding creates, bound to a property, and watched.
        OmuiValue hello{};
        hello.type = OMUI_VALUE_TYPE_STRING;
        hello.s = str("hello");
        OmuiSignal sig = 0;
        check_eq(omuiSignalCreate(r.rt, &hello, &sig), OMUI_SUCCESS);
        check_eq(omuiBind(r.rt, r.find("main/label"), str("text"), sig), OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        check_eq(r.name_of("main/label"), std::string("hello"));
        std::vector<std::string> changes;
        OmuiSubscription watch = 0;
        check_eq(omuiSignalWatch(r.rt, sig,
                                 [](void* u, OmuiSignal, OmuiString v) { static_cast<std::vector<std::string>*>(u)->emplace_back(v.data, v.size); },
                                 &changes, &watch),
                 OMUI_SUCCESS);
        OmuiValue list{};
        list.type = OMUI_VALUE_TYPE_JSON;
        list.s = str(R"j(["a", 1])j");
        check_eq(omuiSignalSet(r.rt, sig, &list), OMUI_SUCCESS);
        check_eq(omuiSignalSet(r.rt, sig, &list), OMUI_SUCCESS);  // the same value: no change
        check(changes == std::vector<std::string>{R"j(["a",1])j"}, changes.empty() ? "" : changes[0]);
        check_eq(omuiOff(r.rt, watch), OMUI_SUCCESS);
        check_eq(omuiSignalSet(r.rt, 999, &list), OMUI_ERROR_NOT_FOUND);
    });

    s.test("a shared signal is one state for the binding and every view; a view's events reach the binding", [] {
        Rt r;
        OmuiValue zero{};
        zero.type = OMUI_VALUE_TYPE_INT;
        OmuiSignal count = 0;
        check_eq(omuiSignalCreate(r.rt, &zero, &count), OMUI_SUCCESS);
        check_eq(omuiSignalShare(r.rt, count, str("count")), OMUI_SUCCESS);
        constexpr std::string_view shared = R"j({"omui": "0.1", "view": {"signals": {"count": {"type": "int", "provided": true}},
          "root": {"type": "button", "id": "b", "props": {"text": {"expr": "format('{}', count)"}},
                   "on": {"activated": [{"set": "count", "expr": "count + 1"}, {"emit": "h1", "args": {"n": "count"}}]}}}})j";
        check_eq(omuiMount(r.rt, str("a"), str(shared)), OMUI_SUCCESS);
        check_eq(omuiMount(r.rt, str("b"), str(shared)), OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        std::vector<std::string> seen;
        OmuiSubscription sub = 0;
        check_eq(omuiOn(r.rt, r.find("a"), str("h1"),
                        [](void* u, OmuiNode, OmuiString, OmuiString args) { static_cast<std::vector<std::string>*>(u)->emplace_back(args.data, args.size); },
                        &seen, &sub),
                 OMUI_SUCCESS);
        std::string out;
        check_eq(omuiAct(r.rt, str(R"j({"node": "a", "action": "activate"})j"), collect, &out), OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        check_eq(r.name_of("b"), std::string("1"));
        check(seen == std::vector<std::string>{R"j({"n":1})j"}, seen.empty() ? "" : seen[0]);
        std::string value;
        check_eq(omuiSignalGet(r.rt, count, collect, &value), OMUI_SUCCESS);
        check_eq(value, std::string("1"));
        check_eq(omuiMount(r.rt, str("c"), str(R"j({"omui": "0.1", "view": {"signals": {"other": {"type": "int", "provided": true}},
            "root": {"type": "text"}}})j")), OMUI_ERROR_INVALID_ARGUMENT);
    });

    s.test("models: one splice is push, insert, patch, remove and clear", [] {
        Rt r;
        auto rows = [&] {
            std::string out;
            check_eq(omuiModelRows(r.rt, str("notes"), 0, 100, collect, &out), OMUI_SUCCESS);
            return out;
        };
        check_eq(omuiModelSplice(r.rt, str("notes"), 0, 0, str(R"j([{"key": "a", "title": "A"}, {"key": "b", "title": "B"}])j")), OMUI_SUCCESS);
        check_eq(omuiModelSplice(r.rt, str("notes"), 1, 0, str(R"j([{"key": "x", "title": "X"}])j")), OMUI_SUCCESS);
        check_eq(rows(), std::string(R"j([{"key":"a","title":"A"},{"key":"x","title":"X"},{"key":"b","title":"B"}])j"));
        check_eq(omuiModelSplice(r.rt, str("notes"), 1, 1, str(R"j([{"key": "x", "title": "Y"}])j")), OMUI_SUCCESS);  // a patch
        check_eq(omuiModelSplice(r.rt, str("notes"), 0, 1, str("")), OMUI_SUCCESS);  // a removal
        check_eq(rows(), std::string(R"j([{"key":"x","title":"Y"},{"key":"b","title":"B"}])j"));
        check_eq(omuiModelSplice(r.rt, str("notes"), 0, 0, str(R"j([{"key": "b"}])j")), OMUI_ERROR_INVALID_ARGUMENT);  // b twice
        check_eq(omuiModelSplice(r.rt, str("notes"), 0, 0, str(R"j([{"title": "no key"}])j")), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiModelSplice(r.rt, str("notes"), 9, 0, str("[]")), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(rows(), std::string(R"j([{"key":"x","title":"Y"},{"key":"b","title":"B"}])j"));  // a refused change changes nothing
        check_eq(omuiModelSplice(r.rt, str("notes"), 0, 100, str("")), OMUI_SUCCESS);  // clear
        check_eq(rows(), std::string("[]"));
    });

    s.test("a verb chained as the person's own (S-10): its action marked in the tree, a wrong link refused", [] {
        Rt r;
        int allowed = 0;
        OmuiVerbPerson person{};
        person.type = OMUI_STRUCTURE_TYPE_VERB_PERSON;
        person.person = 1;
        OmuiVerbDesc desc{};
        desc.type = OMUI_STRUCTURE_TYPE_VERB_DESC;
        desc.next = &person;
        desc.name = str("cockpit.allow");
        desc.summary = str("Allow what the agent asked");
        desc.effect = OMUI_EFFECT_REVERSIBLE;
        check_eq(omuiRegisterVerb(
                     r.rt, &desc,
                     [](void* user, OmuiString, PFN_omuiSink, void*) {
                         ++*static_cast<int*>(user);
                         return OMUI_SUCCESS;
                     },
                     &allowed),
                 OMUI_SUCCESS);
        OmuiFrameInput wrong{};
        wrong.type = OMUI_STRUCTURE_TYPE_FRAME_INPUT;
        OmuiVerbDesc bad = desc;
        bad.name = str("cockpit.other");
        bad.next = &wrong;
        check_eq(omuiRegisterVerb(r.rt, &bad, [](void*, OmuiString, PFN_omuiSink, void*) { return OMUI_SUCCESS; }, nullptr),
                 OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiMount(r.rt, str("main"), str(R"j({"omui": "0.1", "view": {"root": {"type": "button", "id": "allow",
            "props": {"text": "Allow", "verb": "cockpit.allow"}}}})j")),
                 OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        std::string tree;
        check_eq(omuiSnapshot(r.rt, str(R"j({"interactive": true})j"), collect, &tree), OMUI_SUCCESS);
        check(tree.contains(R"("name":"activate","effect":"reversible","verb":"cockpit.allow","person":true)"), tree);
        // The program itself (its scripts, its host) is not the bus: it may invoke it.
        std::string out;
        check_eq(omuiInvoke(r.rt, str("cockpit.allow"), str("{}"), collect, &out), OMUI_SUCCESS);
        check_eq(allowed, 1);
    });

    s.test("verbs: a C implementation, answers, and the action that invokes one", [] {
        Rt r;
        Store store;
        OmuiVerbDesc desc{};
        desc.type = OMUI_STRUCTURE_TYPE_VERB_DESC;
        desc.name = str("note.delete");
        desc.summary = str("Delete a note");
        desc.effect = OMUI_EFFECT_IRREVERSIBLE;
        desc.answer = str("delete");
        check_eq(omuiRegisterVerb(r.rt, &desc, delete_note, &store), OMUI_SUCCESS);
        desc.answer = str("");
        check_eq(omuiRegisterVerb(r.rt, &desc, [](void*, OmuiString, PFN_omuiSink, void*) { return OMUI_SUCCESS; }, nullptr),
                 OMUI_ERROR_INVALID_ARGUMENT);  // taken, and an irreversible verb states its answer (S-1)
        std::string out;
        check_eq(omuiInvoke(r.rt, str("note.delete"), str(R"j({"id": "a"})j"), collect, &out), OMUI_NEEDS_ANSWER);
        check(store.deleted.empty());
        out.clear();
        check_eq(omuiInvoke(r.rt, str("note.nothing"), str("{}"), collect, &out), OMUI_ERROR_NOT_FOUND);
        // Through an action: the document's activation is the verb (S-1); the agent answers.
        check_eq(omuiMount(r.rt, str("main"), str(R"j({"omui": "0.1", "view": {"root": {"type": "button", "id": "del",
            "props": {"text": "Delete"}, "on": {"activated": [{"verb": "note.delete", "args": {"id": "'n1'"}}]}}}})j")),
                 OMUI_SUCCESS);
        check_eq(r.frame(), OMUI_SUCCESS);
        out.clear();
        check_eq(omuiAct(r.rt, str(R"j({"node": "main", "action": "activate"})j"), collect, &out), OMUI_NEEDS_ANSWER);
        check(out.find("needs-answer") != std::string::npos, out);
        out.clear();
        check_eq(omuiAct(r.rt, str(R"j({"node": "main", "action": "activate", "answer": "delete"})j"), collect, &out), OMUI_SUCCESS);
        check(store.deleted == std::vector<std::string>{"n1"});
        check(out.find("\"deleted\":true") != std::string::npos, out);
    });

    s.test("wait runs frames until the condition holds; post runs on the runtime's thread", [] {
        Rt r;
        check_eq(omuiMount(r.rt, str("main"), str(counter)), OMUI_SUCCESS);
        std::string out;
        check_eq(omuiWait(r.rt, str(R"j({"q": {"id": "main/inc"}})j"), 1000, collect, &out), OMUI_SUCCESS);
        out.clear();
        check_eq(omuiWait(r.rt, str(R"j({"q": {"name": "never"}})j"), 50, collect, &out), OMUI_NOT_READY);
        std::atomic<int> ran{0};
        std::jthread t([&] {
            (void)omuiPost(r.rt, [](void* u) { ++*static_cast<std::atomic<int>*>(u); }, &ran);
        });
        t.join();
        check_eq(ran.load(), 0);
        check_eq(r.frame(), OMUI_SUCCESS);
        check_eq(ran.load(), 1);
    });

    s.test("check, define a component, and frames with pixels", [] {
        Rt r;
        std::string diags;
        check_eq(omuiCheck(r.rt, str(R"j({"omui": "0.1", "view": {"root": {"type": "buton"}}})j"), collect, &diags), OMUI_ERROR_INVALID_ARGUMENT);
        check(diags.find("\"code\":\"unknown-type\"") != std::string::npos && diags.find("did you mean `button`?") != std::string::npos, diags);
        diags.clear();
        check_eq(omuiCheck(r.rt, str(counter), collect, &diags), OMUI_SUCCESS);
        check_eq(omuiDefineComponent(r.rt, str(R"j({"omui": "0.1", "component": {"name": "abi_badge",
            "interface": {"props": {"text": {"type": "string"}}},
            "root": {"type": "text", "id": "t", "props": {"text": {"expr": "upper(text)"}}}}})j"),
                                     collect, &diags),
                 OMUI_SUCCESS);
        check_eq(omuiMount(r.rt, str("main"), str(R"j({"omui": "0.1", "view": {"root": {"type": "abi_badge", "id": "b", "props": {"text": "new"}}}})j")),
                 OMUI_SUCCESS);
        std::vector<std::uint8_t> pixels(160 * 4 * 100, 0);
        OmuiFrameOutput out{};
        out.type = OMUI_STRUCTURE_TYPE_FRAME_OUTPUT;
        out.pixels = pixels.data();
        out.stride = 160 * 4;
        check_eq(r.frame(160, 100, {}, &out), OMUI_SUCCESS);
        check(out.revision > 0);
        check_eq(r.name_of("main/t"), std::string("NEW"));
        check(std::ranges::any_of(pixels, [](std::uint8_t b) { return b != 0; }), "drawn");
        // Input, as the host interface states it: a click on the badge does nothing, harmlessly.
        check_eq(r.frame(160, 100, R"j([{"type": "pointer", "kind": "down", "x": 10, "y": 10, "button": "left"}])j"), OMUI_SUCCESS);
        check_eq(r.frame(160, 100, "[{\"type\": 3"), OMUI_ERROR_INVALID_ARGUMENT);
        out.stride = 4;
        check_eq(r.frame(160, 100, {}, &out), OMUI_ERROR_INVALID_ARGUMENT);
    });

    s.test("native modules: the catalogue, and calls checked against the contract (component-binding §8)", [] {
        check(omui::natives::fuzzy::define().has_value(), "the official fuzzy module");
        Rt r;
        std::string catalog;
        check_eq(omuiNativeCatalog(r.rt, collect, &catalog), OMUI_SUCCESS);
        check(catalog.find(R"j("rank":{"async":true)j") != std::string::npos, catalog.substr(0, 300));
        std::string out;
        check_eq(omuiNativeCall(r.rt, str("fuzzy"), str("rank"), str(R"j(["fb", ["foobar", "foo_bar", "xyz"]])j"), collect, &out), OMUI_SUCCESS);
        check(out.find(R"j("text":"foo_bar")j") != std::string::npos && out.find(R"j("kind":"data")j") != std::string::npos, out);
        check(out.find("foo_bar") < out.find(R"j("text":"foobar")j"), "the word start first");
        std::string bad;
        check_eq(omuiNativeCall(r.rt, str("fuzzy"), str("rank"), str(R"j({"query": 3})j"), collect, &bad), OMUI_ERROR_INVALID_ARGUMENT);
        check(bad.find("`query` is the number 3, not a string") != std::string::npos, bad);
        std::string none;
        check_eq(omuiNativeCall(r.rt, str("fuzzy"), str("sort"), str("[]"), collect, &none), OMUI_ERROR_NOT_FOUND);
        check_eq(omuiNativeCall(r.rt, str("fuzzy"), str("rank"), str("{"), collect, &none), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiNativeCall(nullptr, str("fuzzy"), str("rank"), str("[]"), collect, &none), OMUI_ERROR_INVALID_ARGUMENT);
    });

    s.test("the catalogue of components", [] {
        Rt r;
        std::string out;
        check_eq(omuiCatalog(r.rt, collect, &out), OMUI_SUCCESS);
        auto v = omui::parse_json(out);
        check(v.has_value());
        if (!v) return;
        const omui::Value* button = v->get("components")->get("button");
        check(button && button->get("role")->as_string() == "button");
        check(button->get("props")->get("text")->get("bindable")->as_bool() == true);
        check(button->get("props")->get("variant")->get("values")->list()->size() == 4);
        check(button->get("events")->get("activated") != nullptr);
        check(v->get("components")->get("column")->get("contract")->as_bool() == false);
        check(v->get("properties")->json().find("\"width\"") != std::string::npos);
    });

    s.test("misuse is a result, never a crash", [] {
        check_eq(omuiDestroyRuntime(nullptr), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiMount(nullptr, str("main"), str("{}")), OMUI_ERROR_INVALID_ARGUMENT);
        Rt r;
        check_eq(omuiMount(r.rt, str(""), str(counter)), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiMount(r.rt, str("main"), str("not json")), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiFind(r.rt, str("{bad"), nullptr), OMUI_ERROR_INVALID_ARGUMENT);
        OmuiNode n = 0;
        check_eq(omuiFind(r.rt, str("{bad"), &n), OMUI_ERROR_INVALID_ARGUMENT);
        std::string out;
        check_eq(omuiGet(r.rt, 0xFFFF'FFFF'FFFFull, str("text"), collect, &out), OMUI_ERROR_STALE_HANDLE);
        check_eq(omuiSnapshot(r.rt, str("[1]"), collect, &out), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiAct(r.rt, str(R"j({"node": "nothing", "action": "activate"})j"), collect, &out), OMUI_ERROR_NOT_FOUND);
        check_eq(omuiInvoke(r.rt, str("x.y"), str("{"), collect, &out), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiPost(r.rt, nullptr, nullptr), OMUI_ERROR_INVALID_ARGUMENT);
        check_eq(omuiSignalFind(r.rt, str("main"), str("count"), nullptr), OMUI_ERROR_INVALID_ARGUMENT);
        OmuiSignal sig = 0;
        check_eq(omuiSignalFind(r.rt, str("main"), str("count"), &sig), OMUI_ERROR_NOT_FOUND);
        OmuiValue bad{};
        bad.type = static_cast<OmuiValueType>(99);
        check_eq(omuiSignalCreate(r.rt, &bad, &sig), OMUI_ERROR_INVALID_ARGUMENT);
        OmuiFrameInput in{};
        check_eq(omuiFrame(r.rt, &in, nullptr), OMUI_ERROR_INVALID_ARGUMENT);
    });

    return s.run(argc, argv);
}
