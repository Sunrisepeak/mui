// hanim-timeline's subject, `timeline`, as an agent reaches it: a timeline checked and read at a moment; a clip split, the
// file written as the edit leaves it; the edit taken back to the very bytes; a dry run that writes nothing; refusals that say
// why — every answer through the tool kernel's session, as the command line and MCP give it.
import std;
import mcpplibs.mtest;
import mlibs.json;
import hanim.timeline.tool;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;

namespace {

constexpr std::string_view promo = R"({"$format": "timeline@1", "timeline": {
  "rate": 30,
  "tracks": [
    {"name": "picture", "kind": "video", "clips": [
      {"name": "title", "source": "views/title.view.xdef.json", "at": "0f", "length": "3s"},
      {"name": "fight", "source": "showcase/fighting", "at": "3s", "length": "120f", "in": "10s", "speed": 0.5}
    ]},
    {"name": "music", "kind": "audio", "clips": [
      {"name": "theme", "source": "menu.music.xdef.ts", "at": "0s", "length": "5s", "level": 0.8}
    ]}
  ]
}})";

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

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::string quoted(const std::filesystem::path& path) {
    std::string out;
    for (const char c : path.generic_string()) out += c == '\\' || c == '"' ? std::string{'\\', c} : std::string(1, c);
    return "\"" + out + "\"";
}

// A fresh directory with the promo's timeline in it.
std::filesystem::path fresh(std::string_view name) {
    const auto directory = std::filesystem::temp_directory_path() / std::format("hanim-timeline-tool-{}", name);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "promo.timeline.xdef.json", std::ios::binary) << promo;
    return directory / "promo.timeline.xdef.json";
}

}  // namespace

UTest cases{"hanim.timeline.tool", [] {
    "checked, read at a moment: its tracks and clips; what plays at 1s"_test = [] {
        const auto file = fresh("read");
        hanim::timeline::tool::Timelines timelines;
        agent::SubjectSession session({&timelines});
        const auto checked = ask(session, std::format(R"({{"op":"timeline.check","file":{}}})", quoted(file)));
        expect(fatal(checked.data.has_value())) << checked.error;
        expect(eq(checked["tracks"].size(), std::size_t{2}));
        expect(eq(checked["tracks"][0]["clips"].size(), std::size_t{2}));
        const auto now = ask(session, std::format(R"({{"op":"timeline.at","file":{},"time":"1s"}})", quoted(file)));
        expect(fatal(now.data.has_value())) << now.error;
        expect(eq(now["playing"].size(), std::size_t{2})) << "the title and the theme";
        expect(now["playing"][0]["clip"].text() == "title");
    };

    "a clip split: the file as the edit leaves it; taken back, the very bytes it had; a dry run writes nothing"_test = [] {
        const auto file = fresh("edit");
        const std::string before = read(file);
        hanim::timeline::tool::Timelines timelines;
        agent::SubjectSession session({&timelines});
        const auto dry = ask(session, std::format(R"({{"op":"timeline.edit","file":{},"edit":"split","track":0,"clip":0,"at":"1s","dry_run":true}})", quoted(file)));
        expect(fatal(dry.data.has_value())) << dry.error;
        expect(read(file) == before) << "a dry run only says";
        const auto split = ask(session, std::format(R"({{"op":"timeline.edit","file":{},"edit":"split","track":0,"clip":0,"at":"1s"}})", quoted(file)));
        expect(fatal(split.data.has_value())) << split.error;
        const auto after = ask(session, std::format(R"({{"op":"timeline.check","file":{}}})", quoted(file)));
        expect(fatal(after.data.has_value()));
        expect(eq(after["tracks"][0]["clips"].size(), std::size_t{3})) << "the title in two";
        const auto undone = ask(session, std::format(R"({{"op":"timeline.undo","file":{}}})", quoted(file)));
        expect(fatal(undone.data.has_value())) << undone.error;
        expect(read(file) == before) << "taken back to the very bytes";
        expect(!ask(session, std::format(R"({{"op":"timeline.undo","file":{}}})", quoted(file))).error.empty()) << "nothing left to take back";
    };

    "refusals say why: a clip that is not there, an edit that is not one, a time that is not one, a file that does not read"_test = [] {
        const auto file = fresh("refused");
        hanim::timeline::tool::Timelines timelines;
        agent::SubjectSession session({&timelines});
        expect(!ask(session, std::format(R"({{"op":"timeline.edit","file":{},"edit":"split","track":0,"clip":9,"at":"1s"}})", quoted(file))).error.empty());
        expect(!ask(session, std::format(R"({{"op":"timeline.edit","file":{},"edit":"fold","track":0,"clip":0}})", quoted(file))).error.empty());
        expect(!ask(session, std::format(R"({{"op":"timeline.at","file":{},"time":"soon"}})", quoted(file))).error.empty());
        std::ofstream(file, std::ios::binary) << R"({"$format": "timeline@1", "timeline": {"tracks": [{"name": "a", "kind": "vidoe"}]}})";
        expect(!ask(session, std::format(R"({{"op":"timeline.check","file":{}}})", quoted(file))).error.empty()) << "a kind of track that is not one";
    };
}};
