// hanim — the animation tool without a GUI: hanim.studio's subject `document` behind the tool kernel's entries
// (mlibs.agent:host, T0). Its operations are the catalog's (studio.ops.omui.ts):
//
//   hanim subjects | hanim describe document.render | hanim find render frames
//   hanim document.check  --file rise.animation.omui.json
//   hanim document.render --file rise.animation.omui.json --out rise --frames 5
//   hanim mcp                                  the same operations to an MCP client, on stdio
//
// Records out, one a line; exit 0 done, 1 the document or the work failed (its problems listed), 2 arguments that
// make no request.
import std;
import hanim.studio;

int main(int argc, char** argv) {
    hanim::studio::Documents documents;
    mlibs::agent::SubjectSession session({&documents});
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    if (!arguments.empty() && arguments.front() == "mcp")
        return mlibs::agent::serve_mcp(session, std::cin, std::cout, {.name = "hanim", .version = "0.1.0"});
    return mlibs::agent::command_line(session, arguments, std::cout, std::cerr);
}
