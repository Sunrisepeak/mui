// oweb:tool — the subject `oweb`: a site built, its routes listed, one page read, each answered as data.
module oweb;

import std;
import mcpplibs.os;
import mlibs.delivery;
import mlibs.agent;
import openas;
import omui.core;
import :site;
import :config;
import :tool;

namespace oweb {

namespace {

using Reply = mlibs::agent::Answer;
using omui::Value;

void done(Reply& reply, const Value& data) { reply.data(data.json()); }
void wrong(Reply& reply, std::string_view why) {
    reply.data(Value{}.set("ok", false).set("error", why).json());
    reply.fail(openas::error_code::invalid_argument, why);
}
void failed(Reply& reply, std::string_view why) {
    reply.data(Value{}.set("ok", false).set("error", why).json());
    reply.fail("failed", why);
}

inline constexpr std::uintmax_t not_a_number = std::numeric_limits<std::uintmax_t>::max();

// A site's options: its configuration's (oweb.config.ts), and what the request states over them ("en,zh": its
// languages, the default first). Nothing where the configuration cannot be read: the reply says why.
std::optional<SiteOptions> options_of(Reply& reply, const mlibs::agent::Arguments& args) {
    auto stated = configured(mcpplibs::os::fs::from_utf8(args.value("site").value_or("")));
    if (!stated) {
        failed(reply, stated.error());
        return std::nullopt;
    }
    SiteOptions options = std::move(*stated);
    if (const auto title = args.value("title")) options.title = *title;
    if (const auto url = args.value("url")) options.url = *url;
    // A request's engine is from where the request is made, not from the site's directory.
    if (const auto engine = args.value("engine"); engine && !engine->empty())
        options.engine = mcpplibs::os::fs::utf8(std::filesystem::absolute(mcpplibs::os::fs::from_utf8(*engine)));
    if (const auto load = args.value("load"); load && !load->empty()) options.load = *load;
    if (const auto readable = args.value("readable")) {
        // Not a number of bytes: marked, and refused where the build is asked for (site_build).
        const auto read = std::from_chars(readable->data(), readable->data() + readable->size(), options.readable);
        if (read.ec != std::errc{} || read.ptr != readable->data() + readable->size()) options.readable = not_a_number;
    }
    if (const auto languages = args.value("languages"); languages && !languages->empty()) {
        options.languages.clear();
        for (const auto part : std::views::split(*languages, ','))
            if (!std::ranges::empty(part)) options.languages.emplace_back(part.begin(), part.end());
    }
    return options;
}

Value described(const Route& route) {
    Value::List headings;
    for (const Heading& heading : route.headings)
        headings.push_back(Value{}.set("level", Value(static_cast<std::int64_t>(heading.level))).set("text", heading.text).set("anchor", heading.anchor));
    return Value{}.set("path", route.path).set("source", route.source).set("title", route.title).set("language", route.language).set("headings", Value(std::move(headings)));
}

void site_build(Reply& reply, const mlibs::agent::Arguments& args) {
    const auto site = args.value("site"), out = args.value("out");
    if (!site || !out) return wrong(reply, "site.build takes --site, the site's directory, and --out, the directory written");
    const auto stated = options_of(reply, args);
    if (!stated) return;
    const SiteOptions& options = *stated;
    if (options.readable == not_a_number) return wrong(reply, std::format("--readable is a number of bytes (0 holds no budget), not `{}`", *args.value("readable")));
    const auto built = build(mcpplibs::os::fs::from_utf8(*site), mcpplibs::os::fs::from_utf8(*out), options);
    if (!built) return failed(reply, built.error());
    const auto number = [](std::uintmax_t value) { return Value(static_cast<std::int64_t>(value)); };
    Value::List routes, files, budgets;
    for (const Route& route : built->routes) routes.push_back(described(route));
    const mlibs::delivery::Manifest& delivery = built->delivery;
    for (const mlibs::delivery::File& file : delivery.files)
        files.push_back(Value{}.set("path", file.path).set("bytes", number(file.bytes)).set("sent", number(file.sent())));
    std::string over;
    for (const mlibs::delivery::Verdict& verdict : delivery.verdicts) {
        budgets.push_back(Value{}
                              .set("name", verdict.budget.name)
                              .set("pattern", verdict.budget.pattern)
                              .set("most", number(verdict.budget.most))
                              .set("found", number(verdict.found))
                              .set("where", verdict.where)
                              .set("files", number(verdict.files))
                              .set("within", verdict.within));
        if (!verdict.within && over.empty())
            over = std::format("{}: {} sends {} bytes, over the budget of {} — shorten the page, or state the budget it is held to with --readable", verdict.budget.name, verdict.where,
                               verdict.found, verdict.budget.most);
    }
    Value data = Value{}
                     .set("ok", over.empty())
                     .set("out", *out)
                     .set("routes", Value(std::move(routes)))
                     .set("files", Value(std::move(files)))
                     .set("bytes", number(delivery.bytes))
                     .set("sent", number(delivery.sent))
                     .set("budgets", Value(std::move(budgets)));
    if (over.empty()) return done(reply, data);
    // Written all the same (the manifest says where it is over); the build did not pass.
    reply.data(data.set("error", over).json());
    reply.fail("failed", over);
}

void routes_list(Reply& reply, const mlibs::agent::Arguments& args) {
    const auto site = args.value("site");
    if (!site) return wrong(reply, "routes.list takes --site, the site's directory");
    const auto options = options_of(reply, args);
    if (!options) return;
    const auto routes = collect(mcpplibs::os::fs::from_utf8(*site), *options);
    if (!routes) return failed(reply, routes.error());
    Value::List listed;
    for (const Route& route : *routes) listed.push_back(described(route));
    done(reply, Value{}.set("ok", true).set("routes", Value(std::move(listed))));
}

void page_read(Reply& reply, const mlibs::agent::Arguments& args) {
    const auto site = args.value("site"), address = args.value("route");
    if (!site || !address) return wrong(reply, "page.read takes --site, the site's directory, and --route, the page's address (/guide/start/)");
    const std::string as = args.value("as").value_or("markdown");
    if (as != "markdown" && as != "html") return wrong(reply, std::format("--as is markdown or html, not `{}`", as));
    const auto stated = options_of(reply, args);
    if (!stated) return;
    const SiteOptions& options = *stated;
    const auto routes = collect(mcpplibs::os::fs::from_utf8(*site), options);
    if (!routes) return failed(reply, routes.error());
    const auto route = std::ranges::find(*routes, *address, &Route::path);
    if (route == routes->end()) {
        std::string known;
        for (const Route& one : *routes) known += std::format("{}{}", known.empty() ? "" : ", ", one.path);
        return failed(reply, std::format("no route `{}`: the site's are {}", *address, known));
    }
    const auto page = evaluate(mcpplibs::os::fs::from_utf8(*site), *routes, *route, options);
    if (!page) return failed(reply, page.error());
    done(reply, Value{}.set("ok", true).set("route", described(page->route)).set("as", as).set("text", as == "html" ? page->html : page->markdown).set("nodes", Value(static_cast<std::int64_t>(page->nodes))));
}

void docs_search(Reply& reply, const mlibs::agent::Arguments& args) {
    const auto site = args.value("site"), query = args.value("query");
    if (!site || !query || query->empty()) return wrong(reply, "docs.search takes --site, the site's directory, and --query, the words to find");
    std::size_t limit = 8;
    if (const auto given = args.value("limit")) {
        const auto read = std::from_chars(given->data(), given->data() + given->size(), limit);
        if (read.ec != std::errc{} || limit == 0) return wrong(reply, std::format("--limit is a number above 0, not `{}`", *given));
    }
    const auto options = options_of(reply, args);
    if (!options) return;
    const auto sections = sections_of(mcpplibs::os::fs::from_utf8(*site), *options);
    if (!sections) return failed(reply, sections.error());
    Value::List found;
    for (const Hit& hit : search(*sections, *query, limit)) {
        Value::List trail;
        for (const std::string& above : hit.section.trail) trail.emplace_back(above);
        found.push_back(Value{}
                            .set("route", hit.section.route)
                            .set("anchor", hit.section.anchor)
                            .set("title", hit.section.title)
                            .set("trail", Value(std::move(trail)))
                            .set("excerpt", hit.excerpt)
                            .set("markdown", std::format("{}index.md", hit.section.route))
                            .set("score", Value(hit.score)));
    }
    done(reply, Value{}.set("ok", true).set("query", *query).set("sections", Value(static_cast<std::int64_t>(sections->size()))).set("found", Value(std::move(found))));
}

}  // namespace

mlibs::control::Guide Sites::guide() const noexcept {
    static constexpr std::array<std::string_view, 3> read{R"({"op":"routes.list","site":"docs"})", R"({"op":"docs.search","site":"docs","query":"install"})",
                                                           R"({"op":"page.read","site":"docs","route":"/"})"};
    static constexpr std::array<std::string_view, 1> made{R"({"op":"site.build","site":"docs","out":"dist"})"};
    static constexpr std::array<mlibs::control::Task, 2> tasks{{{"read-a-site", "A site's routes, the sections that answer a question, then the page as Markdown", read},
                                                               {"build-a-site", "Every route written as HTML and as Markdown, with the site's tables", made}}};
    return {"A site on omui without opening a page: its pages built into routes, each its semantic tree written as HTML and as Markdown.", tasks};
}

void Sites::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& reply) {
    const mlibs::agent::Arguments args(request);
    if (operation == "site.build") site_build(reply, args);
    else if (operation == "routes.list") routes_list(reply, args);
    else if (operation == "page.read") page_read(reply, args);
    else if (operation == "docs.search") docs_search(reply, args);
}

}  // namespace oweb
