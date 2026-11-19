// How a subject was designed, as data a release plays (mlibs.agent:replay; replay@1): written and read back the
// same, each text once; a mistake named at its place.
import std;
import mlibs.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;

UTest cases{"agent.replay", [] {
    "written and read back the same; a text the steps share kept once; the same replay the same bytes"_test = [] {
        const std::string page = "mount(\"main\", text({ text: \"Plans\" }));\n", styles = "{\"accent\": \"#7c3aed\"}";
        agent::Replay replay{.doc = "How the page was designed", .subject = "org.example.page", .branch = "main"};
        replay.steps.push_back({.caption = "1/2 · the page", .actor = "agent:designer", .seconds = 1.25, .texts = {{"app/main.ts", page}, {"app/look.json", styles}},
                                .node = "main/plans"});
        replay.steps.push_back({.caption = "2/2 · \"Plans\" → \"Your plan\"", .actor = "person:developer", .texts = {{"app/main.ts", "mount(\"main\", text({ text: \"Your plan\" }));\n"}, {"app/look.json", styles}}});
        const std::string text = agent::write_replay(replay);
        expect(text.starts_with("{\n  \"$format\": \"replay@1\"")) << text;
        std::size_t kept = 0;
        for (std::size_t at = text.find("#7c3aed"); at != std::string::npos; at = text.find("#7c3aed", at + 1)) ++kept;
        expect(eq(kept, std::size_t{1})) << "a text two steps share is kept once";
        expect(eq(agent::write_replay(replay), text)) << "the same bytes";

        const auto read = agent::read_replay(text, "page.replay.xdef.json");
        expect(fatal(read.has_value())) << (read ? "" : read.error());
        expect(eq(read->subject, std::string("org.example.page")) && eq(read->branch, std::string("main")) && eq(read->steps.size(), std::size_t{2}));
        expect(eq(read->steps[0].texts.at("app/main.ts"), page) && eq(read->steps[0].seconds, 1.25) && eq(read->steps[0].actor, std::string("agent:designer")));
        expect(eq(read->steps[0].node, std::string("main/plans")) && read->steps[1].node.empty()) << "where its maker is shown, when known";
        expect(eq(read->steps[1].caption, std::string("2/2 · \"Plans\" → \"Your plan\"")) && eq(read->steps[1].seconds, 1.5)) << "the default time";
        expect(eq(agent::write_replay(*read), text)) << "read and written again: the same";
    };

    "a mistake is named where it is: the format, a step's time, a text it names that is not there"_test = [] {
        const auto wrong_format = agent::read_replay(R"({"$format": "film@1", "replay": {}})", "x.replay.xdef.json");
        expect(!wrong_format && wrong_format.error().contains("x.replay.xdef.json:1:") && wrong_format.error().contains("/$format")) << wrong_format.error();
        const auto too_long = agent::read_replay(R"({"$format": "replay@1", "replay": {"texts": {"a": "x"}, "steps": [{"seconds": 90, "files": {"main.ts": "a"}}]}})");
        expect(!too_long && too_long.error().contains("/replay/steps/0/seconds")) << too_long.error();
        const auto missing = agent::read_replay(R"({"$format": "replay@1", "replay": {"texts": {"a": "x"}, "steps": [{"files": {"main.ts": "b"}}]}})");
        expect(!missing && missing.error().contains("/replay/steps/0/files/main.ts") && missing.error().contains("no text `b`")) << missing.error();
        const auto empty = agent::read_replay(R"({"$format": "replay@1", "replay": {"texts": {}, "steps": []}})");
        expect(!empty && empty.error().contains("/replay/steps")) << empty.error();
    };
}};
