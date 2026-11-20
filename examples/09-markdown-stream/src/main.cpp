// Example 09: Markdown as it streams in. `md-stream` reads standard input
// (pipe a model's answer into it); `md-stream --demo` plays a built-in answer;
// `--rate N` its pieces per second; `--no-fade`; the rest is openmui's
// (--dark, --headless --png out.png, --serve …).
import std;
import mcpplibs.cmdline;
import omui;
import stream;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("md-stream")
        .description("Markdown as it streams in: standard input, or a built-in answer")
        .option(Option("demo").help("play a built-in answer"))
        .option(Option("rate").takes_value().value_name("N").help("pieces a second (default 30)"))
        .option(Option("no-fade").help("text appears without fading in"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    stream::Model model;
    const bool demo = command->args.is_flag_set("demo");
    const double rate = std::max(1.0, command->args.value_as<double>("rate").value_or(30.0));
    if (command->args.is_flag_set("no-fade")) model.fade = false;
    omui::AppSpec spec = stream::app(model);
    spec.setup = [&model, demo, rate](omui::Runtime& rt) {
        model.runtime = &rt;
        if (demo) stream::play(model, stream::demo_tokens(), rate);
        else stream::read_stdin(model);
    };
    return omui::run(std::move(spec), command->options);
}
