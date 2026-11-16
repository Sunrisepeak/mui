// hanim-timeline — the timeline every editor shares without a GUI, for people, agents and CI: hanim.timeline.tool's
// subject `timeline` behind the tool kernel's entries (mlibs.agent:host, T0). Its operations are the catalog's
// (timeline.ops.xdef.ts):
//
//   hanim-timeline timeline.check --file F             the timeline, or every problem at its line and column
//   hanim-timeline timeline.at    --file F --time T    what plays at T: each clip, where in its source, at what level
//   hanim-timeline timeline.edit  --file F --edit split|trim|move|remove|ripple-remove --track N --clip I
//                                 [--at T] [--length T] [--to_track N] [--dry_run]
//   hanim-timeline timeline.undo  --file F             the last edit taken back
//   hanim-timeline mcp                                 the same operations to an MCP client, on stdio
//
// Records out, one a line; exit 0 done, 1 refused or failed (why, and the ways out), 2 arguments that make no request.
import std;
import hanim.timeline.tool;

int main(int argc, char** argv) {
    hanim::timeline::tool::Timelines timelines;
    mlibs::agent::SubjectSession session({&timelines});
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    if (!arguments.empty() && arguments.front() == "mcp")
        return mlibs::agent::serve_mcp(session, std::cin, std::cout, {.name = "hanim-timeline", .version = "0.1.0"});
    return mlibs::agent::command_line(session, arguments, std::cout, std::cerr);
}
