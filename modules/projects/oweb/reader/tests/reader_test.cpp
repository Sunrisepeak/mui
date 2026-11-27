// The reader without a window: the page it makes from what a page wrote, and the site's search as its box asks —
// the index fetched once, at the first words; askings made meanwhile answered when it comes; a site without one.
import std;
import oweb.reader;
import omui.core;
import omui.view;
import omui.semantic;
import omui.host.headless;
import mlibs.agent;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace shell = oweb::shell;
namespace reader = oweb::reader;

namespace {

// What a page wrote, as the page hands it over (omui::shown_document): each written node's id, label, address, words.
std::string projection_of(const semantic::Snapshot& snapshot) {
    const auto quoted = [](std::string_view text) {
        std::string out = "\"";
        for (const char character : text) {
            if (character == '"' || character == '\\') out += '\\';
            out += character;
        }
        return out + "\"";
    };
    std::string nodes;
    for (const spec::SemanticNode& node : snapshot.nodes) {
        const Value* href = node.properties.get("href");
        nodes += std::format("{}{{\"id\":{},\"label\":{},\"href\":{},\"text\":{}}}", nodes.empty() ? "" : ",", quoted(node.id), quoted(node.role == Role::link ? "" : node.name),
                             quoted(href ? href->as_string() : ""), quoted(node.role == Role::link ? node.name : ""));
    }
    return std::format("{{\"lang\":\"en\",\"nodes\":[{}]}}", nodes);
}

// A site of three sections, as its index says them.
std::string index_text() {
    const std::vector<shell::Section> sections{{.route = "/", .anchor = "", .title = "Field notes", .trail = {}, .text = "Notes kept in the field, about windows and agents."},
                                               {.route = "/guide/start/", .anchor = "install", .title = "Install", .trail = {"Start"}, .text = "Install the tools, then open the first window."},
                                               {.route = "/guide/start/", .anchor = "run", .title = "Run", .trail = {"Start"}, .text = "Run it: a window opens."}};
    return shell::search_json(sections);
}

// The same site's routes, as its table says them (`routes@1`).
std::string routes_text() {
    return R"({"$format":"routes@1","site":{"title":"Notes","url":"https://example.org/notes/","languages":["en"]},"routes":[)"
           R"({"path":"/","source":"index.md","title":"Field notes","language":"en","headings":[{"level":1,"text":"Field notes","anchor":"field-notes"}]},)"
           R"({"path":"/guide/start/","source":"guide/start.md","title":"Start","language":"en","headings":[{"level":2,"text":"Install","anchor":"install"}]}]})";
}

struct Asked {
    std::vector<std::string>                                                                   relations, addresses;
    std::vector<std::function<void(std::expected<std::string, std::string>)>>                  waiting;
};

reader::Site site_of(std::shared_ptr<Asked> asked, std::string address) {
    return {.link = [asked, address](std::string_view relation) {
                asked->relations.emplace_back(relation);
                return address;
            },
            .file = [asked](std::string_view where, std::function<void(std::expected<std::string, std::string>)> done) {
                asked->addresses.emplace_back(where);
                asked->waiting.push_back(std::move(done));
            }};
}

// A conversation with a page's subject: what each request was answered with, as its records came.
struct Conversation {
    explicit Conversation(mlibs::agent::Subject& subject) : session({&subject}) {}
    void ask(std::string_view line) {
        session.request(line, [this](std::string_view record) { records.emplace_back(record); });
    }
    // The data of the answer to the request that gave `re`, as sent ("" while none came), and whether it ended well.
    [[nodiscard]] std::string data(int re) const {
        const std::string head = std::format(R"({{"kind":"data","re":{},"data":)", re);
        for (const std::string& record : records)
            if (record.starts_with(head)) return record.substr(head.size(), record.size() - head.size() - 1);
        return {};
    }
    [[nodiscard]] std::string record(int re, std::string_view kind) const {
        const std::string head = std::format(R"({{"kind":"{}","re":{},)", kind, re);
        for (const std::string& one : records)
            if (one.starts_with(head)) return one;
        return {};
    }
    mlibs::agent::SubjectSession session;
    std::vector<std::string>     records;
};

}  // namespace

