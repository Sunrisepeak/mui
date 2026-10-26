module openmui.cli;

import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import openmui.core;
import openmui.agent;

namespace omui::cli {

namespace cmd = mcpplibs::cmdline;

namespace {

// Builders commit into the App they were called on when the statement ends,
// so every App here is a named object.
cmd::App with_app(std::string_view name, std::string_view what) {
    cmd::App a{name};
    (void)a.description(what);
    a.arg("app").required().help("the application's endpoint name (omui agent ls)");
    return a;
}

cmd::App agent_command() {
    cmd::App a{"agent"};
    (void)a.description("Operate a running openmui application through its semantic tree (openmui.tree@0.1)");
    cmd::App ls{"ls"};
    (void)ls.description("List the applications that have an endpoint");
    (void)a.subcommand(std::move(ls));
    (void)a.subcommand(with_app("describe", "The interface, spec version and operations an application offers"));

    cmd::App snapshot = with_app("snapshot", "The semantic tree (compact)");
    snapshot.option("root").takes_value().value_name("ID").help("a subtree");
    snapshot.option("depth").takes_value().value_name("N").help("levels below the root");
    snapshot.option("interactive").help("only nodes an agent can operate");
    snapshot.option("offset").takes_value().value_name("N").help("skip this many nodes (paging)");
    snapshot.option("limit").takes_value().value_name("N").help("at most this many nodes");
    (void)a.subcommand(std::move(snapshot));

    (void)a.subcommand(with_app("changes", "What changed since this connection began (everything, for one command)"));

    cmd::App find = with_app("find", "Nodes matching a query");
    find.arg("query").help(R"(a JSON query, e.g. '{"role":"button","name":"Save*"}')");
    find.option("role").takes_value().value_name("ROLE").help("button, text_box, …");
    find.option("name").takes_value().value_name("GLOB").help("a name glob: Save*");
    find.option("state").takes_value().value_name("STATE").help("a state the nodes have");
    find.option("limit").takes_value().value_name("N").help("at most this many");
    (void)a.subcommand(std::move(find));

    cmd::App act = with_app("act", "Perform an action of a node");
    act.arg("node").required().help("the node's id (a path)");
    act.arg("action").required().help("activate, toggle, set_value, …");
    act.option("args").takes_value().value_name("JSON").help("the action's arguments");
    act.option("answer").takes_value().value_name("TEXT").help("the answer an irreversible or external action asks for");
    act.option("actor").takes_value().value_name("ACTOR").help("who acts: agent:name;via=person:name");
    (void)a.subcommand(std::move(act));

    cmd::App wait = with_app("wait", "Wait until a condition holds");
    wait.option("q").takes_value().value_name("JSON").help("a query that must match");
    wait.option("absent").help("the query must not match");
    wait.option("settled").help("nothing animates or is being removed");
    wait.option("timeout").takes_value().value_name("SECONDS").help("give up after this long (default 5)");
    (void)a.subcommand(std::move(wait));

    cmd::App capture = with_app("capture", "Pixels of the window or of a node, as PNG");
    capture.option("node").takes_value().value_name("ID").help("a node (default: the window)");
    capture.option("out").takes_value().value_name("FILE").help("write the PNG here");
    (void)a.subcommand(std::move(capture));

    cmd::App inspect = with_app("inspect", "Everything the runtime knows of a node: type, source line, props, layout, state");
    inspect.arg("node").help("the node's id (default: the root)");
    (void)a.subcommand(std::move(inspect));

    (void)a.subcommand(with_app("stats", "Frame times (mean, p50, p95, p99, max), their phases, and counts"));

    cmd::App profile = with_app("profile", "Record where the time goes: start, then stop");
    profile.arg("action").required().help("start or stop");
    profile.option("out").takes_value().value_name("FILE").help("stop: write the Chrome trace (open in Perfetto)");
    (void)a.subcommand(std::move(profile));

    cmd::App logs = with_app("logs", "The application's recent log records");
    logs.option("level").takes_value().value_name("LEVEL").help("at least: trace, debug, info, warning, error");
    logs.option("category").takes_value().value_name("NAME").help("layout, text, frame, agent …");
    logs.option("limit").takes_value().value_name("N").help("the last N (default 100)");
    (void)a.subcommand(std::move(logs));

    cmd::App debug = with_app("debug", "Turn development overlays on or off in the running application");
    for (std::string_view o : {"bounds", "repaints", "inspector", "hud"}) debug.option(o).help("turn it on");
    debug.option("off").help("turn every overlay off");
    debug.option("slow").takes_value().value_name("MS").help("log frames slower than this");
    debug.option("log").takes_value().value_name("FILTER").help("log levels per category: layout=debug,*=info");
    (void)a.subcommand(std::move(debug));

    cmd::App call = with_app("call", "Send a raw request");
    call.arg("request").required().help(R"(e.g. '{"op":"snapshot","depth":1}')");
    (void)a.subcommand(std::move(call));
    return a;
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

std::expected<std::vector<std::uint8_t>, std::string> base64_decode(std::string_view text) {
    auto digit = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<std::uint8_t> out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int d = digit(c);
        if (d < 0) return std::unexpected(std::format("not base64: `{}`", c));
        acc = (acc << 6) | static_cast<std::uint32_t>(d);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xff));
        }
    }
    return out;
}

