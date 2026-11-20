// closure: what an application carries, from what it wrote (B7b) — the names its scripts take from openmui, as
// contract names; the steps of its replay read as scripts too; every component, with why, where a script takes
// everything; the unit registers those named (or all), and closure.json says where each was taken.
import std;
import omui.gen.closure;
import mcpplibs.mtest;
import nlohmann.json;

using namespace mcpplibs::mtest;
namespace gen = omui::gen;

static UTest closure_cases{"gen closure", [] {
    "contract names from builders: textBox is text_box"_test = [] {
        expect(eq(gen::contract_name("textBox"), std::string("text_box")) && eq(gen::contract_name("button"), std::string("button")) &&
               eq(gen::contract_name("agentPresence"), std::string("agent_presence")));
    };

    "the names a page takes from openmui, other modules' left out; the unit names them"_test = [] {
        const std::array sources{
            gen::ClosureSource{"app/main.ts", R"ts(import { signal, button, segmented, textBox } from "openmui";
import { fuzzy } from "openmui:native/fuzzy";
import type { Signal } from "openmui";
)ts"},
            gen::ClosureSource{"app/more.ts", "export { link } from \"openmui\";\n"},
        };
        const auto closure = gen::derive_closure(sources);
        expect(closure.has_value()) << (closure ? "" : closure.error());
        if (!closure) return;
        expect(!closure->full);
        expect(eq(closure->components, std::vector<std::string>{"button", "link", "segmented", "signal", "text_box"}));
        expect(eq(closure->taken.size(), 2uz) && eq(closure->taken[0].line, 1uz));
        const std::string unit = gen::closure_unit(*closure);
        expect(unit.contains("void omui_application_components()") &&
               unit.contains("omui::register_base_named<\"button\", \"link\", \"segmented\", \"signal\", \"text_box\">();"))
            << unit;
        const auto report = nlohmann::json::parse(gen::closure_report(*closure));
        expect(eq(report["$format"].get<std::string>(), std::string("closure@1")) && !report["full"].get<bool>() && eq(report["names"].size(), 5uz));
    };

    "a replay's steps are scripts too: a component only an earlier step made is kept"_test = [] {
        const std::string replay = R"({"$format":"replay@1","replay":{"steps":[
            {"caption":"1","files":{"app/main.ts":"a1","README.md":"r1"}},
            {"caption":"2","files":{"app/main.ts":"a2"}}],
          "texts":{"a1":"import { column, slider } from \"openmui\";\n","r1":"import { chart } from \"openmui\";","a2":"import { column, button } from \"openmui\";\n"}}})";
        const auto steps = gen::replay_scripts(replay, "design/main.replay.xdef.json");
        expect(steps.has_value() && eq(steps->size(), 2uz));
        if (!steps) return;
        expect(eq(steps->front().name, std::string("design/main.replay.xdef.json#1:app/main.ts")));
        const auto closure = gen::derive_closure(*steps);
        expect(closure.has_value() && eq(closure->components, std::vector<std::string>{"button", "column", "slider"}));
    };

    "everything of openmui, or the designer linked: every component, and why"_test = [] {
        const std::array star{gen::ClosureSource{"app/main.ts", "import { row } from \"openmui\";\nimport * as ui from \"openmui\";\n"}};
        const auto closure = gen::derive_closure(star);
        expect(closure.has_value() && closure->full && closure->why.contains("app/main.ts:2")) << (closure ? closure->why : closure.error());
        if (closure) expect(gen::closure_unit(*closure).contains("omui::register_base_components();"));
        const std::array plain{gen::ClosureSource{"app/main.ts", "import { row } from \"openmui\";\n"}};
        const auto designing = gen::derive_closure(plain, "the designer is linked");
        expect(designing.has_value() && designing->full && eq(designing->why, std::string("the designer is linked")));
    };

    "a script that does not read: the error says where"_test = [] {
        const std::array broken{gen::ClosureSource{"app/main.ts", "enum E { A }\n"}};
        const auto closure = gen::derive_closure(broken);
        expect(!closure.has_value() && closure.error().starts_with("app/main.ts: "));
    };
}};
