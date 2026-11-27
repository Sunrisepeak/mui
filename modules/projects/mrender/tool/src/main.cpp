// mrender — the renderer without a GUI, for people, agents and CI: mrender.tool's subject `mrender` behind the tool
// kernel's entries (mlibs.agent:host, T0). Its operations are the catalog's (mrender.ops.xdef.ts):
//
//   mrender effect.check     --files a.effect.xdef.json [--files b.effect.xdef.json]
//   mrender effect.preview   --file a.effect.xdef.json --out a.png [--image PNG] [--size WxH] [--param NAME=VALUE …]
//   mrender conformance.run  [--report FILE]
//   mrender device.describe
//   mrender model.look       --model M.glb --out M.png [--light HDR] [--orbit T,P,R] [--target X,Y,Z] [--size WxH]
//                            [--upright DEGREES] [--samples N] [--behind] [--golden PNG] [--follow RAYS]
//   mrender fidelity.run     [--names Duck …] [--cache DIR] [--out DIR] [--follow RAYS]
//   mrender mcp              the same operations to an MCP client, on stdio
//
// Records out, one a line; exit 0 done, 1 failed (why, and the ways out), 2 arguments that make no request.
import std;
import mcpplibs.os;
import mrender.tool;

int main(int argc, char** argv) {
    mrender::tool::Renderer renderer(argc > 0 ? mcpplibs::os::fs::from_utf8(argv[0]) : std::filesystem::path{});
    mlibs::agent::SubjectSession session({&renderer});
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    if (!arguments.empty() && arguments.front() == "mcp")
        return mlibs::agent::serve_mcp(session, std::cin, std::cout, {.name = "mrender", .version = "0.1.0"});
    return mlibs::agent::command_line(session, arguments, std::cout, std::cerr);
}
