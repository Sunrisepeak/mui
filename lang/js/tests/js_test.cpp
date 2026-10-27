// openmui in JavaScript, as a TypeScript program compiled to it runs: the
// counter of component-binding §5.2 is the same tree as the document written
// by hand; expressions are the runtime's; loops, verbs and models; errors are
// exceptions with their places; a reload keeps the state.
import std;
import nlohmann.json;
import openmui.abi;
import openmui.lang;
import openmui.lang.js;
import openmui.ui.base;
import openmui.testing;
import openmui.cts;
import openmui.native;
import openmui.native.fuzzy;
import openmui.theme;
import openmui.theme.default_theme;
import openmui.theme.compact;

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
    std::int64_t act(std::string_view node, std::string_view action, Json args = Json::object()) {
        std::string out;
        const std::string text = Json{{"node", node}, {"action", action}, {"args", std::move(args)}}.dump();
        return omuiAct(rt, str(text), collect, &out);
    }
};

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
        if (rec.value("kind", "") == "error") return Json{{"runner-error", rec.value("code", "") + ": " + rec.value("message", "")}};
        if (rec.value("kind", "") == "data" && rec.contains("data")) data = rec["data"];
    }
    return data;
}

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

struct Js {
    Runtime&        r;
    lang::Host      host;
    lang::Session   session;
    lang::js::Script script;
    std::vector<std::string> errors;
    explicit Js(Runtime& rt) : r(rt), host(rt.rt), session(host), script(session) {
        script.on_error([this](std::string_view m) { errors.emplace_back(m); });
    }
    lang::Result<void> run(std::string code, std::string name = "main.js") { return r.wait(host, script.run(std::move(code), std::move(name))); }
};

constexpr std::string_view counter_js = R"js(
import { signal, fmt, column, text, button, mount } from "openmui";
const count = signal(0);
export const Counter = () => column({ gap: 8 }, [
  text({ text: fmt("{} clicks", count) }),
  button({ id: "inc", text: "+1", onActivated: () => count.update((v) => v + 1) }),
]);
mount("main", Counter());
)js";

constexpr std::string_view counter_document = R"j({"omui": "0.1", "view": {
  "signals": {"count": {"type": "int", "value": 0}},
  "root": {"type": "column", "props": {"gap": 8}, "children": [
    {"type": "text", "props": {"text": {"expr": "format('{} clicks', count)"}}},
    {"type": "button", "id": "inc", "props": {"text": "+1"}, "on": {"activated": [{"set": "count", "expr": "count + 1"}]}}]}}})j";

}  // namespace

