// video — films without a GUI, for people, agents and CI: meng.video.tool's subject `video` behind the tool
// kernel's entries (mlibs.agent:host, T0). Its operations are the catalog's (video.ops.xdef.ts):
//
//   video render FILE --out OUT [--format webm|y4m|png|wav] [--size WxH] [--fps R] [--seconds S | --frames N] …
//   video still  FILE (--frame N | --at SECONDS) --out x.png
//   video check  FILE
//   video probe  FILE.webm | FILE.y4m | FILE.wav
//   video timing --fps R [--rate HZ] [--frames N]
//   video mcp    the same operations to an MCP client, on stdio
//
// FILE is a video description (`*.video.xdef.ts` or its JSON): the sync card or PNG files, with its sound track.
// A view's frames are omui's: `xtools video.render x.view.xdef.json …` adds omui's view source.
// Records out, one a line; exit 0 done, 1 failed (why, and the ways out), 2 arguments that make no request.
import std;
import meng.video.tool;

int main(int argc, char** argv) {
    meng::video::tool::Video video;
    mlibs::agent::SubjectSession session({&video});
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    if (!arguments.empty() && arguments.front() == "mcp")
        return mlibs::agent::serve_mcp(session, std::cin, std::cout, {.name = "video", .version = "0.1.0"});
    return mlibs::agent::command_line(session, arguments, std::cout, std::cerr, {.product = "video"});
}
