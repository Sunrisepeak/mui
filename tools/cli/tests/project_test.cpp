// omui's project commands (D-150): the help names them; doctor says what is
// there and what each tool is for, as records for an agent; new lists its
// templates (TypeScript first) and refuses one it does not have — before any
// tool runs; check, in a project, finds what is not erasable TypeScript and
// what a view document gets wrong, where each is, in one list.
import std;
import mcpplibs.mtest;
import mcpplibs.os;
import omui.cli;
import omui.core;
import omui.designer;
import omui.mcp;

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

    "design sync: Live Design's commits become git commits on design/<branch>, the work tree and HEAD untouched; again, nothing new"_test = [] {
        const auto repository = std::filesystem::temp_directory_path() / "omui-cli-design-sync";
        std::error_code error;
        std::filesystem::remove_all(repository, error);
        std::filesystem::create_directories(repository / "app", error);
        const auto git = [&](std::vector<std::string> args) {
            mcpplibs::os::process::spawn_options options;
            options.program = "git";
            options.args = std::move(args);
            options.cwd = repository;
            options.env = {{"GIT_AUTHOR_NAME", "omui"}, {"GIT_AUTHOR_EMAIL", "omui@example.org"}, {"GIT_COMMITTER_NAME", "omui"},
                           {"GIT_COMMITTER_EMAIL", "omui@example.org"}};
            auto ran = mcpplibs::os::process::run(std::move(options));
            std::string out = ran ? ran->out : std::string();
            while (!out.empty() && out.back() == '\n') out.pop_back();
            return std::pair{ran && ran->status.shell_code() == 0, out};
        };
        if (!mcpplibs::os::env::find_executable("git")) return;  // no git here: nothing to carry it into
        std::ofstream(repository / "app" / "app.ts", std::ios::binary) << "mount(\"main\", text({ text: \"one\" }));\n";
        expect(git({"init", "-q", "-b", "work"}).first && git({"add", "."}).first && git({"commit", "-q", "-m", "start"}).first);
        const std::string start = git({"rev-parse", "HEAD"}).second;
        designer::DesignHistory history(repository / "app" / ".omui" / "design");
        expect(history.commit({{"app.ts", "mount(\"main\", text({ text: \"two\" }));\n"}}, "says two", "agent:designer", "t2").has_value());
        expect(history.branch("loud").has_value() && history.switch_to("loud").has_value());
        expect(history.commit({{"app.ts", "mount(\"main\", text({ text: \"THREE\" }));\n"}}, "shouts three", "agent:designer", "t3").has_value());
        expect(history.tag("first-try", "main").has_value());

        const auto ran = invoke({"omui", "design", "sync", "--dir", mcpplibs::os::fs::utf8(repository / "app"), "--json"});
        expect(eq(ran.code, 0)) << ran.err;
        expect(eq(records(ran.out).size(), std::size_t{2})) << ran.out;
        expect(eq(git({"show", "design/main:app/app.ts"}).second, std::string("mount(\"main\", text({ text: \"two\" }));")));
        expect(eq(git({"show", "design/loud:app/app.ts"}).second, std::string("mount(\"main\", text({ text: \"THREE\" }));")));
        expect(eq(git({"rev-parse", "design/loud~1"}).second, git({"rev-parse", "design/main"}).second)) << "a branch grows from its parent's commit";
        expect(eq(git({"log", "-1", "--format=%an", "design/main"}).second, std::string("agent:designer"))) << "a design commit is its actor's";
        expect(eq(git({"rev-parse", "design/main~1"}).second, start)) << "the design grows from where the repository was";
        expect(eq(git({"rev-parse", "design/first-try^{commit}"}).second, git({"rev-parse", "design/main"}).second)) << "tags too";
        expect(eq(git({"rev-parse", "HEAD"}).second, start) && eq(git({"status", "--porcelain", "--untracked-files=no"}).second, std::string()))
            << "HEAD and the work tree as they were";
        const auto again = invoke({"omui", "design", "sync", "--dir", mcpplibs::os::fs::utf8(repository / "app")});
        expect(eq(again.code, 0) && again.out.contains("0 design commits written")) << again.out << again.err;
    };

    "omui mcp: the command line's commands as tools — one that reads runs, one that writes something to keep asks the user first"_test = [] {
        // A client that cannot ask the user itself (no elicitation).
        struct Lines final : mcp::Transport {
            std::deque<std::string> in;
            std::vector<Value> out;
            std::optional<std::string> read() override {
                if (in.empty()) return std::nullopt;
                std::string line = std::move(in.front());
                in.pop_front();
                return line;
            }
            void write(std::string_view line) override {
                if (auto value = parse_json(line)) out.push_back(std::move(*value));
            }
        } client;
        const auto call = [](int id, std::string_view tool, Value args) {
            return Value{}.set("jsonrpc", "2.0").set("id", id).set("method", "tools/call").set("params", Value{}.set("name", tool).set("arguments", std::move(args))).json();
        };
        client.in = {Value{}.set("jsonrpc", "2.0").set("id", 1).set("method", "initialize").set("params", Value{}.set("protocolVersion", "2025-06-18").set("capabilities", Value(Value::Map{}))).json(),
                     Value{}.set("jsonrpc", "2.0").set("id", 2).set("method", "tools/list").json(),
                     call(3, "omui_doctor", Value(Value::Map{})),
                     call(4, "omui_package", Value(Value::Map{}))};
        mcp::Server server({.directory = std::filesystem::temp_directory_path() / "omui-cli-mcp", .commands = parse_json(cli::ops::listing).value_or(Value{}),
                            .run_command = cli::run_for_agent, .design = false});
        server.serve(client);
        const auto reply = [&](int id) -> const Value* {
            for (const Value& message : client.out)
                if (message.get("id") && message.get("id")->as_int() == id) return message.get("result");
            return nullptr;
        };
        const Value* listed = reply(2);
        std::set<std::string> names;
        if (listed && listed->get("tools") && listed->get("tools")->list())
            for (const Value& tool : *listed->get("tools")->list()) names.insert(std::string(tool.get("name")->as_string()));
        for (const auto* name : {"omui_doctor", "omui_build", "omui_test", "omui_package", "omui_new", "omui_design_film", "omui_design_sync"})
            expect(names.contains(name)) << name;
        const Value* doctor = reply(3);
        const Value* ran = doctor && doctor->get("structuredContent") ? doctor->get("structuredContent")->get("data") : nullptr;
        expect(ran && ran->get("exit") && ran->get("out")->as_string().contains("\"tool\":\"mcpp\"")) << (doctor ? doctor->json() : std::string("none"));
        const Value* package = reply(4);
        expect(package && package->get("isError")->as_bool() && package->json().contains("needs the user's confirmation") && package->json().contains("needs_answer"))
            << (package ? package->json() : std::string("none"));
        expect(!package || !package->json().contains("exited")) << "nothing run before the user says so";
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

    "deps, tools and config read the project here: each dependency by its kind; a tool not installed is said; the config over the defaults"_test = [] {
        const auto root = temp_dir() / "manifest";
        std::filesystem::create_directories(root);
        std::ofstream(root / "mcpp.toml") << "[package]\nname = \"probe\"\n\n[dependencies]\nmcpplibs.os = \"0.4.1\"\n"
                                              "\"omui.lang\" = { path = \"../lang\" }\n\n[dev-dependencies]\nmcpplibs.mtest = \"0.5.0\"\n\n"
                                              "[xlings.workspace]\n\"xim:no-such-tool-for-omui\" = { version = \"1.0.0\", when = \"dev\" }\n";
        std::ofstream(root / "omui.config.ts") << "import { defineConfig } from \"omui/define\";\nexport default defineConfig({ agent: { release: \"dual-user\" } });\n";
        const auto here = std::filesystem::current_path();
        std::filesystem::current_path(root);
        const auto deps = invoke({"omui", "deps", "--json"});
        const auto tools = invoke({"omui", "tools", "--json"});
        const auto config = invoke({"omui", "config", "--json"});
        std::filesystem::current_path(here);
        const auto listed = records(deps.out);
        expect(eq(deps.code, 0) && eq(listed.size(), std::size_t{3})) << deps.out + deps.err;
        expect(std::ranges::any_of(listed, [](const Value& v) { return v.get("package")->as_string() == "mcpplibs.os" && v.get("version")->as_string() == "0.4.1"; }));
        expect(std::ranges::any_of(listed, [](const Value& v) { return v.get("package")->as_string() == "omui.lang" && v.get("path")->as_string() == "../lang"; }));
        expect(std::ranges::any_of(listed, [](const Value& v) { return v.get("package")->as_string() == "mcpplibs.mtest" && v.get("kind")->as_string() == "dev-dependencies"; }));
        const auto named = records(tools.out);
        expect(eq(tools.code, 1) && eq(named.size(), std::size_t{1}) && !named.front().get("installed")->as_bool()) << tools.out + tools.err;
        const auto read = records(config.out);
        expect(eq(config.code, 0) && !read.empty()) << config.out + config.err;
        if (!read.empty()) {
            expect(read.front().get("agent")->get("release")->as_string() == "dual-user") << "omui.config.ts over the default";
            expect(read.front().get("record")->get("development")->as_string() == "full") << "the default where it says nothing";
            expect(read.front().get("designer")->get("development")->as_bool() && !read.front().get("designer")->get("release")->as_bool())
                << "the designer linked in development, left out of a release";
        }
    };

    "the designer is linked as omui.config.ts says: in development by default, in a release when the application ships it"_test = [] {
        const auto root = temp_dir() / "designer";
        std::filesystem::create_directories(root);
        // A project the player runs (it names the player's entry); mcpp stops at once: the commands are what is looked at.
        std::ofstream(root / "mcpp.toml") << "[package\nomui.entry.player = { path = \"player\" }\n";
        const auto here = std::filesystem::current_path();
        std::filesystem::current_path(root);
        const auto command_of = [](std::vector<std::string> line) {
            const auto ran = records(invoke(std::move(line)).out);
            std::string joined;
            if (!ran.empty())
                for (const Value& part : *ran.front().get("command")->list()) joined += std::string(part.as_string()) + " ";
            return joined;
        };
        const std::string developing = command_of({"omui", "dev", "--json"});
        const std::string shipping = command_of({"omui", "build", "--release", "--json"});
        std::ofstream(root / "omui.config.ts") << "import { defineConfig } from \"omui/define\";\n"
                                                  "export default defineConfig({ designer: { development: false, release: true } });\n";
        const std::string plain = command_of({"omui", "dev", "--json"});
        const std::string tool = command_of({"omui", "build", "--release", "--json"});
        std::ofstream(root / "omui.config.ts") << "import { defineConfig } from \"omui/define\";\n"
                                                  "export default defineConfig({ designer: { release: \"yes\" } });\n";
        const auto refused = invoke({"omui", "build", "--json"});
        std::filesystem::current_path(here);
        expect(developing.contains("--features omui.entry.player/design -- --dev")) << developing;
        expect(!shipping.contains("omui.entry.player/design") && shipping.contains("build --release")) << shipping;
        expect(!plain.contains("omui.entry.player/design") && plain.contains("run -- --dev")) << plain;
        expect(tool.contains("build --release --features omui.entry.player/design")) << tool;
        expect(eq(refused.code, 1) && refused.err.contains("designer.release is true or false")) << refused.err;
        // A C++ application (its own main): omui dev is the development host, which builds it and draws it in its window (W16).
        std::filesystem::current_path(root);
        std::ofstream(root / "mcpp.toml") << "[package\n";
        std::filesystem::remove(root / "omui.config.ts");
        const std::string hosted = command_of({"omui", "dev", "--json"});
        std::filesystem::current_path(here);
        expect(hosted.contains("omui-devhost --project .")) << hosted;
    };
}};

