// `omui-site` — openmui's website in a window, in a browser (dist-toy's page),
// or, with --prerender <index.html> [--lang en|zh], its places written into a
// packed page as static HTML (what a reader without JavaScript and a search
// engine read until the program runs). The page's hash route picks the place
// and the language: #/zh/features runs it with --zh --features.
import std;
import openmui.site;

int main(int argc, char** argv) {
    std::vector<char*> rest{argv[0]};
    std::string prerender, lang = "en", place = "home";
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--prerender" && i + 1 < argc) prerender = argv[++i];
        else if (a == "--lang" && i + 1 < argc) lang = argv[++i];
        else if (a == "--zh" || a == "--en") lang = a.substr(2);
        else if (std::ranges::any_of(omui::site::places(), [&](const omui::site::Place& p) { return a == std::format("--{}", p.id); })) place = a.substr(2);
        else rest.push_back(argv[i]);
    }
    if (!prerender.empty()) {
        std::ifstream in(prerender, std::ios::binary);
        const std::string page{std::istreambuf_iterator<char>(in), {}};
        const auto out = omui::site::inject(page, omui::site::static_html(lang), lang);
        if (!out) {
            std::println(std::cerr, "omui-site: {} is not a page dist-toy wrote", prerender);
            return 1;
        }
        std::ofstream(prerender, std::ios::binary) << *out;
        std::println("{{\"command\": \"prerender\", \"ok\": true, \"page\": \"{}\", \"bytes\": {}}}", prerender, out->size());
        return 0;
    }
    return omui::run(omui::site::app(place, lang), static_cast<int>(rest.size()), rest.data());
}
