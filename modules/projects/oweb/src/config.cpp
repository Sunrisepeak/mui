module oweb;

import std;
import mcpplibs.os;
import mlibs.json;
import mlibs.names;
import mlibs.ts;
import mlibs.xdef;
import :site;
import :config;

namespace oweb {

namespace {

constexpr std::array<std::string_view, 8> site_fields{"title", "url", "languages", "routes", "budgets", "look", "engine", "load"};
constexpr std::array<std::string_view, 5> route_fields{"path", "source", "reference", "title", "load"};
constexpr std::array<std::string_view, 3> loads{"eager", "idle", "interaction"};
constexpr std::array<std::string_view, 1> budget_fields{"readable"};

}  // namespace

std::string define_module() {
    constexpr std::array<mlibs::ts::DefinedKind, 1> kinds{mlibs::ts::DefinedKind{"site", 1}};
    return mlibs::ts::define_module(kinds);
}

std::expected<SiteOptions, std::string> options_from(std::string_view snapshot, std::string file) {
    const auto document = mlibs::json::Document::parse(snapshot, std::move(file));
    if (!document) return std::unexpected(document.error().text());
    const mlibs::json::Node root = document->root();
    if (const auto wrong = mlibs::xdef::format_problem(root["$format"].text().value_or(""), "site", 1)) return std::unexpected(std::format("{}: {}", document->file(), *wrong));
    const auto problem = [&](const mlibs::json::Node& at, std::string message, std::string hint = {}) {
        return std::unexpected(document->problem(at, std::move(message), std::move(hint)).text());
    };
    // Every field is one the kind has: a name mistyped is said, not passed over.
    const auto known = [&](const mlibs::json::Node& object, std::span<const std::string_view> fields) -> std::optional<std::pair<std::string, mlibs::json::Node>> {
        for (const auto& [key, value] : object.members())
            if (!std::ranges::contains(fields, key)) return std::pair{key, value};
        return std::nullopt;
    };
    const mlibs::json::Node site = root["site"];
    if (!site.is_object()) return problem(root, "a site's configuration is an object", "export default defineSite({ title: \"…\" })");
    if (const auto stray = known(site, site_fields)) return problem(stray->second, "no field of a site's configuration", mlibs::names::nearest(stray->first, site_fields));

    SiteOptions options;
    if (const auto title = site["title"]) {
        if (!title.is_string()) return problem(title, "the site's title is text");
        options.title = *title.text();
    }
    if (const auto look = site["look"]) {
        if (!look.is_boolean()) return problem(look, "whether the pages are written as their frames look is yes or no", "look: true");
        options.look = *look.boolean();
    }
    if (const auto engine = site["engine"]) {
        if (!engine.is_string() || engine.text()->empty())
            return problem(engine, "the site's engine is the directory of the program its pages load, from the site's directory", "engine: \"../reader/target/web/oweb-reader\"");
        options.engine = *engine.text();
    }
    if (const auto load = site["load"]) {
        if (!load.is_string() || !std::ranges::contains(loads, *load.text())) return problem(load, "when a page takes the program is eager, idle or interaction", "load: \"idle\"");
        options.load = *load.text();
    }
    if (const auto url = site["url"]) {
        if (!url.is_string() || !(url.text()->starts_with("https://") || url.text()->starts_with("http://")))
            return problem(url, "where the site is served is an address", "url: \"https://example.org/docs/\"");
        options.url = *url.text();
    }
    if (const auto languages = site["languages"]) {
        if (!languages.is_array() || languages.size() == 0) return problem(languages, "the site's languages are a list, the default first", "languages: [\"en\", \"zh\"]");
        options.languages.clear();
        for (const mlibs::json::Node& language : languages.items()) {
            if (!language.is_string() || language.text()->empty()) return problem(language, "a language is its tag", "\"en\"");
            if (std::ranges::contains(options.languages, *language.text())) return problem(language, std::format("`{}` is stated twice", *language.text()));
            options.languages.push_back(*language.text());
        }
    }
    if (const auto routes = site["routes"]) {
        if (!routes.is_array() || routes.size() == 0)
            return problem(routes, "stated routes are a list of pages; leave the field out for every page of pages/", "routes: [{ path: \"/\", source: \"pages/index.md\" }]");
        for (const mlibs::json::Node& route : routes.items()) {
            if (!route.is_object()) return problem(route, "a route is an object", "{ path: \"/start/\", source: \"pages/start.md\" }");
            if (const auto stray = known(route, route_fields)) return problem(stray->second, "no field of a route", mlibs::names::nearest(stray->first, route_fields));
            Declared declared;
            const auto path = route["path"], source = route["source"], title = route["title"], reference = route["reference"];
            if (!path.is_string() || !path.text()->starts_with('/') || !path.text()->ends_with('/') || path.text()->contains("//"))
                return problem(path ? path : route, "a route's path is where it is served: it begins and ends with `/`", "path: \"/guide/start/\"");
            if (source && reference) return problem(reference, "a route is its page (`source`) or a catalog's reference (`reference`), not both");
            if (reference) {
                // A catalog belongs to its product's sources: it may stand outside the site's directory. The command
                // that builds the site carries its own, by its name: a site anywhere can show how it is built.
                if (!reference.is_string() || !(*reference.text() == own_catalog || reference.text()->ends_with(".ops.xdef.ts") || reference.text()->ends_with(".ops.xdef.json")))
                    return problem(reference, "a route's reference is an operation catalog, from the site's directory — or `oweb`, the catalog of the command that builds the site",
                                   "reference: \"../tool/tool.ops.xdef.ts\"");
                declared.reference = *reference.text();
            } else if (!source.is_string() || !source.text()->ends_with(".md") || source.text()->starts_with('/') ||
                       mcpplibs::os::fs::utf8(mcpplibs::os::fs::from_utf8(*source.text()).lexically_normal()).starts_with(".."))
                return problem(source ? source : route, "a route's source is its page, a Markdown file inside the site's directory", "source: \"pages/guide/start.md\"");
            if (title && !title.is_string()) return problem(title, "a route's title is text (left out: its page's first heading)");
            if (const auto load = route["load"]) {
                if (!load.is_string() || !std::ranges::contains(loads, *load.text())) return problem(load, "when a route's page takes the program is eager, idle or interaction", "load: \"interaction\"");
                declared.load = *load.text();
            }
            declared.path = *path.text();
            declared.source = source ? *source.text() : std::string{};
            declared.title = title ? *title.text() : std::string{};
            if (std::ranges::contains(options.routes, declared.path, &Declared::path)) return problem(path, std::format("the route `{}` is stated twice", declared.path));
            options.routes.push_back(std::move(declared));
        }
    }
    if (const auto budgets = site["budgets"]) {
        if (!budgets.is_object()) return problem(budgets, "budgets are an object", "budgets: { readable: 100000 }");
        if (const auto stray = known(budgets, budget_fields)) return problem(stray->second, "no budget of a site", mlibs::names::nearest(stray->first, budget_fields));
        if (const auto readable = budgets["readable"]) {
            if (!readable.is_integer() || *readable.integer() < 0) return problem(readable, "the budget is a number of bytes (0 holds none)", "readable: 100000");
            options.readable = static_cast<std::uintmax_t>(*readable.integer());
        }
    }
    return options;
}

std::expected<SiteOptions, std::string> configured(const std::filesystem::path& site) {
    const std::filesystem::path file = site / "oweb.config.ts";
    std::error_code error;
    if (!std::filesystem::exists(file, error)) return SiteOptions{};
    const auto data = mlibs::ts::evaluate(file, {.modules = {{std::string(define_name), define_module()}}});
    if (!data) return std::unexpected(data.error().message);
    if (data->kind != "site")
        return std::unexpected(std::format("{}: its default export is a `{}`, not a site: export default defineSite({{ … }}) from \"{}\"", mcpplibs::os::fs::utf8(file), data->kind, define_name));
    return options_from(data->snapshot(), mcpplibs::os::fs::utf8(file));
}

}  // namespace oweb
