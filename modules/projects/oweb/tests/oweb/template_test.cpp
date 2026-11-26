// The minimal documents (the `docs` sample, 0.1.0 plan D-274): built, every route's two writings are its source's
// document; its reference is the catalog's; the questions asked of it are answered by its sections.
import std;
import oweb;
import oweb.shell;
import omui.core;
import omui.view;
import omui.semantic;
import omui.host.headless;
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

// What a browser's page says of a written page (omui.host.web's page::projection): each written node's id, its
// element's label and address, and the words of an element that holds only words.
std::string projection_of(std::string_view html) {
    const auto plain = [](std::string_view text) {
        std::string out;
        for (std::size_t at = 0; at < text.size(); ++at) {
            bool named = false;
            for (const auto& [entity, character] : {std::pair<std::string_view, char>{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#39;", '\''}})
                if (text.substr(at).starts_with(entity)) {
                    out += character, at += entity.size() - 1, named = true;
                    break;
                }
            if (!named) out += text[at];
        }
        return out;
    };
    const auto quoted = [](std::string_view text) {
        std::string out = "\"";
        for (const char character : text) {
            if (character == '"' || character == '\\') out += '\\';
            out += character;
        }
        return out + "\"";
    };
    std::string nodes;
    const std::string_view mark = " data-omui-target=\"";
    for (std::size_t at = html.find(mark); at != std::string_view::npos; at = html.find(mark, at + 1)) {
        const std::size_t open = html.rfind('<', at), close = html.find('>', at);
        const std::string_view tag = html.substr(open, close - open);
        const auto stated = [&](std::string_view name) -> std::string {
            const std::string key = std::format(" {}=\"", name);
            const std::size_t from = tag.find(key);
            return from == std::string_view::npos ? std::string() : plain(tag.substr(from + key.size(), tag.find('"', from + key.size()) - from - key.size()));
        };
        const std::size_t next = html.find('<', close);
        const std::string_view words = html.substr(close + 1, next - close - 1);
        const bool only_words = html.substr(next).starts_with("</");
        nodes += std::format("{}{{\"id\":{},\"label\":{},\"href\":{},\"text\":{}}}", nodes.empty() ? "" : ",", quoted(stated("data-omui-target")), quoted(stated("aria-label")),
                             quoted(stated("href")), quoted(only_words ? plain(words) : std::string()));
    }
    return std::format("{{\"nodes\":[{}]}}", nodes);
}

std::string_view body_of(std::string_view html) {
    const std::size_t from = html.find("<body>\n"), to = html.rfind("</body>");
    return from == std::string_view::npos || to == std::string_view::npos ? std::string_view{} : html.substr(from + 7, to - from - 7);
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
        expect(built->routes.size() == 8u && built->routes[1].path == "/start/" && built->routes[4].path == "/live/" && built->routes[5].path == "/reference/ops/") << built->routes.size();
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
            expect(html.contains(chinese ? "<nav data-omui-target=\"main/page/toc\" aria-label=\"本页目录\">" : "<nav data-omui-target=\"main/page/toc\" aria-label=\"On this page\">") == (sections >= 2))
                << route.path << " has " << sections << " sections";
            if (sections >= 2)
                for (const oweb::Heading& heading : route.headings)
                    if (heading.level >= 2 && heading.level <= 3)
                        expect(html.contains(std::format("href=\"#{}\">", heading.anchor)) && html.contains(std::format(" id=\"{}\"", heading.anchor))) << route.path << "#" << heading.anchor;
            const oweb::Route* before = at > 0 && built->routes[at - 1].language == route.language ? &built->routes[at - 1] : nullptr;
            const oweb::Route* after = at + 1 < built->routes.size() && built->routes[at + 1].language == route.language ? &built->routes[at + 1] : nullptr;
            expect(html.contains("data-omui-target=\"main/page/turn/before\"") == (before != nullptr) && html.contains("data-omui-target=\"main/page/turn/after\"") == (after != nullptr)) << route.path;
            if (before) expect(html.contains(std::format("href=\"{}\">← ", oweb::relative(route.path, before->path)))) << route.path;
            if (after) expect(html.contains(std::format("href=\"{}\">", oweb::relative(route.path, after->path)))) << route.path;
        }
        expect(read(out / "start/index.html").contains("href=\"../\">← The minimal documents</a>") && read(out / "zh/start/index.html").contains("aria-label=\"上一页与下一页\""));
        // The other language: this page in it where the site has it, that language's first page where not.
        const std::string start_en = read(out / "start/index.html"), start_zh = read(out / "zh/start/index.html"), ui_en = read(out / "ui/index.html");
        expect(start_en.contains("<nav data-omui-target=\"main/page/nav\"") && start_en.contains("href=\"../zh/start/\">开始</a>")) << "from a page to the same page in Chinese";
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

    "a page read back from its own HTML is the page as it was written: the program a page loads needs nothing else"_test = [] {
        using namespace omui;
        const auto site = package() / "templates" / "docs";
        auto options = oweb::configured(site);
        expect(options.has_value());
        if (!options) return;
        const auto routes = oweb::collect(site, *options);
        expect(routes.has_value()) << (routes ? "" : routes.error());
        if (!routes) return;
        std::size_t compared = 0;
        oweb::SiteOptions unstyled = *options;
        unstyled.look = false;
        for (const bool look : {false, true}) {
            options->look = look;
            for (const oweb::Route& route : *routes) {
                // The page with its look says the same as the page without: its styles are no part of what it is.
                if (look && route.path != "/ui/" && route.path != "/zh/start/") continue;
                const auto plain = oweb::evaluate(site, *routes, route, unstyled);
                const auto page = look ? oweb::evaluate(site, *routes, route, *options) : plain;
                expect(plain.has_value() && page.has_value()) << route.path;
                if (!plain || !page) continue;
                const auto parts = oweb::shell::read(projection_of(page->html), [&](std::string_view) { return std::string(document_of(page->html)); });
                expect(parts.has_value()) << route.path << ": " << (parts ? "" : parts.error());
                // A picture is as large as its file says, in the page as it looks: read as the frame was drawn.
                if (look && route.path == "/ui/") expect(page->html.contains("width:100px;height:auto;aspect-ratio:100/100;")) << "the mark's own size";
                if (!parts) continue;
                expect(parts->html && !parts->document.empty());
                Headless host({.level = HeadlessLevel::layout, .size = {960, 1200}});
                host.mount(oweb::shell::mounted_at, oweb::shell::page(*parts));
                host.settle();
                semantic::Snapshot again = host.tree().snapshot({});
                // As the builder writes it: without what a view adds to operate its document.
                std::unordered_set<std::string> left_out;
                for (const spec::SemanticNode& node : again.nodes)
                    if (node.role == Role::button && node.id.contains("/doc/") && !node.id.contains("/live/")) left_out.insert(node.id);
                for (spec::SemanticNode& node : again.nodes) std::erase_if(node.children, [&](const std::string& id) { return left_out.contains(id); });
                std::erase_if(again.nodes, [&](const spec::SemanticNode& node) { return left_out.contains(node.id); });
                const std::string written(body_of(plain->html)), read_back = semantic::static_html(again);
                expect(read_back == written) << std::format("{} ({}):\n{}\n--- as written\n{}", route.path, look ? "with its look" : "plain", read_back, written);
                ++compared;
            }
        }
        expect(compared == routes->size() + 2) << compared;
    };

    "the search box on a page: for the words typed it shows the sections the query answers, in its order, each led to; a late answer for other words is not shown"_test = [] {
        using namespace omui;
        namespace shell = oweb::shell;
        const auto site = package() / "templates" / "docs";
        const auto options = oweb::configured(site);
        expect(options.has_value());
        if (!options) return;
        const auto sections = oweb::sections_of(site, *options);
        expect(sections.has_value()) << (sections ? "" : sections.error());
        if (!sections) return;
        // The index a site serves, read back: the sections it was written from.
        const auto served = shell::sections_from(shell::search_json(*sections));
        expect(served.has_value() && served->size() == sections->size()) << (served ? "" : served.error());
        if (!served) return;
        expect(std::ranges::equal(*served, *sections, [](const shell::Section& one, const shell::Section& other) {
            return one.route == other.route && one.anchor == other.anchor && one.title == other.title && one.trail == other.trail && one.text == other.text;
        })) << "the index says every section as it is";
        expect(!shell::sections_from("{\"$format\":\"other@1\"}").has_value() && !shell::sections_from("not an index").has_value());

        // A link a reader presses in the page's document is handed to who follows it — a page of the site, a
        // section, an address elsewhere alike; in a page as written (no one to follow) it goes nowhere.
        {
            std::vector<std::string> followed;
            shell::Parts linked{.site = "Docs", .document = "# A page\n\nSee [the guide](../guide/), [below](#more) and [mcpp](https://example.org/mcpp).\n\n## More\n\nText.\n"};
            Headless page({.level = HeadlessLevel::layout, .size = {960, 900}});
            page.mount(shell::mounted_at, shell::page(std::move(linked), {.follow = [&](std::string href) { followed.push_back(std::move(href)); }}));
            page.settle();
            std::vector<std::string> links;
            for (const spec::SemanticNode& node : page.tree().snapshot({}).nodes)
                if (node.role == Role::link) links.push_back(node.id);
            expect(links.size() == 3u) << links.size();
            for (const std::string& id : links) expect(page.click(id)) << id;
            expect(followed == std::vector<std::string>{"../guide/", "#more", "https://example.org/mcpp"}) << followed.size();
        }
        // A page with the box, its finder answering later (an index is fetched): each asking kept until answered.
        std::vector<std::pair<std::string, std::function<void(std::vector<shell::Found>)>>> asked;
        shell::Parts parts{.site = "Docs", .document = "# A page\n\nIts text.\n", .search = "Search"};
        Headless host({.level = HeadlessLevel::layout, .size = {960, 900}});
        host.mount(shell::mounted_at, shell::page(std::move(parts), {.find = [&](std::string words, std::function<void(std::vector<shell::Found>)> answer) {
                                                                         asked.emplace_back(std::move(words), std::move(answer));
                                                                     }}));
        host.settle();
        const auto part = host.tree().node(std::format("{}/page/{}", shell::mounted_at, shell::search_part));
        expect(part && part->role == Role::group && part->name == "Search") << "the part by its own name";
        // The page as written says its search by name; read back from its own HTML the part is there, written again the same.
        {
            const std::string written = semantic::static_html(host.tree().snapshot({}));
            const auto again = shell::read(projection_of(written), [&](std::string_view) { return std::string("<h1>A page</h1>\n<p>Its text.</p>\n"); });
            expect(again.has_value() && again->search == "Search" && again->pages.empty()) << (again ? again->search : again.error()) << ": the search read back, its box no link of the site's";
            if (again) {
                Headless second({.level = HeadlessLevel::layout, .size = {960, 900}});
                second.mount(shell::mounted_at, shell::page(*again, {}));
                second.settle();
                expect(semantic::static_html(second.tree().snapshot({})) == written) << "the page as written, drawn from what it says";
            }
        }
        const std::string box_id = std::format("{}/page/{}/box", shell::mounted_at, shell::search_part), results = std::format("{}/page/{}/results", shell::mounted_at, shell::search_part);
        expect(host.tree().node(box_id) && host.tree().node(box_id)->role == Role::text_box && host.tree().node(box_id)->name == "Search") << "its box";
        const auto shown = [&] {
            std::vector<std::pair<std::string, std::string>> out;  // where each leads and what it is called
            for (std::size_t at = 0;; ++at) {
                const auto one = host.tree().node(std::format("{}/f{}/h{}", results, at, at));
                if (!one) break;
                const auto href = one->properties.get("href");
                out.emplace_back(href ? std::string(href->as_string()) : std::string(), one->name);
            }
            return out;
        };
        host.runtime().focus(host.runtime().find(box_id));
        std::size_t found_in_all = 0;
        for (const std::string_view words : {"takes over", "live example", "搜索", "ops reference", "agent"}) {
            const std::size_t before = asked.size();
            host.key(Key::a, Modifiers(primary_modifier()));
            host.type(words);
            host.settle();
            expect(asked.size() > before && asked.back().first == words) << words << ": asked for the words in the box";
            if (asked.size() == before) continue;
            asked.back().second(shell::found_in(*served, words, "https://example.org/docs/"));
            host.settle();
            const auto query = oweb::search(*sections, words);
            const auto on_page = shown();
            expect(on_page.size() == query.size()) << words << ": " << on_page.size() << " shown, " << query.size() << " answered";
            std::println("    the box, \"{}\": {} section(s){}", words, on_page.size(), on_page.empty() ? std::string() : std::format(", first {} — {}", on_page[0].second, on_page[0].first));
            found_in_all += on_page.size();
            for (std::size_t at = 0; at < std::min(on_page.size(), query.size()); ++at)
                expect(on_page[at].second == query[at].section.title &&
                       on_page[at].first == std::format("https://example.org/docs/{}{}{}", query[at].section.route.substr(1), query[at].section.anchor.empty() ? "" : "#", query[at].section.anchor))
                    << words << " " << at << ": " << on_page[at].first << " " << on_page[at].second;
            if (query.empty()) expect(host.tree().node(results + "/none") && host.tree().node(results + "/none")->name == "Search · 0") << words << ": nothing found is said";
        }
        expect(found_in_all >= 5u && !shown().empty()) << found_in_all << ": the words found sections, the last too";
        // An answer for words no longer in the box: not theirs, not shown.
        const auto late = asked.front().second;
        host.key(Key::a, Modifiers(primary_modifier()));
        host.type("nothing-of-the-kind");
        host.settle();
        late(shell::found_in(*served, "takes over", ""));
        host.settle();
        expect(shown().empty() && !host.tree().node(results + "/none")) << "still waiting for its own answer";
        asked.back().second({});
        host.settle();
        expect(shown().empty() && host.tree().node(results + "/none"));
        // Emptied: nothing asked, nothing shown.
        const std::size_t before = asked.size();
        host.key(Key::a, Modifiers(primary_modifier()));
        host.key(Key::backspace);
        host.settle();
        expect(asked.size() == before && shown().empty() && !host.tree().node(results + "/none"));
    };

    "a navigation of one's own: the pages at the side, the contents under them, no turn — each route says the same document, and reads back as written"_test = [] {
        using namespace omui;
        namespace shell = oweb::shell;
        // A site's own view of a page's parts: what the shell is made of, placed its own way.
        const shell::View sided = [](shell::Parts parts, shell::Layout layout) {
            std::vector<Element> side;
            if (!parts.pages.empty()) side.push_back(shell::navigation(column({.id = std::string(shell::pages_part), .gap = 6}, shell::links(std::move(parts.pages))), parts.site));
            if (!parts.sections.empty())
                side.push_back(shell::navigation(column({.id = std::string(shell::contents_part), .gap = 4}, shell::links(std::move(parts.sections))), parts.contents));
            std::vector<Element> middle;
            middle.push_back(shell::document(parts));
            std::vector<Element> across;
            across.push_back(column({.id = "side", .width = 220, .gap = 24}, std::move(side)));
            across.push_back(column({.id = "middle", .grow = 1}, std::move(middle)));
            std::vector<Element> held;
            held.push_back(row({.id = "page", .padding = 24, .gap = 32}, std::move(across)));
            return shell::view(std::move(held), layout);
        };
        const auto site = package() / "templates" / "docs";
        auto options = oweb::configured(site);
        expect(options.has_value());
        if (!options) return;
        options->look = false;
        const auto routes = oweb::collect(site, *options);
        expect(routes.has_value()) << (routes ? "" : routes.error());
        if (!routes) return;
        oweb::SiteOptions own = *options;
        own.view = sided;
        const auto moved = [](std::string_view markup) {
            std::string out(markup);
            for (std::size_t at = out.find("main/page/middle/"); at != std::string::npos; at = out.find("main/page/middle/", at)) out.replace(at, 17, "main/page/");
            return out;
        };
        std::size_t compared = 0;
        for (const oweb::Route& route : *routes) {
            const auto plain = oweb::evaluate(site, *routes, route, *options);
            const auto page = oweb::evaluate(site, *routes, route, own);
            expect(plain.has_value() && page.has_value()) << route.path << (page ? "" : page.error());
            if (!plain || !page) continue;
            // The document is the page's whatever stands around it: its Markdown, its HTML but for where it stands.
            expect(page->problems.empty() && page->markdown == plain->markdown) << route.path << ": the same Markdown";
            expect(moved(document_of(page->html)) == document_of(plain->html)) << route.path << ": the same document in the page";
            expect(body_of(page->html).contains("<nav data-omui-target=\"main/page/side/nav\"") && !body_of(page->html).contains("/turn\"")) << route.path << ": its navigation where the view put it";
            // What the page says of itself is what the shell's own page says, but for the turn this view leaves out.
            const auto shells = shell::read(projection_of(plain->html), [&](std::string_view) { return std::string(document_of(plain->html)); });
            const auto parts = shell::read(projection_of(page->html), [&](std::string_view id) {
                expect(id == "main/page/middle/main/doc") << id;
                return std::string(document_of(page->html));
            });
            expect(shells.has_value() && parts.has_value()) << route.path << (parts ? "" : parts.error());
            if (!shells || !parts) continue;
            const auto same = [](const std::vector<shell::Link>& one, const std::vector<shell::Link>& other) {
                return std::ranges::equal(one, other, [](const shell::Link& left, const shell::Link& right) { return left.id == right.id && left.href == right.href && left.text == right.text; });
            };
            expect(parts->site == shells->site && same(parts->pages, shells->pages) && parts->contents == shells->contents && same(parts->sections, shells->sections) && parts->beside.empty())
                << route.path << ": the same parts read back";
            Headless host({.level = HeadlessLevel::layout, .size = {960, 1200}});
            host.mount(shell::mounted_at, sided(*parts, {}));
            host.settle();
            semantic::Snapshot again = host.tree().snapshot({});
            std::unordered_set<std::string> left_out;
            for (const spec::SemanticNode& node : again.nodes)
                if (node.role == Role::button && node.id.contains("/doc/") && !node.id.contains("/live/")) left_out.insert(node.id);
            for (spec::SemanticNode& node : again.nodes) std::erase_if(node.children, [&](const std::string& id) { return left_out.contains(id); });
            std::erase_if(again.nodes, [&](const spec::SemanticNode& node) { return left_out.contains(node.id); });
            const std::string written(body_of(page->html)), read_back = semantic::static_html(again);
            expect(read_back == written) << std::format("{}:\n{}\n--- as written\n{}", route.path, read_back, written);
            ++compared;
        }
        expect(compared == routes->size()) << compared;
        // And the site is built with it: every table and every route's two files, within its budget.
        const auto out = std::filesystem::temp_directory_path() / "openmui-tests" / "oweb" / "own-view";
        std::filesystem::remove_all(out);
        const auto built = oweb::build(site, out, own);
        expect(built.has_value()) << (built ? "" : built.error());
        expect(read(out / "start" / "index.html").contains("data-omui-target=\"main/page/side/toc\"") && std::filesystem::exists(out / "start" / "index.md"));
    };

    "a live example: in the page before any program, its source the block's; operated, it answers; the document is the block alone"_test = [] {
        using namespace omui;
        const auto site = package() / "templates" / "docs";
        auto options = oweb::configured(site);
        expect(options.has_value());
        if (!options) return;
        options->look = false;
        const auto routes = oweb::collect(site, *options);
        expect(routes.has_value()) << (routes ? "" : routes.error());
        if (!routes) return;
        const auto live = std::ranges::find(*routes, "/live/", &oweb::Route::path);
        expect(live != routes->end() && live->load == "idle") << "its route takes the program once the page is idle";
        if (live == routes->end()) return;
        const auto page = oweb::evaluate(site, *routes, *live, *options);
        expect(page.has_value()) << (page ? "" : page.error());
        if (!page) return;
        expect(page->problems.empty() && page->examples.empty()) << (page->examples.empty() ? "" : page->examples.front());
        // The page as it reads with no program: the example as it starts, marked as what its block gave, and the block.
        const std::string_view body = body_of(page->html);
        expect(body.contains("<figure data-omui-target=\"main/page/main/doc/b3\">") && body.contains("data-omui-target=\"main/page/main/doc/b3/live\" data-output=\"\" aria-label=\"Example\" role=\"group\"")) << body;
        expect(body.contains(">Add one</button>") && body.contains("Not pressed yet") && body.contains(">Reset</button>")) << "the example's own nodes, as they start";
        expect(body.contains("<pre data-omui-target=\"main/page/main/doc/b3/code\"><code class=\"language-view\">{\n  \"omui\": \"0.1\",")) << "its source, the block of code";
        // The document is the block alone: its Markdown the source's, and nothing of the example in its text or its search.
        const std::string source = read(site / "pages" / "live.md");
        expect(page->markdown.contains("```view\n{\n  \"omui\": \"0.1\",") && !page->markdown.contains("\nNot pressed yet") && !page->markdown.contains("\nAdd one\n")) << page->markdown;
        expect(md::to_html(md::from_html(document_of(page->html))) == md::to_html(md::parse(page->markdown))) << "the page's HTML read back is the document its Markdown says";
        for (const oweb::Section& section : page->sections) expect(!section.text.contains("\nNot pressed yet") && !section.text.starts_with("Not pressed yet")) << section.title;
        // Operated — as the page's program runs it, read back from the page's own HTML — it answers.
        const auto parts = oweb::shell::read(projection_of(page->html), [&](std::string_view) { return std::string(document_of(page->html)); });
        expect(parts.has_value()) << (parts ? "" : parts.error());
        if (!parts) return;
        Headless host({.level = HeadlessLevel::layout, .size = {960, 1200}});
        host.mount(oweb::shell::mounted_at, oweb::shell::page(*parts, {.scrolls = true}));
        host.settle();
        const auto named = [&](std::string_view id) {
            const semantic::Snapshot now = host.tree().snapshot({});
            const auto found = std::ranges::find(now.nodes, id, &spec::SemanticNode::id);
            return found == now.nodes.end() ? std::string("(no such node)") : found->name;
        };
        expect(named("main/page/main/doc/b3/live/counter/said") == "Not pressed yet") << named("main/page/main/doc/b3/live/counter/said");
        expect(host.click("main/page/main/doc/b3/live/counter/add") && host.click("main/page/main/doc/b3/live/counter/add"));
        host.settle();
        expect(named("main/page/main/doc/b3/live/counter/said") == "Presses: 2") << named("main/page/main/doc/b3/live/counter/said");
        expect(host.click("main/page/main/doc/b3/live/counter/reset"));
        host.settle();
        expect(named("main/page/main/doc/b3/live/counter/said") == "Not pressed yet") << "reset";
    };

    "an example that cannot run: the page says what is wrong and where, and the site is not built"_test = [] {
        const auto site = std::filesystem::temp_directory_path() / "openmui-tests" / "oweb" / "wrong-example";
        std::filesystem::remove_all(site);
        std::filesystem::create_directories(site / "pages");
        std::ofstream(site / "pages" / "index.md") << "# Home\n\n```view\n{ \"omui\": \"0.1\", \"imports\": [\"omui.ui.base\"], \"view\": { \"root\": { \"type\": \"buton\", \"props\": { \"text\": \"Go\" } } } }\n```\n";
        const auto built = oweb::build(site, site / "out", {});
        expect(!built.has_value());
        if (built) return;
        expect(built.error().contains("1 example of the site's pages cannot run; nothing was written") && built.error().contains("pages/index.md: an example cannot run") &&
               built.error().contains("button"))
            << built.error();
        expect(!std::filesystem::exists(site / "out" / "index.html"));
    };

    "a long page: the repository's guides as one page — written within a route's budget with its look, read back to its document"_test = [] {
        // The seven guides of docs/en/guide, one after another under one title: a page of some twenty screens, the
        // corpus' long one (real documents, none copied). A link from one guide to another is its words here.
        const auto guides = package().parent_path().parent_path().parent_path() / "docs" / "en" / "guide";
        std::string source = "# The guides, on one page\n";
        std::size_t words = 0;
        for (const std::string_view name : {"start", "ui", "motion", "agent", "world", "world3d", "game"}) {
            const std::string guide = read(guides / std::format("{}.md", name));
            expect(!guide.empty()) << name;
            bool fenced = false;
            for (const auto part : std::views::split(guide, '\n')) {
                std::string line(part.begin(), part.end());
                if (line.starts_with("```")) fenced = !fenced;
                if (!fenced && line.starts_with('#')) line.insert(line.begin(), '#');
                // [words](another guide) — its words; a link to the Web or to a place in the page stays.
                for (std::size_t at = line.find("]("); !fenced && at != std::string::npos; at = line.find("](", at + 1)) {
                    const std::size_t open = line.rfind('[', at), close = line.find(')', at);
                    if (open == std::string::npos || close == std::string::npos || line.compare(at + 2, 4, "http") == 0 || line[at + 2] == '#') continue;
                    line = line.substr(0, open) + line.substr(open + 1, at - open - 1) + line.substr(close + 1);
                    at = open;
                }
                source += '\n' + line;
            }
            source += '\n';
        }
        for (const auto word : std::views::split(source, ' ')) words += !std::ranges::empty(word);
        const auto site = std::filesystem::temp_directory_path() / "openmui-tests" / "oweb" / "long";
        std::filesystem::remove_all(site);
        std::filesystem::create_directories(site / "pages");
        std::ofstream(site / "pages" / "index.md", std::ios::binary) << source;
        oweb::SiteOptions options;
        options.look = true;
        const auto built = oweb::build(site, site / "out", options);
        expect(built.has_value()) << (built ? "" : built.error());
        if (!built) return;
        const std::string html = read(site / "out" / "index.html"), markdown = read(site / "out" / "index.md");
        md::Document read_back = md::from_html(document_of(html));
        for (md::Link& link : read_back.links) link.href = "…";
        expect(md::to_html(read_back) == shape_of(source)) << "its HTML read back is its source's document";
        expect(shape_of(markdown) == shape_of(source)) << "and so is its Markdown";
        expect(words > 5000 && built->routes.front().headings.size() > 40) << words;
        expect(built->delivery.within() && built->delivery.verdicts.front().found < 60'000) << built->delivery.verdicts.front().found;
        std::println("    a long page: {} words under {} headings, index.html {} B stored and {} B sent of a budget of {}", words, built->routes.front().headings.size(), html.size(),
                     built->delivery.verdicts.front().found, built->delivery.verdicts.front().budget.most);
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
        expect(reference != routes->end() && reference->reference == oweb::own_catalog && reference->title == "oweb operations" && !reference->headings.empty());
        if (reference == routes->end()) return;
        expect(!std::filesystem::exists(site / "pages" / "reference")) << "no copy to go stale";
        const auto text = oweb::source_text(site, *reference);
        expect(text.has_value() && *text == read(package() / "src" / "catalog" / "oweb.ops.md")) << "what the catalog's generator writes: the builder's own, carried with it";
        // A product's own catalog is its file, from the site's directory: read at each build.
        {
            oweb::Route stated = *reference;
            stated.reference = stated.source = "../../oweb.ops.xdef.ts";
            const auto from_file = oweb::source_text(site, stated);
            expect(from_file.has_value() && *from_file == *text) << "the same catalog by its file: the same page";
        }
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
