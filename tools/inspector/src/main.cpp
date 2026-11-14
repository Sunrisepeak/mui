// omui-inspector — inspect a running openmui application.
//
//   omui-inspector [--app NAME] [--inspect-dir DIR] [omui::run options: --size, --dark, --headless …]
//
// NAME is an application id as `omui agent ls` lists them; without it, the
// first application that listens (other than the Inspector).
import std;
import omui;
import omui.inspector;
import mcpplibs.os;
import mcpplibs.cmdline;

int main(int argc, char** argv) {
    auto line = mcpplibs::cmdline::App("omui-inspector")
                    .description("Inspect a running openmui application")
                    .option(mcpplibs::cmdline::Option("app").takes_value().value_name("NAME").help("an application id as `omui agent ls` lists them (default: the first that listens)"))
                    .option(mcpplibs::cmdline::Option("inspect-dir").takes_value().value_name("DIR").help("where the endpoints are"))
                    .option(mcpplibs::cmdline::Option("motion").takes_value().value_name("FILE").help("the motion workbench over an animation document"))
                    .option(mcpplibs::cmdline::Option("cellular").help("the cellular workbench"))
                    .option(mcpplibs::cmdline::Option("cellular-zh").help("the cellular workbench, in Chinese"))
                    .option(mcpplibs::cmdline::Option("cellular-batch").takes_value().value_name("FILE").help("run a batch of cellular steps, headless (JSON out)"))
                    .option(mcpplibs::cmdline::Option("out").takes_value().value_name("DIR").help("where --cellular-batch writes"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    const auto& args = command->args;
    std::string argument_error;
    const auto batch_file = args.value("cellular-batch");
    if (batch_file && batch_file->empty()) argument_error = "--cellular-batch needs a nonempty path";
    if (!batch_file && args.value("out")) argument_error = "--out needs --cellular-batch";
    if (!argument_error.empty()) {
        std::cout << omui::Value{}.set("ok", false).set("error", argument_error).json() << '\n';
        return 2;
    }
    if (batch_file)
        return omui::inspector::cellular::batch(mcpplibs::os::fs::from_utf8(*batch_file), mcpplibs::os::fs::from_utf8(args.value("out").value_or("")), std::cout);
    if (auto motion_file = args.value("motion")) {
        const auto session = std::make_shared<omui::inspector::motion::Session>();
        return omui::run(omui::inspector::motion::app(session, mcpplibs::os::fs::from_utf8(*motion_file)), command->options);
    }
    if (args.is_flag_set("cellular") || args.is_flag_set("cellular-zh")) {
        const auto session = std::make_shared<omui::inspector::cellular::Session>();
        return omui::run(omui::inspector::cellular::app(session, args.is_flag_set("cellular-zh")), command->options);
    }
    omui::inspector::Options options;
    if (auto app = args.value("app")) options.app = *app;
    if (auto dir = args.value("inspect-dir")) options.directory = mcpplibs::os::fs::from_utf8(*dir);
    return omui::run(omui::inspector::app(std::move(options)), command->options);
}
