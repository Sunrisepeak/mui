// openmui in JavaScript, as a TypeScript program compiled to it runs: the
// counter of component-binding §5.2 is the same tree as the document written
// by hand; expressions are the runtime's; loops, verbs and models; errors are
// exceptions with their places; a reload keeps the state.
import std;
import mcpplibs.os;
import nlohmann.json;
import omui.abi;
import omui.lang;
import omui.lang.js;
import omui.ui.base;
import mlibs.testing;
import mcpplibs.mtest;
import omui.view;
import omui.cts;
import omui.native;
import omui.native.fuzzy;
import omui.theme;
import omui.theme.default_theme;
import omui.theme.compact;

using namespace mlibs::testing;
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
    int         width = 320, height = 240;
    Runtime() {
        OmuiRuntimeCreateInfo info{};
        info.type = OMUI_STRUCTURE_TYPE_RUNTIME_CREATE_INFO;
        info.spec_version = OMUI_SPEC_VERSION;
        info.headless = 1;
        expect(eq(omuiCreateRuntime(&info, &rt), OMUI_SUCCESS));
    }
    ~Runtime() { (void)omuiDestroyRuntime(rt); }
    // Whether another frame is wanted (something still moves).
    bool frame() {
        OmuiFrameInput in{};
        in.type = OMUI_STRUCTURE_TYPE_FRAME_INPUT;
        in.time = time += 1.0 / 60.0;
        in.width = width;
        in.height = height;
        in.scale = 1;
        OmuiFrameOutput out{};
        out.type = OMUI_STRUCTURE_TYPE_FRAME_OUTPUT;
        (void)omuiFrame(rt, &in, &out);
        return out.animating != 0;
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
        if (rec.value("kind", "") == "error") return Json{{"runner-error", rec.value("code", "") + ": " + rec.value("text", "")}};
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

UTest cases{"lang.js", [] {
    omui::register_base_components();

    "the counter in JavaScript is the hand-written document's tree, before and after a click (S level)"_test = [] {
        Runtime js_rt, doc_rt;
        Js js(js_rt);
        auto ran = js.run(std::string(counter_js));
        expect(ran.has_value()) << (ran ? "" : ran.error().text());
        expect(eq(omuiMount(doc_rt.rt, str("main"), str(counter_document)), OMUI_SUCCESS));
        doc_rt.frame();
        expect(eq(js_rt.tree("main"), doc_rt.tree("main")));
        for (int i = 0; i < 3; ++i) {
            expect(eq(js_rt.act("main/inc", "activate"), std::int64_t{0}));
            expect(eq(doc_rt.act("main/inc", "activate"), std::int64_t{0}));
        }
        js_rt.settle(js.host);
        doc_rt.frame();
        expect(eq(js_rt.name_of("main/text:0"), std::string("3 clicks")));
        expect(eq(js_rt.tree("main"), doc_rt.tree("main")));
        expect(js.errors.empty()) << (js.errors.empty() ? "" : js.errors[0]);
    };

    "expressions and signals: methods, combinators, mirrored reads"_test = [] {
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
        expect(ran.has_value()) << (ran ? "" : ran.error().text());
        expect(eq(r.name_of("main/sum"), std::string("8")));
        expect(eq(r.name_of("main/who"), std::string("ADA")));
        expect(eq(r.name_of("main/big"), std::string("small")));
    };

    "tr: a message in the person's language, its arguments by name; a key no catalog has is its own message"_test = [] {
        (void)omui::languages().add_messages(*mlibs::i18n::Locale::parse("en"), "greeting = \"Hello, {$name}!\"\n", "en.messages.omui.toml");
        omui::languages().request({*mlibs::i18n::Locale::parse("en")});
        Runtime r;
        Js js(r);
        auto ran = js.run(R"js(
import { signal, tr, column, text, mount } from "openmui";
const name = signal("Ada");
mount("main", column([
  text({ id: "greeting", text: tr("greeting", { name }) }),
  text({ id: "raw", text: tr("{$n} left", { n: 3 }) }),
]));
name.set("Grace");
)js");
        expect(ran.has_value()) << (ran ? "" : ran.error().text());
        expect(eq(r.name_of("main/greeting"), std::string("Hello, Grace!")));
        expect(eq(r.name_of("main/raw"), std::string("3 left")));
    };

    "loops over a model, handlers with their row, a verb in JavaScript, async handlers"_test = [] {
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
        expect(ran.has_value()) << (ran ? "" : ran.error().text());
        expect(eq(r.name_of("main/n2/text:0"), std::string("Bread")));
        expect(eq(r.act("main/n2/archive", "activate"), std::int64_t{0}));
        r.settle(js.host);
        expect(js.errors.empty()) << (js.errors.empty() ? "" : js.errors[0]);
        auto archived = js.run("if (archived[0] !== 'n2') throw new Error('archived ' + archived);", "check.js");
        expect(archived.has_value()) << (archived ? "" : archived.error().text());
        expect(js.run("showFilter();", "show.js").has_value());
        expect(eq(r.name_of("main/filter"), std::string("open only")));
    };

    "a handler reads the signals as its event left them (a bound field's changed)"_test = [] {
        Runtime r;
        Js js(r);
        auto ran = js.run(R"js(
import { signal, column, textBox, mount } from "openmui";
const query = signal("");
globalThis.seen = [];
mount("main", column([textBox({ id: "q", label: "Search", text: query, onChanged: (e) => seen.push(query.get() + "|" + e.text) })]));
)js");
        expect(ran.has_value()) << (ran ? "" : ran.error().text());
        for (std::string_view v : {"middle", "", "rose"}) {
            expect(eq(r.act("main/q", "set_value", Json{{"value", v}}), std::int64_t{0}));
            r.settle(js.host);
        }
        auto seen = js.run("const want = 'middle|middle,|,rose|rose'; if (seen.join() !== want) throw new Error(seen.join());", "check.js");
        expect(seen.has_value()) << (seen ? "" : seen.error().text());
    };

    "errors are exceptions, with what and where"_test = [] {
        Runtime r;
        Js js(r);
        auto ran = js.run(R"js(
import { mount, invoke, element, OmuiError } from "openmui";
let caught = "";
try { mount("main", element("buton", { id: "x" })); } catch (e) { caught = e.message; if (!(e instanceof OmuiError)) throw e; }
if (!caught.includes("did you mean `button`")) throw new Error(caught);
try { invoke("no.such", {}); throw new Error("no throw"); } catch (e) { if (!e.message.includes("not-found")) throw e; }
)js");
        expect(ran.has_value()) << (ran ? "" : ran.error().text());
        auto syntax = js.run("let x = = 1;", "tally.js");
        expect(!syntax && syntax.error().text().find("tally.js") != std::string::npos) << (syntax ? "" : syntax.error().text());
        auto thrown = js.run("import { mount } from 'openmui'; mount('main', 3);", "bad.js");
        expect(!thrown && thrown.error().text().find("mount(slot, element)") != std::string::npos) << (thrown ? "" : thrown.error().text());
        auto missing = js.run("import { x } from 'left-pad';", "dep.js");
        expect(!missing && missing.error().text().find("no module `left-pad`") != std::string::npos) << (missing ? "" : missing.error().text());
    };

    "native modules: openmui:native/<name>, a promise for an async one, its refusals as OmuiError"_test = [] {
        if (!omui::native::find("fuzzy")) expect(omui::natives::fuzzy::define().has_value()) << "the official native module";
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
        expect(ran.has_value()) << (ran ? "" : ran.error().text());
        int quiet = 0;
        for (int i = 0; i < 20000 && quiet < 3; ++i) {
            r.frame();
            quiet = js.session.idle() ? quiet + 1 : 0;
            if (!quiet) std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        const std::string outcome = invoke(r, "t.outcome", Json::object()).get<std::string>();
        expect(outcome.find("invalid-argument") != std::string::npos && outcome.find("`candidates[1]` is the number 3") != std::string::npos) << outcome;
        expect(js.errors.empty()) << (js.errors.empty() ? "" : js.errors[0]);
        auto missing = js.run("import { x } from 'openmui:native/nope';", "nope.js");
        expect(!missing && missing.error().text().find("no native module `nope`") != std::string::npos) << (missing ? "" : missing.error().text());
    };

    "a script destroyed while it loads: its run finishes first, its queued runs are refused, its verbs answer that it is gone"_test = [] {
        // Each verb registration waits on the runtime's thread, which is the
        // one destroying the script: the teardown arrives while the script's
        // code is on the script thread's stack.
        const std::string busy = R"js(import { verb } from "openmui";
for (let i = 0; i < 200; i++) verb("load.v" + (i % 4), { effect: "read" }, () => ({ i: 1 }));
)js";
        for (int round = 0; round < 8; ++round) {
            Runtime r;
            lang::Host host(r.rt);
            lang::Session session(host);
            auto a = std::make_unique<lang::js::Script>(session);
            auto b = std::make_unique<lang::js::Script>(session);
            auto first = a->run(busy, "a.js");
            auto queued_b = b->run(busy, "b.js");
            auto queued_a = a->run(busy, "a2.js");
            std::this_thread::sleep_for(std::chrono::milliseconds(round % 2 ? 2 : 0));
            a.reset();
            b.reset();
            (void)first.get();
            const auto rb = queued_b.get(), ra = queued_a.get();
            // b's run may start while a's teardown waits (b is not destroyed yet); if not, it is refused.
            expect(rb || rb.error().message.contains("gone")) << (rb ? "" : rb.error().message);
            expect(!ra && ra.error().message.contains("gone")) << (ra ? "a's queued run ran after a was destroyed" : ra.error().message);
            const Json answer = invoke(r, "load.v0", Json::object());
            expect(answer.is_null() || answer.contains("runner-error")) << std::format("a verb of a destroyed script answers nothing: {}", answer.dump());
        }
    };

    "a reload keeps the state, the nodes, and replaces the handlers"_test = [] {
        Runtime r;
        Js js(r);
        expect(js.run(std::string(counter_js)).has_value());
        expect(eq(r.act("main/inc", "activate"), std::int64_t{0}));
        expect(eq(r.act("main/inc", "activate"), std::int64_t{0}));
        r.settle(js.host);
        std::string next(counter_js);
        next.replace(next.find("{} clicks"), 9, "{} taps");
        next.replace(next.find("v + 1"), 5, "v + 10");
        const auto start = std::chrono::steady_clock::now();
        auto reloaded = r.wait(js.host, js.script.reload(next));
        const auto took = std::chrono::steady_clock::now() - start;
        expect(reloaded.has_value()) << (reloaded ? "" : reloaded.error().text());
        expect(eq(r.name_of("main/text:0"), std::string("2 taps")));
        expect(took < std::chrono::milliseconds(200)) << std::format("{}", std::chrono::duration_cast<std::chrono::milliseconds>(took));
        expect(eq(r.act("main/inc", "activate"), std::int64_t{0}));
        r.settle(js.host);
        expect(eq(r.name_of("main/text:0"), std::string("12 taps")));
    };

    "a reload waited for on the runtime's thread (Live Design's edit): the state kept, within a frame"_test = [] {
        Runtime r;
        Js js(r);
        expect(js.run(std::string(counter_js)).has_value());
        expect(eq(r.act("main/inc", "activate"), std::int64_t{0}));
        r.settle(js.host);
        std::string next(counter_js);
        next.replace(next.find("{} clicks"), 9, "{} taps");
        std::vector<double> took;
        for (int round = 0; round < 20; ++round) {
            const auto start = std::chrono::steady_clock::now();
            auto reloaded = js.script.reload_now(round % 2 ? std::string(counter_js) : next);
            took.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            expect(reloaded.has_value()) << (reloaded ? "" : reloaded.error().text());
            expect(eq(r.name_of("main/text:0"), std::string(round % 2 ? "1 clicks" : "1 taps"))) << "at once, the count kept";
        }
        std::ranges::sort(took);
        const double p95 = took[took.size() * 95 / 100];
        std::println("reload_now: median {:.2f} ms, p95 {:.2f} ms (unoptimised)", took[took.size() / 2], p95);
        expect(p95 < 16.0) << std::format("p95 {:.2f} ms", p95);
        auto broken = js.script.reload_now("mount(\"main\", column({}, [text({ text: undefinedName })]));");
        expect(!broken.has_value()) << "an error is the answer";
    };

    "a module edited (Live Design, a saved file): it alone evaluated again, with its script; the state kept"_test = [] {
        const auto dir = scratch_dir("lang-js-modules");
        std::ofstream(dir / "label.ts", std::ios::binary) << R"ts(
(globalThis as any).labelRuns = ((globalThis as any).labelRuns ?? 0) + 1;
export const label = (n: number): string => `${n} clicks`;
)ts";
        std::ofstream(dir / "other.ts", std::ios::binary) << R"ts(
(globalThis as any).otherRuns = ((globalThis as any).otherRuns ?? 0) + 1;
export const title = "Counter";
)ts";
        const std::string main = R"ts(
import { signal, fmt, column, text, button, mount } from "openmui";
import { label } from "./label";
import { title } from "./other";
const count = signal(0);
const g = globalThis as any;
mount("main", column({}, [
  text({ id: "title", text: title }),
  text({ id: "caption", text: label(2) }),
  text({ id: "runs", text: `${g.labelRuns}/${g.otherRuns}` }),
  text({ id: "count", text: fmt("{}", count) }),
  button({ id: "inc", text: "+1", onActivated: () => count.update((v: number) => v + 1) }),
]));
)ts";
        auto js = lang::ts::compile_source(main, mcpplibs::os::fs::utf8(dir / "main.ts"));
        expect(js.has_value()) << (js ? "" : js.error());
        if (!js) return;
        Runtime r;
        Js script(r);
        const std::string name = mcpplibs::os::fs::utf8(dir / "main.ts");
        auto ran = script.run(*js, name);
        expect(ran.has_value()) << (ran ? "" : ran.error().text());
        expect(eq(r.name_of("main/caption"), std::string("2 clicks")));
        expect(eq(r.name_of("main/runs"), std::string("1/1")));
        expect(eq(r.act("main/inc", "activate"), std::int64_t{0}));
        r.settle(script.host);
        expect(eq(r.name_of("main/count"), std::string("1")));
        const auto modules = script.script.modules();
        expect(eq(modules.size(), std::size_t{2})) << "both modules it imported by path";
        // An edit to label.ts, not yet in the file: it alone is evaluated again.
        script.script.module_changed(dir / "label.ts", R"ts(
(globalThis as any).labelRuns = ((globalThis as any).labelRuns ?? 0) + 1;
export const label = (n: number): string => `${n} taps`;
)ts");
        auto again = script.script.reload_now(*js, name);
        expect(again.has_value()) << (again ? "" : again.error().text());
        expect(eq(r.name_of("main/caption"), std::string("2 taps")));
        expect(eq(r.name_of("main/runs"), std::string("2/1"))) << "other.ts kept as it was";
        expect(eq(r.name_of("main/count"), std::string("1"))) << "the count kept";
        // Back to the file's text.
        script.script.module_changed(dir / "label.ts");
        expect(script.script.reload_now(*js, name).has_value());
        expect(eq(r.name_of("main/caption"), std::string("2 clicks")));
        expect(eq(r.name_of("main/runs"), std::string("3/1")));
    };

    "TypeScript: its types blanked in this process; its own modules imported by path, their types blanked too"_test = [] {
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
        auto js = lang::ts::compile(dir / "main.ts");
        expect(js.has_value()) << (js ? "" : js.error());
        if (!js) return;
        expect(js->find("from \"openmui\"") != std::string::npos && js->find("interface") == std::string::npos) << *js;
        expect(eq(std::ranges::count(*js, '\n'), std::ranges::count(file_text(dir / "main.ts"), '\n'))) << "the lines are the source's";
        Runtime r;
        Js script(r);
        auto ran = script.run(*js, mcpplibs::os::fs::utf8(dir / "main.ts"));
        expect(ran.has_value()) << (ran ? "" : ran.error().text());
        expect(eq(r.name_of("main/caption"), std::string("3 clicks")));
        expect(eq(r.act("main/inc", "activate"), std::int64_t{0}));
        r.settle(script.host);
        expect(eq(r.name_of("main/text:1"), std::string("1 clicks")));
        auto broken = lang::ts::compile(dir / "missing.ts");
        expect(!broken && !broken.error().empty());
    };

    "TypeScript from an editor's text: transformed, its types gone, openmui an import; an error names the line"_test = [] {
        auto js = lang::ts::compile_source(R"ts(
import { verb } from "openmui";
const half: number = 110;
verb("gift.wide", { effect: "read" }, (): { half: number } => ({ half }));
)ts", "wide.ts");
        expect(js.has_value()) << (js ? "" : js.error());
        if (!js) return;
        expect(js->find("from \"openmui\"") != std::string::npos && js->find(": number") == std::string::npos) << *js;
        auto broken = lang::ts::compile_source("const x: number = ;\n", "broken.ts");
        expect(!broken && broken.error().starts_with("broken.ts:1:19: ")) << (broken ? *broken : broken.error());
    };

    "CTS level S: every scenario records its pinned trace (spec/conformance/S, tests/cts.js)"_test = [] {
        if (!omui::native::find("fuzzy")) expect(omui::natives::fuzzy::define().has_value()) << "the official native module";
        expect(omui::themes::add_default().has_value() && omui::themes::add_compact().has_value()) << "the official themes";
        const std::string runner = file_text(std::filesystem::path(std::source_location::current().file_name()).parent_path() / "cts.js");
        const auto report = omui::cts::conform([&](const omui::cts::Scenario& scenario) -> Json {
            Runtime r;
            r.height = 480;  // the scenarios' viewport: 320 × 480
            Js js(r);
            auto ran = js.run(runner, "cts.js");
            expect(ran.has_value()) << (ran ? "" : ran.error().text());
            if (!ran) return Json::array();
            const Json trace = omui::cts::run(
                scenario, [&](std::string_view verb, const Json& args) { return invoke(r, verb, args); },
                [&] {
                    // Until the session is idle: an async native's answer is in flight too.
                    int quiet = 0;
                    for (int i = 0; i < 20000 && quiet < 3; ++i) {
                        // And until what the interface shows has settled too.
                        const bool moving = r.frame();
                        quiet = js.session.idle() && !moving ? quiet + 1 : 0;
                        if (!quiet) std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                });
            expect(js.errors.empty()) << (js.errors.empty() ? "" : scenario.name + ": " + js.errors[0]);
            return trace;
        }, {.subject = "javascript"});
        expect(report.has_value() && report->outcomes.size() >= 3) << (report ? "fewer than three scenarios" : report.error());
        if (!report) return;
        std::print("{}", report->text());
        expect(report->ok() && report->count(mlibs::cts::Status::passed) == report->outcomes.size()) << (report->text());
    };
}};
