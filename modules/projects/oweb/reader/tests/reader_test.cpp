// The reader without a window: the page it makes from what a page wrote, and the site's search as its box asks —
// the index fetched once, at the first words; askings made meanwhile answered when it comes; a site without one.
import std;
import oweb.reader;
import omui.core;
import omui.view;
import omui.semantic;
import omui.host.headless;
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
}};
