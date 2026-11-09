// Interface generated into a running TypeScript application, end to end (W5,
// D-144; W8): the player (this example's program, run with another manifest)
// whose omui.config.ts declares a surface and opens generation to `limited`
// in a release. An agent, over the application's endpoint, drafts a view from
// TypeScript, previews it (nothing left of it), commits it, undoes and redoes
// it, drafts in the background; refused a component it was not given and code
// that does not end; the version kept across a restart.
import std;
import omui.agent;
import mcpplibs.os;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;

using namespace mcpplibs::mtest;
namespace os = mcpplibs::os;
using omui::Value;

namespace {

constexpr std::string_view manifest = R"([package]
name = "plan"
version = "0.1.0"

[package.metadata.openmui]
kind = "app"
id = "org.openmui.plan"
title = "Plan"
size = "360x240"

[package.metadata.openmui.scripts]
main = "logic/main.ts"
)";
constexpr std::string_view script = R"(import { column, text, slot, mount } from "openmui";
mount("main", column({ padding: 8, gap: 6 }, [text({ id: "plan", text: "Basic plan" }), slot("extras")]));
)";
constexpr std::string_view config = R"(import { defineConfig } from "omui/define";
export default defineConfig({
  generation: { surfaces: { extras: "cards an agent adds under the plan" }, open: { release: "limited" }, components: ["text", "column"] },
});
)";
constexpr std::string_view plans = R"(import { defineView } from "openmui/define";
const plans = [["Pro", 9], ["Team", 29]];
export default defineView({ omui: "0.1", view: { root: { type: "column", children: plans.map(([name, price]) => ({ type: "text", id: name.toLowerCase(), props: { text: `${name}, ${price} a month` } })) } } });
)";
constexpr std::string_view later = R"(import { defineView } from "openmui/define";
export default defineView({ omui: "0.1", view: { root: { type: "text", id: "later", props: { text: "Made in the background" } } } });
)";

// The application running, and an agent's connection to it.
struct Running {
    std::optional<os::process::process> process;
    std::optional<omui::agent::Client>  client;

    Running(const std::filesystem::path& app, const std::filesystem::path& endpoints) {
        os::process::spawn_options options;
        options.program = os::fs::from_utf8(e2e::program("text-stats"));
        options.args = {"--manifest", os::fs::utf8(app), "--serve", "--agent-dir", os::fs::utf8(endpoints)};
        options.out = options.err = os::process::stdio::null;
        options.bound_lifetime = true;
        if (auto spawned = os::process::process::spawn(options)) process.emplace(std::move(*spawned));
        for (int tries = 0; process && tries < 200 && !client; ++tries) {
            if (auto connected = omui::agent::Client::connect("org.openmui.plan", endpoints)) client.emplace(std::move(*connected));
            else std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    ~Running() {
        if (process) process->kill();
    }
    // The request's data (null when none) and its exit code.
    std::pair<Value, int> ask(const Value& request) {
        Value data;
        int code = -1;
        for (const Value& record : client->call(request)) {
            const std::string_view kind = record.get("kind") ? record.get("kind")->as_string() : std::string_view{};
            if (kind == "data") data = *record.get("data");
            if (kind == "result") code = static_cast<int>(record.get("exitCode")->as_int());
        }
        return {data, code};
    }
    std::pair<Value, int> invoke(std::string_view verb, Value args = Value(Value::Map{})) {
        return ask(Value{}.set("op", "invoke").set("verb", verb).set("args", std::move(args)));
    }
    bool finds(std::string_view name) {
        const auto [data, code] = ask(Value{}.set("op", "find").set("q", Value{}.set("name", name)));
        return code == 0 && data.get("nodes") && !data.get("nodes")->list()->empty();
    }
    void settle() { (void)ask(Value{}.set("op", "wait").set("settled", true).set("timeout", 5)); }
};

}  // namespace

E2E generation{"generation", timeout{std::chrono::minutes{5}}, [] {
    "drafted from TypeScript, previewed, committed, undone and redone, in the background, refused where it must be, kept across a restart"_test[artifact{"target:text-stats"}] = [] {
        const auto app = e2e::tree({{"mcpp.toml", manifest}, {"logic/main.ts", script}, {"omui.config.ts", config}});
        const auto endpoints = app / "endpoints";
        std::filesystem::create_directories(endpoints);
        {
            Running plan(app, endpoints);
            expect(plan.client.has_value()) << "the application's endpoint";
            if (!plan.client) return;
            const auto [surfaces, surfaces_code] = plan.invoke("generate.surfaces");
            expect(eq(surfaces_code, 0) && surfaces.get("openness")->as_string() == "limited") << surfaces.json();
            const auto [drafted, drafted_code] = plan.invoke("generate.draft", Value{}.set("surface", "extras").set("language", "ts").set("code", plans));
            expect(eq(drafted_code, 0) && drafted.get("stage")->as_string() == "prepared") << drafted.json();
            const auto [previewed, previewed_code] = plan.invoke("generate.preview", Value{}.set("draft", "d1"));
            expect(eq(previewed_code, 0) && previewed.json().contains("Pro, 9 a month") && previewed.get("after")) << "pictures and the changes";
            expect(!plan.finds("*a month")) << "the preview left nothing";
            expect(eq(plan.invoke("generate.commit", Value{}.set("draft", "d1")).second, 0));
            plan.settle();
            expect(plan.finds("Team, 29 a month"));
            expect(eq(plan.ask(Value{}.set("op", "undo")).second, 0));
            plan.settle();
            expect(!plan.finds("*a month")) << "undone";
            expect(eq(plan.ask(Value{}.set("op", "redo")).second, 0));
            const auto [task, task_code] =
                plan.invoke("generate.draft", Value{}.set("surface", "extras").set("language", "ts").set("code", later).set("background", true).set("commit", true));
            expect(eq(task_code, 0) && task.get("task")) << task.json();
            (void)plan.ask(Value{}.set("op", "wait").set("q", Value{}.set("name", "Made in the background")).set("timeout", 10));
            expect(plan.finds("Made in the background")) << "made in the background, committed";
            expect(eq(plan.ask(Value{}.set("op", "undo")).second, 0));
            plan.settle();
            const auto [boxed, boxed_code] = plan.invoke("generate.draft", Value{}.set("surface", "extras").set("document", omui::parse_json(R"({"omui":"0.1","view":{"root":{"type":"box"}}})").value()));
            expect(eq(boxed_code, 1)) << "a component not given: refused";
            const auto [looped, looped_code] = plan.invoke("generate.draft", Value{}.set("surface", "extras").set("language", "ts").set("code", "while (true) {}"));
            expect(eq(looped_code, 1)) << "code that does not end: refused";
        }
        // Started again: the version shown is shown again.
        Running again(app, endpoints);
        expect(again.client.has_value());
        if (!again.client) return;
        (void)again.ask(Value{}.set("op", "wait").set("q", Value{}.set("name", "Team, 29 a month")).set("timeout", 10));
        expect(again.finds("Team, 29 a month")) << "kept across a restart";
    };
}};
