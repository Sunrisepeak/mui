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

}  // namespace

cmd::App project_command(std::string_view name) {
    cmd::App a{std::string(name)};
    if (name == "new") {
        (void)a.description("A new project from a template (default app-ts): mcpp new NAME --template openmui:TEMPLATE");
        a.arg("name").required().help("the project's name and directory");
        a.option("template").takes_value().value_name("TEMPLATE").help("app-ts, app-cpp, component, native or theme");
        a.option("list").help("the templates, and what each makes");
    } else if (name == "dev") {
        (void)a.description("Runs the application in development mode: the application is its own designer, a person and an agent design it live (D-149)");
        a.option("headless").help("no window: an agent designs through the protocol (the same operations, the same pixels)");
    } else if (name == "run") {
        (void)a.description("Runs the application as it ships: mcpp run (after --, its own switches: --headless, --png FILE, --serve …)");
        a.option("release").help("an optimised build");
    } else if (name == "build") {
        (void)a.description("Builds the project: mcpp build (omui's steps — contracts, embedded files — are mcpp plugins)");
        a.option("release").help("an optimised build, the designer left out");
    } else if (name == "test") {
        (void)a.description("Runs the project's tests (mtest): mcpp test (after --, which cases)");
    } else if (name == "doctor") {
        (void)a.description("Says which tools are there and which are missing, and what each is for");
    }
    a.option("json").help("one JSON record a step, then the exit code (for an agent)");
    return a;
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
        if (p.is_flag_set("headless")) args.push_back("--headless");
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
    }
    record(out, json, name, ran);
    return ran.code;
}

}  // namespace omui::cli
