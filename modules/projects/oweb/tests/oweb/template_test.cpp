// The minimal documents (the `docs` sample, 0.1.0 plan D-274): built, every route's two writings are its source's
// document; its reference is the catalog's; the questions asked of it are answered by its sections.
import std;
import oweb;
import mlibs.markdown;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace md = mlibs::markdown;

namespace {

std::filesystem::path package() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path(); }

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// A document's HTML as the tree can say it (a line's end inside a paragraph the space it reads as) and without where
// its links lead: a writing addresses another page in its own form, which site_test holds.
std::string shape_of(std::string_view source) {
    md::Document document = md::parse(source);
    const auto unbroken = [](this auto const& self, md::Block& block) -> void {
        std::erase_if(block.content.marks, [](const md::Mark& mark) { return mark.op == md::Op::soft_break; });
        for (md::Block& child : block.children) self(child);
    };
    unbroken(document.root);
    for (md::Link& link : document.links) link.href = "…";
    return md::to_html(document);
}

std::string_view document_of(std::string_view html) {
    const std::size_t from = html.find("role=\"document\"");
    const std::size_t begin = html.rfind('<', from), end = html.rfind("</main>");
    return from == std::string_view::npos || end == std::string_view::npos ? std::string_view{} : html.substr(begin, end - begin);
}

}  // namespace

