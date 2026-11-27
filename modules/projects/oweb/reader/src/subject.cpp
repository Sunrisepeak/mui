// oweb.reader's subject `site` (0.1.0 plan AG5; oweb's design §4.10): the site a page is of, asked from inside
// the page. The command's reading operations over the site as built — here its files come by the page's own
// fetch, so an answer is kept (Answer::later) and ended when its file has come. And the site's tables kept, for
// the page's search box and this subject alike.
module oweb.reader;

import std;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
import openas;
import :catalog;

namespace oweb::reader {

namespace {

using mlibs::agent::Answer;
using Text = std::expected<std::string, std::string>;

// Where the site is from the page: the directory of the index its document names ("../site.search.xdef.json":
// "../"). Nothing: the page names none — it is no page of a built site.
std::optional<std::string> top_of(const Site& site) {
    const std::string address = site.link ? site.link("search") : std::string();
    if (address.empty() || !site.file) return std::nullopt;
    return address.substr(0, address.rfind('/') + 1);
}

// An answer that ends later, shared by what waits for a file.
using Kept = std::shared_ptr<Answer>;

std::string asking(std::string_view why, std::string_view request) { return std::format("[{}]", mlibs::agent::way_out(why, request)); }

void wrong(Answer& answer, std::string_view why, std::string_view next = {}) {
    answer.fail(openas::error_code::invalid_argument, why, openas::exit_code::failed, next);
}
// A file of the site did not come: what was asked for, why, and the way on — the site's own description says
// what it holds; a site built before it wrote a file is built again.
void lost(Answer& answer, std::string_view address, std::string_view why) {
    answer.fail("failed", std::format("the site's file {} did not come: {} — a site as `oweb site.build` writes it holds it", address, why), openas::exit_code::failed,
                asking("what the site says it holds", R"({"op":"site.describe"})"));
}

void with_routes(const Site& site, const Kept& answer, std::function<void(const shell::Table&, std::string_view top)> then) {
    const auto top = top_of(site);
    if (!top) return answer->fail("failed", "this page names no site (no <link rel=\"search\">): it is not a page `oweb site.build` wrote, and has no site to ask");
    const std::string address = *top + "site.routes.xdef.json";
    site.file(address, [answer, address, top = *top, then = std::move(then)](Text text) {
        if (!text) return lost(*answer, address, text.error());
        const auto read = shell::table_from(*text);
        if (!read) return lost(*answer, address, read.error());
        then(*read, top);
    });
}

class Pages final : public mlibs::agent::Subject {
public:
    Pages(Site site, std::function<void(std::string_view)> shown) : site_(std::move(site)), shown_(std::move(shown)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "site"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "oweb.site@1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return oweb::reader::guide; }
    [[nodiscard]] std::string_view listing() const noexcept override { return oweb::reader::listing; }

    void operate(std::string_view operation, const mlibs::agent::Request& request, Answer& reply) override {
        const auto given = [&request](std::string_view name) { return request[name].text(); };
        if (operation == "site.describe") {
            const Kept answer = std::make_shared<Answer>(reply.later());
            return with_routes(site_, answer, [answer](const shell::Table& routes, std::string_view) {
                // The ways on, each a request this door takes as it stands.
                const std::string first = routes.routes.empty() ? std::string("/") : routes.routes.front().path;
                answer->data(shell::describe_answer(routes, true,
                                                    std::format(R"([{{"op":"page.read","route":{}}},{{"op":"docs.search","query":{}}}])", mlibs::agent::json_text(first),
                                                                mlibs::agent::json_text(routes.title))));
                answer->result();
            });
        }
        if (operation == "routes.list") {
            const Kept answer = std::make_shared<Answer>(reply.later());
            return with_routes(site_, answer, [answer](const shell::Table& routes, std::string_view) {
                answer->data(shell::routes_answer(routes));
                answer->result();
            });
        }
        if (operation == "page.read") {
            const auto route = given("route");
            const std::string as(given("as").value_or("markdown"));
            if (!route || route->empty()) return wrong(reply, "page.read takes route, a page's address in the site (/guide/start/)", asking("the site's routes", R"({"op":"routes.list"})"));
            if (as != "markdown" && as != "html") return wrong(reply, std::format("as is markdown or html, not `{}`", as));
            const Kept answer = std::make_shared<Answer>(reply.later());
            return with_routes(site_, answer, [site = site_, answer, route = std::string(*route), as](const shell::Table& routes, std::string_view top) {
                const auto listed = std::ranges::find(routes.routes, route, &shell::Entry::path);
                if (listed == routes.routes.end())
                    return wrong(*answer, std::format("the site has no route `{}`", route), asking("the site's routes", R"({"op":"routes.list"})"));
                const std::string address = std::format("{}{}index.{}", top, std::string_view(route).substr(1), as == "html" ? "html" : "md");
                site.file(address, [answer, address, entry = *listed, as](Text text) {
                    if (!text) return lost(*answer, address, text.error());
                    answer->data(shell::page_answer(entry, as, *text));
                    answer->result();
                });
            });
        }
        if (operation == "docs.search") {
            const auto query = given("query");
            if (!query || query->empty()) return wrong(reply, "docs.search takes query, the words to look for");
            std::size_t limit = 8;
            if (const auto stated = request["limit"]; stated && !stated.is_null()) {
                const std::string text = stated.text() ? std::string(*stated.text()) : stated.dump();
                const auto read = std::from_chars(text.data(), text.data() + text.size(), limit);
                if (read.ec != std::errc{} || read.ptr != text.data() + text.size() || limit == 0) return wrong(reply, std::format("limit is a number above 0, not `{}`", text));
            }
            const auto top = top_of(site_);
            if (!top) return reply.fail("failed", "this page names no site (no <link rel=\"search\">): it is not a page `oweb site.build` wrote, and has no site to ask");
            // What was asked is shown where a reader searches: the page's own box, with what it finds.
            if (shown_) shown_(*query);
            const Kept answer = std::make_shared<Answer>(reply.later());
            const std::string address = *top + "site.search.xdef.json";
            return site_.file(address, [answer, address, query = std::string(*query), limit](Text text) {
                if (!text) return lost(*answer, address, text.error());
                const auto sections = shell::sections_from(*text);
                if (!sections) return lost(*answer, address, sections.error());
                answer->data(shell::search_answer(*sections, query, limit));
                answer->result();
            });
        }
    }

private:
    Site                                  site_;
    std::function<void(std::string_view)> shown_;
};

}  // namespace

Site keeping_tables(Site site) {
    struct Table {
        std::optional<std::string>             text;
        std::vector<std::function<void(Text)>> waiting;
    };
    struct Tables {
        Site                                      site;
        std::map<std::string, Table, std::less<>> kept;
    };
    auto tables = std::make_shared<Tables>();
    tables->site = std::move(site);
    return {.link = tables->site.link,
            .file = [tables](std::string_view address, std::function<void(Text)> done) {
                if (!address.ends_with(".xdef.json")) return tables->site.file(address, std::move(done));
                Table& table = tables->kept[std::string(address)];
                if (table.text) return done(Text(*table.text));
                table.waiting.push_back(std::move(done));
                if (table.waiting.size() > 1) return;  // on its way
                tables->site.file(address, [tables, address = std::string(address)](Text came) {
                    Table& arrived = tables->kept[address];
                    if (came) arrived.text = *came;
                    for (auto& told : std::exchange(arrived.waiting, {})) told(came);
                });
            }};
}

std::unique_ptr<mlibs::agent::Subject> subject(Site site, std::function<void(std::string_view query)> shown) {
    return std::make_unique<Pages>(std::move(site), std::move(shown));
}

}  // namespace oweb::reader
