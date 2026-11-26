// A site's configuration (oweb.config.ts, kind site@1): read to its options; stated routes are the site's, in their
// order; what is not a configuration says where and what a configuration has; a link that leads nowhere is not built.
import std;
import oweb;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {

std::filesystem::path data() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data"; }

std::filesystem::path scratch(std::string_view name) {
    const auto directory = std::filesystem::temp_directory_path() / "openmui-tests" / "oweb" / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return directory;
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path& file, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream(file, std::ios::binary) << text;
}

}  // namespace

static UTest cases{"oweb.config", [] {
    "oweb.config.ts: a site's title, address, languages, stated routes and budget, read from its evaluated snapshot"_test = [] {
        const auto options = oweb::configured(data() / "stated");
        expect(options.has_value()) << (options ? "" : options.error());
        if (!options) return;
        expect(options->title == "Stated" && options->url == "https://example.org/stated/" && options->languages == std::vector<std::string>{"en"} && options->readable == 50'000u);
        expect(options->routes.size() == 3u && options->routes[1].path == "/start/" && options->routes[1].source == "pages/guide/start.md" && options->routes[1].title == "Start here");
        // A site without the file is the defaults.
        const auto plain = oweb::configured(data() / "docs");
        expect(plain.has_value() && plain->title.empty() && plain->routes.empty() && plain->readable == 100'000u && plain->languages == std::vector<std::string>{"en"});
    };

    "stated routes: these pages and no others, in their order — the navigation's, the tables'; a picture found from where the route is served"_test = [] {
        const auto site = data() / "stated";
        const auto options = oweb::configured(site);
        expect(options.has_value());
        if (!options) return;
        const auto out = scratch("stated");
        const auto built = oweb::build(site, out, *options);
        expect(built.has_value()) << (built ? "" : built.error());
        if (!built) return;
        expect(built->routes.size() == 3u && built->routes[0].path == "/" && built->routes[1].path == "/start/" && built->routes[2].path == "/about/") << "as stated, not by address";
        expect(built->routes[1].title == "Start here" && built->routes[1].headings.front().text == "First steps") << "the navigation's name is the stated one; the page keeps its heading";
        expect(std::filesystem::exists(out / "start" / "index.html") && !std::filesystem::exists(out / "guide") && !std::filesystem::exists(out / "draft")) << "a page is served where it is stated";
        const std::string home = read(out / "index.html"), start = read(out / "start/index.html");
        const std::size_t first = home.find(">Start here</a>"), second = home.find(">About</a>");
        expect(first != std::string::npos && second != std::string::npos && first < second) << "the navigation in the stated order";
        expect(home.contains("href=\"start/#the-second-step\">the first steps</a>") && read(out / "index.md").contains("(start/index.md#the-second-step)")) << "a link follows the page to its stated address";
        expect(start.contains("src=\"../media/mark.svg\"") && std::filesystem::exists(out / "media" / "mark.svg")) << "a picture stands where it stands in pages/, found from the route";
        expect(start.contains("href=\"../\">the beginning</a>"));
        const std::string routes = read(out / "site.routes.xdef.json"), sitemap = read(out / "sitemap.xml"), agents = read(out / "llms.txt");
        expect(routes.find("\"/start/\"") < routes.find("\"/about/\"") && !routes.contains("draft") && sitemap.contains("<loc>https://example.org/stated/start/</loc>") &&
               agents.find("Start here") < agents.find("About"))
            << routes;
        expect(built->delivery.verdicts.size() == 1u && built->delivery.verdicts.front().budget.most == 50'000u) << "the budget is the configuration's";
        // A lost address under a site served below its host's top: the links are from the site's own address.
        const std::string lost = read(out / "404.html");
        expect(lost.contains("<base href=\"/stated/\">") && lost.contains("href=\"start/\">Start here</a>") && lost.contains("href=\"./\">Stated</a>") && !lost.contains("<nav data-omui-target=\"page/turn\"") &&
               !routes.contains("404") && !sitemap.contains("404") && !agents.contains("404"))
            << "no table lists it: " << lost;

        // The command reads the same configuration; what the request states stands over it.
        oweb::Sites sites;
        mlibs::agent::SubjectSession session({&sites});
        std::ostringstream listed, tight, errors;
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"routes.list", "--site", site.generic_string()}, listed, errors) == 0) << errors.str();
        expect(listed.str().find("\"/start/\"") < listed.str().find("\"/about/\"") && listed.str().contains("\"title\":\"Start here\"")) << listed.str();
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"site.build", "--site", site.generic_string(), "--out", scratch("stated-tight").generic_string(), "--readable", "100"},
                                          tight, errors) == 1);
        expect(tight.str().contains("over the budget of 100")) << tight.str();
    };

    "what is no configuration says where, and what a configuration has"_test = [] {
        const auto of = [](std::string_view site) { return oweb::options_from(std::format("{{\"$format\":\"site@1\",\"site\":{}}}\n", site), "oweb.config.ts"); };
        const auto says = [&](std::string_view site, std::string_view first, std::string_view second = {}) {
            const auto options = of(site);
            const bool said = !options && options.error().contains(first) && options.error().contains(second);
            if (!said) std::println("    {} → {}", site, options ? "read" : options.error());
            return said;
        };
        expect(of(R"({"title":"A","languages":["zh","en"]})").has_value());
        expect(says(R"({"titel":"A"})", "/site/titel", "it has: title, url, languages, routes, budgets")) << "a field mistyped is said, with the kind's";
        expect(says(R"({"url":"example.org"})", "/site/url", "https://"));
        expect(says(R"({"languages":[]})", "/site/languages"));
        expect(says(R"({"languages":["en","en"]})", "/site/languages/1", "twice"));
        expect(says(R"({"routes":[{"path":"start","source":"pages/start.md"}]})", "/site/routes/0/path", "begins and ends with `/`"));
        expect(says(R"({"routes":[{"path":"/a/","source":"../outside.md"}]})", "/site/routes/0/source", "inside the site's directory"));
        expect(says(R"({"routes":[{"path":"/a/","source":"pages/a.md"},{"path":"/a/","source":"pages/b.md"}]})", "/site/routes/1/path", "stated twice"));
        expect(says(R"({"routes":[{"path":"/a/","source":"pages/a.md","nav":true}]})", "/site/routes/0/nav", "it has: path, source, title"));
        expect(says(R"({"budgets":{"readable":-1}})", "/site/budgets/readable", "number of bytes"));
        const auto other = oweb::options_from("{\"$format\":\"routes@1\",\"routes\":[]}\n", "oweb.config.ts");
        expect(!other.has_value() && other.error().contains("a routes snapshot, not a site one")) << (other ? "" : other.error());

        // The file itself: TypeScript that does not evaluate, a default export of another kind, a stated page that is not there.
        const auto site = scratch("config-wrong");
        write(site / "pages/index.md", "# Home\n");
        write(site / "oweb.config.ts", "import { defineSite } from \"oweb/define\";\nexport default defineSite({ title: missing });\n");
        const auto unevaluated = oweb::configured(site);
        expect(!unevaluated.has_value() && unevaluated.error().contains("oweb.config.ts") && unevaluated.error().contains("missing")) << (unevaluated ? "" : unevaluated.error());
        write(site / "oweb.config.ts", "import { defineSite } from \"oweb/define\";\nexport default defineSite({ routes: [{ path: \"/\", source: \"pages/index.md\" }, { path: \"/gone/\", source: \"pages/gone.md\" }] });\n");
        const auto stated = oweb::configured(site);
        expect(stated.has_value());
        if (stated) {
            const auto routes = oweb::collect(site, *stated);
            expect(!routes.has_value() && routes.error().contains("`/gone/`") && routes.error().contains("pages/gone.md") && routes.error().contains("oweb.config.ts")) << (routes ? "" : routes.error());
        }
    };

    "a link inside the site that leads nowhere: the page, the link and why, each said; nothing is written"_test = [] {
        const auto site = scratch("links-wrong");
        write(site / "pages/index.md", "# Home\n\nSee [the guide](guide.md#setup), [a draft](draft.md), [a place](guide.md#nowhere), [marked](guide.md#marked) and ![a picture](media/none.png).\n\n"
                                       "Out of the site: [elsewhere](https://example.org/x.md), [mail](mailto:a@example.org), [here](#home).\n");
        write(site / "pages/guide.md", "# Guide\n\n## Setup\n\n<a id=\"marked\"></a>\n\nBack [home](index.md).\n");
        const auto out = scratch("links-wrong-out");
        const auto built = oweb::build(site, out);
        expect(!built.has_value());
        if (built) return;
        const std::string& why = built.error();
        expect(why.contains("3 links inside the site lead nowhere") && why.contains("nothing was written")) << why;
        expect(why.contains("pages/index.md links to draft.md, which is no page of the site") && why.contains("guide.md#nowhere: pages/guide.md has no heading of the anchor `nowhere`") &&
               why.contains("pages/index.md names media/none.png, which is not there"))
            << why;
        expect(!why.contains("#setup") && !why.contains("#marked") && !why.contains("example.org")) << "a heading's anchor, a place the page's markup names, and what is out of the site are no problems: " << why;
        expect(std::filesystem::is_empty(out)) << "nothing was written";
        // One page read alone says the same of itself.
        const auto routes = oweb::collect(site);
        expect(routes.has_value());
        if (routes) {
            const auto home = oweb::evaluate(site, *routes, routes->back().path == "/" ? routes->back() : routes->front());
            expect(home.has_value() && home->problems.size() == 3u);
        }
    };
}};
