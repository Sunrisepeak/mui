// xrender — the renderer without a GUI, for people, agents and CI: xrender.tool's subject `xrender` behind the tool
// kernel's entries (mlibs.agent:host, T0). Its operations are the catalog's (xrender.ops.xdef.ts):
//
//   xrender effect.check     --files a.effect.xdef.json [--files b.effect.xdef.json]
//   xrender effect.preview   --file a.effect.xdef.json --out a.png [--image PNG] [--size WxH] [--param NAME=VALUE …]
//   xrender conformance.run  [--report FILE]
//   xrender device.describe
//   xrender model.look       --model M.glb --out M.png [--light HDR] [--orbit T,P,R] [--target X,Y,Z] [--size WxH]
//                            [--upright DEGREES] [--samples N] [--behind] [--golden PNG] [--follow RAYS]
//   xrender fidelity.run     [--names Duck …] [--cache DIR] [--out DIR] [--follow RAYS]
//   xrender mcp              the same operations to an MCP client, on stdio
//
// Records out, one a line; exit 0 done, 1 failed (why, and the ways out), 2 arguments that make no request.
import std;
import mcpplibs.os;
import xrender.tool;

int main(int argc, char** argv) {
    xrender::tool::Renderer renderer(argc > 0 ? mcpplibs::os::fs::from_utf8(argv[0]) : std::filesystem::path{});
    mlibs::agent::SubjectSession session({&renderer});
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    if (!arguments.empty() && arguments.front() == "mcp")
        return mlibs::agent::serve_mcp(session, std::cin, std::cout, {.name = "xrender", .version = "0.1.0"});
    return mlibs::agent::command_line(session, arguments, std::cout, std::cerr);
}
