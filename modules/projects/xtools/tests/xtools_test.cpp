// xtools: every toolkit behind one tool kernel — a toolkit's command the same under it, an operation reached by its
// whole name, what is here across the toolkits, and an MCP server that lists no operation holding a terminal.
import std;
import mcpplibs.mtest;
import xtools;
import hanim.studio;

using namespace mcpplibs::mtest;

namespace {

struct Ran {
    int code = -1;
    std::string out, err;
};

Ran run(std::vector<std::string> words, std::string_view input = {}) {
    words.insert(words.begin(), "xtools");
    std::istringstream in{std::string(input)};
    std::ostringstream out, err;
    const int code = xtools::run(words, in, out, err);
    return {code, out.str(), err.str()};
}

}  // namespace

static UTest cases{"xtools", [] {
    "every toolkit's subject is here: what answers is the toolkit's own"_test = [] {
        const Ran here = run({"subjects"});
        expect(eq(here.code, 0)) << here.err;
        for (const std::string_view subject : {R"("omui")", R"("document")", R"("timeline")", R"("xrender")", R"("gongfu")"})
            expect(here.out.contains(subject)) << subject << " in " << here.out;
        // What answers an intent, across them: fighters' hurtboxes are gongfu's.
        const Ran found = run({"find", "baked hurtboxes current"});
        expect(eq(found.code, 0) && found.out.contains("fighter.check")) << found.out;
    };

    "a toolkit's command is its own under xtools: the same request, the same answer"_test = [] {
        // As `hanim document.check --help` answers it: hanim.studio's subject alone, asked the same way.
        hanim::studio::Documents documents;
        mlibs::agent::SubjectSession alone({&documents}, {.command_line = true});
        std::ostringstream own, own_errors;
        const std::array asked{std::string_view("document.check"), std::string_view("--help")};
        const int own_code = mlibs::agent::command_line(alone, asked, own, own_errors);
        const Ran composed = run({"hanim", "document.check", "--help"});
        expect(eq(composed.code, own_code) && eq(composed.out, own.str())) << composed.out;
        // omui's command line is omui's: its words, its answer.
        const Ran omui = run({"omui", "templates"});
        expect(eq(omui.code, 0) && omui.out.contains(R"("template":"app-ts")")) << omui.out << omui.err;
        // An operation by its whole name reaches the toolkit that has it; a name none has is not found, with the nearest.
        const Ran described = run({"describe", "fighter.check"});
        expect(eq(described.code, 0) && described.out.contains(R"("name":"fighter.check")")) << described.out;
        const Ran unknown = run({"fighter.chek", "here"});
        expect(eq(unknown.code, 1) && unknown.out.contains("not-found") && unknown.out.contains("fighter.check")) << unknown.out;
    };

    "one MCP server over every toolkit: what holds a terminal is not a tool"_test = [] {
        const Ran served = run({"mcp"}, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})" "\n" R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})" "\n");
        expect(eq(served.code, 0)) << served.err;
        for (const std::string_view tool : {"omui_build", "document_check", "timeline_check", "effect_check", "fighter_check"})
            expect(served.out.contains(std::format(R"("name":"{}")", tool))) << tool;
        for (const std::string_view held : {"omui_dev", "omui_run", "omui_connect", "omui_mcp"})
            expect(!served.out.contains(std::format(R"("name":"{}")", held))) << held << " holds a terminal";
    };
}};
