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
        expect(built->routes.size() == 3u && built->delivery.files.size() == 12u) << built->delivery.files.size();

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
        const std::string routes = read(out / "site.routes.xdef.json");
        expect(routes.starts_with("{\"$format\":\"routes@1\"") && routes.contains("\"path\":\"/guide/start/\"") && routes.contains("\"markdown\":\"/guide/start/index.md\"") &&
               routes.contains("\"anchor\":\"what-is-here\"") && routes.contains("\"languages\":[\"en\",\"zh\"]"))
            << routes;
        const std::string list = read(out / "llms.txt");
        expect(list.starts_with("# Field notes\n") && list.contains("- [Getting started](https://example.org/notes/guide/start/index.md)") && list.contains("- [田野笔记](https://example.org/notes/zh/index.md)")) << list;
        const std::string sitemap = read(out / "sitemap.xml");
        expect(sitemap.contains("<loc>https://example.org/notes/</loc>") && sitemap.contains("<loc>https://example.org/notes/guide/start/</loc>")) << sitemap;
    };

    "a site with a look: each page written as its frame looks, the frame's faces the site's; the same document, read back"_test = [] {
        const auto out = scratch("docs-look"), plain_out = scratch("docs-plain");
        const auto built = oweb::build(data() / "docs", out, {.languages = {"en", "zh"}, .look = true});
        const auto plain = oweb::build(data() / "docs", plain_out, {.languages = {"en", "zh"}});
        expect(built.has_value() && plain.has_value()) << (built ? "" : built.error());
        if (!built || !plain) return;
        const std::string home = read(out / "index.html"), deep = read(out / "guide/start/index.html");
        // Its sheet is the look's, not the default one; every element says its node's layout and text.
        expect(home.contains("[data-omui-look] *{box-sizing:border-box;") && !home.contains("max-width:46rem")) << "the look's sheet";
        expect(home.contains("<div data-omui-target=\"app\" data-omui-look=\"") && home.contains("font-family:'omui-inter',system-ui,sans-serif;") && home.contains("max-width:760px;")) << home.substr(0, 3000);
        expect(home.contains("@media (prefers-color-scheme:dark){[data-omui-look]{--o0:")) << "light and dark, one look";
        // The faces: the site's own files, from wherever a page is served.
        expect(home.contains("src:url('assets/omui-inter.ttf')") && deep.contains("src:url('../../assets/omui-inter.ttf')")) << "a face's address from each page";
        // Cut to what the site shows: a fraction of the whole faces (Inter 876,576 B, Noto Sans SC 4,151,468 B).
        const auto inter = std::filesystem::file_size(out / "assets/omui-inter.ttf"), han = std::filesystem::file_size(out / "assets/omui-notosanssc.ttf");
        expect(inter > 20'000u && inter < 500'000u && han > 2'000u && han < 400'000u) << "the faces' files: " << inter << " and " << han;
        std::println("    a site's faces cut to what it shows: Inter {} B, Noto Sans SC {} B", inter, han);
        expect(std::ranges::any_of(built->delivery.files, [](const auto& file) { return file.path == "assets/omui-inter.ttf"; })) << "counted in the delivery";
        expect(!home.contains("<script")) << "the page reads with no script";
        // The same document as the page without a look: its HTML read back is the same.
        for (const std::string_view route : {"", "guide/start/", "zh/"}) {
            const std::string looked = md::to_html(md::from_html(document_of(read(out / route / "index.html"))));
            expect(looked == md::to_html(md::from_html(document_of(read(plain_out / route / "index.html"))))) << "/" << route << ":\n" << looked;
            expect(read(out / route / "index.md") == read(plain_out / route / "index.md")) << "/" << route << " as Markdown";
        }
        // A route's bytes before it reads are still within the budget.
        for (const auto& verdict : built->delivery.verdicts) expect(verdict.within) << verdict.budget.name << ": " << verdict.found << " in " << verdict.where;
    };

    "a site with an engine: each page made from the world's page — still its document, the program's files served once, each route saying when it takes it"_test = [] {
        // A world as dist-toy packs one (omui-gen dist-toy --world): a page waiting for its document, the program beside it.
        const auto world = scratch("world");
        const auto put = [&](const std::string& name, std::string_view bytes) {
            std::filesystem::create_directories((world / name).parent_path());
            std::ofstream(world / name, std::ios::binary) << bytes;
        };
        put("index.html", "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<title>oweb</title>\n<!--omui:head-->\n</head>\n<body>\n"
                          "<div data-omui=\"static\">\n<!--omui:document-->\n</div>\n<canvas id=\"canvas\"></canvas>\n<script>\nvar omuiRoot = \"\";\nvar omuiTakes = \"eager\";\nvar omuiFaces = null;\n"
                          "var omuiLauncher = \"reader-0123.js\";\n</script>\n</body>\n</html>\n");
        put("reader-0123.js", "/* the launcher */");
        put("reader-4567.wasm", std::string("\0asm\1\0\0\0", 8));
        put("assets/89ab.ttf", "a face");
        const auto out = scratch("docs-engine"), looked_out = scratch("docs-engine-look");
        const auto built = oweb::build(data() / "docs", out, {.languages = {"en", "zh"}, .engine = world.string(), .load = "idle"});
        const auto looked = oweb::build(data() / "docs", looked_out, {.languages = {"en", "zh"}, .look = true});
        expect(built.has_value() && looked.has_value()) << (built ? "" : built.error());
        if (!built || !looked) return;
        const std::string home = read(out / "index.html"), deep = read(out / "guide/start/index.html"), chinese = read(out / "zh/index.html");
        // The world's page, this page's document in it: the title, the language, the head, the document.
        expect(home.starts_with("<!doctype html>\n<html lang=\"en\">") && chinese.contains("<html lang=\"zh\">") && deep.contains("<title>") && !deep.contains("<title>oweb</title>"));
        expect(home.contains("<meta name=\"generator\" content=\"oweb\">") && home.contains("<link rel=\"alternate\" type=\"text/markdown\" href=\"index.md\">") &&
               home.contains("[data-omui-look] *{box-sizing:border-box;") && home.contains("src:url('assets/omui-inter.ttf')"))
            << "what its head says more: where its Markdown is, its look's sheet, its faces";
        expect(home.contains("<div data-omui=\"static\">\n<div data-omui-target=\"app\" data-omui-look=\"") && home.contains("<canvas id=\"canvas\">")) << "its document over the canvas's place";
        expect(!home.contains("<!--omui:") && !home.contains("var omuiRoot = \"\";")) << "no place left unfilled";
        // The world's files once for the site, found from each page; each route says when it takes the program.
        expect(home.contains("var omuiRoot = \"engine/\";") && deep.contains("var omuiRoot = \"../../engine/\";") && chinese.contains("var omuiRoot = \"../engine/\";"));
        expect(home.contains("var omuiTakes = \"idle\";"));
        expect(home.contains("var omuiFaces = {\"omui-inter\":\"assets/omui-inter.ttf\"") && deep.contains("\"omui-inter\":\"../../assets/omui-inter.ttf\"") && !home.contains("var omuiFaces = null;"))
            << "the faces a page is set in, as the page finds them: the program's first";
        expect(home.contains("<link rel=\"search\" type=\"application/json\" href=\"site.search.xdef.json\">") && deep.contains("href=\"../../site.search.xdef.json\">") &&
               read(looked_out / "guide/start/index.html").contains("<link rel=\"search\" type=\"application/json\" href=\"../../site.search.xdef.json\">"))
            << "where the site's index is, from each page — with a program or without";
        expect(home.contains("data-omui-target=\"main/page/search/box\"") && chinese.contains("aria-label=\"搜索\" role=\"group\"") && !read(looked_out / "index.html").contains("main/page/search"))
            << "the search box where a program answers it, named in the page's language; none where nothing would";
        {
            // The box as tall as the program draws it (a line of its words, its padding, its border): a field holds
            // no words the page sets, and what stands under it must not move when the program takes the page.
            const std::size_t box = home.find("data-omui-target=\"main/page/search/box\"");
            const std::string_view tag = box == std::string::npos ? std::string_view() : std::string_view(home).substr(box, home.find('>', box) - box);
            const std::size_t stated = tag.rfind("min-height:");
            float tall = 0;
            if (stated != std::string_view::npos) std::from_chars(tag.data() + stated + 11, tag.data() + tag.size(), tall);
            expect(tall > 38 && tall < 42) << tag;
        }
        expect(read(out / "engine/reader-0123.js") == "/* the launcher */" && std::filesystem::file_size(out / "engine/reader-4567.wasm") == 8u &&
               read(out / "engine/assets/89ab.ttf") == "a face" && !std::filesystem::exists(out / "engine/index.html"))
            << "the program's files, not the page they waited in";
        expect(std::ranges::any_of(built->delivery.files, [](const auto& file) { return file.path == "engine/reader-4567.wasm"; })) << "counted in the delivery";
        {
            // The engine's share: the program (its launcher and its code, as sent) counted and held to what the
            // site states — nothing here; the faces counted beside it, the pages' cuts and the whole ones apart.
            const auto share = [&](std::string_view name) { return std::ranges::find(built->delivery.verdicts, name, [](const auto& verdict) { return std::string_view(verdict.budget.name); }); };
            const auto program = share("the program"), cuts = share("the pages' faces"), whole = share("the whole faces, on demand");
            expect(program != built->delivery.verdicts.end() && cuts != built->delivery.verdicts.end() && whole != built->delivery.verdicts.end());
            if (program == built->delivery.verdicts.end() || cuts == built->delivery.verdicts.end() || whole == built->delivery.verdicts.end()) return;
            const auto sent_of = [&](std::string_view path) { return std::ranges::find(built->delivery.files, path, &mlibs::delivery::File::path)->sent(); };
            expect(program->files == 2u && program->found == sent_of("engine/reader-0123.js") + sent_of("engine/reader-4567.wasm") && program->budget.most == 0u && program->within)
                << program->files << " " << program->found;
            expect(whole->files == 1u && whole->found == sent_of("engine/assets/89ab.ttf") && cuts->files >= 1u && cuts->found > 0u && cuts->within && whole->within);
            expect(built->delivery.within() && read(out / "site.delivery.xdef.json").contains("\"name\":\"the program\"")) << "counted is not held";
            // Stated, it is held: a byte under what the program sends is over, and the answer names the share.
            const auto tight = oweb::build(data() / "docs", scratch("docs-engine-tight"), {.languages = {"en", "zh"}, .program = program->found - 1, .engine = world.string(), .load = "idle"});
            expect(tight.has_value() && !tight->delivery.within());
            if (tight) {
                const auto held = std::ranges::find(tight->delivery.verdicts, std::string_view("the program"), [](const auto& verdict) { return std::string_view(verdict.budget.name); });
                expect(held != tight->delivery.verdicts.end() && !held->within && held->found == program->found && held->budget.most == program->found - 1);
            }
            expect(!read(looked_out / "site.delivery.xdef.json").contains("the program"))
                << "a site without a program has no such share";
        }
        expect(read(out / "site.routes.xdef.json").contains("\"load\":\"idle\"") && !read(looked_out / "site.routes.xdef.json").contains("\"load\"")) << "a route's load, where the site has a program";
        // The page scrolls in a view of the window's size (a canvas is not scrolled by its window); the page for a
        // lost address takes no program.
        expect(home.contains("overflow-y:auto") || home.contains("overflow:auto") || home.contains("overflow-y:scroll")) << "a view that scrolls";
        expect(!read(out / "404.html").contains("omuiRoot") && read(out / "404.html").contains("<base href=")) << "a lost address reads, and leads home";
        // The same document as the page without a program, and the same Markdown.
        for (const std::string_view route : {"", "guide/start/", "zh/"}) {
            expect(md::to_html(md::from_html(document_of(read(out / route / "index.html")))) == md::to_html(md::from_html(document_of(read(looked_out / route / "index.html"))))) << "/" << route;
            expect(read(out / route / "index.md") == read(looked_out / route / "index.md")) << "/" << route << " as Markdown";
        }
        for (const auto& verdict : built->delivery.verdicts) expect(verdict.within) << verdict.budget.name << ": " << verdict.found << " in " << verdict.where;
        // A route says its own; what is no world's page, and what is no time to take a program, are said.
        const auto stated = oweb::collect(data() / "docs", {.languages = {"en", "zh"}, .routes = {{.path = "/", .source = "pages/index.md", .load = "interaction"},
                                                                                                    {.path = "/start/", .source = "pages/guide/start.md"}}, .load = "idle"});
        expect(stated.has_value() && stated->size() == 2u && (*stated)[0].load == "interaction" && (*stated)[1].load == "idle") << (stated ? "" : stated.error());
        const auto never = oweb::collect(data() / "docs", {.load = "soon"});
        expect(!never && never.error().contains("`eager`, `idle` or on `interaction`")) << (never ? "" : never.error());
        put("index.html", "<!doctype html><html lang=\"en\"><head><title>x</title></head><body></body></html>");
        const auto refused = oweb::build(data() / "docs", scratch("docs-no-world"), {.languages = {"en", "zh"}, .engine = world.string()});
        expect(!refused && refused.error().contains("no world's page") && refused.error().contains("dist-toy --world")) << (refused ? "" : refused.error());
    };

    "a site of one page: one route, no navigation, no sitemap without an address"_test = [] {
        const auto out = scratch("page");
        const auto built = oweb::build(data() / "page", out);
        expect(built.has_value()) << (built ? "" : built.error());
        if (!built) return;
        expect(built->routes.size() == 1u && built->routes[0].path == "/" && built->delivery.files.size() == 7u) << built->delivery.files.size();
        const std::string html = read(out / "index.html");
        expect(!html.contains("<nav")) << "one page has nowhere else to go, and one section has nothing to choose between";
        // A lost address: a page that says so and leads home, from the site's own address (none stated: the host's top).
        const std::string lost = read(out / "404.html");
        expect(lost.contains("<base href=\"/\">") && lost.contains("<meta name=\"robots\" content=\"noindex\">") && lost.contains("<h1") && lost.contains(">No such page</h1>") &&
               lost.contains("href=\"./\">One page</a>") && !lost.contains("rel=\"alternate\""))
            << lost;
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
        const std::string index = read(out / "site.search.xdef.json");
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

    "the delivery manifest: every file as stored and as a static host sends it, a route's bytes before it reads held to its budget"_test = [] {
        namespace delivery = mlibs::delivery;
        const auto out = scratch("delivery");
        const auto built = oweb::build(data() / "docs", out, {.languages = {"en", "zh"}});
        expect(built.has_value()) << (built ? "" : built.error());
        if (!built) return;
        // The manifest's numbers are those of what is on disk: the directory measured again says the same of each.
        const auto on_disk = delivery::measure(out);
        expect(on_disk.has_value() && on_disk->files.size() == built->delivery.files.size() + 1) << "the manifest is written too, and is not of itself";
        if (!on_disk) return;
        for (const delivery::File& file : built->delivery.files) {
            const auto same = std::ranges::find(on_disk->files, file.path, &delivery::File::path);
            expect(same != on_disk->files.end() && same->bytes == file.bytes && same->gzip == file.gzip && same->compressed == file.compressed) << file.path;
        }
        expect(std::filesystem::exists(out / ".nojekyll")) << "a static host serves the directory as it is";
        // A route reads once its index.html has come: that file as the host sends it, each route held.
        expect(built->delivery.verdicts.size() == 1u && built->delivery.within());
        const delivery::Verdict& readable = built->delivery.verdicts.front();
        expect(readable.files == 3u && readable.budget.most == 100'000u && readable.found > 0u && readable.found < 100'000u && readable.where.ends_with("index.html")) << readable.found;
        const auto largest = std::ranges::find(built->delivery.files, readable.where, &delivery::File::path);
        expect(largest != built->delivery.files.end() && largest->compressed && largest->sent() == readable.found && largest->gzip < largest->bytes);
        std::println("    a route readable: at most {} B sent ({}), of {} B stored; the site {} B stored, {} B sent", readable.found, readable.where, largest->bytes, built->delivery.bytes,
                     built->delivery.sent);
        const std::string manifest = read(out / "site.delivery.xdef.json");
        expect(manifest.starts_with("{\"$format\":\"delivery@1\"") && manifest.contains("\"name\":\"a route readable\"") && manifest.contains("\"within\":true") &&
               manifest.contains("{\"path\":\"guide/start/index.html\",") && !manifest.contains("site.delivery.xdef.json"))
            << manifest;

        // Held to less than its largest route sends: written all the same, and the verdict names the route.
        const auto tight = oweb::build(data() / "docs", scratch("delivery-tight"), {.languages = {"en", "zh"}, .readable = readable.found - 1});
        expect(tight.has_value() && !tight->delivery.within() && tight->delivery.verdicts.front().where == readable.where && tight->delivery.verdicts.front().found == readable.found);
        const auto unheld = oweb::build(data() / "docs", scratch("delivery-unheld"), {.languages = {"en", "zh"}, .readable = 0});
        expect(unheld.has_value() && unheld->delivery.verdicts.empty() && unheld->delivery.within());

        // The command: a site over its budget is a build that failed, saying where and what to do.
        oweb::Sites sites;
        mlibs::agent::SubjectSession session({&sites});
        const std::string site = (data() / "docs").generic_string(), to = scratch("delivery-command").generic_string();
        std::ostringstream over, within, not_a_number, errors;
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"site.build", "--site", site, "--out", to, "--languages", "en,zh", "--readable", "300"}, over, errors) == 1);
        expect(over.str().contains("\"ok\":false") && over.str().contains("over the budget of 300") && over.str().contains(readable.where) && over.str().contains("--readable") &&
               over.str().contains("\"within\":false"))
            << over.str();
        expect(std::filesystem::exists(std::filesystem::path(to) / "site.delivery.xdef.json")) << "written all the same: the manifest says where";
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"site.build", "--site", site, "--out", to, "--languages", "en,zh"}, within, errors) == 0) << errors.str();
        expect(within.str().contains("\"ok\":true") && within.str().contains("\"sent\":") && within.str().contains("\"most\":100000") && within.str().contains("\"within\":true")) << within.str();
        expect(mlibs::agent::command_line(session, std::vector<std::string_view>{"site.build", "--site", site, "--out", to, "--readable", "small"}, not_a_number, errors) != 0);
        expect(not_a_number.str().contains("--readable is a number of bytes")) << not_a_number.str();
    };

    "a route that is an application's page: served as built with its files, and in the site's tables by what its own document says"_test = [] {
        // An application's page as its build writes it (omui build --web): its document for a reader without the
        // program, the program's files beside it.
        const auto app = scratch("adopted-app"), site = scratch("adopted-site"), out = scratch("adopted-out");
        const auto put = [](const std::filesystem::path& file, std::string_view text) {
            std::filesystem::create_directories(file.parent_path());
            std::ofstream(file, std::ios::binary) << text;
        };
        const std::string built =
            "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<title>Tide &amp; moon</title>\n</head>\n<body>\n"
            "<div data-omui=\"static\">\n<div data-omui-target=\"app\">\n  <h1 data-omui-target=\"main/line\">One tree, every reading.</h1>\n"
            "  <button data-omui-target=\"main/live\" type=\"button\">Live Design</button>\n"
            "  <p data-omui-target=\"main/words\">Start with <a data-omui-target=\"main/words/start\" href=\"start/\">the first steps</a>, see <a href=\"start/#install\">the install</a>, "
            "the <a href=\"media/mark.svg\">mark</a> or <a href=\"https://example.org/\">elsewhere</a>.</p>\n  <h2 data-omui-target=\"main/tides\">Tides</h2>\n"
            "  <p data-omui-target=\"main/tides/words\">A spring tide follows the new moon.</p>\n</div>\n</div>\n"
            "<script>window.addEventListener('omui-load', function (e) {});</script>\n<canvas id=\"canvas\"></canvas>\n<script src=\"tide-1a2b.js\"></script>\n</body>\n</html>\n";
        put(app / "index.html", built);
        put(app / "tide-1a2b.js", "var tide = 1;\n");
        put(app / "tide-1a2b.wasm", std::string("\0asm\1\0\0\0", 8));
        put(app / "media" / "mark.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\"/>\n");
        put(site / "pages" / "start.md", "# First steps\n\n## Install\n\nBack to [the front](/), to [its tides](/#tides).\n");
        oweb::SiteOptions options{.url = "https://example.org/notes/", .routes = {{.path = "/", .page = app.generic_string()}, {.path = "/start/", .source = "pages/start.md"}}};

        const auto routes = oweb::collect(site, options);
        expect(fatal(routes.has_value())) << (routes ? "" : routes.error());
        const oweb::Route& front = routes->front();
        expect(front.title == "Tide & moon") << "named as it names itself: " << front.title;
        expect(front.headings.size() == 2 && front.headings.front().text == "One tree, every reading." && front.headings.back().anchor == "tides");
        const auto text = oweb::source_text(site, front);
        expect(text.has_value() && text->contains("# One tree, every reading.") && text->contains("[the first steps](start/)") && text->contains("Live Design") && !text->contains("<"))
            << "its document, as Markdown: " << text.value_or(text.error());

        const auto made = oweb::build(site, out, options);
        expect(fatal(made.has_value())) << (made ? "" : made.error());
        // The page as built, saying in its head what every route says; its files beside it.
        const std::string html = read(out / "index.html");
        const std::size_t head = html.find("</head>");
        expect(html.contains("<canvas id=\"canvas\"></canvas>") && html.contains("href=\"start/\">the first steps</a>") && html.size() > built.size()) << "the page its build wrote";
        expect(html.find("<link rel=\"alternate\" type=\"text/markdown\" href=\"index.md\">") < head && html.find("<link rel=\"search\" type=\"application/json\" href=\"site.search.xdef.json\">") < head) << html;
        expect(read(out / "tide-1a2b.js") == "var tide = 1;\n" && read(out / "tide-1a2b.wasm").size() == 8 && std::filesystem::exists(out / "media" / "mark.svg"));
        // Its Markdown: the document, a link to another route the Markdown's, a file of its own and an address elsewhere as they were.
        const std::string markdown = read(out / "index.md");
        expect(markdown.contains("# One tree, every reading.") && markdown.contains("[the first steps](start/index.md)") && markdown.contains("(start/index.md#install)") &&
               markdown.contains("(media/mark.svg)") && markdown.contains("(https://example.org/)") && markdown.contains("## Tides"))
            << markdown;
        // The tables: one more route, which is an application's; the search finds its words; the other page leads to it.
        const std::string table = read(out / "site.routes.xdef.json");
        expect(table.contains("\"source\":\"adopted-app/index.html\"") && !table.contains(app.generic_string())) << "a published table names no path of the machine that built it: " << table;
        expect(table.contains("\"path\":\"/\"") && table.contains("\"application\":true") && table.contains("\"title\":\"Tide & moon\"") && table.contains("\"anchor\":\"tides\"")) << table;
        expect(read(out / "llms.txt").contains("- [Tide & moon](https://example.org/notes/index.md)") && read(out / "sitemap.xml").contains("<loc>https://example.org/notes/</loc>"));
        const std::string index = read(out / "site.search.xdef.json");
        expect(index.contains("A spring tide follows the new moon.") && index.contains("\"anchor\":\"tides\"")) << index;
        const std::string start = read(out / "start" / "index.html");
        expect(start.contains(">Tide &amp; moon</a>") && start.contains("href=\"../\">the front</a>") && start.contains("href=\"../#tides\">its tides</a>")) << start;
        expect(read(out / "404.html").contains("Tide &amp; moon")) << "the page for a lost address leads to it";
        expect(std::ranges::contains(made->delivery.files, std::string("tide-1a2b.wasm"), &mlibs::delivery::File::path)) << "its files are the site's, in the delivery manifest";

        // A link of the page that leads to no place of the site is said, as any page's; so is an anchor its page has not.
        put(app / "index.html", [&] {
            std::string wrong = built;
            wrong.replace(wrong.find("href=\"start/\""), 13, "href=\"begin/\"");
            return wrong;
        }());
        const auto nowhere = oweb::build(site, scratch("adopted-nowhere"), options);
        expect(!nowhere.has_value() && nowhere.error().contains("links to /begin/, which is no route of the site")) << (nowhere ? "" : nowhere.error());
        put(app / "index.html", built);
        put(site / "pages" / "start.md", "# First steps\n\n## Install\n\nTo [its moons](/#moons).\n");
        const auto anchorless = oweb::build(site, scratch("adopted-anchor"), options);
        expect(!anchorless.has_value() && anchorless.error().contains("no heading of the anchor `moons`")) << (anchorless ? "" : anchorless.error());
        put(site / "pages" / "start.md", "# First steps\n\n## Install\n\nBack to [the front](/).\n");

        // A page with no document says nothing to the tables: not adopted. A directory with no page: build it first.
        put(app / "index.html", "<!doctype html>\n<html lang=\"en\">\n<head>\n<title>Tide</title>\n</head>\n<body>\n<canvas id=\"canvas\"></canvas>\n</body>\n</html>\n");
        const auto silent = oweb::collect(site, options);
        expect(!silent.has_value() && silent.error().contains("holds no document") && silent.error().contains("omui build --web")) << (silent ? "" : silent.error());
        options.routes.front().page = (app / "none").generic_string();
        const auto unbuilt = oweb::collect(site, options);
        expect(!unbuilt.has_value() && unbuilt.error().contains("has no index.html") && unbuilt.error().contains("omui build --web")) << (unbuilt ? "" : unbuilt.error());

        // Two files at one address — a file of the page's and a file beside the site's pages — are said, not one over the other.
        options.routes.front().page = app.generic_string();
        put(app / "index.html", built);
        put(site / "pages" / "media" / "mark.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n");
        const auto twice = oweb::build(site, scratch("adopted-twice"), options);
        expect(!twice.has_value() && twice.error().contains("two files of the site are `media/mark.svg`")) << (twice ? "" : twice.error());
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
