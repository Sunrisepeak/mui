// The mrender command's subject, as an agent reaches it: effect files checked — one read, compiled for the device formats and
// registered, its parameters said; one that binds resources of its own and one whose name is taken refused, each problem at
// its file — every answer through the tool kernel's session, as the command line and MCP give it.
import std;
import mcpplibs.mtest;
import mlibs.json;
import mrender.tool;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;

namespace {

std::filesystem::path data(std::string_view name) {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path() / ".." / ".." / "effect" / "tests" / "data" / name;
}

std::string quoted(const std::filesystem::path& path) {
    std::string out;
    for (const char c : path.generic_string()) out += c == '\\' || c == '"' ? std::string{'\\', c} : std::string(1, c);
    return "\"" + out + "\"";
}

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};

Answered ask(agent::SubjectSession& session, const std::string& line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        const auto kind = parsed->root()["kind"].text();
        if (kind == "error") out.error = parsed->root()["code"].text().value_or("");
        if (kind == "data" && !out.data) out.data.emplace(std::move(*parsed));
    });
    return out;
}

}  // namespace

UTest cases{"mrender.tool", [] {
    "effect.check: one read, compiled and registered, its parameters said; two refused, each with its problem"_test = [] {
        mrender::tool::Renderer renderer;
        agent::SubjectSession session({&renderer});
        const auto checked = ask(session, std::format(R"({{"op":"effect.check","files":[{}]}})", quoted(data("scanlines.effect.xdef.json"))));
        expect(fatal(checked.data.has_value())) << checked.error;
        expect(checked["ok"].boolean().value_or(false));
        const auto effect = checked["effects"][0];
        expect(effect["name"].text() == "scanlines");
        expect(fatal(eq(effect["params"].size(), std::size_t{2})));
        expect(effect["params"][1]["name"].text() == "period");

        const auto refused = ask(session, std::format(R"({{"op":"effect.check","files":[{},{}]}})", quoted(data("greedy.effect.xdef.json")), quoted(data("blur.effect.xdef.json"))));
        expect(!refused.error.empty()) << "refused: neither registers";
        expect(fatal(refused.data.has_value())) << "the refusal lists each file's problems";
        const auto problem = [&](std::size_t file) {
            const auto problems = refused["effects"][file]["problems"];
            return problems.size() > 0u ? problems[0]["message"].text().value_or("") : std::string();
        };
        expect(!refused["effects"][0]["ok"].boolean().value_or(true) && problem(0).contains("binds resources of its own")) << problem(0);
        expect(!refused["effects"][1]["ok"].boolean().value_or(true) && problem(1).contains("registered already")) << problem(1);
    };
}};
