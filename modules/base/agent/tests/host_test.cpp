// The tool kernel's entries (RC3.8): the same operation asked over the protocol, from a command line and by an MCP
// client is one request to one session — decided, run, recorded and answered alike.
import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace control = mlibs::control;

namespace {

class Counter final : public agent::Subject {
public:
    std::string_view name() const noexcept override { return "counter"; }
    std::string_view interface_name() const noexcept override { return "example.counter@1"; }
    agent::Capabilities capabilities() const noexcept override { return {.observation = true, .control = true}; }
    std::span<const control::CatalogEntry> operations() const noexcept override {
        static constexpr std::array<control::Parameter, 1> add_parameters{{{"by", "integer", "how much (1 when none is given)", true}}};
        static constexpr std::array<control::CatalogEntry, 2> all{{
            {{"counter.read", openas::Effect::read, false}, "The count", {}, "json", control::Cost::instant, "", false, false, false},
            {{"counter.add", openas::Effect::reversible, false}, "Adds to the count", add_parameters, "json", control::Cost::instant, "counter.add with the opposite", false, false, false},
        }};
        return all;
    }
    control::Guide guide() const noexcept override { return {"A count.", {}}; }
    std::string_view listing() const noexcept override {
        return R"({"kind":"omui.ops","tools":[{"name":"counter.add","inputSchema":{"type":"object","properties":{"by":{"type":"integer"}}}}]})";
    }
    std::uint64_t state_hash() const override { return static_cast<std::uint64_t>(count) + 1; }
    void operate(std::string_view operation, const agent::Request& request, agent::Answer& answer) override {
        if (operation == "counter.add") {
            if (!request["by"].is_integer() && request["by"]) return answer.fail(openas::error_code::invalid_argument, "`by` is a whole number");
            count += request["by"].integer().value_or(1);
        }
        answer.data(std::format("{{\"count\":{}}}", count));
    }
    std::int64_t count = 0;
};

std::vector<std::string_view> words(std::initializer_list<std::string_view> all) { return all; }

std::string request(std::initializer_list<std::string_view> arguments) {
    const auto made = agent::request_from(words(arguments));
    return made ? *made : "error: " + made.error();
}

mlibs::json::Document parsed(std::string_view text) {
    auto document = mlibs::json::Document::parse(text, "reply");
    expect(fatal(document.has_value())) << text;
    return std::move(*document);
}

}  // namespace