static UTest cases{"oweb.reader", [] {
    "no page of a site (the program run on its own): a page that says so, no search to ask"_test = [] {
        Headless host({.level = HeadlessLevel::layout, .size = {960, 900}});
        host.mount(shell::mounted_at, reader::view("", [](std::string_view) { return std::string(); }, {.scrolls = true, .find = [](std::string, std::function<void(std::vector<shell::Found>)>) {}}));
        host.settle();
        const auto nodes = host.tree().snapshot({}).nodes;
        expect(std::ranges::any_of(nodes, [](const spec::SemanticNode& node) { return node.role == Role::heading && node.name == "No page to read"; }));
        expect(std::ranges::none_of(nodes, [](const spec::SemanticNode& node) { return node.role == Role::text_box; }));
    };

    "a page read back and drawn: its parts where the builder put them, its search asking the site — the index once, at the first words; what was asked meanwhile answered when it comes"_test = [] {
        // The page as its builder wrote it: the shell's own view of its parts.
        std::string written;
        {
            Headless builder({.level = HeadlessLevel::layout, .size = {960, 900}});
            std::vector<shell::Link> pages;
            pages.push_back({.id = "r0", .href = "../../", .text = "Field notes"});
            pages.push_back({.id = "r1", .href = "./", .text = "Start"});
            builder.mount(shell::mounted_at, shell::page({.site = "Notes", .pages = std::move(pages), .document = "# Start\n\nInstall the tools.\n", .search = "Search"}, {.scrolls = true}));
            builder.settle();
            written = projection_of(builder.tree().snapshot({}));
        }
        const auto asked = std::make_shared<Asked>();
        Headless host({.level = HeadlessLevel::layout, .size = {960, 900}});
        host.mount(shell::mounted_at, reader::view(written, [](std::string_view) { return std::string("<h1>Start</h1>\n<p>Install the tools.</p>\n"); },
                                                   {.scrolls = true, .find = reader::finder(site_of(asked, "../../site.search.xdef.json"))}));
        host.settle();
        const std::string box = std::format("{}/page/{}/box", shell::mounted_at, shell::search_part), results = std::format("{}/page/{}/results", shell::mounted_at, shell::search_part);
        expect(host.tree().node(box) && host.tree().node(std::format("{}/page/nav/r1", shell::mounted_at))) << "the parts it read, where the builder put them";
        expect(asked->relations.empty() && asked->addresses.empty()) << "nothing fetched for a page that is only read";
        const auto shown = [&] {
            std::vector<std::pair<std::string, std::string>> out;
            for (std::size_t at = 0;; ++at) {
                const auto one = host.tree().node(std::format("{}/f{}/h{}", results, at, at));
                if (!one) break;
                const Value* href = one->properties.get("href");
                out.emplace_back(href ? std::string(href->as_string()) : std::string(), one->name);
            }
            return out;
        };
        host.runtime().focus(host.runtime().find(box));
        host.type("win");
        host.settle();
        host.type("dow");
        host.settle();
        expect(asked->relations == std::vector<std::string>{"search"} && asked->addresses == std::vector<std::string>{"../../site.search.xdef.json"})
            << asked->addresses.size() << ": asked for once, where the page says it is";
        expect(shown().empty() && !host.tree().node(results + "/none")) << "nothing said while it is on its way";
        asked->waiting.front()(index_text());
        host.settle();
        using Shown = std::vector<std::pair<std::string, std::string>>;
        Shown expected;
        for (const shell::Found& found : shell::found_in(*shell::sections_from(index_text()), "window", "../../")) expected.emplace_back(found.href, found.title);
        expect(expected.size() == 3u && shown() == expected) << shown().size() << ": the words now in the box (not the first asking's), as the query answers them";
        expect(std::ranges::contains(expected, std::pair<std::string, std::string>{"../../guide/start/#install", "Install"}) &&
               std::ranges::contains(expected, std::pair<std::string, std::string>{"../../", "Field notes"}))
            << "each section from where the index is";
        host.key(Key::a, Modifiers(primary_modifier()));
        host.type("agents");
        host.settle();
        expect(shown() == Shown{{"../../", "Field notes"}} && asked->addresses.size() == 1u) << "answered from the index it has";
    };

    "a site whose index does not come, and a page that names none: nothing found, said so, and not asked for again"_test = [] {
        for (const bool named : {true, false}) {
            const auto asked = std::make_shared<Asked>();
            std::vector<std::vector<shell::Found>> answers;
            const shell::Finder find = reader::finder(site_of(asked, named ? "site.search.xdef.json" : ""));
            find("window", [&](std::vector<shell::Found> found) { answers.push_back(std::move(found)); });
            if (named) {
                expect(answers.empty() && asked->waiting.size() == 1u);
                asked->waiting.front()(std::unexpected("HTTP 404 for /site.search.xdef.json"));
            }
            find("agents", [&](std::vector<shell::Found> found) { answers.push_back(std::move(found)); });
            expect(answers.size() == 2u && answers[0].empty() && answers[1].empty()) << named;
            expect(asked->addresses.size() == (named ? 1u : 0u)) << named << ": " << asked->addresses.size();
        }
        // An index of the site's top: a section's address from there, the first page's `./`.
        const auto asked = std::make_shared<Asked>();
        std::vector<shell::Found> found;
        reader::finder(site_of(asked, "site.search.xdef.json"))("notes", [&](std::vector<shell::Found> answer) { found = std::move(answer); });
        asked->waiting.front()(index_text());
        expect(found.size() == 1u && found[0].href == "./" && found[0].title == "Field notes") << found.size();
    };

    "the site asked from inside its page (AG5): nothing fetched until asked, an answer when its file has come, a table once — and the answers the command's own"_test = [] {
        const auto asked = std::make_shared<Asked>();
        std::vector<std::string> shown;
        const auto site = reader::subject(reader::keeping_tables(site_of(asked, "../../site.search.xdef.json")), [&](std::string_view query) { shown.emplace_back(query); });
        expect(site->name() == "site" && site->operations().size() == 4u && !site->listing().empty() && !site->guide().tasks.empty());
        Conversation with(*site);
        expect(asked->addresses.empty()) << "a page that is only read fetches nothing for it";

        // Two askings wait for one table: asked for once, both answered when it comes.
        with.ask(R"({"op":"site.describe","re":1})");
        with.ask(R"({"op":"routes.list","re":2})");
        expect(with.records.empty() && asked->addresses == std::vector<std::string>{"../../site.routes.xdef.json"}) << with.records.size() << " " << asked->addresses.size();
        asked->waiting[0](routes_text());
        const shell::Table table = *shell::table_from(routes_text());
        expect(with.data(1) == shell::describe_answer(table, true, R"([{"op":"page.read","route":"/"},{"op":"docs.search","query":"Notes"}])")) << with.data(1);
        expect(with.data(1).contains(R"("markdown":"/guide/start/index.md")") && with.data(1).contains("# Notes")) << "the routes with their Markdown, and the text a build writes as llms.txt";
        expect(with.data(2) == shell::routes_answer(table) && with.record(1, "result").contains(R"("exitCode":0)") && with.record(2, "result").contains(R"("exitCode":0)")) << with.data(2);

        // A page: the table is here; its Markdown is asked for where the site is from the page.
        with.ask(R"({"op":"page.read","re":3,"route":"/guide/start/"})");
        expect(with.data(3).empty() && asked->addresses.size() == 2u && asked->addresses[1] == "../../guide/start/index.md") << asked->addresses.back();
        asked->waiting[1]("# Start\n\nInstall the tools.\n");
        expect(with.data(3) == shell::page_answer(table.routes[1], "markdown", "# Start\n\nInstall the tools.\n")) << with.data(3);
        with.ask(R"({"op":"page.read","re":4,"route":"/guide/start/","as":"html"})");
        expect(asked->addresses.back() == "../../guide/start/index.html");
        asked->waiting[2]("<h1>Start</h1>");
        expect(with.data(4).contains(R"("as":"html")") && with.data(4).contains("<h1>Start</h1>")) << with.data(4);

        // What is refused — by the session for what the catalog requires, by the site for the rest — says the way
        // on, and fetches nothing.
        with.ask(R"({"op":"page.read","re":5,"route":"/nowhere/"})");
        with.ask(R"({"op":"page.read","re":6})");
        with.ask(R"({"op":"page.read","re":7,"route":"/","as":"pdf"})");
        with.ask(R"({"op":"docs.search","re":8})");
        with.ask(R"({"op":"docs.search","re":9,"query":"window","limit":0})");
        expect(asked->addresses.size() == 3u && shown.empty()) << asked->addresses.size();
        expect(with.record(5, "error").contains("no route `/nowhere/`") && with.record(5, "error").contains(R"("op":"routes.list")")) << with.record(5, "error");
        expect(with.record(6, "error").contains("needs `route`") && with.record(7, "error").contains("markdown or html") && with.record(8, "error").contains("query") &&
               with.record(9, "error").contains("above 0"))
            << with.record(6, "error") << with.record(7, "error") << with.record(8, "error") << with.record(9, "error");

        // A search: the words shown where a reader searches, the index once, the sections the command would find.
        with.ask(R"({"op":"docs.search","re":10,"query":"window","limit":2})");
        expect(shown == std::vector<std::string>{"window"} && asked->addresses.back() == "../../site.search.xdef.json" && with.data(10).empty());
        asked->waiting[3](index_text());
        const auto sections = *shell::sections_from(index_text());
        expect(with.data(10) == shell::search_answer(sections, "window", 2) && with.data(10).contains(R"("route":"/guide/start/")")) << with.data(10);
        with.ask(R"({"op":"docs.search","re":11,"query":"agents"})");
        expect(with.data(11) == shell::search_answer(sections, "agents", 8) && asked->addresses.size() == 4u && shown.size() == 2u) << "answered from the index it has";
    };

    "a site's file that does not come, and a page that names no site: said with the way on; asked for again by the next asking"_test = [] {
        const auto asked = std::make_shared<Asked>();
        const auto site = reader::subject(reader::keeping_tables(site_of(asked, "site.search.xdef.json")));
        Conversation with(*site);
        with.ask(R"({"op":"site.describe","re":1})");
        asked->waiting[0](std::unexpected("HTTP 404"));
        expect(with.record(1, "error").contains("site.routes.xdef.json did not come: HTTP 404") && with.record(1, "result").contains(R"("exitCode":1)")) << with.record(1, "error");
        with.ask(R"({"op":"site.describe","re":2})");
        expect(asked->addresses == std::vector<std::string>{"site.routes.xdef.json", "site.routes.xdef.json"}) << "asked for again";
        asked->waiting[1](routes_text());
        expect(with.data(2).contains(R"("title":"Notes")")) << with.data(2);
        // What came is not a table: said so, with what the site says of itself as the way on.
        with.ask(R"({"op":"docs.search","re":3,"query":"window"})");
        asked->waiting[2]("<html>not found</html>");
        expect(with.record(3, "error").contains("site.search.xdef.json did not come") && with.record(3, "error").contains(R"("op":"site.describe")")) << with.record(3, "error");
        // A page a build did not write (the program run on its own) has no site to ask.
        const auto alone = std::make_shared<Asked>();
        const auto none = reader::subject(reader::keeping_tables(site_of(alone, "")));
        Conversation without(*none);
        without.ask(R"({"op":"site.describe","re":1})");
        without.ask(R"({"op":"docs.search","re":2,"query":"window"})");
        expect(without.record(1, "error").contains("names no site") && without.record(2, "error").contains("names no site") && alone->addresses.empty()) << without.record(1, "error");
    };

    "what an agent searched for is in the page's own box: the reader sees the words and what they found — from one index, the box's and the subject's"_test = [] {
        std::string written;
        {
            Headless builder({.level = HeadlessLevel::layout, .size = {960, 900}});
            builder.mount(shell::mounted_at, shell::page({.site = "Notes", .document = "# Start\n\nInstall the tools.\n", .search = "Search"}, {.scrolls = true}));
            builder.settle();
            written = projection_of(builder.tree().snapshot({}));
        }
        const auto asked = std::make_shared<Asked>();
        const reader::Site kept = reader::keeping_tables(site_of(asked, "../../site.search.xdef.json"));
        Headless host({.level = HeadlessLevel::layout, .size = {960, 900}});
        host.mount(shell::mounted_at, reader::view(written, [](std::string_view) { return std::string("<h1>Start</h1>\n<p>Install the tools.</p>\n"); }, {.scrolls = true, .find = reader::finder(kept)}));
        host.settle();
        const auto site = reader::subject(kept, [&](std::string_view query) { expect(reader::show_search(host.runtime(), query)); });
        Conversation with(*site);
        with.ask(R"({"op":"docs.search","re":1,"query":"agents"})");
        host.settle();
        expect(asked->addresses == std::vector<std::string>{"../../site.search.xdef.json"}) << asked->addresses.size() << ": the box and the subject ask for one index, once";
        asked->waiting.front()(index_text());
        host.settle();
        const std::string box = std::format("{}/page/{}/box", shell::mounted_at, shell::search_part), results = std::format("{}/page/{}/results", shell::mounted_at, shell::search_part);
        const auto typed = host.tree().node(box);
        expect(fatal(typed.has_value())) << box;
        expect(typed->value.as_string() == "agents") << "the words, in the box";
        const auto found = host.tree().node(results + "/f0/h0");
        expect(found && found->name == "Field notes") << "what they found, under it";
        expect(with.data(1) == shell::search_answer(*shell::sections_from(index_text()), "agents", 8)) << with.data(1);

        // A page with no search has no box to show them in: the subject still answers.
        Headless bare({.level = HeadlessLevel::layout, .size = {960, 900}});
        bare.mount(shell::mounted_at, reader::view("", [](std::string_view) { return std::string(); }, {.scrolls = true}));
        bare.settle();
        expect(!reader::show_search(bare.runtime(), "agents"));
    };
}};
