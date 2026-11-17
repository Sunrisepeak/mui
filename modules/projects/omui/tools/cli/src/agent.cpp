module omui.cli;

import std;
import mcpplibs.os;
import omui.core;
import omui.agent;
import omui.mcp;

namespace omui::cli {

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

namespace {

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

int serve_mcp(const mlibs::agent::Arguments& a, std::ostream& err) {
    // The protocol is this process's standard streams, whatever `out` is.
    mcp::ServerOptions o;
    if (auto v = a.value("app")) o.app = *v;
    if (auto v = a.value("actor")) o.actor = *v;
    if (auto v = a.value("way")) {
        const auto way = mcp::way_named(*v);
        if (!way) {
            err << "omui mcp: --way is semantic, a11y-tree or screenshot\n";
            return 1;
        }
        o.way = *way;
    }
    if (auto directory = a.value("dir")) o.directory = mcpplibs::os::fs::from_utf8(*directory);
    for (const std::string& file : a.values("ops")) {
        auto listing = mcp::read_listing(mcpplibs::os::fs::from_utf8(file));
        if (!listing) {
            err << "omui mcp: " << listing.error() << "\n";
            return 1;
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

int run(std::span<const std::string> argv, std::ostream& out, std::ostream& err) {
    std::vector<std::string> words(argv.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(argv.size(), 1)), argv.end());
    // `--dir DIR` before the command is the command's own (where endpoints and the bridge say where they are).
    if (words.size() > 2 && words.front() == "--dir") std::rotate(words.begin(), words.begin() + 2, words.begin() + 3);
    if (words.empty() || words.front() == "--help" || words.front() == "-h" || words.front() == "help") {
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
               "  films: the `video` command; a view's film and a timeline's audition through xtools\n"
               "Running applications\n"
               "  omui agent <operation> <app> …   the protocol's operations and verbs (omui agent --help lists them)\n"
               "  omui mcp             an MCP server: agents operate running applications and the project's commands\n"
               "  omui connect --code CODE; omui page <operation> …   a page in a browser, paired and operated\n\n"
               "Each is an operation of a catalog: `omui` (the project's) or the protocol's (agent, page). Its words are its\n"
               "parameters in order, or --name value; its answer is records a line each. dev, run, connect and mcp hold the\n"
               "terminal: a person's command line runs them. omui <command> --help describes one; omui find WORDS finds one.\n";
        return 0;
    }
    if (words.front() == "--version" || words.front() == "-V") {
        out << "omui 0.1.0\n";
        return 0;
    }
    // A running application's operations (omui agent, omui page): the protocol's catalogs through the tool kernel.
    if (words.front() == "agent" || words.front() == "page") {
        std::optional<std::string> dir;
        if (const auto at = std::ranges::find(words.begin() + 1, words.end(), std::string("--dir")); at != words.end() && at + 1 != words.end()) {
            dir = *(at + 1);
            words.erase(at, at + 2);
        }
        const bool paired_page = words.front() == "page";
        words.erase(words.begin());
        if (paired_page) return page(std::move(words), dir ? std::optional(mcpplibs::os::fs::from_utf8(*dir)) : std::nullopt, out, err);
        return agent(std::move(words), dir, out, err);
    }
    // The project's operations (omui build, omui design sync, omui.check …): the subject `omui` through the tool kernel's
    // command line, a person's — what holds the terminal runs here too —, the product's own names without it; a name of
    // two words (design sync) is one.
    Project project;
    project.live_out = &out;
    project.live_errors = &err;
    mlibs::agent::SubjectSession session({&project}, {.command_line = true});
    if (words.size() > 1 && !words[1].starts_with("-") &&
        std::ranges::any_of(ops::catalog, [&](const auto& entry) { return entry.operation.name == std::format("omui.{}.{}", words[0], words[1]); })) {
        words[0] += "." + words[1];
        words.erase(words.begin() + 1);
    }
    // `omui describe` is the project's API (omui.describe), not the session's describe: a catalog name is taken whole.
    if (const std::string whole = words.front().starts_with("omui.") ? words.front() : "omui." + words.front();
        std::ranges::any_of(ops::catalog, [&](const auto& entry) { return entry.operation.name == whole; }))
        words.front() = whole;
    const std::vector<std::string_view> viewed(words.begin(), words.end());
    return mlibs::agent::command_line(session, viewed, out, err, {.product = "omui"});
}

}  // namespace omui::cli
