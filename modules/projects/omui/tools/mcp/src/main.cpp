// omui-mcp — an MCP server for the openmui applications running on this
// machine, on standard input and output (an agent starts it):
//
//   claude mcp add openmui -- omui-mcp
//   omui-mcp --app org.openmui.notes        one application by default
//   omui-mcp --directory DIR                endpoints in DIR, not the per-user one
//   omui-mcp --ops inspector.ops.json       an application's verbs as tools, from its listing
//
// Diagnostics go to standard error; standard output is the protocol's.
import std;
import mcpplibs.os;
import mcpplibs.cmdline;
import omui.mcp;

int main(int argc, char** argv) {
    auto line = mcpplibs::cmdline::App("omui-mcp")
                    .description("An MCP server for the openmui applications running here, on standard input and output")
                    .option(mcpplibs::cmdline::Option("app").takes_value().value_name("NAME").help("the application a call names none of"))
                    .option(mcpplibs::cmdline::Option("directory").takes_value().value_name("DIR").help("endpoints in DIR, not the per-user one"))
                    .option(mcpplibs::cmdline::Option("actor").takes_value().value_name("KIND:NAME").help("who acts (default: the client's name)"))
                    .option(mcpplibs::cmdline::Option("ops").takes_value().multiple().value_name("LISTING").help("an application's verbs as tools, from its listing"))
                    .option(mcpplibs::cmdline::Option("no-design").help("leave Live Design's verbs (design.*) out"))
                    .option(mcpplibs::cmdline::Option("no-generate").help("leave runtime generation's verbs (generate.*) out"));
    auto parsed = line.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;
        (void)mcpplibs::os::stdio::write_error(std::format("omui-mcp: {}\n", parsed.error().message));
        return 2;
    }
    omui::mcp::ServerOptions options;
    if (auto app = parsed->value("app")) options.app = *app;
    if (auto directory = parsed->value("directory")) options.directory = mcpplibs::os::fs::from_utf8(*directory);
    if (auto actor = parsed->value("actor")) options.actor = *actor;
    options.design = !parsed->is_flag_set("no-design");
    options.generate = !parsed->is_flag_set("no-generate");
    for (const std::string& file : parsed->option_or_empty("ops").values) {
        auto listing = omui::mcp::read_listing(mcpplibs::os::fs::from_utf8(file));
        if (!listing) {
            (void)mcpplibs::os::stdio::write_error(std::format("omui-mcp: {}\n", listing.error()));
            return 2;
        }
        options.listings.push_back(std::move(*listing));
    }
    omui::mcp::Server server(std::move(options));
    omui::mcp::StdioTransport stdio;
    server.serve(stdio);
    return 0;
}
