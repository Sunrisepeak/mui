// `omui-site` — openmui's website in a window, in a browser (dist-toy's page),
// or, with --prerender <index.html> [--lang en|zh], its places written into a
// packed page as static HTML (what a reader without JavaScript and a search
// engine read until the program runs); with --agent-guide <dir>, what an agent
// needs to operate it (agent.md), as every openmui application writes it. The page's hash route picks the place
// and the language: #/zh/features runs it with --zh --features.
// --first-frame <PNG> [--route PATH] [--lang en|zh] [--width W --height H]
// [--root EXPLICIT-ID] [--time SECONDS] [--reduced-motion] exports production pixels
// and the actual semantic geometry beside them; it does not open a window.
import std;
import omui.site;
import mcpplibs.os;

int main(int argc, char** argv) {
    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
    if constexpr (omui::site::native_build_tools) if (std::ranges::any_of(arguments, [](std::string_view value) { return value.starts_with("--first-frame"); })) {
        const auto options = omui::site::parse_first_frame_options(arguments);
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
    std::vector<char*> rest{argv[0]};
    std::string prerender, opening_assets, lang = "en", place = "home", route;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--prerender" && i + 1 < argc) prerender = argv[++i];
        else if (a == "--opening-assets" && i + 1 < argc) opening_assets = argv[++i];
        else if (a == "--route" && i + 1 < argc) route = argv[++i];
        else if (a == "--lang" && i + 1 < argc) lang = argv[++i];
        else if (a == "--zh" || a == "--en") lang = a.substr(2);
        else if (std::ranges::any_of(omui::site::places(), [&](const omui::site::Place& p) { return a == std::format("--{}", p.id); }))
            place = a.substr(2);
        else rest.push_back(argv[i]);
    }
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
    return omui::run(omui::site::app(route.empty() ? place : route, lang, route), static_cast<int>(rest.size()), rest.data());
}
