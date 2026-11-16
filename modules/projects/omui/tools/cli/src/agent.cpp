module omui.cli;

import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import omui.core;
import omui.agent;
import omui.mcp;

namespace omui::cli {

namespace cmd = mcpplibs::cmdline;

Value run_for_agent(std::string_view command, const Value& args) {
    // The operation asked of the subject `omui` as any entry asks it: one request, its records (what it wrote) and
    // its exit code; what it said went wrong, from its error record.
    Project project;
    mlibs::agent::SubjectSession session({&project});
    Value request = args.map() ? args : Value(Value::Map{});
    request.set("op", std::string(command));
    const mlibs::agent::Answered answered = mlibs::agent::answer_to(session, request.json());
    std::string written, said;
    for (const std::string& record : answered.records) {
        const auto parsed = parse_json(record);
        if (!parsed) continue;
        const std::string kind(parsed->get("kind") ? parsed->get("kind")->as_string() : std::string_view{});
        if (kind == "data" && parsed->get("data")) written += parsed->get("data")->json() + "\n";
        if (kind == "error" && parsed->get("text")) said += std::string(parsed->get("text")->as_string()) + "\n";
    }
    return Value{}.set("exit", static_cast<double>(answered.exit_code)).set("out", written).set("err", said);
}

cmd::App app() {
    using cmd::Option;
    cmd::App line("omui");
    (void)line.version("0.1.0")
        .description("The openmui command line")
        .option(Option("dir").takes_value().value_name("DIR").global().help(
            "connect, mcp: where the bridge and the endpoints say where they are (default: the per-user one)"));
    for (const auto name : project_commands) (void)line.subcommand(project_command(name));  // new, dev, run … in their order
    (void)line.subcommand(connect_command())
        .subcommand(cmd::App("mcp")
                        .description("An MCP server on standard input and output: agents (Claude Code …) operate running applications with ui_* tools")
                        .option(Option("app").takes_value().value_name("NAME").help("the application a call names none of (default: the one running)"))
                        .option(Option("actor").takes_value().value_name("KIND:NAME").help("who acts (default: the client's name, agent:claude-code)"))
                        .option(Option("way").takes_value().value_name("WAY").help(
                            "semantic (default); a11y-tree or screenshot: what a browser agent's accessibility tree or computer use offers, on the same application (D-142)"))
                        .option(Option("ops").takes_value().multiple().value_name("LISTING").help(
                            "an application's verbs as tools, from its <product>.ops.json listing (omui-gen ops)")));
    return line;
}

namespace {

// A global option's value, wherever on the command line it was given: the
// parser marks it on every level, and keeps its value on the level it was
// written at.
std::optional<std::string> global(const cmd::ParsedArgs& p, std::string_view name) {
    std::optional<std::string> found;
    for (const cmd::ParsedArgs* level = &p; level;) {
        if (auto v = level->option(name); v && !v->get().values.empty()) found = v->get().value();
        auto next = level->subcommand();
        level = next ? &next->get() : nullptr;
    }
    return found;
}

// omui agent <operation> <app> …: an application by its endpoint's name. `ls` lists them, `sweep` presses every
// control, `call` sends a request of the protocol's as it is; every other word names an operation of its catalogs.
int agent(std::vector<std::string> words, const std::optional<std::string>& dir_option, std::ostream& out, std::ostream& err) {
    const std::filesystem::path dir = dir_option ? mcpplibs::os::fs::from_utf8(*dir_option) : agent::default_directory();
    if (words.empty() || words.front() == "--help" || words.front() == "-h") {
        std::string names;
        for (const auto& entry : agent::catalog) names += std::format(" {}", entry.operation.name.substr(3));
        (words.empty() ? err : out) << "usage: omui agent <operation> <app> [its parameters, in order or --name value] [--actor A] [--out FILE]\n"
                                       "  the protocol's operations:"
                                    << names
                                    << "\n  the verbs of Live Design, generation and tasks: design.*, generate.*, task.*\n"
                                       "  omui agent ls | sweep <app> [--root ID] | call <app> '<request>'; omui agent <operation> --help describes one\n";
        return words.empty() ? 2 : 0;
    }
    if (words.front() == "ls") {
        for (const agent::EndpointInfo& e : agent::list(dir)) out << std::format("{}\t{}\t{}\n", e.name, e.app.empty() ? "-" : e.app, e.interface_name);
        return 0;
    }
    const std::string op = words.front();
    if (words.size() > 1 && (words[1] == "--help" || words[1] == "-h"))  // described by the catalog: no application needed
        return operate_running(std::move(words), [](const Value&, std::chrono::milliseconds) { return std::vector<Value>{}; }, out, err);
    if (words.size() < 2 || words[1].starts_with("--")) {
        err << std::format("omui agent {}: which application? (omui agent ls lists them)\n", op);
        return 2;
    }
    const std::string app_name = words[1];
    words.erase(words.begin() + 1);
    auto client = agent::Client::connect(app_name, dir);
    if (!client) {
        err << client.error().text() << "\n(omui agent ls lists the applications that listen)\n";
        return 1;
    }
    if (op == "sweep") return sweep([&](const Value& request) { return client->call(request, std::chrono::seconds(30)); }, sweeping(words), out, err);
    if (op == "call") {
        auto request = raw_request(std::span(words).subspan(1));
        if (!request) {
            err << "omui agent call: " << request.error() << '\n';
            return 2;
        }
        return print_records(client->call(*request, std::chrono::seconds(30)), out);
    }
    return operate_running(std::move(words), [&](const Value& request, std::chrono::milliseconds limit) { return client->call(request, limit); }, out, err);
}

}  // namespace

int run(std::span<const std::string> argv, std::ostream& out, std::ostream& err) {
    // A running application's operations (omui agent, omui page): the protocol's catalogs through the tool kernel;
    // `--dir`, before the command or among its words, where the endpoints or the bridge say where they are.
    {
        std::vector<std::string> words(argv.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(argv.size(), 1)), argv.end());
        std::optional<std::string> dir;
        const auto take_dir = [&](std::size_t from) {
            const auto at = std::ranges::find(words.begin() + static_cast<std::ptrdiff_t>(from), words.end(), std::string("--dir"));
            if (at == words.end() || at + 1 == words.end()) return;
            dir = *(at + 1);
            words.erase(at, at + 2);
        };
        const bool leading = words.size() > 2 && words.front() == "--dir" && (words[2] == "agent" || words[2] == "page");
        if (leading) take_dir(0);
        if (!words.empty() && (words.front() == "agent" || words.front() == "page")) {
            take_dir(1);
            const bool paired_page = words.front() == "page";
            words.erase(words.begin());
            if (paired_page) return page(std::move(words), dir ? std::optional(mcpplibs::os::fs::from_utf8(*dir)) : std::nullopt, out, err);
            return agent(std::move(words), dir, out, err);
        }
    }
    // The catalog's operations (omui build, omui design sync, omui.check …): the subject `omui` through the tool
    // kernel's command line, the product's own names without it; a name of two words (design sync) is one.
    if (argv.size() > 1) {
        Project project;
        mlibs::agent::SubjectSession session({&project});
        std::vector<std::string> words(argv.begin() + 1, argv.end());
        if (words.size() > 1 && !words[1].starts_with("-") && std::ranges::any_of(ops::catalog, [&](const auto& entry) {
                return entry.operation.name == std::format("omui.{}.{}", words[0], words[1]);
            })) {
            words[0] += "." + words[1];
            words.erase(words.begin() + 1);
        }
        const std::string first = words.front().starts_with("omui.") ? words.front() : "omui." + words.front();
        // The session's own: who is here, what answers an intent, what was asked.
        if (words.front() == "subjects" || words.front() == "find" || words.front() == "log") {
            const std::vector<std::string_view> viewed(words.begin(), words.end());
            return mlibs::agent::command_line(session, viewed, out, err, {.product = "omui"});
        }
        if (std::ranges::any_of(ops::catalog, [&](const auto& entry) { return entry.operation.name == first; })) {
            words.front() = first;  // whole: `omui describe` is the project's API (omui.describe), not the session's describe
            const std::vector<std::string_view> viewed(words.begin(), words.end());
            return mlibs::agent::command_line(session, viewed, out, err, {.product = "omui"});
        }
    }
    auto a = app();
    cmd::Argv args(argv.begin(), argv.end());
    auto parsed = a.parse_from(args);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;  // --help or --version: printed
        err << "omui: " << parsed.error().message << '\n';
        return 2;
    }
    if (!parsed->has_subcommand() && parsed->positional_count() > 0) {
        err << "omui: no command `" << parsed->positional(0) << "` (omui --help lists them)\n";
        return 2;
    }
    if (!parsed->has_subcommand()) {
        out << "omui — the openmui command line\n\n"
               "A project\n"
               "  omui new NAME [--template app-ts]   a project from a template (omui templates lists them)\n"
               "  omui dev [--headless]   the application in development mode: its own designer (person and agent, live)\n"
               "  omui run | build | test [--release]   run, build, test (mtest) — over mcpp (after --, passed on)\n"
               "  omui replay FILE     a recording played in a fresh instance, its end checked byte for byte\n"
               "  omui package [--format F]   the application packaged to give away (mcpp pack)\n"
               "  omui add | remove PKG; omui deps; omui tools [install]; omui config; omui doctor\n"
               "  omui design sync | film   Live Design's history carried into git, or made a film\n"
               "Its documents\n"
               "  omui check [FILE…]; omui describe [--llms FILE]; omui fmt FILE… [--check]; omui schema\n"
               "  omui video render | still | check | probe | timing; omui audio automation FILE …\n"
               "Running applications\n"
               "  omui agent ls | describe | snapshot | changes | find | act | wait | capture | call\n"
               "  omui agent inspect | stats | profile | logs | debug | pipeline | clock   (development)\n"
               "  omui mcp             an MCP server: agents operate running applications and the project's commands\n"
               "  omui connect --code CODE; omui page status | describe | snapshot | find | act | wait | …\n\n"
               "Except dev, run, agent, page, connect and mcp, each is an operation of the subject `omui`: its words are\n"
               "its parameters in order, its answer records a line each (omui subjects, omui find WORDS).\n"
               "Run `omui <command> --help` for its options.\n";
        return 0;
    }
    if (std::ranges::contains(project_commands, parsed->subcommand_name()))
        return project(parsed->subcommand_name(), parsed->subcommand()->get(), out, err);
    if (parsed->subcommand_name() == "connect") return connect(parsed->subcommand()->get(), global(*parsed, "dir"), out, err);
    if (parsed->subcommand_name() == "mcp") {
        // The protocol is this process's standard streams, whatever `out` is.
        const cmd::ParsedArgs& m = parsed->subcommand()->get();
        mcp::ServerOptions o;
        if (auto v = m.value("app")) o.app = *v;
        if (auto v = m.value("actor")) o.actor = *v;
        if (auto v = m.value("way")) {
            const auto way = mcp::way_named(*v);
            if (!way) {
                err << "omui mcp: --way is semantic, a11y-tree or screenshot\n";
                return 2;
            }
            o.way = *way;
        }
        if (auto d = global(*parsed, "dir")) o.directory = mcpplibs::os::fs::from_utf8(*d);
        for (const std::string& file : m.option_or_empty("ops").values) {
            auto listing = mcp::read_listing(mcpplibs::os::fs::from_utf8(file));
            if (!listing) {
                err << "omui mcp: " << listing.error() << "\n";
                return 2;
            }
            o.listings.push_back(std::move(*listing));
        }
        // The command line's own commands, for the agent to run here (W15: what writes something to keep asks first).
        o.commands = parse_json(ops::listing).value_or(Value{});
        o.run_command = run_for_agent;
        mcp::Server server(std::move(o));
        mcp::StdioTransport stdio;
        server.serve(stdio);
        return 0;
    }
    err << "omui: no command `" << parsed->subcommand_name() << "`\n";
    return 2;
}

}  // namespace omui::cli
