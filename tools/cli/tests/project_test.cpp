// omui's project commands (D-150): the help names them; doctor says what is
// there and what each tool is for, as records for an agent; new lists its
// templates (TypeScript first) and refuses one it does not have — before any
// tool runs; check, in a project, finds what is not erasable TypeScript and
// what a view document gets wrong, where each is, in one list.
import std;
import mcpplibs.mtest;
import omui.cli;
import omui.core;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

struct Ran {
    int code;
    std::string out, err;
};

Ran invoke(std::vector<std::string> args) {
    std::ostringstream out, err;
    const auto code = cli::run(args, out, err);
    return {code, out.str(), err.str()};
}

std::vector<Value> records(const std::string& text) {
    std::vector<Value> out;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);)
        if (auto value = parse_json(line)) out.push_back(std::move(*value));
    return out;
}

}  // namespace

UTest cases{"cli.project", [] {
    "the help names the project commands first"_test = [] {
        const auto ran = invoke({"omui"});
        expect(eq(ran.code, 0));
        for (const auto* command : {"omui new NAME", "omui dev", "omui run | build | test", "omui doctor"})
            expect(ran.out.contains(command)) << command;
        expect(ran.out.find("omui new") < ran.out.find("omui agent")) << "a project's commands before the agent's";
    };

    "doctor: each tool, found or not, required or not, what it is for"_test = [] {
        const auto ran = invoke({"omui", "doctor", "--json"});
        const auto list = records(ran.out);
        expect(eq(list.size(), std::size_t{4})) << ran.out;
        std::vector<std::string> tools;
        for (const auto& item : list) {
            const auto* tool = item.get("tool");
            expect(tool && item.get("required") && item.get("for") && item.get("found")) << item.json();
            if (tool) tools.emplace_back(tool->as_string());
        }
        expect(eq(tools, std::vector<std::string>{"mcpp", "xlings", "tsc", "esbuild"}));
        const bool required_missing = std::ranges::any_of(list, [](const Value& item) {
            return item.get("required")->as_bool() && item.get("found")->is_null();
        });
        expect(eq(ran.code, required_missing ? 1 : 0));
    };

    "new: the templates, TypeScript first; one it has not is refused before mcpp runs"_test = [] {
        const auto listed = records(invoke({"omui", "new", "x", "--list", "--json"}).out);
        expect(!listed.empty() && listed.front().get("template")->as_string() == "app-ts");
        const auto refused = invoke({"omui", "new", "x", "--template", "app-lua"});
        expect(eq(refused.code, 2));
        expect(refused.err.contains("no template `app-lua`")) << refused.err;
    };

    "check in a project: TypeScript that is not erasable and a view document's mistake, where each is, as records"_test = [] {
        const auto root = temp_dir() / "project";
        std::filesystem::create_directories(root / "contracts");
        std::filesystem::create_directories(root / "design");
        std::filesystem::create_directories(root / "target" / "skipped");
        std::ofstream(root / "mcpp.toml") << "[package]\nname = \"probe\"\n";
        std::ofstream(root / "contracts" / "dial.contract.omui.ts") << "import { defineContract } from \"openmui/define\";\nenum Size { S }\n"
                                                                      "export default defineContract({ dial: { role: \"slider\", doc: \"A dial.\" } });\n";
        std::ofstream(root / "design" / "main.view.omui.json") << R"({"omui": "0.1", "view": {"root": {"type": "colum", "id": "page"}}})";
        std::ofstream(root / "target" / "skipped" / "bad.ts") << "enum Ignored { A }\n";  // builds are not sources
        const auto here = std::filesystem::current_path();
        std::filesystem::current_path(root);
        const auto ran = invoke({"omui", "check", "--json"});
        std::filesystem::current_path(here);
        expect(eq(ran.code, 1)) << ran.out + ran.err;
        const auto found = records(ran.out);
        expect(eq(found.size(), std::size_t{2})) << ran.out;
        const auto erasable = std::ranges::find_if(found, [](const Value& v) { return v.get("code")->as_string() == "erasable"; });
        expect(erasable != found.end() && erasable->get("file")->as_string() == "contracts/dial.contract.omui.ts" && erasable->get("line")->as_int() == 2)
            << ran.out;
        expect(std::ranges::any_of(found, [](const Value& v) { return v.get("file")->as_string() == "design/main.view.omui.json"; })) << ran.out;
    };
}};
