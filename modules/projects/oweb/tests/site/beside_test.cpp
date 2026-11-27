// A page's navigation is its neighbourhood, not the site (oweb's design §4.11): the pages in its own directory
// and in each directory above it, and every other directory at those levels by its entry — its own page, or its
// first. Held on a site with directories, a second language and a directory whose own page comes after its pages;
// and a page's navigation does not grow with the pages of directories it is not in.
import std;
import oweb;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {

std::filesystem::path scratch(std::string_view name) {
    const auto directory = std::filesystem::temp_directory_path() / "openmui-tests" / "oweb" / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return directory;
}

void page(const std::filesystem::path& site, std::string_view file, std::string_view title) {
    const std::filesystem::path at = site / "pages" / file;
    std::filesystem::create_directories(at.parent_path());
    std::ofstream(at, std::ios::binary) << "# " << title << "\n\nWords of " << title << ".\n";
}

// Where the page's navigation leads, in its order: each link's address, from the site's top.
std::vector<std::string> named(const std::filesystem::path& site, std::span<const oweb::Route> routes, std::string_view path, const oweb::SiteOptions& options) {
    const auto route = std::ranges::find(routes, path, &oweb::Route::path);
    if (route == routes.end()) return {"no such route"};
    const auto written = oweb::evaluate(site, routes, *route, options);
    if (!written) return {written.error()};
    std::vector<std::string> out;
    const std::string_view html = written->html;
    const std::size_t nav = html.find("<nav data-omui-target=\"main/page/nav\"");
    if (nav == std::string_view::npos) return out;
    const std::string_view inside = html.substr(nav, html.find("</nav>", nav) - nav);
    for (std::size_t at = 0; (at = inside.find("href=\"", at)) != std::string_view::npos;) {
        at += 6;
        const std::string_view relative = inside.substr(at, inside.find('"', at) - at);
        // From the page's own address to the site's: as a browser resolves it.
        std::filesystem::path whole = (std::filesystem::path(std::string(path)) / std::string(relative)).lexically_normal();
        std::string address = whole.generic_string();
        if (!address.ends_with('/')) address += '/';
        out.push_back(address == "//" ? std::string("/") : address);
    }
    return out;
}

using Named = std::vector<std::string>;

}  // namespace

static UTest cases{"oweb.beside", [] {
    "a page's navigation names its neighbourhood: its directory's pages, those above, each other directory by its entry — the routes' order kept"_test = [] {
        const auto site = scratch("beside");
        page(site, "index.md", "Home");
        page(site, "start.md", "Start");
        page(site, "guide/index.md", "Guide");
        page(site, "guide/one.md", "One");
        page(site, "guide/two.md", "Two");
        page(site, "guide/deep/x.md", "X");
        page(site, "guide/deep/y.md", "Y");
        page(site, "ref/alpha.md", "Alpha");
        page(site, "ref/beta.md", "Beta");
        page(site, "zh/index.md", "首页");
        page(site, "zh/guide/one.md", "一");
        oweb::SiteOptions options;
        options.languages = {"en", "zh"};
        const auto routes = oweb::collect(site, options);
        expect(fatal(routes.has_value())) << (routes ? "" : routes.error());
        // Where the other language's link leads is the last of each list: this page there, or its first page.
        const auto english = [&](std::string_view path) {
            Named all = named(site, *routes, path, options);
            if (!all.empty()) all.pop_back();
            std::ranges::sort(all);
            return all;
        };
        const auto sorted = [](Named names) {
            std::ranges::sort(names);
            return names;
        };
        // At the top: the top's pages, and each directory by its entry — the guide's own page, ref's first.
        expect(english("/") == sorted({"/", "/start/", "/guide/", "/ref/alpha/"})) << english("/");
        expect(english("/start/") == english("/"));
        // In a directory: its pages, the top's, a directory under it by its first page, the others' entries.
        expect(english("/guide/one/") == sorted({"/", "/start/", "/guide/", "/guide/one/", "/guide/two/", "/guide/deep/x/", "/ref/alpha/"})) << english("/guide/one/");
        expect(english("/guide/") == sorted({"/", "/start/", "/guide/", "/guide/one/", "/guide/two/", "/guide/deep/x/", "/ref/alpha/"})) << "a directory's own page stands at the level above, and names its pages' entry";
        // Two levels down: its own pages, the level above's, the top's, and ref's entry.
        expect(english("/guide/deep/y/") == sorted({"/", "/start/", "/guide/", "/guide/one/", "/guide/two/", "/guide/deep/x/", "/guide/deep/y/", "/ref/alpha/"})) << english("/guide/deep/y/");
        expect(english("/ref/beta/") == sorted({"/", "/start/", "/guide/", "/ref/alpha/", "/ref/beta/"})) << english("/ref/beta/");
        // The other language's pages are their own neighbourhood; and each page leads to itself in the other one.
        const Named chinese = named(site, *routes, "/zh/guide/one/", options);
        expect(chinese == Named{"/zh/", "/zh/guide/one/", "/guide/one/"}) << chinese;
        expect(named(site, *routes, "/guide/one/", options).back() == "/zh/guide/one/" && named(site, *routes, "/start/", options).back() == "/zh/");
        // The order is the routes': a page's links read as the site's table does.
        const Named ordered = named(site, *routes, "/guide/one/", options);
        std::vector<std::string> table;
        for (const oweb::Route& route : *routes)
            if (std::ranges::contains(ordered, route.path) && route.language == "en") table.push_back(route.path);
        expect(Named(ordered.begin(), ordered.end() - 1) == table) << ordered;
    };

    "a page's navigation does not grow with the pages of directories it is not in"_test = [] {
        const auto links = [](int directories, int pages) {
            const auto site = scratch(std::format("beside-{}x{}", directories, pages));
            page(site, "index.md", "Home");
            for (int directory = 0; directory < directories; ++directory)
                for (int index = 0; index < pages; ++index) page(site, std::format("s{:02}/p{:03}.md", directory, index), std::format("Page {} of {}", index, directory));
            const auto routes = oweb::collect(site);
            if (!routes) return std::pair<std::size_t, std::size_t>{0, 0};
            return std::pair{routes->size(), named(site, *routes, "/s01/p002/", {}).size()};
        };
        const auto small = links(3, 4), wide = links(3, 40), many = links(12, 4);
        expect(small == std::pair<std::size_t, std::size_t>{13, 7}) << small.first << " " << small.second << ": home, its directory's four, the two others' entries";
        expect(wide.first == 121u && wide.second == 43u) << wide.second << ": its own directory's forty, not the hundred and twenty";
        expect(many.first == 49u && many.second == 16u) << many.second << ": one entry a directory";
    };
}};
