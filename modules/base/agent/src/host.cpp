module mlibs.agent;

import std;
import mlibs.json;

namespace mlibs::agent {

namespace {

namespace json = mlibs::json;

// A value as JSON: as it is when it reads as JSON, else as text.
std::string value_of(std::string_view value) {
    if (const auto parsed = json::Document::parse(value, ""); parsed && !value.empty()) return std::string(value);
    return json_text(value);
}

std::optional<json::Document> parsed(std::string_view text) {
    auto document = json::Document::parse(text, "");
    if (!document) return std::nullopt;
    return std::move(*document);
}

// A request object with `op` first and the object `arguments`' members after it.
std::string request_of(std::string_view op, const json::Node& arguments) {
    std::string out = std::format("{{\"op\":{}", json_text(op));
    if (arguments.is_object())
        for (const auto& [key, value] : arguments.members())
            if (key != "op") out += std::format(",{}:{}", json_text(key), value.dump());
    return out + "}";
}

// The session's own requests as tools, and each subject's operations: its name, a line, its schema.
struct Tool {
    std::string operation, description, schema;
    bool        reads = false;
};

constexpr std::string_view session_tools[][3] = {
    {"subjects", "Who is here: each subject, what it can do, and how to begin", R"({"type":"object","properties":{}})"},
    {"describe", "What a subject is, the tasks to begin with and its operations briefly; or one operation whole (its parameters, cost, undo, examples)",
     R"({"type":"object","properties":{"subject":{"type":"string"},"operation":{"type":"string","description":"an operation's whole name"},"detail":{"type":"string","enum":["full"]}}})"},
    {"find", "What answers an intent, in words, across the subjects: operations, tasks and content, each with requests to send",
     R"({"type":"object","properties":{"q":{"type":"string"},"limit":{"type":"integer"}},"required":["q"]})"},
    {"log", "What was asked here, as facts: every change, and every request that failed and why",
     R"({"type":"object","properties":{"since":{"type":"integer"},"subject":{"type":"string"},"failed":{"type":"boolean"},"limit":{"type":"integer"}}})"},
};

// The first data record's `data` of an answer.
std::optional<json::Document> data_of(const Answered& answered) {
    for (const std::string& record : answered.records)
        if (auto document = parsed(record); document && document->root()["kind"].text() == "data") return parsed(document->root()["data"].dump());
    return std::nullopt;
}

std::vector<Tool> tools_of(Session& session) {
    std::vector<Tool> tools;
    for (const auto& own : session_tools) tools.push_back({std::string(own[0]), std::string(own[1]), std::string(own[2]), true});
    const auto here = data_of(answer_to(session, R"({"op":"subjects"})"));
    if (!here) return tools;
    for (const json::Node& subject : here->root()["subjects"].items()) {
        const auto name = subject["name"].text();
        if (!name) continue;
        const auto brief = data_of(answer_to(session, std::format(R"({{"op":"describe","subject":{}}})", json_text(*name))));
        const auto full = data_of(answer_to(session, std::format(R"({{"op":"describe","subject":{},"detail":"full"}})", json_text(*name))));
        if (!brief) continue;
        for (const json::Node& operation : brief->root()["operations"].items()) {
            Tool tool{operation["name"].text().value_or(""), operation["summary"].text().value_or(""), R"({"type":"object"})", operation["effect"].text() == "read"};
            if (tool.operation.empty() || operation["terminal"].boolean().value_or(false)) continue;  // a person's command line's
            if (full)
                for (const json::Node& listed : full->root()["tools"].items())
                    if (listed["name"].text() == tool.operation && listed["inputSchema"].is_object()) tool.schema = listed["inputSchema"].dump();
            tools.push_back(std::move(tool));
        }
    }
    return tools;
}

std::string rpc_result(const json::Node& id, std::string_view result) { return std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id.dump(), result); }
std::string rpc_error(const json::Node& id, int code, std::string_view message) {
    return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":{},"message":{}}}}})", id ? id.dump() : "null", code, json_text(message));
}

}  // namespace

