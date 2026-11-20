// omui new, dev, run, build, test and doctor (D-150, D-153): a project from
// its start to its package, in the framework's words. mcpp builds, tests and
// packages; omui's own build steps are mcpp plugins, so `omui build` is
// `mcpp build` — a person who is no programmer need not learn mcpp, and one
// who is may use either. Each command says what it ran; with --json, one
// record a step and its exit code, for an agent.
module omui.cli;

import std;
import mlibs.project;
import mlibs.agent;
import openas;
import mcpplibs.os;
import tomlplusplus;
import omui.lang.ts;
import omui.core;

namespace omui::cli {


namespace {

// The templates `omui new` offers (modules/projects/omui/sdk/templates, carried by omui.sdk): TypeScript first (D-138).
constexpr std::array<std::pair<std::string_view, std::string_view>, 5> templates{{
    {"app-ts", "an application in TypeScript, run by the player: no C++ to write (the default)"},
    {"app-cpp", "an application in C++"},
    {"component", "a component package: its contract, its C++, its tests"},
    {"native", "a native module scripts call (openmui:native/<name>)"},
    {"theme", "a theme package"},
}};

// What a command ran: the program, its arguments, its exit code.
struct Ran {
    std::vector<std::string> command;
    int code = 0;
};

void record(std::ostream& out, bool json, std::string_view step, const Ran& ran) {
    if (!json) return;
    Value::List command;
    for (const auto& part : ran.command) command.push_back(Value(part));
    out << Value{}.set("step", std::string(step)).set("command", Value(std::move(command))).set("exit", static_cast<double>(ran.code)).json() << '\n';
}

// Runs `program args…` with this process's terminal (a window, a build's
// progress, a test's report go where the person looks); its exit code.
Ran run_tool(std::string_view program, std::vector<std::string> args, std::ostream& err) {
    Ran ran;
    ran.command.push_back(std::string(program));
    ran.command.insert(ran.command.end(), args.begin(), args.end());
    const auto found = mcpplibs::os::env::find_executable(program);
    if (!found) {
        err << std::format("omui: `{}` is not on PATH (xlings install {}; omui doctor says what is missing)\n", program, program);
        ran.code = 127;
        return ran;
    }
    mcpplibs::os::process::spawn_options options;
    options.program = *found;
    options.args = std::move(args);
    if (tools_caught()) {
        // For an agent: what the tool says becomes the command's.
        auto caught = mcpplibs::os::process::run(std::move(options));
        if (!caught) {
            err << std::format("omui: {} did not start: {}\n", program, caught.error().message());
            ran.code = 127;
            return ran;
        }
        err << caught->out << caught->err;
        ran.code = caught->status.shell_code();
        return ran;
    }
    options.out = mcpplibs::os::process::stdio::inherit;
    options.err = mcpplibs::os::process::stdio::inherit;
    auto process = mcpplibs::os::process::process::spawn(options);
    if (!process) {
        err << std::format("omui: {} did not start: {}\n", program, process.error().message());
        ran.code = 127;
        return ran;
    }
    auto status = process->wait();
    ran.code = status ? status->shell_code() : 1;
    return ran;
}

// What `omui doctor` looks for: the tool, whether a project needs it, what it is for.
struct Need {
    std::string_view tool;
    bool required;
    std::string_view for_what;
};

constexpr std::array needs{
    Need{"mcpp", true, "builds, tests, runs and packages (omui new, dev, run, build, test)"},
    Need{"xlings", true, "installs the tools and toolchains"},
    Need{"tsc", false, "type-checks TypeScript against the generated .d.ts (omui check)"},
    Need{"esbuild", false, "the oracle of omui.lang.ts's type stripper in its tests; nothing runs it otherwise"},
};

// The project's manifest here, parsed; why not, said on `err`.
std::optional<toml::table> manifest(std::ostream& err) {
    // toml++ is built to throw its parse errors: they stop here, said.
    try {
        return toml::parse_file("mcpp.toml");
    } catch (const toml::parse_error& error) {
        err << std::format("omui: mcpp.toml:{}:{}: {}\n", error.source().begin.line, error.source().begin.column, error.description());
        return std::nullopt;
    }
}

// A dependency table's packages, its dotted namespaces flattened
// (`mcpplibs.os = "0.4.1"`, `"omui.lang" = { path = ".." }`).
void packages_of(const toml::table& table, const std::string& prefix, std::string_view kind, Value::List& out) {
    for (auto&& [key, node] : table) {
        const std::string name = prefix.empty() ? std::string(key.str()) : std::format("{}.{}", prefix, key.str());
        if (auto version = node.value<std::string>()) {
            out.push_back(Value{}.set("package", name).set("kind", std::string(kind)).set("version", *version));
        } else if (const toml::table* inner = node.as_table()) {
            if (!inner->contains("path") && !inner->contains("version") && !inner->contains("git")) {
                packages_of(*inner, name, kind, out);
                continue;
            }
            Value one;
            one.set("package", name).set("kind", std::string(kind));
            if (auto version = (*inner)["version"].value<std::string>()) one.set("version", *version);
            if (auto path = (*inner)["path"].value<std::string>()) one.set("path", *path);
            out.push_back(std::move(one));
        }
    }
}

int deps(bool json, std::ostream& out, std::ostream& err) {
    const auto root = manifest(err);
    if (!root) return 1;
    Value::List list;
    for (const std::string_view kind : {"dependencies", "dev-dependencies", "build-dependencies"})
        if (const toml::table* table = (*root)[kind].as_table()) packages_of(*table, "", kind, list);
    // A platform's own: [target.'cfg(…)'.dependencies].
    if (const toml::table* targets = (*root)["target"].as_table())
        for (auto&& [condition, node] : *targets)
            if (const toml::table* table = node.as_table())
                if (const toml::table* dependencies = (*table)["dependencies"].as_table())
                    packages_of(*dependencies, "", std::format("dependencies ({})", condition.str()), list);
    for (const Value& one : list) {
        if (json) {
            out << one.json() << '\n';
            continue;
        }
        const Value* version = one.get("version");
        const Value* path = one.get("path");
        out << std::format("{:32} {:14} {}\n", one.get("package")->as_string(), version ? version->as_string() : "",
                           path ? std::format("path {}", path->as_string()) : std::format("({})", one.get("kind")->as_string()));
    }
    return 0;
}

// Whether xlings has a tool installed, as it says (`xlings info … --agent`).
bool installed(std::string_view tool, std::string_view version) {
    mcpplibs::os::process::spawn_options options;
    const auto program = mcpplibs::os::env::find_executable("xlings");
    if (!program) return false;
    options.program = *program;
    options.args = {"info", version.empty() ? std::string(tool) : std::format("{}@{}", tool, version), "--agent"};
    auto ran = mcpplibs::os::process::run(std::move(options));
    return ran && ran->out.contains("selected installed: yes");
}

int tools(bool json, std::ostream& out, std::ostream& err) {
    const auto root = manifest(err);
    if (!root) return 1;
    int missing = 0;
    for (const toml::table* table : {(*root)["xlings"]["workspace"].as_table(), (*root)["workspace"]["xlings"]["workspace"].as_table()}) {
        if (!table) continue;
        for (auto&& [tool, node] : *table) {
            std::string version, when;
            if (auto text = node.value<std::string>()) version = *text;
            else if (const toml::table* entry = node.as_table()) {
                version = (*entry)["version"].value_or(std::string());
                when = (*entry)["when"].value_or(std::string());
            }
            const bool here = installed(tool.str(), version);
            if (!here) ++missing;
            if (json)
                out << Value{}.set("tool", std::string(tool.str())).set("version", version).set("when", when).set("installed", here).json() << '\n';
            else
                out << std::format("{:22} {:12} {:6} {}\n", tool.str(), version, when.empty() ? "always" : when, here ? "ok" : "MISSING");
        }
    }
    if (missing && !json) err << "omui tools: `omui tools install` installs what is missing\n";
    return missing ? 1 : 0;
}

// The player's defaults (D-155, D-148, D-149), then what omui.config.ts says over them: each
// setting's value in development and in a release, and where they came from.
struct Config {
    Value settings;
    std::string from;
    bool designer(bool development) const {
        const Value* linked = settings.get("designer")->get(development ? "development" : "release");
        return linked->as_bool();
    }
};

std::expected<Config, std::string> read_config() {
    Config config{Value{}
                      .set("agent", Value{}.set("development", "development").set("release", "automatic"))
                      .set("record", Value{}.set("development", "full").set("release", "errors"))
                      .set("designer", Value{}.set("development", true).set("release", false)),
                  "the defaults (no omui.config.ts)"};
    if (!std::filesystem::exists("omui.config.ts")) return config;
    auto data = lang::ts::evaluate("omui.config.ts");
    if (!data) return std::unexpected(data.error().text());
    if (data->kind != "config") return std::unexpected(std::format("omui.config.ts's default export is defineConfig(…), not a {}", data->kind));
    const auto given = parse_json(data->json);
    if (given && given->map())
        for (const auto& [section, value] : *given->map())
            if (const Value* defaults = config.settings.get(section); defaults && value.map()) {
                Value table = *defaults;
                for (const auto& [mode, setting] : *value.map()) {
                    if (section == "designer" && setting.type() != Value::Type::boolean)
                        return std::unexpected(std::format("omui.config.ts: designer.{} is true or false", mode));
                    table.set(mode, setting);
                }
                config.settings.set(section, std::move(table));
            }
    config.from = "omui.config.ts over the defaults";
    return config;
}

// A project the player runs (TypeScript, D-138): its manifest names the player's entry. Else its
// own main, in C++.
bool scripted_project() {
    const auto manifest = mcpplibs::os::fs::read("mcpp.toml");
    return !manifest || manifest->contains("omui.entry.player");
}

int config(bool json, std::ostream& out, std::ostream& err) {
    const auto config = read_config();
    if (!config) {
        err << std::format("omui config: {}\n", config.error());
        return 1;
    }
    if (json) {
        out << Value(config->settings).set("from", config->from).json() << '\n';
        return 0;
    }
    for (const std::string_view section : {"agent", "record", "designer"})
        for (const std::string_view mode : {"development", "release"}) {
            const Value* setting = config->settings.get(section)->get(mode);
            const std::string shown = setting->type() == Value::Type::boolean ? (setting->as_bool() ? "linked" : "left out")
                                                                              : std::string(setting->as_string());
            out << std::format("{:8} {:12} {}\n", section, mode, shown);
        }
    out << std::format("({})\n", config->from);
    return 0;
}

}  // namespace

bool& tools_caught() {
    static bool caught = false;
    return caught;
}

bool names_shipped_player(std::string_view manifest) {
    try {
        const toml::table project = toml::parse(manifest);
        return project["dependencies"]["omui"]["entry"]["player"].value<std::string>() == std::optional<std::string>(std::string(release_version));
    } catch (const toml::parse_error&) {
        return false;
    }
}

bool declares_runtime(std::string_view manifest) {
    try {
        const toml::table project = toml::parse(manifest);
        return project["package"]["metadata"]["openmui"]["runtime"].as_table() != nullptr;
    } catch (const toml::parse_error&) {
        return false;
    }
}

namespace {

// The launcher an installed runtime puts on PATH (E3b), for an application of this release that declares its runtime:
// whether one of the installed runtimes satisfies it (`omui-launch --which`), and when none does, what the launcher says
// (why each does not, and the command that installs one).
struct RuntimeLauncher {
    std::filesystem::path program;
    bool                  satisfied = false;
    std::string           why;
};

std::optional<RuntimeLauncher> runtime_launcher() {
    const std::string manifest = mcpplibs::os::fs::read("mcpp.toml").value_or("");
    if (!names_shipped_player(manifest) || !declares_runtime(manifest)) return std::nullopt;
    const auto found = mcpplibs::os::env::find_executable("omui-launch");
    if (!found) return std::nullopt;
    mcpplibs::os::process::spawn_options options;
    options.program = *found;
    options.args = {"--which", "."};
    options.in = mcpplibs::os::process::stdio::null;
    const auto said = mcpplibs::os::process::run(std::move(options), mcpplibs::os::process::bounds{.hard = std::chrono::seconds(20)});
    if (!said) return std::nullopt;
    RuntimeLauncher launcher{.program = *found, .satisfied = said->status.shell_code() == 0, .why = said->err};
    while (!launcher.why.empty() && (launcher.why.back() == '\n' || launcher.why.back() == ' ')) launcher.why.pop_back();
    return launcher;
}

// The player the omui tool ships beside itself (E2): built with the designer, at this release's version. A
// TypeScript application whose player is this release's (`omui.entry.player = "0.1.0"`) runs through it — `omui new`
// then `omui dev` call no compiler. A checkout's paths, or another version, and nothing is found: mcpp builds the
// project's own player, as before.
std::optional<std::filesystem::path> shipped_player() {
    if (!names_shipped_player(mcpplibs::os::fs::read("mcpp.toml").value_or(""))) return std::nullopt;
    const auto found = mcpplibs::os::env::find_executable("omui-player");
    if (!found) return std::nullopt;
    // What it says it is: another release's on PATH is not this one's.
    mcpplibs::os::process::spawn_options options;
    options.program = *found;
    options.args = {"--version"};
    options.in = mcpplibs::os::process::stdio::null;
    const auto said = mcpplibs::os::process::run(std::move(options), mcpplibs::os::process::bounds{.hard = std::chrono::seconds(20)});
    if (!said || said->status.shell_code() != 0) return std::nullopt;
    std::string version = said->out;
    while (!version.empty() && (version.back() == '\n' || version.back() == '\r' || version.back() == ' ')) version.pop_back();
    if (version != release_version) return std::nullopt;
    return *found;
}

// dev, run and build: mcpp run or build, the designer linked as omui.config.ts says — in development by default,
// left out of a release at compile time (D-149) unless the application is a design tool that ships it. A
// TypeScript application of this release runs through the player the tool ships (nothing built); a build compiles.
int launch(std::string_view name, bool release, bool headless, std::vector<std::string> rest, bool json, std::ostream& out, std::ostream& err) {
    Ran ran;
    // run: an application of this release that declares its runtime starts on the newest installed runtime that
    // satisfies it, as on a user's machine (E3b); with none, on the tool's own player, saying how to install one. dev
    // stays on the tool's player, which carries the designer.
    if (name == "run" && scripted_project())
        if (const auto launcher = runtime_launcher()) {
            if (launcher->satisfied) {
                std::vector<std::string> args{"."};
                if (!rest.empty()) args.push_back("--");
                std::ranges::move(rest, std::back_inserter(args));
                ran = run_tool(mcpplibs::os::fs::utf8(launcher->program), std::move(args), err);
                record(out, json, name, ran);
                return ran.code;
            }
            err << std::format("omui run: no installed runtime satisfies this application; it runs on omui's own player.\n{}\n", launcher->why);
        }
    const std::optional<std::filesystem::path> player = name != "build" && scripted_project() ? shipped_player() : std::nullopt;
    if (player) {
        const bool development = name == "dev";
        std::vector<std::string> args{"--manifest", "."};
        if (development) {
            args.push_back("--dev");
            if (headless) args.push_back("--serve");  // headless, kept running for agents
        }
        std::ranges::move(rest, std::back_inserter(args));
        ran = run_tool(mcpplibs::os::fs::utf8(*player), std::move(args), err);
    } else if (name == "dev" && !scripted_project()) {
        // An application in C++ (W16, D-154): the development host owns the window; mcpp builds the
        // application in the background as its sources change, each build drawn in the host's window.
        std::vector<std::string> args{"--project", "."};
        if (headless) args.push_back("--serve");
        std::ranges::move(rest, std::back_inserter(args));
        ran = run_tool("omui-devhost", std::move(args), err);
    } else {
        const auto config = read_config();
        if (!config) {
            err << std::format("omui {}: {}\n", name, config.error());
            return 1;
        }
        const bool development = name == "dev";
        std::vector<std::string> args{development ? "run" : std::string(name)};
        if (!development && release) args.push_back("--release");
        if (config->designer(development)) args.insert(args.end(), {"--features", "omui.entry.player/design"});
        if (development) {
            args.insert(args.end(), {"--", "--dev"});
            if (headless) args.push_back("--serve");  // headless, kept running for agents
        } else if (!rest.empty() && name == "run") {
            args.push_back("--");
        }
        std::ranges::move(rest, std::back_inserter(args));
        ran = run_tool("mcpp", std::move(args), err);
    }
    record(out, json, name, ran);
    return ran.code;
}

// The page on the Web (D-236): the program, run natively, writes its first screen as static HTML; the pack (the
// package's build.mcpp declares dist_toy, its static_html that file) puts it in the page as the document a reader and
// a search engine read before the program runs.
int build_web(const std::string& lang, std::vector<std::string> rest, std::ostream& out, std::ostream& err) {
    const auto project = manifest(err);
    if (!project) return 1;
    std::string program;
    if (const auto* targets = (*project)["targets"].as_table())
        for (const auto& [target, declared] : *targets)
            if (const auto* table = declared.as_table(); table && (*table)["kind"].value_or(std::string{}) == "bin") {
                program = std::string(target.str());
                break;
            }
    if (program.empty()) {
        err << "omui build --web: mcpp.toml names no program (a [targets.<name>] of kind \"bin\")\n";
        return 1;
    }
    std::error_code error;
    std::filesystem::create_directories("target/web", error);
    std::vector<std::string> first{"run", "--", "--static-html", std::format("target/web/{}.static.html", program)};
    if (!lang.empty()) first.insert(first.end(), {"--lang", lang});
    const Ran projected = run_tool("mcpp", std::move(first), err);
    if (projected.code != 0) {
        record(out, true, "build", projected);
        return projected.code;
    }
    std::vector<std::string> pack{"pack", "--target", "wasm32-emscripten", "--release", "--pkg-format", "dist-toy"};
    // The designer, where omui.config.ts links it into a release (`designer.release`): it starts as the person's agent
    // links (D-149, D-255), as `omui build` links it.
    if (const auto config = read_config(); config && config->designer(false)) pack.insert(pack.end(), {"--features", "omui.entry.player/design"});
    std::ranges::move(rest, std::back_inserter(pack));
    const Ran packed = run_tool("mcpp", std::move(pack), err);
    record(out, true, "build", packed);
    return packed.code;
}

}  // namespace

int operation(std::string_view op, const mlibs::agent::Arguments& a, std::ostream& out, std::ostream& err) {
    const std::string_view name = op.substr(op.starts_with("omui.") ? 5 : 0);
    if (name == "doctor") {
        int missing = 0;
        for (const auto& need : needs) {
            const auto found = mcpplibs::os::env::find_executable(need.tool);
            if (!found && need.required) ++missing;
            out << Value{}
                       .set("tool", std::string(need.tool))
                       .set("found", found ? Value(mcpplibs::os::fs::utf8(*found)) : Value{})
                       .set("required", need.required)
                       .set("for", std::string(need.for_what))
                       .json()
                << '\n';
        }
        if (missing) err << std::format("{} required tool{} missing\n", missing, missing == 1 ? "" : "s");
        // The project's health (D-207): the general rules, the workspace's, and omui's profiles, each finding a record.
        std::size_t findings = 0;
        const auto dir = mcpplibs::os::fs::from_utf8(a.value("dir").value_or("."));
        std::error_code error;
        if (std::filesystem::exists(dir / "mcpp.toml", error)) {
            auto project = mlibs::project::read(dir);
            if (!project) {
                err << std::format("omui doctor: {}\n", project.error());
                return 1;
            }
            // omui's, and those the project declares itself (a domain's owner's, as data): a name declared in the tree is
            // its own.
            auto all = profiles();
            if (auto declared = mlibs::project::profiles_in(dir)) {
                for (auto& profile : *declared) {
                    std::erase_if(all, [&](const mlibs::project::Profile& known) { return known.name == profile.name; });
                    all.push_back(std::move(profile));
                }
            } else {
                err << std::format("omui doctor: {}\n", declared.error());
                return 1;
            }
            for (const auto& finding : mlibs::project::check(*project, all)) {
                ++findings;
                out << Value{}.set("rule", finding.rule).set("where", finding.where).set("problem", finding.text).set("fix", finding.fix).json() << '\n';
            }
            if (findings) err << std::format("{} finding{} in the project\n", findings, findings == 1 ? "" : "s");
        }
        return missing || findings ? 1 : 0;
    }
    if (name == "templates") {
        for (const auto& [template_name, what] : templates) out << Value{}.set("template", std::string(template_name)).set("makes", std::string(what)).json() << '\n';
        return 0;
    }
    if (name == "design.sync") return design_sync(mcpplibs::os::fs::from_utf8(a.value("dir").value_or(".")), true, out, err);
    if (name == "design.film") {
        // The film: the application, with its designer, makes the frames and the description (design.film);
        // the engine's video toolkit renders them, as a program (`xtools video render`): omui's command line does not
        // build the engine in (D-231), and without it the frames stay where they were made.
        const std::string branch = a.value("branch").value_or("");
        const std::string frames = std::format(".omui/design/film/{}", branch.empty() ? "head" : branch);
        std::vector<std::string> args{"run", "-q", "--features", "omui.entry.player/design", "--", "--design-film", frames};
        if (!branch.empty()) args.insert(args.end(), {"--film-branch", branch});
        for (const std::string_view key : {"fps", "seconds"})
            if (auto given = a.value(key)) args.insert(args.end(), {std::format("--film-{}", key), *given});
        Ran made = run_tool("mcpp", std::move(args), err);
        record(out, true, "frames", made);
        if (made.code != 0) return made.code;
        const std::string film = a.value("out").value_or("design.webm");
        Ran rendered = run_tool("xtools", {"video", "render", frames + "/design.video.xdef.json", "--out", film}, err);
        record(out, true, "film", rendered);
        if (rendered.code == 127)
            err << std::format("omui: the frames are in {}; the engine's toolkit renders them: xtools video render {}/design.video.xdef.json --out {}\n", frames,
                               frames, film);
        return rendered.code;
    }
    if (name == "check") {
        const auto files = a.values("files");
        if (files.empty()) return check_project(true, a.flag("portable"), out, err);
        auto found = check_documents(files, err);
        if (!found) return 1;
        return report(*found, files.size(), true, out, err);
    }
    if (name == "describe") return describe(a.value("llms"), out, err);
    if (name == "fmt") return fmt(a, out, err);
    if (name == "schema") return schema(a, out, err);
    if (name == "replay") {
        // mcpp run -- --replay FILE: a fresh instance, headless; its exit code says whether the end matched.
        const Ran ran = run_tool("mcpp", {"run", "-q", "--", "--replay", a.value("file").value_or("")}, err);
        record(out, true, name, ran);
        return ran.code;
    }
    // What holds the terminal (dev, run, connect, mcp): a person's command line runs it; an agent's session refuses it.
    if (name == "dev" || name == "run") return launch(name, a.flag("release"), a.flag("headless"), a.values("rest"), true, out, err);
    if (name == "connect") return connect(a, out, err);
    if (name == "mcp") return serve_mcp(a, err);
    if (name == "build" && a.flag("web")) return build_web(a.value("lang").value_or(""), a.values("rest"), out, err);
    if (name == "build") return launch(name, a.flag("release"), false, a.values("rest"), true, out, err);
    if (name == "deps") return deps(true, out, err);
    if (name == "tools") return tools(true, out, err);
    if (name == "config") return config(true, out, err);
    Ran ran;
    if (name == "new") {
        const std::string chosen = a.value("template").value_or("app-ts");
        if (std::ranges::find(templates, chosen, &std::pair<std::string_view, std::string_view>::first) == templates.end()) {
            err << std::format("no template `{}` (omui templates)\n", chosen);
            return 2;
        }
        ran = run_tool("mcpp", {"new", a.value("name").value_or(""), "--template", "omui.sdk:" + chosen}, err);
    } else if (name == "test") {
        std::vector<std::string> args{"test"};
        std::ranges::move(a.values("rest"), std::back_inserter(args));
        ran = run_tool("mcpp", std::move(args), err);
    } else if (name == "package") {
        std::vector<std::string> args{"pack"};
        if (auto format = a.value("format")) args.insert(args.end(), {"--pkg-format", *format});
        ran = run_tool("mcpp", std::move(args), err);
    } else if (name == "add" || name == "remove") {
        ran = run_tool("mcpp", {std::string(name), a.value("package").value_or("")}, err);
    } else if (name == "tools.install") {
        ran = run_tool("xlings", {"install", "-y"}, err);
    } else {
        err << std::format("`{}` is no operation of omui's\n", op);
        return 2;
    }
    record(out, true, name, ran);
    return ran.code;
}

void Project::operate(std::string_view op, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    // What holds the terminal (only a person's command line runs it) writes to it as it goes — a bridge's address,
    // a server's lines, the application's own output — and answers its exit code.
    if (const auto entry = std::ranges::find(ops::catalog, op, [](const auto& e) { return e.operation.name; }); entry != ops::catalog.end() && entry->terminal) {
        const int code = operation(op, mlibs::agent::Arguments(request), live_out ? *live_out : std::cout, live_errors ? *live_errors : std::cerr);
        if (code != 0) return answer.fail("failed", std::format("{} ended with {}", op, code), code);
        return answer.result(0);
    }
    std::ostringstream out, err;
    // The tools it starts write to what it writes, not to this process's terminal (the protocol may be it).
    tools_caught() = true;
    const int code = operation(op, mlibs::agent::Arguments(request, op == "omui.check" || op == "omui.fmt" ? "files" : ""), out, err);
    tools_caught() = false;
    // Each record it wrote is a data record of the answer; anything else, its text.
    std::istringstream lines(out.str());
    std::string text;
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with('{')) answer.data(line);
        else if (!line.empty()) text += line + "\n";
    }
    if (!text.empty()) answer.data(Value{}.set("text", text).json());
    if (code != 0) {
        std::string said = err.str();
        while (!said.empty() && said.back() == '\n') said.pop_back();
        answer.fail(code == 2 ? openas::error_code::invalid_argument : std::string_view("failed"), said.empty() ? std::format("{} ended with {}", op, code) : said);
    }
}

mlibs::control::Guide Project::guide() const noexcept {
    static constexpr std::array<std::string_view, 3> begin{R"({"op":"omui.doctor"})", R"({"op":"omui.new","name":"hello"})", R"({"op":"omui.build"})"};
    static constexpr std::array<mlibs::control::Task, 1> tasks{{{"a-first-project", "What is installed, a project from a template, then built", begin}}};
    return {"The project's commands: create, build, test, check, describe, package; dependencies, tools and the design's history.", tasks};
}

}  // namespace omui::cli
