// gongfu-studio — making fighters from the command line, for people, agents and CI (D-61): gongfu.studio's subject
// `gongfu` behind the tool kernel's entries (mlibs.agent:host, T0). Its operations are the catalog's
// (gongfu.ops.xdef.ts); words without names are a request's parameters in the catalog's order:
//
//   gongfu-studio fighter.bake  <dir>      writes each <name>.boxes.gongfu.toml in <dir>
//   gongfu-studio fighter.check <dir>      whether they are what the files make now (exit 1 if not)
//   gongfu-studio fighter.embed <dir> <module> <namespace> <out>
//                                          the fighters as the partition <module>:gongfu_fighters, refused while a bake is stale
//   gongfu-studio fighter.sheet <dir> <name> <out.png> [--clips A,B] [--frames 0,12] [--palette N]
//   gongfu-studio stage.picture <dir> <name> <out.png> [--fighters DIR --who a,b] [--at X] [--apart U] [--frame N]
//   gongfu-studio art.cut <sheet.png> <dir> [--key r,g,b] [--gap PX] [--min PX] [--alpha A] [--scale S] [--webp Q]
//   gongfu-studio art.picture <file> <out> [--key r,g,b] [--trim] [--width PX] [--quality Q]
//   gongfu-studio roster.versus <dir> [--rounds N] [--seed S] [--system FILE]
//   gongfu-studio mcp                      the same operations to an MCP client, on stdio
//
// Records out, one a line; exit 0 done, 1 failed (why, and the ways out), 2 arguments that make no request.
import std;
import gongfu.studio;

int main(int argc, char** argv) {
    gongfu::studio::Studio studio;
    mlibs::agent::SubjectSession session({&studio}, {.command_line = true});
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    if (!arguments.empty() && arguments.front() == "mcp")
        return mlibs::agent::serve_mcp(session, std::cin, std::cout, {.name = "gongfu-studio", .version = "0.1.0"});
    return mlibs::agent::command_line(session, arguments, std::cout, std::cerr);
}
