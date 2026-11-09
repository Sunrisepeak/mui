module omui.cli;

import std;
import mlibs.base64;
import mcpplibs.cmdline;
import mcpplibs.os;
import omui.core;
import omui.agent;
import omui.mcp;

namespace omui::cli {

namespace cmd = mcpplibs::cmdline;

namespace {

// An operation's command: `omui agent <op> <app> …` names the application; `omui page <op> …` operates the paired page.
bool naming_app = true;
cmd::App with_app(std::string_view name, std::string_view what) {
    cmd::App command(name);
    (void)command.description(what);
    if (naming_app) (void)command.arg(cmd::Arg("app").required().help("the application's endpoint name (omui agent ls)"));
    return command;
}

// The operations of the semantic protocol (openmui.tree@0.1), as `omui agent` and `omui page` both offer them.
cmd::App operations(cmd::App a, bool app) {
    using cmd::Arg, cmd::Option;
    naming_app = app;
    const auto actor = Option("actor").takes_value().value_name("ACTOR").help("who acts: agent:name;via=person:name");
    if (app) (void)a.subcommand(cmd::App("ls").description("List the applications that have an endpoint"));
    (void)a.subcommand(with_app("describe", "The interface, spec version and operations an application offers"))
        .subcommand(with_app("snapshot", "The semantic tree (compact)")
                        .option(Option("root").takes_value().value_name("ID").help("a subtree"))
                        .option(Option("depth").takes_value().value_name("N").help("levels below the root"))
                        .option(Option("interactive").help("only nodes an agent can operate"))
                        .option(Option("offset").takes_value().value_name("N").help("skip this many nodes (paging)"))
                        .option(Option("limit").takes_value().value_name("N").help("at most this many nodes")))
        .subcommand(with_app("changes", "What changed since this connection began (everything, for one command)"))
        .subcommand(with_app("find", "Nodes matching a query")
                        .arg(Arg("query").help(R"(a JSON query, e.g. '{"role":"button","name":"Save*"}')"))
                        .option(Option("role").takes_value().value_name("ROLE").help("button, text_box, …"))
                        .option(Option("name").takes_value().value_name("GLOB").help("a name glob: Save*"))
                        .option(Option("state").takes_value().value_name("STATE").help("a state the nodes have"))
                        .option(Option("limit").takes_value().value_name("N").help("at most this many")))
        .subcommand(with_app("act", "Perform an action of a node")
                        .arg(Arg("node").required().help("the node's id (a path)"))
                        .arg(Arg("action").required().help("activate, toggle, set_value, …"))
                        .option(Option("args").takes_value().value_name("JSON").help("the action's arguments"))
                        .option(Option("answer").takes_value().value_name("TEXT").help("the answer an irreversible or external action asks for"))
                        .option(actor)
                        .option(Option("dry-run").help("only what would happen (its effect, whether it can be undone, the value after); nothing is done")))
        .subcommand(with_app("undo", "Take back the last step of the history (S-12)")
                        .option(Option("scope").takes_value().value_name("ID").help("a node with its own history (default: the window)"))
                        .option(actor)
                        .option(Option("dry-run").help("only what it would do")))
        .subcommand(with_app("redo", "Put forward again what was undone")
                        .option(Option("scope").takes_value().value_name("ID").help("a node with its own history (default: the window)"))
                        .option(actor)
                        .option(Option("dry-run").help("only what it would do")))
        .subcommand(with_app("history", "What can be undone and redone, oldest first")
                        .option(Option("scope").takes_value().value_name("ID").help("a node (its scope's history; default: the window)")))
        .subcommand(with_app("transaction", "Several actions as one step: all done, or all taken back (S-13)")
                        .arg(Arg("steps").required().help(
                            R"(a JSON list: '[{"node":"main/a","action":"toggle"},{"node":"main/b","action":"set_value","args":{"value":3}}]')"))
                        .option(Option("label").takes_value().value_name("TEXT").help("what the step is called in the history"))
                        .option(actor)
                        .option(Option("dry-run").help("only what each step would do")))
        .subcommand(with_app("wait", "Wait until a condition holds")
                        .option(Option("q").takes_value().value_name("JSON").help("a query that must match"))
                        .option(Option("absent").help("the query must not match"))
                        .option(Option("settled").help("nothing animates or is being removed"))
                        .option(Option("timeout").takes_value().value_name("SECONDS").help("give up after this long (default 5)")))
        .subcommand(with_app("capture", "The window or a node as drawn: PNG pixels, or SVG with every node's drawing marked")
                        .option(Option("node").takes_value().value_name("ID").help("a node (default: the window)"))
                        .option(Option("format").takes_value().value_name("png|svg").help("png (default) or svg (vectors at any size, the nodes marked)"))
                        .option(Option("out").takes_value().value_name("FILE").help("write the picture here")))
        .subcommand(with_app("inspect", "Everything the runtime knows of a node: type, source line, props, layout, state")
                        .arg(Arg("node").help("the node's id (default: the root)")))
        .subcommand(with_app("stats", "Frame times (mean, p50, p95, p99, max), their phases, and counts"))
        .subcommand(with_app("profile", "Record where the time goes: start, then stop")
                        .arg(Arg("action").required().help("start or stop"))
                        .option(Option("out").takes_value().value_name("FILE").help("stop: write the Chrome trace (open in Perfetto)")))
        .subcommand(with_app("logs", "The application's recent log records")
                        .option(Option("level").takes_value().value_name("LEVEL").help("at least: trace, debug, info, warning, error"))
                        .option(Option("category").takes_value().value_name("NAME").help("layout, text, frame, agent …"))
                        .option(Option("limit").takes_value().value_name("N").help("the last N (default 100)")))
        .subcommand(with_app("debug", "Turn development overlays on or off in the running application")
                        .option(Option("bounds").help("turn it on"))
                        .option(Option("repaints").help("turn it on"))
                        .option(Option("inspector").help("turn it on"))
                        .option(Option("hud").help("turn it on"))
                        .option(Option("off").help("turn every overlay off"))
                        .option(Option("slow").takes_value().value_name("MS").help("log frames slower than this"))
                        .option(Option("log").takes_value().value_name("FILTER").help("log levels per category: layout=debug,*=info")))
        .subcommand(with_app("clock", "The application's time: pause, step, slow down, seek (animations are pure functions of time)")
                        .arg(Arg("action").help("show (default), pause, resume, rate, step, seek, release"))
                        .arg(Arg("value").help("rate: the factor (0.25); step: seconds (0.016, or negative); seek: the time in seconds")))
        .subcommand(with_app("pipeline", "The frame's pipeline: its stages, their plugins and times; change one while it runs")
                        .arg(Arg("action").help("show (default), replace, insert or remove"))
                        .option(Option("stage").takes_value().value_name("NAME").help("the stage to change"))
                        .option(Option("plugin").takes_value().value_name("PLUGIN").help("replace / insert: the stage plugin"))
                        .option(Option("after").takes_value().value_name("STAGE").help("insert: declared right after this stage"))
                        .option(Option("config").takes_value().value_name("JSON").help(R"(the stage's configuration, e.g. '{"parallel":true}')"))
                        .option(Option("toml").help("show the description as <name>.pipeline.omui.toml")))
        .subcommand(with_app("sweep", "Every action of every node an agent can operate, really performed (and undone where it can be); a failure exits 1")
                        .option(Option("root").takes_value().value_name("ID").help("only this subtree"))
                        .option(Option("actor").takes_value().value_name("ACTOR").help("who performs them (default agent:omui-sweep)")))
        .subcommand(with_app("call", "Send a raw request").arg(Arg("request").required().help(R"(e.g. '{"op":"snapshot","depth":1}')")));
    naming_app = true;
    return a;
}

cmd::App agent_command() {
    return operations(cmd::App("agent").description("Operate a running openmui application through its semantic tree (openmui.tree@0.1)"), true);
}

std::optional<std::int64_t> integer(const cmd::ParsedArgs& p, std::string_view name) {
    auto v = p.value(name);
    if (!v) return std::nullopt;
    std::int64_t x = 0;
    auto r = std::from_chars(v->data(), v->data() + v->size(), x);
    if (r.ec != std::errc{} || r.ptr != v->data() + v->size()) return std::nullopt;
    return x;
}

std::expected<Value, std::string> json_option(const cmd::ParsedArgs& p, std::string_view name) {
    auto v = p.value(name);
    if (!v) return Value{};
    auto parsed = parse_json(*v);
    if (!parsed) return std::unexpected(std::format("--{}: {}", name, parsed.error().text()));
    return *parsed;
}

}  // namespace

std::expected<std::string, std::string> agent_request(const cmd::ParsedArgs& p, std::size_t first) {
    const std::string op(p.subcommand_name());
    const cmd::ParsedArgs& a = p.subcommand()->get();
    Value req;
    req.set("op", op);
    if (op == "describe" || op == "changes") return req.json();
    if (op == "snapshot") {
        if (auto v = a.value("root")) req.set("root", *v);
        for (std::string_view k : {"depth", "offset", "limit"}) {
            if (!a.value(k)) continue;
            auto n = integer(a, k);
            if (!n) return std::unexpected(std::format("--{} is a whole number", k));
            req.set(k, *n);
        }
        if (a.is_flag_set("interactive")) req.set("interactive", true);
        return req.json();
    }
    if (op == "find") {
        Value q = Value(Value::Map{});
        if (a.positionals.size() > first) {
            auto parsed = parse_json(a.positional(first));
            if (!parsed) return std::unexpected(std::format("the query: {}", parsed.error().text()));
            q = std::move(*parsed);
        }
        if (auto v = a.value("role")) q.set("role", *v);
        if (auto v = a.value("name")) q.set("name", *v);
        if (auto v = a.value("state")) q.set("state", Value(Value::List{Value(*v)}));
        if (a.value("limit")) {
            auto n = integer(a, "limit");
            if (!n) return std::unexpected("--limit is a whole number");
            q.set("limit", *n);
        }
        req.set("q", std::move(q));
        return req.json();
    }
    if (op == "act") {
        req.set("node", a.positional(first)).set("action", a.positional(first + 1));
        auto args = json_option(a, "args");
        if (!args) return std::unexpected(args.error());
        if (!args->is_null()) req.set("args", *args);
        if (auto v = a.value("answer")) req.set("answer", *v);
        req.set("actor", a.value("actor").value_or("agent:omui-cli"));
        if (a.is_flag_set("dry-run")) req.set("dry_run", true);
        return req.json();
    }
    if (op == "undo" || op == "redo") {
        if (auto v = a.value("scope")) req.set("scope", *v);
        req.set("actor", a.value("actor").value_or("agent:omui-cli"));
        if (a.is_flag_set("dry-run")) req.set("dry_run", true);
        return req.json();
    }
    if (op == "history") {
        if (auto v = a.value("scope")) req.set("scope", *v);
        return req.json();
    }
    if (op == "transaction") {
        auto steps = parse_json(a.positional(first));
        if (!steps) return std::unexpected(std::format("the steps: {}", steps.error().text()));
        if (!steps->list()) return std::unexpected("the steps are a JSON list of {node, action, args}");
        req.set("steps", *steps);
        if (auto v = a.value("label")) req.set("label", *v);
        req.set("actor", a.value("actor").value_or("agent:omui-cli"));
        if (a.is_flag_set("dry-run")) req.set("dry_run", true);
        return req.json();
    }
    if (op == "wait") {
        auto q = json_option(a, "q");
        if (!q) return std::unexpected(q.error());
        if (!q->is_null()) req.set("q", *q);
        if (a.is_flag_set("absent")) req.set("absent", true);
        if (a.is_flag_set("settled")) req.set("settled", true);
        if (a.value("timeout")) {
            double t = 0;
            const std::string s = *a.value("timeout");  // owned: value() returns a temporary
            if (std::from_chars(s.data(), s.data() + s.size(), t).ec != std::errc{}) return std::unexpected("--timeout is in seconds");
            req.set("timeout", t);
        }
        if (q->is_null() && !a.is_flag_set("settled")) return std::unexpected("wait for what? give --q JSON and/or --settled");
        return req.json();
    }
    if (op == "inspect") {
        if (a.positionals.size() > first) req.set("node", a.positional(first));
        return req.json();
    }
    if (op == "stats") return req.json();
    if (op == "profile") {
        const std::string action = a.positional(first);
        if (action != "start" && action != "stop") return std::unexpected("profile takes start or stop");
        req.set("action", action);
        if (action == "stop" && a.value("out")) req.set("chrome", true);
        return req.json();
    }
    if (op == "logs") {
        if (auto v = a.value("level")) req.set("level", *v);
        if (auto v = a.value("category")) req.set("category", *v);
        if (a.value("limit")) {
            auto n = integer(a, "limit");
            if (!n) return std::unexpected("--limit is a whole number");
            req.set("limit", *n);
        }
        return req.json();
    }
    if (op == "debug") {
        const bool off = a.is_flag_set("off");
        for (std::string_view o : {"bounds", "repaints", "inspector", "hud"})
            if (off || a.is_flag_set(o)) req.set(o, !off);
        if (a.value("slow")) {
            double ms = 0;
            const std::string v = *a.value("slow");  // owned: value() returns a temporary
            if (std::from_chars(v.data(), v.data() + v.size(), ms).ec != std::errc{}) return std::unexpected("--slow is milliseconds");
            req.set("slow_frame_ms", ms);
        }
        if (auto v = a.value("log")) req.set("log", *v);
        return req.json();
    }
    if (op == "clock") {
        const std::string action = a.positionals.size() > first ? std::string(a.positional(first)) : std::string("show");
        req.set("action", action);
        if (action == "rate" || action == "step" || action == "seek") {
            if (a.positionals.size() < first + 2) return std::unexpected(std::format("clock {} needs a number", action));
            const std::string_view v = a.positional(first + 1);
            double x = 0;
            if (std::from_chars(v.data(), v.data() + v.size(), x).ec != std::errc{}) return std::unexpected(std::format("`{}` is not a number", v));
            req.set(action == "rate" ? "rate" : "seconds", x);
        }
        return req.json();
    }
    if (op == "pipeline") {
        const std::string action = a.positionals.size() > first ? std::string(a.positional(first)) : std::string("show");
        if (action != "show" && action != "replace" && action != "insert" && action != "remove")
            return std::unexpected("pipeline takes show, replace, insert or remove");
        req.set("action", action);
        if (action != "show") {
            auto stage = a.value("stage");
            if (!stage) return std::unexpected(std::format("pipeline {} needs --stage", action));
            req.set("stage", *stage);
        }
        if (action == "replace" || action == "insert") {
            auto plugin = a.value("plugin");
            if (!plugin) return std::unexpected(std::format("pipeline {} needs --plugin", action));
            req.set("plugin", *plugin);
        }
        if (auto after = a.value("after")) req.set("after", Value(Value::List{Value(*after)}));
        if (auto cfg = a.value("config")) {
            auto v = parse_json(*cfg);
            if (!v || !v->map()) return std::unexpected("--config is a JSON object");
            req.set("config", std::move(*v));
        }
        return req.json();
    }
    if (op == "capture") {
        if (auto v = a.value("node")) req.set("node", *v);
        if (auto v = a.value("format")) req.set("format", *v);
        return req.json();
    }
    if (op == "call") {
        auto parsed = parse_json(a.positional(first));
        if (!parsed) return std::unexpected(std::format("the request: {}", parsed.error().text()));
        if (!parsed->map()) return std::unexpected("a request is a JSON object");
        return parsed->json();
    }
    return std::unexpected(std::format("no agent operation `{}`", op));
}

cmd::App app() {
    using cmd::Option;
    cmd::App line("omui");
    (void)line.version("0.1.0")
        .description("The openmui command line")
        .option(Option("dir").takes_value().value_name("DIR").global().help(
            "agent: the endpoint directory; connect, page: where the bridge says where it is (default: the per-user one)"));
    for (const auto name : project_commands) (void)line.subcommand(project_command(name));  // new, dev, run … in their order
    (void)line.subcommand(agent_command())
        .subcommand(check_command())
        .subcommand(describe_command())
        .subcommand(fmt_command())
        .subcommand(schema_command())
        .subcommand(video_command())
        .subcommand(audio_command())
        .subcommand(connect_command())
        .subcommand(page_command(operations(cmd::App("page"), false)))
        .subcommand(cmd::App("mcp")
                        .description("An MCP server on standard input and output: agents (Claude Code …) operate running applications with ui_* tools")
                        .option(Option("app").takes_value().value_name("NAME").help("the application a call names none of (default: the one running)"))
                        .option(Option("actor").takes_value().value_name("KIND:NAME").help("who acts (default: the client's name, agent:claude-code)"))
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

int agent(const cmd::ParsedArgs& p, std::optional<std::string> dir_option, std::ostream& out, std::ostream& err) {
    if (!p.has_subcommand()) {
        err << "usage: omui agent <ls|describe|snapshot|changes|find|act|undo|redo|history|transaction|wait|capture|inspect|stats|profile|logs|debug|pipeline|clock|call> …\n";
        return 2;
    }
    const cmd::ParsedArgs& sub = p.subcommand()->get();
    const std::filesystem::path dir = dir_option ? std::filesystem::path(*dir_option) : agent::default_directory();
    if (p.subcommand_name() == "ls") {
        for (const agent::EndpointInfo& e : agent::list(dir))
            out << std::format("{}\t{}\t{}\n", e.name, e.app.empty() ? "-" : e.app, e.interface_name);
        return 0;
    }
    const std::string app_name(sub.positional(0));
    if (p.subcommand_name() == "sweep") {
        auto client = agent::Client::connect(app_name, dir);
        if (!client) {
            err << client.error() << "\n(omui agent ls lists the applications that listen)\n";
            return 1;
        }
        return sweep([&](const Value& req) { return client->call(req, std::chrono::seconds(30)); }, sub, out, err);
    }
    auto request = agent_request(p);
    if (!request) {
        err << "omui agent " << p.subcommand_name() << ": " << request.error() << '\n';
        return 1;
    }
    auto client = agent::Client::connect(app_name, dir);
    if (!client) {
        err << client.error() << "\n(omui agent ls lists the applications that listen)\n";
        return 1;
    }
    std::chrono::milliseconds limit = std::chrono::seconds(30);
    auto parsed = parse_json(*request);
    if (const Value* t = parsed->get("timeout")) limit = std::chrono::milliseconds(static_cast<std::int64_t>(t->as_double() * 1000) + 5000);
    std::vector<Value> records = client->call(*parsed, limit);
    int code = 1;
    const auto out_file = p.subcommand_name() == "capture" ? sub.value("out") : std::nullopt;
    const auto trace_file = p.subcommand_name() == "profile" ? sub.value("out") : std::nullopt;
    for (Value& r : records) {
        if (const Value* re = r.get("re"); re && r.map()) std::erase_if(*r.map(), [](const auto& kv) { return kv.first == "re"; });
        const std::string_view kind = r.get("kind") ? r.get("kind")->as_string() : "";
        if (kind == "result") code = static_cast<int>(r.get("exitCode") ? r.get("exitCode")->as_int(1) : 1);
        if (trace_file && kind == "data" && r.get("data") && r.get("data")->get("chrome")) {
            const std::string_view json = r.get("data")->get("chrome")->as_string();
            if (auto w = mcpplibs::os::fs::write_atomic(*trace_file, json); !w) {
                err << w.error().message() << '\n';
                return 1;
            }
            r.map()->erase(std::ranges::find(*r.map(), std::string("data"), &std::pair<std::string, Value>::first));
            r.set("data", Value{}.set("file", *trace_file));
        }
        if (out_file && kind == "data" && r.get("data") && r.get("data")->get("svg")) {
            if (auto w = mcpplibs::os::fs::write_atomic(*out_file, r.get("data")->get("svg")->as_string()); !w) {
                err << w.error().message() << '\n';
                return 1;
            }
            out << std::format("{{\"kind\":\"data\",\"data\":{{\"file\":{},\"islands\":{}}}}}\n", Value(*out_file).json(),
                               r.get("data")->get("islands") ? r.get("data")->get("islands")->json() : std::string("[]"));
            continue;
        }
        if (out_file && kind == "data") {
            const Value* b = r.get("data") ? r.get("data")->get("base64") : nullptr;
            auto bytes = mlibs::base64::decode(b ? b->as_string() : "");
            if (!bytes) {
                err << bytes.error() << '\n';
                return 1;
            }
            auto w = mcpplibs::os::fs::write_atomic(*out_file, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
            if (!w) {
                err << w.error().message() << '\n';
                return 1;
            }
            out << std::format("{{\"kind\":\"data\",\"data\":{{\"file\":{}}}}}\n", Value(*out_file).json());
            continue;
        }
        if (p.subcommand_name() == "pipeline" && sub.is_flag_set("toml") && kind == "data" && r.get("data") && r.get("data")->get("toml")) {
            out << r.get("data")->get("toml")->as_string();
            continue;
        }
        out << r.json() << '\n';
    }
    return code;
}

}  // namespace

int run(std::span<const std::string> argv, std::ostream& out, std::ostream& err) {
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
               "  omui new NAME [--template app-ts]   a project from a template (--list)\n"
               "  omui dev [--headless]   the application in development mode: its own designer (person and agent, live)\n"
               "  omui run | build | test [--release]   run, build, test (mtest) — over mcpp\n"
               "  omui replay FILE     a recording (record) played in a fresh instance, its end checked byte for byte\n"
               "  omui design sync     Live Design's history (.omui/design) carried into git as design/<branch>\n"
               "  omui design film     the design's history as a film (WebM): each step as it was presented, captioned\n"
               "  omui package [--format F]   the application packaged to give away (mcpp pack)\n"
               "  omui add | remove PKG    a dependency; omui deps lists them; omui tools: the xlings tools it names (--install)\n"
               "  omui config          omui.config.ts as the player reads it: agents' level, what the recorder keeps\n"
               "  omui doctor          which tools are there, which are missing\n\n"
               "  omui agent ls | describe | snapshot | changes | find | act | wait | capture | call\n"
               "  omui agent inspect | stats | profile | logs | debug | pipeline | clock   (development: what the application is doing)\n"
               "  omui check [FILE…]  the project here: TypeScript (erasable syntax, tsc types), view documents (--portable); or the documents given (--json)\n"
               "  omui describe --api  what writing for the project takes: builders, natives, tokens, verbs (--json, --llms FILE)\n"
               "  omui fmt FILE…       the canonical format (--check)\n"
               "  omui schema          the JSON Schema of view documents\n"
               "  omui video render | still | check | probe | timing   videos: frames and sound on exact counts\n"
               "  omui audio automation FILE --track NAME --clip NAME --out PREFIX   audition and inspect a gain curve\n"
               "  omui mcp             an MCP server: agents operate running applications (claude mcp add openmui -- omui mcp)\n"
               "  omui connect --code CODE   the bridge a page in your browser pairs with (its prompt gives the code)\n"
               "  omui page status | describe | snapshot | find | act | wait | …   operate the paired page\n\n"
               "Run `omui <command> --help` for its options.\n";
        return 0;
    }
    if (std::ranges::contains(project_commands, parsed->subcommand_name()))
        return project(parsed->subcommand_name(), parsed->subcommand()->get(), out, err);
    if (parsed->subcommand_name() == "agent") return agent(parsed->subcommand()->get(), global(*parsed, "dir"), out, err);
    if (parsed->subcommand_name() == "check") return check(parsed->subcommand()->get(), out, err);
    if (parsed->subcommand_name() == "describe") return describe(parsed->subcommand()->get(), out, err);
    if (parsed->subcommand_name() == "fmt") return fmt(parsed->subcommand()->get(), out, err);
    if (parsed->subcommand_name() == "schema") return schema(parsed->subcommand()->get(), out, err);
    if (parsed->subcommand_name() == "video") return video(parsed->subcommand()->get(), out, err);
    if (parsed->subcommand_name() == "audio") return audio(parsed->subcommand()->get(), out, err);
    if (parsed->subcommand_name() == "connect") return connect(parsed->subcommand()->get(), global(*parsed, "dir"), out, err);
    if (parsed->subcommand_name() == "page") return page(parsed->subcommand()->get(), global(*parsed, "dir"), out, err);
    if (parsed->subcommand_name() == "mcp") {
        // The protocol is this process's standard streams, whatever `out` is.
        const cmd::ParsedArgs& m = parsed->subcommand()->get();
        mcp::ServerOptions o;
        if (auto v = m.value("app")) o.app = *v;
        if (auto v = m.value("actor")) o.actor = *v;
        if (auto d = global(*parsed, "dir")) o.directory = mcpplibs::os::fs::from_utf8(*d);
        for (const std::string& file : m.option_or_empty("ops").values) {
            auto listing = mcp::read_listing(mcpplibs::os::fs::from_utf8(file));
            if (!listing) {
                err << "omui mcp: " << listing.error() << "\n";
                return 2;
            }
            o.listings.push_back(std::move(*listing));
        }
        mcp::Server server(std::move(o));
        mcp::StdioTransport stdio;
        server.serve(stdio);
        return 0;
    }
    err << "omui: no command `" << parsed->subcommand_name() << "`\n";
    return 2;
}

}  // namespace omui::cli
