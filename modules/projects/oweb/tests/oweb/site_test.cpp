// A site built: its routes, each route's two writings held to its source's document, links between pages in each
// writing's own form, the tables made from the routes, and the command's operations (0.1.0 plan ST8, ST2).
import std;
import oweb;
import mlibs.markdown;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace md = mlibs::markdown;

namespace {

std::filesystem::path data() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data"; }

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// A scratch directory of this test's own, emptied.
std::filesystem::path scratch(std::string_view name) {
    const auto directory = std::filesystem::temp_directory_path() / "openmui-tests" / "oweb" / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return directory;
}

// A document's HTML with a line's end inside a paragraph read as the space the tree has.
std::string html_of(std::string_view source) {
    md::Document document = md::parse(source);
    const auto unbroken = [](this auto const& self, md::Block& block) -> void {
        std::erase_if(block.content.marks, [](const md::Mark& mark) { return mark.op == md::Op::soft_break; });
        for (md::Block& child : block.children) self(child);
    };
    unbroken(document.root);
    return md::to_html(document);
}

// A source with its links to other pages as a writing addresses them.
std::string addressed(std::string source, std::initializer_list<std::pair<std::string_view, std::string_view>> links) {
    for (const auto& [from, to] : links)
        for (std::size_t at = source.find(from); at != std::string::npos; at = source.find(from, at + to.size())) source.replace(at, from.size(), to);
    return source;
}

// The part of a page's HTML that is its document.
std::string_view document_of(std::string_view html) {
    const std::size_t from = html.find("role=\"document\"");
    const std::size_t begin = html.rfind('<', from), end = html.rfind("</main>");
    return from == std::string_view::npos || end == std::string_view::npos ? std::string_view{} : html.substr(begin, end - begin);
}

}  // namespace

