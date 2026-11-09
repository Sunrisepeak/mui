// omui new, dev, run, build, test and doctor (D-150, D-153): a project from
// its start to its package, in the framework's words. mcpp builds, tests and
// packages; omui's own build steps are mcpp plugins, so `omui build` is
// `mcpp build` — a person who is no programmer need not learn mcpp, and one
// who is may use either. Each command says what it ran; with --json, one
// record a step and its exit code, for an agent.
module omui.cli;

import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import tomlplusplus;
import omui.lang.ts;
import omui.core;

namespace omui::cli {

namespace cmd = mcpplibs::cmdline;

namespace {

// The templates `omui new` offers (modules/projects/omui/templates): TypeScript first (D-138).
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

// The arguments after `--`, for the application or the tool.
std::vector<std::string> rest_of(const cmd::ParsedArgs& p, std::size_t from) {
    std::vector<std::string> rest;
    for (std::size_t index = from; index < p.positional_count(); ++index) rest.push_back(p.positional(index));
    return rest;
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
    if (missing && !json) err << "omui tools: `omui tools --install` installs what is missing\n";
    return missing ? 1 : 0;
}

int config(bool json, std::ostream& out, std::ostream& err) {
    // The player's defaults (D-155, D-148), then what omui.config.ts says over them.
    Value config = Value{}
                       .set("agent", Value{}.set("development", "development").set("release", "automatic"))
                       .set("record", Value{}.set("development", "full").set("release", "errors"));
    std::string from = "the defaults (no omui.config.ts)";
    if (std::filesystem::exists("omui.config.ts")) {
        auto data = lang::ts::evaluate("omui.config.ts");
        if (!data) {
            err << std::format("omui config: {}\n", data.error());
            return 1;
        }
        if (data->kind != "config") {
            err << std::format("omui config: omui.config.ts's default export is defineConfig(…), not a {}\n", data->kind);
            return 1;
        }
        const auto given = parse_json(data->json);
        if (given && given->map())
            for (const auto& [section, value] : *given->map())
                if (const Value* defaults = config.get(section); defaults && value.map()) {
                    Value table = *defaults;
                    for (const auto& [mode, level] : *value.map()) table.set(mode, level);
                    config.set(section, std::move(table));
                }
        from = "omui.config.ts over the defaults";
    }
    if (json) {
        out << Value(config).set("from", from).json() << '\n';
        return 0;
    }
    for (const std::string_view section : {"agent", "record"})
        for (const std::string_view mode : {"development", "release"})
            out << std::format("{:7} {:12} {}\n", section, mode, config.get(section)->get(mode)->as_string());
    out << std::format("({})\n", from);
    return 0;
}

}  // namespace

cmd::App project_command(std::string_view name) {
    using cmd::App, cmd::Arg, cmd::Option;
    const auto json = Option("json").help("one JSON record a step, then the exit code (for an agent)");
    if (name == "new")
        return App("new")
            .description("A new project from a template (default app-ts): mcpp new NAME --template openmui:TEMPLATE")
            .arg(Arg("name").required().help("the project's name and directory"))
            .option(Option("template").takes_value().value_name("TEMPLATE").help("app-ts, app-cpp, component, native or theme"))
            .option(Option("list").help("the templates, and what each makes"))
            .option(json);
    if (name == "dev")
        return App("dev")
            .description("Runs the application in development mode: the application is its own designer, a person and an agent design it live (D-149)")
            .option(Option("headless").help("no window: an agent designs through the protocol (the same operations, the same pixels)"))
            .option(json);
    if (name == "run")
        return App("run")
            .description("Runs the application as it ships: mcpp run (after --, its own switches: --headless, --png FILE, --serve …)")
            .option(Option("release").help("an optimised build"))
            .option(json);
    if (name == "build")
        return App("build")
            .description("Builds the project: mcpp build (omui's steps — contracts, embedded files — are mcpp plugins)")
            .option(Option("release").help("an optimised build, the designer left out"))
            .option(json);
    if (name == "test") return App("test").description("Runs the project's tests (mtest): mcpp test (after --, which cases)").option(json);
    if (name == "replay")
        return App("replay")
            .description("Plays a recording (the protocol's `record`, development mode) in a fresh instance, headless, and checks it ends "
                         "as it did — the semantic tree's hash and the pixels' (W12): mcpp run -- --replay FILE")
            .arg(Arg("file").required().help("the recording (JSON)"))
            .option(json);
    if (name == "package")
        return App("package")
            .description("Packages the application to give away: mcpp pack (a program's bundle; --format dist-toy: a page for Bilibili Toy)")
            .option(Option("release").help("an optimised build (the default for a package)"))
            .option(Option("format").takes_value().value_name("FORMAT").help("the package's format (mcpp pack --pkg-format)"))
            .option(json);
    if (name == "add")
        return App("add")
            .description("Adds a dependency to mcpp.toml: mcpp add PACKAGE@VERSION")
            .arg(Arg("package").required().help("[namespace.]name@version, e.g. mcpplibs.os@0.4.1"))
            .option(json);
    if (name == "remove")
        return App("remove").description("Removes a dependency from mcpp.toml: mcpp remove PACKAGE").arg(Arg("package").required()).option(json);
    if (name == "design")
        return App("design")
            .description("Live Design's history (.omui/design: the design.history verbs keep it)")
            .subcommand(App("sync")
                            .description("Carries it into git: each design commit a commit on design/<branch>, tags as design/<tag>; the work tree untouched")
                            .option(Option("dir").takes_value().value_name("DIR").help("the application's directory (here when absent)"))
                            .option(json));
    if (name == "deps")
        return App("deps").description("The project's dependencies, as its mcpp.toml declares them: each package, its kind, its version or path").option(json);
    if (name == "tools")
        return App("tools")
            .description("The tools the project names for xlings ([xlings.workspace]): each, its version, when it is needed, whether it is here")
            .option(Option("install").help("install them now: xlings install"))
            .option(json);
    if (name == "config")
        return App("config")
            .description("The project's omui.config.ts as the player reads it: how far agents may go, what the recorder keeps, in development and a release")
            .option(json);
    return App("doctor").description("Says which tools are there and which are missing, and what each is for").option(json);
}

int project(std::string_view name, const cmd::ParsedArgs& p, std::ostream& out, std::ostream& err) {
    const bool json = p.is_flag_set("json");
    if (name == "doctor") {
        int missing = 0;
        for (const auto& need : needs) {
            const auto found = mcpplibs::os::env::find_executable(need.tool);
            if (!found && need.required) ++missing;
            if (json)
                out << Value{}
                           .set("tool", std::string(need.tool))
                           .set("found", found ? Value(mcpplibs::os::fs::utf8(*found)) : Value{})
                           .set("required", need.required)
                           .set("for", std::string(need.for_what))
                           .json()
                    << '\n';
            else
                out << std::format("{:8} {:7} {}{}\n", need.tool, found ? "ok" : need.required ? "MISSING" : "absent", need.for_what,
                                   found ? std::format(" ({})", mcpplibs::os::fs::utf8(*found)) : std::string());
        }
        return missing ? 1 : 0;
    }
    if (name == "new" && p.is_flag_set("list")) {
        for (const auto& [template_name, what] : templates)
            if (json)
                out << Value{}.set("template", std::string(template_name)).set("makes", std::string(what)).json() << '\n';
            else
                out << std::format("{:10} {}\n", template_name, what);
        return 0;
    }
    if (name == "design") {
        if (!p.has_subcommand() || p.subcommand_name() != "sync") {
            err << "omui design: sync (omui design sync --help)\n";
            return 2;
        }
        const cmd::ParsedArgs& sync = p.subcommand()->get();
        return design_sync(mcpplibs::os::fs::from_utf8(sync.value("dir").value_or(".")), sync.is_flag_set("json"), out, err);
    }
    Ran ran;
    if (name == "new") {
        const std::string chosen = p.value("template").value_or("app-ts");
        if (std::ranges::find(templates, chosen, &std::pair<std::string_view, std::string_view>::first) == templates.end()) {
            err << std::format("omui new: no template `{}` (omui new --list)\n", chosen);
            return 2;
        }
        ran = run_tool("mcpp", {"new", *p.value("name"), "--template", "openmui:" + chosen}, err);
    } else if (name == "dev") {
        std::vector<std::string> args{"run", "--", "--dev"};
        if (p.is_flag_set("headless")) args.push_back("--serve");  // headless, kept running for agents
        std::ranges::move(rest_of(p, 0), std::back_inserter(args));
        ran = run_tool("mcpp", std::move(args), err);
    } else if (name == "run" || name == "build") {
        std::vector<std::string> args{std::string(name)};
        if (p.is_flag_set("release")) args.push_back("--release");
        auto rest = rest_of(p, 0);
        if (!rest.empty() && name == "run") args.push_back("--");
        std::ranges::move(rest, std::back_inserter(args));
        ran = run_tool("mcpp", std::move(args), err);
    } else if (name == "test") {
        std::vector<std::string> args{"test"};
        std::ranges::move(rest_of(p, 0), std::back_inserter(args));
        ran = run_tool("mcpp", std::move(args), err);
    } else if (name == "replay") {
        ran = run_tool("mcpp", {"run", "-q", "--", "--replay", p.positional(0)}, err);
    } else if (name == "package") {
        std::vector<std::string> args{"pack"};
        if (auto format = p.value("format")) args.insert(args.end(), {"--pkg-format", *format});
        ran = run_tool("mcpp", std::move(args), err);
    } else if (name == "add" || name == "remove") {
        ran = run_tool("mcpp", {std::string(name), p.positional(0)}, err);
    } else if (name == "deps") {
        return deps(json, out, err);
    } else if (name == "tools") {
        if (p.is_flag_set("install")) ran = run_tool("xlings", {"install", "-y"}, err);
        else return tools(json, out, err);
    } else if (name == "config") {
        return config(json, out, err);
    }
    record(out, json, name, ran);
    return ran.code;
}

}  // namespace omui::cli
