// oweb — a site on omui without opening a page, for people, agents and CI: the subject `oweb` behind the tool
// kernel's entries (mlibs.agent:host, T0). Its operations are the catalog's (oweb.ops.xdef.ts):
//
//   oweb site.build   --site DIR --out DIR [--title T] [--languages en,zh] [--url https://example.org/docs/]
//   oweb routes.list  --site DIR [--languages en,zh]
//   oweb page.read    --site DIR --route /guide/start/ [--as markdown|html]
//   oweb docs.search  --site DIR --query "first window" [--limit N]
//   oweb mcp          the same operations to an MCP client, on stdio
//
// Records out, one a line; exit 0 done, 1 failed (why, and the ways out), 2 arguments that make no request.
import std;
import oweb;

int main(int argc, char** argv) {
    oweb::Sites sites;
    mlibs::agent::SubjectSession session({&sites});
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    if (!arguments.empty() && arguments.front() == "mcp") return mlibs::agent::serve_mcp(session, std::cin, std::cout, {.name = "oweb", .version = "0.1.0"});
    return mlibs::agent::command_line(session, arguments, std::cout, std::cerr);
}