std::expected<std::string, std::string> request_from(std::span<const std::string_view> arguments) {
    if (arguments.empty()) return std::unexpected("say what to ask: an operation's name and its --arguments, or `subjects` to see who is here");
    const std::string_view op = arguments.front();
    if (op == "--help" || op == "-h") return std::string(R"({"op":"help"})");
    if (op == "--request") {
        if (arguments.size() != 2) return std::unexpected("--request takes one request, whole: '{\"op\":\"subjects\"}'");
        const auto document = parsed(arguments[1]);
        if (!document || !document->root().is_object() || !document->root()["op"].is_string()) return std::unexpected("--request takes a JSON object with its `op`");
        return std::string(arguments[1]);
    }
    if (op.starts_with("-")) return std::unexpected(std::format("`{}` comes after what is asked: an operation's name first", op));
    std::string out = std::format("{{\"op\":{}", json_text(op));
    std::size_t at = 1;
    // The session's own, with what they take unnamed: `describe world.act`, `find set something on fire`.
    if (op == "describe" && at < arguments.size() && !arguments[at].starts_with("--")) {
        out += std::format(",{}:{}", arguments[at].contains('.') ? "\"operation\"" : "\"subject\"", json_text(arguments[at]));
        ++at;
    } else if (op == "find" && at < arguments.size() && !arguments[at].starts_with("--")) {
        std::string words;
        for (; at < arguments.size() && !arguments[at].starts_with("--"); ++at) words += (words.empty() ? "" : " ") + std::string(arguments[at]);
        out += std::format(",\"q\":{}", json_text(words));
    }
    // Each name once, in the order first given; a name given again gathers its values into an array.
    std::vector<std::pair<std::string, std::vector<std::string>>> named;
    for (; at < arguments.size(); ++at) {
        const std::string_view argument = arguments[at];
        // After a bare `--`, the words are passed on as they are: `rest`, a list (a program's own switches, which cases).
        if (argument == "--") {
            std::string rest = "[";
            for (++at; at < arguments.size(); ++at) rest += (rest.size() > 1 ? "," : "") + json_text(arguments[at]);
            named.push_back({"rest", {rest + "]"}});
            break;
        }
        if (!argument.starts_with("--")) return std::unexpected(std::format("`{}`: each argument is --name value", argument));
        std::string_view name = argument.substr(2);
        std::string value;
        if (const auto equals = name.find('='); equals != std::string_view::npos) {
            value = value_of(name.substr(equals + 1));
            name = name.substr(0, equals);
        } else if (at + 1 < arguments.size() && !arguments[at + 1].starts_with("--")) {
            value = value_of(arguments[++at]);
        } else {
            value = "true";
        }
        const auto found = std::ranges::find(named, name, &std::pair<std::string, std::vector<std::string>>::first);
        if (found != named.end()) found->second.push_back(std::move(value));
        else named.push_back({std::string(name), {std::move(value)}});
    }
    for (const auto& [name, values] : named) {
        std::string value = values.front();
        if (values.size() > 1) {
            value = "[";
            for (std::size_t index = 0; index < values.size(); ++index) value += (index ? "," : "") + values[index];
            value += "]";
        }
        out += std::format(",{}:{}", json_text(name), value);
    }
    return out + "}";
}