static UTest cases{"agent.host", [] {
    "a command line's arguments are the request: values as JSON when they read so, else text"_test = [] {
        expect(eq(request({"counter.add", "--by", "2"}), std::string(R"({"op":"counter.add","by":2})")));
        expect(eq(request({"counter.add", "--by=2", "--note", "two more", "--quiet"}), std::string(R"({"op":"counter.add","by":2,"note":"two more","quiet":true})")));
        expect(eq(request({"world.act", "--at", "[3,4]"}), std::string(R"({"op":"world.act","at":[3,4]})")));
        expect(eq(request({"document.sample", "--at", "0", "--file", "a.json", "--at", "1.5"}), std::string(R"({"op":"document.sample","at":[0,1.5],"file":"a.json"})")));
        expect(eq(request({"describe", "counter"}), std::string(R"({"op":"describe","subject":"counter"})")));
        expect(eq(request({"describe", "counter.add"}), std::string(R"({"op":"describe","operation":"counter.add"})")));
        expect(eq(request({"find", "add", "to", "it"}), std::string(R"({"op":"find","q":"add to it"})")));
        expect(eq(request({"--request", R"({"op":"subjects"})"}), std::string(R"({"op":"subjects"})")));
        expect(request({}).starts_with("error: ") && request({"--by", "2"}).starts_with("error: ") && request({"counter.add", "by"}).starts_with("error: "));
    };

    "one operation through three entries: the same request, the same answer, each on the record"_test = [] {
        Counter counter;
        agent::SubjectSession session({&counter});
        // The protocol itself.
        const agent::Answered over_protocol = agent::answer_to(session, R"({"op":"counter.add","by":2})");
        expect(over_protocol.finished && eq(over_protocol.exit_code, 0) && over_protocol.records.front().contains(R"("count":2)"));
        // A command line.
        std::ostringstream out, errors;
        const std::array arguments{std::string_view("counter.add"), std::string_view("--by"), std::string_view("2")};
        expect(eq(agent::command_line(session, arguments, out, errors), 0)) << errors.str();
        expect(out.str().contains(R"("count":4)") && out.str().contains(R"("kind":"result")")) << out.str();
        // An MCP client: the tool its listing names, called with the same arguments.
        const std::string listed = agent::mcp_reply(session, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
        const auto tools = parsed(listed);
        std::vector<std::string> names;
        for (const auto& tool : tools.root()["result"]["tools"].items()) names.push_back(tool["name"].text().value_or(""));
        expect(std::ranges::contains(names, "counter_add") && std::ranges::contains(names, "counter_read") && std::ranges::contains(names, "subjects") &&
               std::ranges::contains(names, "find"))
            << listed;
        expect(listed.contains(R"("by":{"type":"integer"})")) << "the catalog's own schema";
        expect(listed.contains(R"("name":"counter_read","title":"counter.read")") && listed.contains(R"("readOnlyHint":true)"));
        const auto called = parsed(agent::mcp_reply(session, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"counter_add","arguments":{"by":2}}})"));
        expect(called.root()["result"]["isError"].boolean() == false && called.root()["result"]["structuredContent"]["count"].integer() == 6) << called.root().dump();
        expect(eq(counter.count, std::int64_t{6}));
        // Each was a request on the record, alike.
        const agent::Answered log = agent::answer_to(session, R"({"op":"log"})");
        const auto facts = parsed(log.records.front());
        std::size_t adds = 0;
        for (const auto& fact : facts.root()["data"]["facts"].items()) adds += fact.dump().contains("counter.add");
        expect(eq(adds, std::size_t{3})) << log.records.front();
    };

    "a failure is a failure through each: the exit code, and MCP's isError with the error's way out"_test = [] {
        Counter counter;
        agent::SubjectSession session({&counter});
        std::ostringstream out, errors;
        const std::array arguments{std::string_view("counter.add"), std::string_view("--by"), std::string_view("lots")};
        expect(agent::command_line(session, arguments, out, errors) != 0 && out.str().contains(R"("kind":"error")")) << out.str();
        const auto called = parsed(agent::mcp_reply(session, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"counter_add","arguments":{"by":"lots"}}})"));
        expect(called.root()["result"]["isError"].boolean() == true && called.root()["result"]["content"][0]["text"].text().value_or("").contains("whole number"));
        expect(eq(counter.count, std::int64_t{0}));
        const auto unknown = parsed(agent::mcp_reply(session, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"counter_take"}})"));
        expect(unknown.root()["error"]["code"].integer() == -32602);
        // An operation named by its last part alone: the way out is the operation meant.
        std::ostringstream short_out;
        const std::array short_name{std::string_view("add")};
        expect(eq(agent::command_line(session, short_name, short_out, errors), 1) && short_out.str().contains(R"("operation":"counter.add")")) << short_out.str();
        // Words without names: the operation's parameters in its catalog's order.
        std::ostringstream bare_out;
        const std::array bare{std::string_view("counter.add"), std::string_view("3")};
        expect(eq(agent::command_line(session, bare, bare_out, errors), 0) && bare_out.str().contains(R"("count":3)")) << bare_out.str();
        expect(eq(counter.count, std::int64_t{3}));
        std::ostringstream none;
        expect(eq(agent::command_line(session, {}, none, errors), 2));
    };

    "MCP over lines: initialize, a notification unanswered, ping, an unknown method"_test = [] {
        Counter counter;
        agent::SubjectSession session({&counter});
        std::istringstream in(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"ping"}
{"jsonrpc":"2.0","id":3,"method":"resources/list"}
)");
        std::ostringstream out;
        expect(eq(agent::serve_mcp(session, in, out, {.name = "counter-tool"}), 0));
        std::vector<std::string> lines;
        std::istringstream back(out.str());
        for (std::string line; std::getline(back, line);) lines.push_back(line);
        expect(fatal(eq(lines.size(), std::size_t{3}))) << out.str();
        expect(lines[0].contains(R"("protocolVersion":"2025-06-18")") && lines[0].contains(R"("name":"counter-tool")") && lines[0].contains("subjects"));
        expect(lines[1].contains(R"("id":2,"result":{})"));
        expect(lines[2].contains(R"("code":-32601)"));
    };
}};