static UTest cases{"oweb.site", [] {
    "addresses: a page's route, and the way from one route to another"_test = [] {
        expect(oweb::relative("/", "/guide/start/") == "guide/start/");
        expect(oweb::relative("/guide/start/", "/") == "../../");
        expect(oweb::relative("/guide/start/", "/zh/") == "../../zh/");
        expect(oweb::relative("/zh/", "/zh/") == "./");
        const auto routes = oweb::collect(data() / "docs", {.languages = {"en", "zh"}});
        expect(routes.has_value()) << (routes ? "" : routes.error());
        if (!routes) return;
        expect(routes->size() == 3u);
        expect((*routes)[0].path == "/" && (*routes)[0].source == "pages/index.md" && (*routes)[0].title == "Field notes" && (*routes)[0].language == "en");
        expect((*routes)[1].path == "/guide/start/" && (*routes)[1].title == "Getting started");
        expect((*routes)[2].path == "/zh/" && (*routes)[2].language == "zh" && (*routes)[2].title == "田野笔记");
        expect((*routes)[0].headings.size() == 2u && (*routes)[0].headings[1].anchor == "what-is-here" && (*routes)[0].headings[1].level == 2);
        const auto none = oweb::collect(data() / "nowhere");
        expect(!none && none.error().contains("no pages/")) << "a directory that is no site says what a site is";
    };

    "a site of many pages: every route written as HTML and as Markdown, each read back to its source's document"_test = [] {
        const auto out = scratch("docs");
        const auto built = oweb::build(data() / "docs", out, {.languages = {"en", "zh"}, .url = "https://example.org/notes/"});
        expect(built.has_value()) << (built ? "" : built.error());
        if (!built) return;
        expect(built->routes.size() == 3u && built->files.size() == 10u) << built->files.size();

        // The home page: its links are each writing's own addresses.
        const std::string home = read(out / "index.html"), home_md = read(out / "index.md"), source = read(data() / "docs/pages/index.md");
        expect(home.starts_with("<!doctype html>\n<html lang=\"en\">") && home.contains("<title>Field notes</title>") &&
               home.contains("<link rel=\"alternate\" type=\"text/markdown\" href=\"index.md\">"));
        expect(home.contains("href=\"guide/start/\">Getting started</a>") && home.contains("href=\"zh/\">中文</a>")) << "in HTML a page is its route's directory";
        expect(home_md.contains("[Getting started](guide/start/index.md)") && home_md.contains("[中文](zh/index.md)")) << "in Markdown, its index.md: " << home_md;
        expect(home.contains("<nav data-omui-target=") && home.contains("aria-label=\"Field notes\"")) << "the navigation the routes give, a landmark";
        expect(home.contains("<main data-omui-target=")) << "the document in the main landmark";
        expect(home.contains("<!-- code: tests/oweb/data/docs/pages/index.md -->") && home_md.contains("<!-- code: tests/oweb/data/docs/pages/index.md -->"))
            << "a comment of the source is in both writings, shown by neither";
        expect(home.contains("<td") && home.contains("align=\"right\">the first window</td>")) << "a table's cells";
        expect(!home.contains("<script")) << "the page reads with no script";

        // Each route: the HTML read back and the Markdown parsed again are the source's document (links as addressed).
        struct Case {
            std::string route, source;
            std::initializer_list<std::pair<std::string_view, std::string_view>> html_links, markdown_links;
        };
        for (const Case& one : {Case{"", "pages/index.md", {{"(guide/start.md)", "(guide/start/)"}, {"(zh/index.md)", "(zh/)"}},
                                     {{"(guide/start.md)", "(guide/start/index.md)"}, {"(zh/index.md)", "(zh/index.md)"}}},
                                Case{"guide/start/", "pages/guide/start.md", {{"(../index.md#what-is-here)", "(../../#what-is-here)"}, {"(../zh/index.md)", "(../../zh/)"}},
                                     {{"(../index.md#what-is-here)", "(../../index.md#what-is-here)"}, {"(../zh/index.md)", "(../../zh/index.md)"}}},
                                Case{"zh/", "pages/zh/index.md", {{"(../index.md)", "(../)"}}, {{"(../index.md)", "(../index.md)"}}}}) {
            const std::string page_source = read(data() / "docs" / one.source);
            const std::string html = read(out / one.route / "index.html"), markdown = read(out / one.route / "index.md");
            expect(!html.empty() && !markdown.empty()) << one.route;
            const std::string from_html = md::to_html(md::from_html(document_of(html)));
            expect(from_html == html_of(addressed(page_source, one.html_links))) << std::format("/{} as HTML:\n{}\n--- its source's\n{}", one.route, from_html, html_of(addressed(page_source, one.html_links)));
            expect(html_of(markdown) == html_of(addressed(page_source, one.markdown_links))) << std::format("/{} as Markdown:\n{}", one.route, markdown);
        }
        expect(read(out / "zh/index.html").contains("<html lang=\"zh\">")) << "a language's pages say their language";
        expect(read(out / "guide/start/index.html").contains("<code class=\"language-sh\">xlings install omui\nomui new hello\n</code>")) << "code keeps its lines and says its language";

        // The tables, made from the routes.
        const std::string routes = read(out / "routes.xdef.json");
        expect(routes.starts_with("{\"$format\":\"routes@1\"") && routes.contains("\"path\":\"/guide/start/\"") && routes.contains("\"markdown\":\"/guide/start/index.md\"") &&
               routes.contains("\"anchor\":\"what-is-here\"") && routes.contains("\"languages\":[\"en\",\"zh\"]"))
            << routes;
        const std::string list = read(out / "llms.txt");
        expect(list.starts_with("# Field notes\n") && list.contains("- [Getting started](https://example.org/notes/guide/start/index.md)") && list.contains("- [田野笔记](https://example.org/notes/zh/index.md)")) << list;
        const std::string sitemap = read(out / "sitemap.xml");
        expect(sitemap.contains("<loc>https://example.org/notes/</loc>") && sitemap.contains("<loc>https://example.org/notes/guide/start/</loc>")) << sitemap;
    };

    "a site of one page: one route, no navigation, no sitemap without an address"_test = [] {
        const auto out = scratch("page");
        const auto built = oweb::build(data() / "page", out);
        expect(built.has_value()) << (built ? "" : built.error());
        if (!built) return;
        expect(built->routes.size() == 1u && built->routes[0].path == "/" && built->files.size() == 5u) << built->files.size();
        const std::string html = read(out / "index.html");
        expect(!html.contains("<nav")) << "one page has nowhere else to go";
        expect(html.contains("href=\"https://example.com/source\" title=\"Where it lives\">the source</a>")) << "a link out keeps its address and its title";
        expect(!std::filesystem::exists(out / "sitemap.xml") && std::filesystem::exists(out / "llms.txt") && std::filesystem::exists(out / "index.md"));
        expect(read(out / "index.md").contains("[the source](https://example.com/source \"Where it lives\")"));
    };

    "search: one index made from the tree, the sections that hold every word, best first; text without spaces by its characters"_test = [] {
        const auto sections = oweb::sections_of(data() / "docs", {.languages = {"en", "zh"}});
        expect(sections.has_value()) << (sections ? "" : sections.error());
        if (!sections) return;
        expect(sections->size() == 4u) << sections->size();
        const auto under = std::ranges::find(*sections, std::string("what-is-here"), &oweb::Section::anchor);
        expect(under != sections->end() && under->route == "/" && under->title == "What is here" && under->trail == std::vector<std::string>{"Field notes"} &&
               under->text.contains("the first window"))
            << "a heading and what stands under it, the headings it is under its trail";
        expect((*sections)[0].text.contains("read by people") && !(*sections)[0].text.contains("code: tests")) << "a comment is no text to find";

        const auto window = oweb::search(*sections, "first window");
        expect(window.size() == 1u && window[0].section.anchor == "what-is-here" && window[0].excerpt == "the first window") << "every word, and the line that says so";
        const auto install = oweb::search(*sections, "Install OMUI");
        expect(!install.empty() && install[0].section.route == "/guide/start/" && install[0].excerpt.contains("Install")) << "without case";
        const auto command = oweb::search(*sections, "xlings");
        expect(command.size() == 1u && command[0].excerpt.contains("xlings install omui")) << "code is text to find";
        expect(oweb::search(*sections, "start").front().section.route == "/guide/start/") << "a word in a title before one in the text";
        const auto begun = oweb::search(*sections, "getting sta");
        expect(begun.size() == 3u && begun[0].section.route == "/guide/start/") << "a word's beginning; the page it titles before the two sections that name it";
        const auto chinese = oweb::search(*sections, "搜索引擎");
        expect(chinese.size() == 1u && chinese[0].section.route == "/zh/") << "characters in a row";
        expect(oweb::search(*sections, "笔记").front().section.title == "田野笔记");
        expect(oweb::search(*sections, "nothing-of-the-kind").empty() && oweb::search(*sections, "  ").empty());

        const auto out = scratch("search");
        const auto built = oweb::build(data() / "docs", out, {.languages = {"en", "zh"}});
        expect(built.has_value());
        const std::string index = read(out / "search.xdef.json");
        expect(index.starts_with("{\"$format\":\"search@1\"") && index.contains("\"anchor\":\"what-is-here\"") && index.contains("\"trail\":[\"Field notes\"]")) << index;

        oweb::Sites sites;
        mlibs::agent::SubjectSession session({&sites});
        std::ostringstream found, errors;
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"docs.search", "--site", (data() / "docs").generic_string(), "--query", "first window", "--languages", "en,zh"}, found,
                                          errors) == 0)
            << errors.str();
        expect(found.str().contains("\"anchor\":\"what-is-here\"") && found.str().contains("\"markdown\":\"/index.md\"") && found.str().contains("\"excerpt\":\"the first window\""))
            << "the same sections to an agent, each with where its page's Markdown is: " << found.str();
    };

    "the subject `oweb`: a site's routes, one page read as an agent fetches it, the site built; what is not a request says why"_test = [] {
        oweb::Sites sites;
        mlibs::agent::SubjectSession session({&sites});
        const std::string site = (data() / "docs").generic_string();
        std::ostringstream listed, errors;
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"routes.list", "--site", site, "--languages", "en,zh"}, listed, errors) == 0) << errors.str();
        expect(listed.str().contains("\"path\":\"/guide/start/\"") && listed.str().contains("\"language\":\"zh\"")) << listed.str();
        std::ostringstream page;
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"page.read", "--site", site, "--route", "/guide/start/", "--languages", "en,zh"}, page, errors) == 0) << errors.str();
        expect(page.str().contains("# Getting started") && page.str().contains("../../zh/index.md")) << "the page as Markdown, its links the Markdown's: " << page.str();
        std::ostringstream missing;
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"page.read", "--site", site, "--route", "/nowhere/"}, missing, errors) == 1);
        expect(missing.str().contains("no route `/nowhere/`") && missing.str().contains("/guide/start/")) << "a route that is none names the site's: " << missing.str();
        const auto out = scratch("command");
        std::ostringstream made;
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"site.build", "--site", site, "--out", out.generic_string(), "--languages", "en,zh"}, made, errors) == 0) << errors.str();
        expect(made.str().contains("\"path\":\"guide/start/index.md\"") && std::filesystem::exists(out / "zh" / "index.html")) << made.str();
        std::ostringstream wrong;
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"site.build", "--site", site}, wrong, errors) != 0) << "a build with nowhere to write is no request";
    };
}};
