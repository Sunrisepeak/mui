// omui-mcp — an MCP server for the openmui applications running on this
// machine, on standard input and output (an agent starts it):
//
//   claude mcp add openmui -- omui-mcp
//   omui-mcp --app org.openmui.notes        one application by default
//   omui-mcp --directory DIR                endpoints in DIR, not the per-user one
//
// Diagnostics go to standard error; standard output is the protocol's.
import std;
import mcpplibs.os;
import openmui.mcp;

int main(int argc, char** argv) {
    omui::mcp::ServerOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&]() -> std::optional<std::string_view> {
            if (i + 1 >= argc) return std::nullopt;
            return std::string_view(argv[++i]);
        };
        if (a == "--app") {
            auto v = value();
            if (!v) return 2;
            options.app = *v;
        } else if (a == "--directory") {
            auto v = value();
            if (!v) return 2;
            options.directory = mcpplibs::os::fs::from_utf8(*v);
        } else if (a == "--actor") {
            auto v = value();
            if (!v) return 2;
            options.actor = *v;
        } else {
            (void)mcpplibs::os::stdio::write_error(std::format("omui-mcp: no option `{}`\nusage: omui-mcp [--app NAME] [--directory DIR] [--actor KIND:NAME]\n", a));
            return 2;
        }
    }
    omui::mcp::Server server(std::move(options));
    omui::mcp::StdioTransport stdio;
    server.serve(stdio);
    return 0;
}