int main(int argc, char** argv) {
    Suite s{"lang.js"};
    omui::register_base_components();

    s.test("the counter in JavaScript is the hand-written document's tree, before and after a click (S level)", [] {
        Runtime js_rt, doc_rt;
        Js js(js_rt);
        auto ran = js.run(std::string(counter_js));
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(omuiMount(doc_rt.rt, str("main"), str(counter_document)), OMUI_SUCCESS);
        doc_rt.frame();
        check_eq(js_rt.tree("main"), doc_rt.tree("main"));
        for (int i = 0; i < 3; ++i) {
            check_eq(js_rt.act("main/inc", "activate"), std::int64_t{0});
            check_eq(doc_rt.act("main/inc", "activate"), std::int64_t{0});
        }
        js_rt.settle(js.host);
        doc_rt.frame();
        check_eq(js_rt.name_of("main/text:0"), std::string("3 clicks"));
        check_eq(js_rt.tree("main"), doc_rt.tree("main"));
        check(js.errors.empty(), js.errors.empty() ? "" : js.errors[0]);
    });

    s.test("expressions and signals: methods, combinators, mirrored reads", [] {
        Runtime r;
        Js js(r);
        auto ran = js.run(R"js(
import { signal, fmt, call, select, gt, column, text, mount } from "openmui";
const a = signal(6), b = signal(7), name = signal("ada");
mount("main", column([
  text({ id: "sum", text: fmt("{}", a.mul(b).add(1)) }),
  text({ id: "who", text: call("upper", name) }),
  text({ id: "big", text: select(gt(a, 5), "big", "small") }),
  text({ id: "wide", maxWidth: 200, text: "x" }),
]));
if (a.get() !== 6) throw new Error("a is " + a.get());
a.set(1);
if (a.value !== 1) throw new Error("the mirror has it");
)js");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(r.name_of("main/sum"), std::string("8"));
        check_eq(r.name_of("main/who"), std::string("ADA"));
        check_eq(r.name_of("main/big"), std::string("small"));
    });

    s.test("loops over a model, handlers with their row, a verb in JavaScript, async handlers", [] {
        Runtime r;
        Js js(r);
        auto ran = js.run(R"js(
import { signal, verb, invoke, model, each, when, not, column, row, text, button, mount } from "openmui";
globalThis.archived = [];
verb("note.archive", { summary: "Archive a note" }, (args) => { archived.push(args.id); return { archived: args.id }; });
const onlyOpen = signal(false);
model("notes").push([{ key: "n1", title: "Milk" }, { key: "n2", title: "Bread" }]);
mount("main", column([
  when(onlyOpen, [text({ id: "filter", text: "open only" })]),
  each("notes", (note) => row([
    text({ text: note.title }),
    button({ id: "archive", text: "Archive", onActivated: async (e) => {
      await Promise.resolve();
      invoke("note.archive", { id: e.rows.row.key });
    } }),
  ])),
]));
globalThis.showFilter = () => onlyOpen.set(true);
)js");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(r.name_of("main/n2/text:0"), std::string("Bread"));
        check_eq(r.act("main/n2/archive", "activate"), std::int64_t{0});
        r.settle(js.host);
        check(js.errors.empty(), js.errors.empty() ? "" : js.errors[0]);
        auto archived = js.run("if (archived[0] !== 'n2') throw new Error('archived ' + archived);", "check.js");
        check(archived.has_value(), archived ? "" : archived.error().text());
        check(js.run("showFilter();", "show.js").has_value());
        check_eq(r.name_of("main/filter"), std::string("open only"));
    });

    s.test("a handler reads the signals as its event left them (a bound field's changed)", [] {
        Runtime r;
        Js js(r);
        auto ran = js.run(R"js(
import { signal, column, textBox, mount } from "openmui";
const query = signal("");
globalThis.seen = [];
mount("main", column([textBox({ id: "q", label: "Search", text: query, onChanged: (e) => seen.push(query.get() + "|" + e.text) })]));
)js");
        check(ran.has_value(), ran ? "" : ran.error().text());
        for (std::string_view v : {"middle", "", "rose"}) {
            check_eq(r.act("main/q", "set_value", Json{{"value", v}}), std::int64_t{0});
            r.settle(js.host);
        }
        auto seen = js.run("const want = 'middle|middle,|,rose|rose'; if (seen.join() !== want) throw new Error(seen.join());", "check.js");
        check(seen.has_value(), seen ? "" : seen.error().text());
    });

    s.test("errors are exceptions, with what and where", [] {
        Runtime r;
        Js js(r);
        auto ran = js.run(R"js(
import { mount, invoke, element, OmuiError } from "openmui";
let caught = "";
try { mount("main", element("buton", { id: "x" })); } catch (e) { caught = e.message; if (!(e instanceof OmuiError)) throw e; }
if (!caught.includes("did you mean `button`")) throw new Error(caught);
try { invoke("no.such", {}); throw new Error("no throw"); } catch (e) { if (!e.message.includes("not-found")) throw e; }
)js");
        check(ran.has_value(), ran ? "" : ran.error().text());
        auto syntax = js.run("let x = = 1;", "tally.js");
        check(!syntax && syntax.error().text().find("tally.js") != std::string::npos, syntax ? "" : syntax.error().text());
        auto thrown = js.run("import { mount } from 'openmui'; mount('main', 3);", "bad.js");
        check(!thrown && thrown.error().text().find("mount(slot, element)") != std::string::npos, thrown ? "" : thrown.error().text());
        auto missing = js.run("import { x } from 'left-pad';", "dep.js");
        check(!missing && missing.error().text().find("no module `left-pad`") != std::string::npos, missing ? "" : missing.error().text());
    });

    s.test("native modules: openmui:native/<name>, a promise for an async one, its refusals as OmuiError", [] {
        if (!omui::native::find("fuzzy")) check(omui::natives::fuzzy::define().has_value(), "the official native module");
        Runtime r;
        Js js(r);
        auto ran = js.run(R"js(
import { OmuiError, verb } from "openmui";
import { fuzzy, rank, rankSync, match } from "openmui:native/fuzzy";
import fz from "openmui:native/fuzzy";
let outcome = "pending";
verb("t.outcome", () => outcome);
const files = ["src/main.cpp", "my_config.toml", "docs/mcfg.md"];
const now = rankSync("mcfg", files);
if (now[0].text !== "docs/mcfg.md" || now.length !== 2) throw new Error(JSON.stringify(now));
if (match("zz", "foo") !== null || fz.match !== match || fuzzy.rank !== rank) throw new Error("the module's shape");
const p = rank("mcfg", files, 1);
if (!(p instanceof Promise)) throw new Error("an async native answers a promise");
p.then((best) => {
  if (best.length !== 1 || best[0].index !== 2) throw new Error(JSON.stringify(best));
  return rank("q", ["a", 3]);
}).then(() => { outcome = "no rejection"; }, (e) => { outcome = e instanceof OmuiError ? e.message : "not an OmuiError: " + e; });
)js");
        check(ran.has_value(), ran ? "" : ran.error().text());
        int quiet = 0;
        for (int i = 0; i < 20000 && quiet < 3; ++i) {
            r.frame();
            quiet = js.session.idle() ? quiet + 1 : 0;
            if (!quiet) std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        const std::string outcome = invoke(r, "t.outcome", Json::object()).get<std::string>();
        check(outcome.find("invalid-argument") != std::string::npos && outcome.find("`candidates[1]` is the number 3") != std::string::npos, outcome);
        check(js.errors.empty(), js.errors.empty() ? "" : js.errors[0]);
        auto missing = js.run("import { x } from 'openmui:native/nope';", "nope.js");
        check(!missing && missing.error().text().find("no native module `nope`") != std::string::npos, missing ? "" : missing.error().text());
    });

    s.test("a reload keeps the state, the nodes, and replaces the handlers", [] {
        Runtime r;
        Js js(r);
        check(js.run(std::string(counter_js)).has_value());
        check_eq(r.act("main/inc", "activate"), std::int64_t{0});
        check_eq(r.act("main/inc", "activate"), std::int64_t{0});
        r.settle(js.host);
        std::string next(counter_js);
        next.replace(next.find("{} clicks"), 9, "{} taps");
        next.replace(next.find("v + 1"), 5, "v + 10");
        const auto start = std::chrono::steady_clock::now();
        auto reloaded = r.wait(js.host, js.script.reload(next));
        const auto took = std::chrono::steady_clock::now() - start;
        check(reloaded.has_value(), reloaded ? "" : reloaded.error().text());
        check_eq(r.name_of("main/text:0"), std::string("2 taps"));
        check(took < std::chrono::milliseconds(200), std::format("{}", std::chrono::duration_cast<std::chrono::milliseconds>(took)));
        check_eq(r.act("main/inc", "activate"), std::int64_t{0});
        r.settle(js.host);
        check_eq(r.name_of("main/text:0"), std::string("12 taps"));
    });

    s.test("TypeScript: esbuild compiles it, bundling the program's own modules; openmui stays an import", [] {
        const auto dir = scratch_dir("lang-js-ts");
        std::ofstream(dir / "label.ts", std::ios::binary) << R"ts(
export const label = (n: number): string => `${n} clicks`;
)ts";
        std::ofstream(dir / "main.ts", std::ios::binary) << R"ts(
import { signal, fmt, column, text, button, mount, type Signal } from "openmui";
import { label } from "./label";
interface Counter { count: Signal; }
const state: Counter = { count: signal(0) };
mount("main", column({ gap: 8 }, [
  text({ id: "caption", text: label(3) }),
  text({ text: fmt("{} clicks", state.count) }),
  button({ id: "inc", text: "+1", onActivated: (): void => state.count.update((v: number) => v + 1) }),
]));
)ts";
        auto js = lang::js::compile_typescript(dir / "main.ts");
        check(js.has_value(), js ? "" : js.error());
        if (!js) return;
        check(js->find("from \"openmui\"") != std::string::npos && js->find("interface") == std::string::npos, *js);
        Runtime r;
        Js script(r);
        auto ran = script.run(*js, "main.ts");
        check(ran.has_value(), ran ? "" : ran.error().text());
        check_eq(r.name_of("main/caption"), std::string("3 clicks"));
        check_eq(r.act("main/inc", "activate"), std::int64_t{0});
        r.settle(script.host);
        check_eq(r.name_of("main/text:1"), std::string("1 clicks"));
        auto broken = lang::js::compile_typescript(dir / "missing.ts");
        check(!broken && !broken.error().empty());
    });

    s.test("CTS level S: every scenario records its pinned trace (spec/conformance/S, tests/cts.js)", [] {
        if (!omui::native::find("fuzzy")) check(omui::natives::fuzzy::define().has_value(), "the official native module");
        check(omui::themes::add_default().has_value() && omui::themes::add_compact().has_value(), "the official themes");
        auto all = omui::cts::scenarios();
        check(all.has_value() && all->size() >= 3, all ? "fewer than three scenarios" : all.error());
        if (!all) return;
        const std::string runner = file_text(std::filesystem::path(std::source_location::current().file_name()).parent_path() / "cts.js");
        for (const omui::cts::Scenario& scenario : *all) {
            Runtime r;
            r.height = 480;  // the scenarios' viewport: 320 × 480
            Js js(r);
            auto ran = js.run(runner, "cts.js");
            check(ran.has_value(), ran ? "" : ran.error().text());
            if (!ran) continue;
            const Json trace = omui::cts::run(
                scenario, [&](std::string_view verb, const Json& args) { return invoke(r, verb, args); },
                [&] {
                    // Until the session is idle: an async native's answer is in flight too.
                    int quiet = 0;
                    for (int i = 0; i < 20000 && quiet < 3; ++i) {
                        r.frame();
                        quiet = js.session.idle() ? quiet + 1 : 0;
                        if (!quiet) std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                });
            for (const std::string& d : omui::cts::check(scenario, trace)) fail(d);
            check(js.errors.empty(), js.errors.empty() ? "" : scenario.name + ": " + js.errors[0]);
        }
    });

    return s.run(argc, argv);
}
