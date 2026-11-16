// xtools — the toolchain whole (convergence plan §4.5, D-203): every project's toolkit behind one tool kernel (T0).
// A toolkit's command behaves the same under it — the same library, the same request:
//
//   xtools hanim document.check rise.json      is   hanim document.check rise.json
//   xtools omui build --release                is   omui build --release
//   xtools gongfu-studio fighter.check DIR     is   gongfu-studio fighter.check DIR
//   xtools document.check rise.json            an operation by its whole name, whichever toolkit has it
//   xtools subjects | find WORDS | describe X  what is here, across the toolkits
//   xtools mcp                                 every toolkit's operations to an MCP client, on stdio
//
// Records out, one a line; exit 0 done, 1 failed (why, and the ways out), 2 arguments that make no request.
module xtools;

import std;
import mcpplibs.os;
import omui.cli;
import hanim.studio;
import hanim.timeline.tool;
import xrender.tool;
import gongfu.studio;

namespace {

constexpr std::string_view usage =
    "xtools — the toolchain whole: every project's toolkit behind one tool kernel\n\n"
    "  xtools <toolkit> <words…>    a toolkit's own command line: omui, hanim, hanim-timeline, xrender, gongfu-studio\n"
    "  xtools <operation> <words…>  an operation by its whole name (document.check, effect.preview, fighter.bake …)\n"
    "  xtools subjects | find WORDS | describe NAME   what is here, across the toolkits\n"
    "  xtools mcp                   every toolkit's operations to an MCP client, on stdio\n";

}  // namespace

namespace xtools {

int run(std::span<const std::string> argv, std::istream& in, std::ostream& out, std::ostream& errors) {
    const std::vector<std::string> given(argv.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(argv.size(), 1)), argv.end());
    // Each toolkit's subject, as its own program makes it; fidelity.run runs model.look in this program, which has it.
    omui::cli::Project project;
    hanim::studio::Documents documents;
    hanim::timeline::tool::Timelines timelines;
    xrender::tool::Renderer renderer(argv.empty() ? std::filesystem::path{} : mcpplibs::os::fs::from_utf8(argv.front()));
    gongfu::studio::Studio fighters;
    const std::vector<std::pair<std::string_view, mlibs::agent::Subject*>> toolkits{
        {"hanim", &documents}, {"hanim-timeline", &timelines}, {"xrender", &renderer}, {"gongfu-studio", &fighters}};
    if (!given.empty()) {
        // omui's command line is a function of its words (agent, page, the project's catalog): called as `omui` calls it.
        if (given.front() == "omui") {
            std::vector<std::string> words{"omui"};
            words.insert(words.end(), given.begin() + 1, given.end());
            return omui::cli::run(words, out, errors);
        }
        if (const auto toolkit = std::ranges::find(toolkits, std::string_view(given.front()), &std::pair<std::string_view, mlibs::agent::Subject*>::first);
            toolkit != toolkits.end()) {
            mlibs::agent::SubjectSession session({toolkit->second}, {.command_line = true});
            const std::vector<std::string_view> words(given.begin() + 1, given.end());
            if (!words.empty() && words.front() == "mcp")
                return mlibs::agent::serve_mcp(session, in, out, {.name = std::string(toolkit->first), .version = "0.1.0"});
            return mlibs::agent::command_line(session, words, out, errors);
        }
    }
    if (given.empty() || given.front() == "--help" || given.front() == "-h" || given.front() == "help") {
        out << usage;
        return 0;
    }
    mlibs::agent::SubjectSession session({&project, &documents, &timelines, &renderer, &fighters}, {.command_line = true});
    const std::vector<std::string_view> words(given.begin(), given.end());
    if (words.front() == "mcp") return mlibs::agent::serve_mcp(session, in, out, {.name = "xtools", .version = "0.1.0"});
    return mlibs::agent::command_line(session, words, out, errors);
}

}  // namespace xtools
