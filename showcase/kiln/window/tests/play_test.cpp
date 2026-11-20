// kiln-play as it is used: the program run, requests on its input; run again with the same session file, the
// game is where the first run left it.
import std;
import mlibs.json;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;

using namespace mcpplibs::mtest;

namespace {

// The data records of what a run answered, in order.
std::vector<mlibs::json::Document> data_of(std::string_view out) {
    std::vector<mlibs::json::Document> records;
    for (const auto line : std::views::split(out, '\n')) {
        const std::string_view text(line.begin(), line.end());
        if (text.empty()) continue;
        auto parsed = mlibs::json::Document::parse(text, "record");
        expect(fatal(parsed.has_value())) << text;
        if (parsed->root()["kind"].text() == "data") records.push_back(std::move(*parsed));
    }
    return records;
}

}  // namespace

static UTest cases{"kiln.play", [] {
    "an agent with a pipe: what is here, the controls by name; a second run goes on from the session's file"_test = [] {
        const auto directory = std::filesystem::temp_directory_path() / "openmui-tests" / "kiln-play";
        std::filesystem::create_directories(directory);
        const auto session = directory / "game.jsonl";
        std::filesystem::remove(session);
        const std::u8string written = session.generic_u8string();
        const std::string kept(reinterpret_cast<const char*>(written.data()), written.size());
        const std::string program = e2e::program("kiln-play");

        const auto first = e2e::run({.argv = {program, "run", "--session", kept},
                                     .input = "{\"op\":\"subjects\"}\n{\"op\":\"world.state\"}\n{\"op\":\"world.act\",\"axis\":[1]}\n{\"op\":\"world.step\",\"steps\":60}\n"
                                              "{\"op\":\"world.observe\"}\n"});
        expect(fatal(static_cast<bool>(first))) << first.line << "\n" << first.out << first.err;
        const auto answered = data_of(first.out);
        expect(fatal(answered.size() == 5u)) << first.out;
        expect(answered[0].root()["data"]["subjects"][0]["name"].text() == "world");
        expect(answered[1].root()["data"]["controls"][2]["name"].text() == "jump") << "a button is a name: " << answered[1].root()["data"]["controls"].dump();
        const double walked = answered[4].root()["data"]["view"]["cat"]["x"].number().value_or(0);
        const std::string hash(answered[4].root()["data"]["hash"].text().value_or(""));
        expect(answered[4].root()["data"]["frame"].integer() == 60);

        // Another process, the same file: the game is at step 60, Mui where she walked to.
        const auto second = e2e::run({.argv = {program, "run", "--session", kept}, .input = "{\"op\":\"world.observe\"}\n"});
        expect(fatal(static_cast<bool>(second))) << second.out << second.err;
        const auto again = data_of(second.out);
        expect(fatal(again.size() == 1u)) << "only the new request is answered: " << second.out;
        expect(again[0].root()["data"]["frame"].integer() == 60 && again[0].root()["data"]["hash"].text() == hash &&
               again[0].root()["data"]["view"]["cat"]["x"].number() == walked)
            << "the same game, bit for bit: " << second.out;

        // Without a session every run begins again; a request refused is exit 1 and says what to send.
        const auto fresh = e2e::run({.argv = {program, "run"}, .input = "{\"op\":\"world.observe\"}\n{\"op\":\"world.jump\"}\n"});
        expect(eq(fresh.status, 1) && data_of(fresh.out)[0].root()["data"]["frame"].integer() == 0 && fresh.out.contains("\"next\"")) << fresh.out;
        expect(eq(e2e::run({.argv = {program, "walk"}}).status, 2));
    };
}};
