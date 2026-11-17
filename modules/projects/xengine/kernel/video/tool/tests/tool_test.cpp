// The video command's subject, as an agent reaches it through the tool kernel's session (as the command line and
// MCP give it): a description written as its author writes it (a TypeScript data module) rendered to Y4M and WAV and
// to WebM and probed back, frame and sample counts exact; a still; the timing of 29.97 fps; a description's problem
// at its line; a request without its output refused before it runs; and a view's frames refused with the way out
// (they are omui's: xtools adds them).
import std;
import mcpplibs.mtest;
import mlibs.json;
import mlibs.testing;
import xengine.video.tool;

using namespace mcpplibs::mtest;
using namespace mlibs::testing;
namespace agent = mlibs::agent;

namespace {

std::string utf8(const std::filesystem::path& path) {
    const std::u8string u = path.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

std::string quoted(const std::filesystem::path& path) {
    std::string out;
    for (const char c : utf8(path)) out += c == '\\' || c == '"' ? std::string{'\\', c} : std::string(1, c);
    return "\"" + out + "\"";
}

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error;
    int                                  code = -1;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};

// One request; its data record, its exit code and an error's text.
Answered ask(agent::SubjectSession& session, const std::string& line) {
    Answered answered;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        const auto kind = parsed->root()["kind"].text();
        if (kind == "error") answered.error = parsed->root()["text"].text().value_or("");
        if (kind == "result") answered.code = static_cast<int>(parsed->root()["exitCode"].integer().value_or(-1));
        if (kind == "data" && !answered.data) answered.data.emplace(std::move(*parsed));
    });
    return answered;
}

}  // namespace

static UTest cases{"video.tool", [] {
    "films through the subject: rendered, probed, a still, the timing, problems located, a view's frames refused with the way out"_test = [] {
        const auto dir = scratch_dir("video-tool");
        xengine::video::tool::Video video;
        agent::SubjectSession session({&video});
        std::ofstream(dir / "card.video.xdef.ts", std::ios::binary)
            << "import { defineVideo } from \"omui/define\";\n\nexport default defineVideo({ width: 64, height: 36, fps: 30, seconds: 1, from: { card: \"sync\" }, audio: { beeps: true } });\n";
        const auto card = quoted(dir / "card.video.xdef.ts");

        auto answered = ask(session, std::format(R"({{"op":"video.render","file":{},"out":{}}})", card, quoted(dir / "out" / "card.y4m")));
        expect(fatal(eq(answered.code, 0))) << answered.error;
        expect(answered["frames"].integer() == 30 && answered["samples"].integer() == 48000 && answered["files"].size() == 2u) << "Y4M and the WAV beside it";
        answered = ask(session, std::format(R"({{"op":"video.probe","file":{}}})", quoted(dir / "out" / "card.y4m")));
        expect(eq(answered.code, 0) && answered["frames"].integer() == 30 && answered["fps"].text() == "30") << "the Y4M read back: " << answered.error;
        answered = ask(session, std::format(R"({{"op":"video.probe","file":{}}})", quoted(dir / "out" / "card.wav")));
        expect(eq(answered.code, 0) && answered["samples"].integer() == 48000) << answered.error;

        answered = ask(session, std::format(R"({{"op":"video.render","file":{},"out":{},"speed":8}})", card, quoted(dir / "out" / "card.webm")));
        expect(fatal(eq(answered.code, 0))) << answered.error;
        answered = ask(session, std::format(R"({{"op":"video.probe","file":{}}})", quoted(dir / "out" / "card.webm")));
        expect(eq(answered.code, 0) && answered["frames"].integer() == 30 && answered["samples"].integer() == 48000 && answered["video"].text() == "V_VP9" &&
               answered["audio"].text() == "A_OPUS" && answered["bt709"].boolean() == true)
            << "VP9 and Opus, read back, its colours said to be BT.709";

        answered = ask(session, std::format(R"({{"op":"video.still","file":{},"at":0.5,"out":{}}})", card, quoted(dir / "f15.png")));
        expect(eq(answered.code, 0) && answered["frame"].integer() == 15 && std::filesystem::exists(dir / "f15.png")) << answered.error;
        // A number is the whole word: `5x` and `abc` are refused, not read as 5 and the default.
        const Answered five_x = ask(session, std::format(R"({{"op":"video.still","file":{},"frame":"5x","out":{}}})", card, quoted(dir / "f5.png")));
        expect(eq(five_x.code, 1) && five_x.error.contains("--frame") && !std::filesystem::exists(dir / "f5.png")) << five_x.error;
        const Answered abc = ask(session, R"({"op":"video.timing","fps":"30","rate":"abc"})");
        expect(eq(abc.code, 1) && abc.error.contains("--rate")) << abc.error;

        answered = ask(session, R"({"op":"video.timing","fps":"30000/1001","frames":5})");
        expect(eq(answered.code, 0) && answered["samples"].integer() == 8008) << "five frames of 29.97 at 48 kHz";

        std::ofstream(dir / "bad.video.xdef.json", std::ios::binary) << "{\"$format\": \"video@1\", \"video\": {\n  \"width\": 64,\n  \"height\": 36,\n  \"fsp\": 30,\n  \"seconds\": 1\n}}";
        answered = ask(session, std::format(R"({{"op":"video.check","file":{}}})", quoted(dir / "bad.video.xdef.json")));
        expect(eq(answered.code, 1) && answered["problems"][0]["line"].integer() == 4) << "the misspelt key at its line";

        const auto unrun = ask(session, std::format(R"({{"op":"video.render","file":{}}})", card));
        expect(eq(unrun.code, 1) && unrun.error.contains("needs `out`")) << "no output: said before it runs — " << unrun.error;

        std::ofstream(dir / "hello.view.xdef.json", std::ios::binary) << R"({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "openmui"}}}})";
        answered = ask(session, std::format(R"({{"op":"video.render","file":{},"frames":3,"out":{}}})", quoted(dir / "hello.view.xdef.json"), quoted(dir / "view/")));
        expect(eq(answered.code, 1) && answered.error.contains("xtools")) << "a view's frames are omui's: " << answered.error;
    };
}};
