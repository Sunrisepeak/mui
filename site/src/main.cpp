// `omui-site` — openmui's website in a window, in a browser (dist-toy's page),
// or, with --prerender <index.html> [--lang en|zh], its places written into a
// packed page as static HTML (what a reader without JavaScript and a search
// engine read until the program runs); with --agent-guide <dir>, what an agent
// needs to operate it (agent.md), as every openmui application writes it. The page's hash route picks the place
// and the language: #/zh/features runs it with --zh --features.
// --first-frame <PNG> [--route PATH] [--lang en|zh] [--width W --height H]
// [--root EXPLICIT-ID] [--time SECONDS] [--reduced-motion] exports production pixels
// and the actual semantic geometry beside them; it does not open a window.
// `omui-site --help` lists every switch.
import std;
import mcpplibs.cmdline;
import omui;
import omui.site;
import mcpplibs.os;

int main(int argc, char** argv) {
    // One command line (mcpplibs.cmdline): the site's switches, the first-frame
    // export's, and openmui's (RunOptions; its --lang, the person's languages,
    // picks the page's: zh first, Chinese).
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Option;
    auto line = App("omui-site")
        .description("openmui's website: in a window, prerendered into a packed page, or a first frame exported")
        .option(Option("prerender").takes_value().value_name("INDEX.HTML").help("write the places into this packed page as static HTML"))
        .option(Option("opening-assets").takes_value().value_name("FILE").help("the opening's asset catalogue, for --prerender"))
        .option(Option("zh").help("in Chinese"))
        .option(Option("en").help("in English"));
    // A switch a place: --features opens at features.
    for (const omui::site::Place& place : omui::site::places()) (void)line.option(Option(place.id).help(std::format("open at {}", place.id)));
    omui::site::declare_first_frame(line, false);
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    const auto& args = command->args;
    if constexpr (omui::site::native_build_tools) if (args.value("first-frame")) {
        const auto options = omui::site::first_frame_options(args);
        if (!options) {
            std::println("{}", omui::Value{}.set("command", "first-frame").set("ok", false).set("error", options.error()).json());
            return 2;
        }
        const auto report = omui::site::export_first_frame(*options);
        if (!report) {
            std::println("{}", omui::Value{}.set("command", "first-frame").set("ok", false).set("error", report.error()).json());
            return 1;
        }
        std::println("{}", report->json());
        return 0;
    }
    std::string lang = "en", place = "home";
    if (!command->options.languages.empty() && command->options.languages.front().starts_with("zh")) lang = "zh";
    if (args.is_flag_set("zh")) lang = "zh";
    if (args.is_flag_set("en")) lang = "en";
    for (const omui::site::Place& candidate : omui::site::places())
        if (args.is_flag_set(candidate.id)) place = std::string(candidate.id);
    const std::string route = args.value("route").value_or("");
    const std::string prerender = args.value("prerender").value_or("");
    const std::string opening_assets = args.value("opening-assets").value_or("");
    if constexpr (omui::site::native_build_tools) if (!prerender.empty()) {
        std::ifstream in(prerender, std::ios::binary);
        const std::string page{std::istreambuf_iterator<char>(in), {}};
        std::string catalogue;
        if (!opening_assets.empty()) {
            const auto read = mcpplibs::os::fs::read(mcpplibs::os::fs::from_utf8(opening_assets));
            if (!read) {
                std::println(std::cerr, "omui-site: opening assets: {}", read.error().message());
                return 1;
            }
            catalogue = *read;
        }
        const auto out = omui::site::inject(page, omui::site::static_html(lang), lang, catalogue);
        if (!out) {
            std::println(std::cerr, "omui-site: {} is not a page dist-toy wrote", prerender);
            return 1;
        }
        std::ofstream(prerender, std::ios::binary) << *out;
        std::println("{{\"command\": \"prerender\", \"ok\": true, \"page\": \"{}\", \"bytes\": {}}}", prerender, out->size());
        return 0;
    }
    return omui::run(omui::site::app(route.empty() ? place : route, lang, route), command->options);
}