Answered answer_to(Session& session, std::string_view request, std::chrono::milliseconds wait) {
    auto answered = std::make_shared<Answered>();
    session.request(request, [answered](std::string_view line) {
        answered->records.emplace_back(line);
        if (const auto document = parsed(line); document && document->root()["kind"].text() == "result") {
            answered->exit_code = static_cast<int>(document->root()["exitCode"].integer().value_or(1));
            answered->finished = true;
        }
    });
    const auto until = std::chrono::steady_clock::now() + wait;
    while (!answered->finished && std::chrono::steady_clock::now() < until) {
        session.poll();
        if (!answered->finished) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::move(*answered);
}

// An operation's words as its catalog has them, the session describing it: a word without a name is its next
// parameter not named otherwise, in the catalog's order, the ones it needs first (`document.check rise.json` is
// --file rise.json; more words than parameters go to the last: `effect.check a.json b.json`); `--name` is a switch
// when the parameter is one (bool) or the operation has no such parameter, and takes the next word as its value
// only when it is a parameter of another type (`--json bad`: a switch, then a word). An operation the session does
// not know keeps only its name, so that its answer is the session's: not found, and the nearest names.
std::vector<std::string> named_in_order(Session& session, std::span<const std::string_view> arguments) {
    std::vector<std::string> out(arguments.begin(), arguments.end());
    const std::string_view op = arguments.front();
    constexpr std::array own{"subjects", "help", "describe", "find", "log", "--request", "--help", "-h"};
    if (arguments.size() == 1 || std::ranges::contains(own, op)) return out;
    const auto described = data_of(answer_to(session, std::format(R"({{"op":"describe","operation":{}}})", json_text(op))));
    if (!described || !described->root()["parameters"].is_array()) return {std::string(op)};
    std::vector<std::pair<std::string, bool>> parameters;  // name, a switch
    for (const bool optional : {false, true})
        for (const json::Node& parameter : described->root()["parameters"].items())
            if (parameter["optional"].boolean().value_or(false) == optional)
                parameters.emplace_back(parameter["name"].text().value_or(""), parameter["type"].text() == "bool");
    const auto parameter = [&](std::string_view name) { return std::ranges::find(parameters, name, &std::pair<std::string, bool>::first); };
    std::vector<std::string> named{std::string(op)}, words, given, rest;
    for (std::size_t at = 1; at < arguments.size(); ++at) {
        const std::string_view argument = arguments[at];
        if (argument == "--") {  // the rest as it is, last
            rest.assign(arguments.begin() + static_cast<std::ptrdiff_t>(at), arguments.end());
            break;
        }
        if (!argument.starts_with("--")) {
            words.emplace_back(argument);
            continue;
        }
        const std::size_t equals = argument.find('=');
        std::string name(argument.substr(2, equals == std::string_view::npos ? std::string_view::npos : equals - 2));
        // As a person types it: `--dry-run` is the catalog's `dry_run`.
        if (parameter(name) == parameters.end()) {
            std::string underscored = name;
            std::ranges::replace(underscored, '-', '_');
            if (parameter(underscored) != parameters.end()) name = std::move(underscored);
        }
        given.push_back(name);
        named.push_back("--" + name + (equals == std::string_view::npos ? std::string() : std::string(argument.substr(equals))));
        if (equals != std::string_view::npos) continue;
        const auto found = parameter(name);
        // Who asks and a person's answer are the request's own (openas's envelope), every operation's: they take a value.
        // A switch is true alone, or the true or false written after it (`--bounds false`).
        const bool truth_follows = at + 1 < arguments.size() && (arguments[at + 1] == "true" || arguments[at + 1] == "false");
        const bool takes_value = found != parameters.end() ? !found->second || truth_follows : name == "actor" || name == "answer";
        if (takes_value && at + 1 < arguments.size() && !arguments[at + 1].starts_with("--")) named.emplace_back(arguments[++at]);
        else named.back() += "=true";
    }
    std::vector<std::string> open;  // the parameters the words fill, in order
    for (const auto& [name, is_switch] : parameters)
        if (!is_switch && !std::ranges::contains(given, name)) open.push_back(name);
    if (!words.empty() && open.empty()) open.push_back(parameters.empty() ? std::string("rest") : parameters.back().first);
    for (std::size_t index = 0; index < words.size(); ++index) named.insert(named.end(), {"--" + open[std::min(index, open.size() - 1)], words[index]});
    named.insert(named.end(), rest.begin(), rest.end());
    return named;
}

int command_line(Session& session, std::span<const std::string_view> arguments, std::ostream& out, std::ostream& errors, const CommandLine& how) {
    std::vector<std::string_view> given(arguments.begin(), arguments.end());
    // A name without a dot is the product's own, when the program has one.
    constexpr std::array own{"subjects", "help", "describe", "find", "log"};
    std::string prefixed;
    if (!how.product.empty() && !given.empty() && !given.front().starts_with("-") && !given.front().contains('.') && !std::ranges::contains(own, given.front())) {
        prefixed = how.product + "." + std::string(given.front());
        given.front() = prefixed;
    }
    // An operation's --help: the operation described whole (its parameters, cost, undo, examples).
    if (given.size() > 1 && !given.front().starts_with("-") && std::ranges::any_of(given | std::views::drop(1), [](std::string_view word) { return word == "--help" || word == "-h"; })) {
        const Answered described = answer_to(session, std::format(R"({{"op":"describe","operation":{}}})", json_text(given.front())));
        for (const std::string& record : described.records) std::println(out, "{}", record);
        return described.finished ? described.exit_code : 1;
    }
    const std::vector<std::string> words = given.empty() ? std::vector<std::string>{} : named_in_order(session, given);
    const std::vector<std::string_view> viewed(words.begin(), words.end());
    const auto request = request_from(viewed);
    if (!request) {
        std::println(errors, "{}", request.error());
        std::println(errors, "  e.g. subjects | describe <subject or operation> | find <words> | <operation> --name value | --request '{{…}}'");
        return 2;
    }
    const Answered answered = answer_to(session, *request);
    for (const std::string& record : answered.records) std::println(out, "{}", record);
    if (!answered.finished) {
        std::println(errors, "no answer in time to {}", *request);
        return 1;
    }
    return answered.exit_code;
}

namespace {
std::string text_of(const json::Node& node) { return node.is_string() ? node.text().value_or("") : node.dump(); }
}  // namespace

Arguments::Arguments(const Request& request, std::string_view row) : request_(request) {
    if (!row.empty()) positionals_ = values(row);
}
std::optional<std::string> Arguments::value(std::string_view name) const {
    const json::Node node = request_[name];
    if (!node || node.is_null()) return std::nullopt;
    return text_of(node);
}
std::vector<std::string> Arguments::values(std::string_view name) const {
    std::vector<std::string> out;
    const json::Node node = request_[name];
    for (const json::Node& each : node.is_array() ? node.items() : std::vector{node})
        if (each && !each.is_null()) out.push_back(text_of(each));
    return out;
}
bool Arguments::flag(std::string_view name) const { return request_[name].boolean().value_or(false); }

std::string tool_name(std::string_view operation) {
    std::string out;
    for (const char c : operation) out += std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ? c : '_';
    return out.substr(0, 64);
}

std::string mcp_reply(Session& session, std::string_view message, const McpServer& server) {
    const auto document = parsed(message);
    if (!document || !document->root().is_object()) return rpc_error(json::Node(), -32700, "a JSON-RPC message, one a line");
    const json::Node root = document->root(), id = root["id"];
    const std::string method = root["method"].text().value_or("");
    if (!id) return {};  // a notification (initialized, cancelled): nothing to answer
    if (method == "initialize") {
        const std::string asked = root["params"]["protocolVersion"].text().value_or("2025-06-18");
        const std::string instructions = server.instructions.empty()
                                             ? "Begin with `subjects` (who is here), then `describe` a subject (its tasks and operations), or `find` what you want in words. "
                                               "Every error says its way out (`next`: requests to send as they are)."
                                             : server.instructions;
        return rpc_result(id, std::format(R"({{"protocolVersion":{},"capabilities":{{"tools":{{"listChanged":false}}}},"serverInfo":{{"name":{},"version":{}}},"instructions":{}}})",
                                          json_text(asked), json_text(server.name), json_text(server.version), json_text(instructions)));
    }
    if (method == "ping") return rpc_result(id, "{}");
    if (method == "tools/list") {
        std::string out = R"({"tools":[)";
        bool first = true;
        for (const Tool& tool : tools_of(session)) {
            out += std::format(R"({}{{"name":{},"title":{},"description":{},"inputSchema":{},"annotations":{{"readOnlyHint":{}}}}})", first ? "" : ",", json_text(tool_name(tool.operation)),
                               json_text(tool.operation), json_text(tool.description), tool.schema, tool.reads ? "true" : "false");
            first = false;
        }
        return rpc_result(id, out + "]}");
    }
    if (method == "tools/call") {
        const std::string name = root["params"]["name"].text().value_or("");
        std::string operation;
        for (const Tool& tool : tools_of(session))
            if (tool_name(tool.operation) == name) operation = tool.operation;
        if (operation.empty()) return rpc_error(id, -32602, std::format("no tool `{}` (tools/list names them)", name));
        const Answered answered = answer_to(session, request_of(operation, root["params"]["arguments"]));
        std::string content;
        std::optional<std::string> structured;
        std::size_t data = 0;
        for (const std::string& record : answered.records) {
            const auto each = parsed(record);
            if (!each) continue;
            const auto kind = each->root()["kind"].text();
            if (kind == "result") continue;
            const std::string text = kind == "data" ? each->root()["data"].dump() : record;
            if (kind == "data" && ++data == 1 && each->root()["data"].is_object()) structured = text;
            content += std::format(R"({}{{"type":"text","text":{}}})", content.empty() ? "" : ",", json_text(text));
        }
        if (!answered.finished) content += std::format(R"({}{{"type":"text","text":"no answer in time"}})", content.empty() ? "" : ",");
        const bool failed = !answered.finished || answered.exit_code != 0;
        return rpc_result(id, std::format(R"({{"content":[{}],{}"isError":{}}})", content, structured && data == 1 ? std::format("\"structuredContent\":{},", *structured) : std::string(),
                                          failed ? "true" : "false"));
    }
    return rpc_error(id, -32601, std::format("no method `{}` (initialize, ping, tools/list, tools/call)", method));
}

int serve_mcp(Session& session, std::istream& in, std::ostream& out, const McpServer& server) {
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line == "\r") continue;
        if (const std::string reply = mcp_reply(session, line, server); !reply.empty()) {
            out << reply << '\n';
            out.flush();
        }
    }
    return 0;
}

}  // namespace mlibs::agent