static UTest cases{"oweb.template", [] {
    "the minimal documents: every route's HTML read back and its Markdown parsed again are its source's document"_test = [] {
        const auto site = package() / "templates" / "docs";
        const auto out = std::filesystem::temp_directory_path() / "openmui-tests" / "oweb" / "template";
        std::error_code error;
        std::filesystem::remove_all(out, error);
        // Its own configuration: the title, the address, the languages, the pages in a reader's order.
        const auto options = oweb::configured(site);
        expect(options.has_value()) << (options ? "" : options.error());
        if (!options) return;
        const auto built = oweb::build(site, out, *options);
        expect(built.has_value()) << (built ? "" : built.error());
        if (!built) return;
        expect(built->routes.size() == 7u && built->routes[1].path == "/start/" && built->routes[4].path == "/reference/ops/") << built->routes.size();
        std::size_t html_bytes = 0, markdown_bytes = 0;
        for (const oweb::Route& route : built->routes) {
            const std::string source = oweb::source_text(site, route).value_or("");
            const std::string html = read(out / route.path.substr(1) / "index.html"), markdown = read(out / route.path.substr(1) / "index.md");
            html_bytes += html.size(), markdown_bytes += markdown.size();
            md::Document read_back = md::from_html(document_of(html));
            for (md::Link& link : read_back.links) link.href = "…";
            expect(md::to_html(read_back) == shape_of(source)) << std::format("{} as HTML:\n{}\n--- its source's\n{}", route.path, md::to_html(read_back), shape_of(source));
            expect(shape_of(markdown) == shape_of(source)) << std::format("{} as Markdown:\n{}", route.path, markdown);
        }
        std::println("    the minimal documents: {} routes, {} B of HTML, {} B of Markdown", built->routes.size(), html_bytes, markdown_bytes);
        // Each route's bytes before it reads, against the budget (0.1.0 plan ST6): the largest named.
        expect(built->delivery.within() && built->delivery.verdicts.size() == 1u && built->delivery.verdicts.front().files == built->routes.size());
        std::println("    a route readable: at most {} B sent ({}) of a budget of {}; the site {} B stored, {} B sent", built->delivery.verdicts.front().found,
                     built->delivery.verdicts.front().where, built->delivery.verdicts.front().budget.most, built->delivery.bytes, built->delivery.sent);
        // The page's own parts, made from the routes table: its sections as links to their anchors, the page before
        // and the page after among its language's, in the stated order.
        for (std::size_t at = 0; at < built->routes.size(); ++at) {
            const oweb::Route& route = built->routes[at];
            const std::string html = read(out / route.path.substr(1) / "index.html");
            std::size_t sections = 0;
            for (const oweb::Heading& heading : route.headings)
                if (heading.level >= 2 && heading.level <= 3 && !heading.anchor.empty()) ++sections;
            const bool chinese = route.language == "zh";
            expect(html.contains(chinese ? "<nav data-omui-target=\"page/toc\" aria-label=\"本页目录\">" : "<nav data-omui-target=\"page/toc\" aria-label=\"On this page\">") == (sections >= 2))
                << route.path << " has " << sections << " sections";
            if (sections >= 2)
                for (const oweb::Heading& heading : route.headings)
                    if (heading.level >= 2 && heading.level <= 3)
                        expect(html.contains(std::format("href=\"#{}\">", heading.anchor)) && html.contains(std::format(" id=\"{}\"", heading.anchor))) << route.path << "#" << heading.anchor;
            const oweb::Route* before = at > 0 && built->routes[at - 1].language == route.language ? &built->routes[at - 1] : nullptr;
            const oweb::Route* after = at + 1 < built->routes.size() && built->routes[at + 1].language == route.language ? &built->routes[at + 1] : nullptr;
            expect(html.contains("data-omui-target=\"page/turn/before\"") == (before != nullptr) && html.contains("data-omui-target=\"page/turn/after\"") == (after != nullptr)) << route.path;
            if (before) expect(html.contains(std::format("href=\"{}\">← ", oweb::relative(route.path, before->path)))) << route.path;
            if (after) expect(html.contains(std::format("href=\"{}\">", oweb::relative(route.path, after->path)))) << route.path;
        }
        expect(read(out / "start/index.html").contains("href=\"../\">← The minimal documents</a>") && read(out / "zh/start/index.html").contains("aria-label=\"上一页与下一页\""));
        // The other language: this page in it where the site has it, that language's first page where not.
        const std::string start_en = read(out / "start/index.html"), start_zh = read(out / "zh/start/index.html"), ui_en = read(out / "ui/index.html");
        expect(start_en.contains("<nav data-omui-target=\"page/nav\"") && start_en.contains("href=\"../zh/start/\">开始</a>")) << "from a page to the same page in Chinese";
        expect(start_zh.contains("href=\"../../start/\">Start</a>")) << "and back: the first language has no part in its addresses";
        expect(ui_en.contains("href=\"../zh/\">最小文档集</a>")) << "a page the other language has not: that language's first page";
        expect(start_en.contains("<link rel=\"alternate\" hreflang=\"zh\" href=\"../zh/start/\">") && start_en.contains("<link rel=\"alternate\" hreflang=\"en\" href=\"./\">") &&
               start_zh.contains("<link rel=\"alternate\" hreflang=\"en\" href=\"../../start/\">") && !ui_en.contains("hreflang"))
            << "a search engine is told of a page's languages where it has more than one";

        // What the set is there to carry, each found in what was built.
        const std::string home = read(out / "index.html"), ui = read(out / "ui/index.html"), agent = read(out / "agent/index.html"), start = read(out / "start/index.html");
        expect(home.contains("<h3") && home.contains("<em>small</em>") && home.contains("<strong>complete</strong>") && home.contains("<del>") && home.contains("<br>") &&
               home.contains("title=\"The operations of the oweb command\""))
            << "three levels of headings, the inline formats, a hard break, a link's title";
        expect(home.contains("<ol") && home.contains("<ul") && home.contains("href=\"agent/#what-an-agent-may-do\"")) << "ordered and nested lists; a link to an anchor of another page";
        expect(ui.contains("class=\"language-ts\"") && ui.contains("class=\"language-cpp\"") && ui.contains("class=\"language-json\"") && ui.contains("<pre data-omui-target=") &&
               ui.contains("<code>column\n  text"))
            << "code in three languages and one block with none";
        expect(ui.contains("<img") && ui.contains("src=\"../media/mark.svg\"") && ui.contains("title=\"omui's mark\"") && ui.contains("<blockquote")) << "a picture, found from its route; a quotation";
        expect(std::filesystem::exists(out / "media" / "mark.svg")) << "what stands beside the pages is copied";
        expect(agent.contains("align=\"right\"") && agent.contains("type=\"checkbox\" disabled checked") && agent.contains("type=\"checkbox\" disabled aria-label=")) << "a table's alignment; a task list, done and open";
        expect(start.contains("<!-- code: examples/13-live-design/design/start.ts#counter -->")) << "a comment of the source is kept, and shown by nothing";
        expect(read(out / "zh/index.html").contains("<html lang=\"zh\">") && read(out / "zh/start/index.md").contains("# 开始"));
    };

    "its reference is the catalog's, generated at the build: no page of its own, the catalog's own reference its text; pages link to it by its address"_test = [] {
        const auto site = package() / "templates" / "docs";
        const auto options = oweb::configured(site);
        expect(options.has_value());
        if (!options) return;
        const auto routes = oweb::collect(site, *options);
        expect(routes.has_value()) << (routes ? "" : routes.error());
        if (!routes) return;
        const auto reference = std::ranges::find(*routes, "/reference/ops/", &oweb::Route::path);
        expect(reference != routes->end() && reference->reference == "../../oweb.ops.xdef.ts" && reference->title == "oweb operations" && !reference->headings.empty());
        if (reference == routes->end()) return;
        expect(!std::filesystem::exists(site / "pages" / "reference")) << "no copy to go stale";
        const auto text = oweb::source_text(site, *reference);
        expect(text.has_value() && *text == read(package() / "src" / "catalog" / "oweb.ops.md")) << "what the catalog's generator writes, read from the catalog now";
        const auto home = oweb::evaluate(site, *routes, routes->front(), *options);
        expect(home.has_value() && home->problems.empty() && home->html.contains("href=\"reference/ops/\" title=\"The operations of the oweb command\">oweb</a>") &&
               home->markdown.contains("(reference/ops/index.md \"The operations of the oweb command\")"))
            << "a link by the site's own address, written as each writing's";
        const auto chinese = std::ranges::find(*routes, "/zh/", &oweb::Route::path);
        const auto page = oweb::evaluate(site, *routes, *chinese, *options);
        expect(page.has_value() && page->html.contains("href=\"../reference/ops/\">oweb</a>"));
        expect(oweb::routes_json(*routes, *options).contains("\"generated\":true"));
    };

    "the questions asked of it: each answered by the section that holds the answer"_test = [] {
        const auto sections = oweb::sections_of(package() / "templates" / "docs", oweb::configured(package() / "templates" / "docs").value_or(oweb::SiteOptions{}));
        expect(sections.has_value());
        if (!sections) return;
        struct Question {
            std::string_view words, route, anchor, says;
        };
        for (const Question& question : {Question{"install mcpp", "/start/", "1-tools", "xlings"},
                                         Question{"irreversible action", "/agent/", "what-an-agent-may-do", "answer that confirms"},
                                         Question{"explicit id", "/ui/", "identity", "never depends on the text"},
                                         Question{"headless", "/start/", "3-the-everyday-commands", "development mode"},
                                         Question{"第一次构建", "/zh/start/", "1-工具", "xlings"},
                                         Question{"docs.search", "/reference/ops/", "oweb-operations", "every word"}}) {
            const auto found = oweb::search(*sections, question.words, 3);
            expect(!found.empty() && found[0].section.route == question.route && found[0].section.anchor == question.anchor)
                << std::format("`{}`: {}", question.words, found.empty() ? std::string("nothing") : std::format("{}#{}", found[0].section.route, found[0].section.anchor));
            if (!found.empty()) expect(found[0].section.text.contains(question.says)) << question.words;
        }
    };
}};