std::expected<std::string, std::string> agent_request(const cmd::ParsedArgs& p) {
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
        if (a.positionals.size() > 1) {
            auto parsed = parse_json(a.positional(1));
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
        req.set("node", a.positional(1)).set("action", a.positional(2));
        auto args = json_option(a, "args");
        if (!args) return std::unexpected(args.error());
        if (!args->is_null()) req.set("args", *args);
        if (auto v = a.value("answer")) req.set("answer", *v);
        req.set("actor", a.value("actor").value_or("agent:omui-cli"));
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
            const std::string_view s = *a.value("timeout");
            if (std::from_chars(s.data(), s.data() + s.size(), t).ec != std::errc{}) return std::unexpected("--timeout is in seconds");
            req.set("timeout", t);
        }
        if (q->is_null() && !a.is_flag_set("settled")) return std::unexpected("wait for what? give --q JSON and/or --settled");
        return req.json();
    }
    if (op == "inspect") {
        if (a.positionals.size() > 1) req.set("node", a.positional(1));
        return req.json();
    }
    if (op == "stats") return req.json();
    if (op == "profile") {
        const std::string action = a.positional(1);
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
            const std::string_view v = *a.value("slow");
            if (std::from_chars(v.data(), v.data() + v.size(), ms).ec != std::errc{}) return std::unexpected("--slow is milliseconds");
            req.set("slow_frame_ms", ms);
        }
        if (auto v = a.value("log")) req.set("log", *v);
        return req.json();
    }
    if (op == "capture") {
        if (auto v = a.value("node")) req.set("node", *v);
        return req.json();
    }
    if (op == "call") {
        auto parsed = parse_json(a.positional(1));
        if (!parsed) return std::unexpected(std::format("the request: {}", parsed.error().text()));
        if (!parsed->map()) return std::unexpected("a request is a JSON object");
        return parsed->json();
    }
    return std::unexpected(std::format("no agent operation `{}`", op));
}

cmd::App app() {
    cmd::App a{"omui"};
    (void)a.version("0.1.0").description("The openmui command line");
    a.option("dir").takes_value().value_name("DIR").global().help("agent: the endpoint directory (default: the per-user one)");
    (void)a.subcommand(agent_command());
    return a;
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
        err << "usage: omui agent <ls|describe|snapshot|changes|find|act|wait|capture|inspect|stats|profile|logs|debug|call> …\n";
        return 2;
    }
    const cmd::ParsedArgs& sub = p.subcommand()->get();
    const std::filesystem::path dir = dir_option ? std::filesystem::path(*dir_option) : agent::default_directory();
    if (p.subcommand_name() == "ls") {
        for (const agent::EndpointInfo& e : agent::list(dir))
            out << std::format("{}\t{}\t{}\n", e.name, e.app.empty() ? "-" : e.app, e.interface_name);
        return 0;
    }
    auto request = agent_request(p);
    if (!request) {
        err << "omui agent " << p.subcommand_name() << ": " << request.error() << '\n';
        return 1;
    }
    const std::string app_name(sub.positional(0));
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
        if (out_file && kind == "data") {
            const Value* b = r.get("data") ? r.get("data")->get("base64") : nullptr;
            auto bytes = base64_decode(b ? b->as_string() : "");
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
        out << "omui — the openmui command line\n\n  omui agent ls | describe | snapshot | changes | find | act | wait | capture | call\n"
               "  omui agent inspect | stats | profile | logs | debug   (development: what the application is doing)\n\n"
               "Run `omui agent --help` for the agent operations.\n";
        return 0;
    }
    if (parsed->subcommand_name() == "agent") return agent(parsed->subcommand()->get(), global(*parsed, "dir"), out, err);
    err << "omui: no command `" << parsed->subcommand_name() << "`\n";
    return 2;
}

}  // namespace omui::cli
