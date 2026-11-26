module oweb;

import std;
import mcpplibs.os;
import mlibs.control.gen;
import mlibs.delivery;
import mlibs.image;
import mlibs.markdown;
import omui.core;
import omui.view;
import omui.semantic;
import mlibs.text.subset;
import omui.host.headless;
import omui.ui.base;
import omui.ui.markdown;
import oweb.shell;
import :site;

namespace oweb {

namespace {

namespace md = mlibs::markdown;
using Node = omui::spec::SemanticNode;

std::string utf8(const std::filesystem::path& path) { return mcpplibs::os::fs::utf8(path); }
// With `/` between its parts on every system: an address, a path a table records.
std::string generic(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::expected<std::string, std::string> read(const std::filesystem::path& file) {
    auto text = mcpplibs::os::fs::read(file);
    if (!text) return std::unexpected(std::format("{}: {}", utf8(file), text.error().message()));
    return std::move(*text);
}

// Text and attribute values as HTML and XML read them.
std::string escaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// A page's address: pages/index.md is "/", pages/guide/start.md "/guide/start/", a directory's index.md its own.
std::string address_of(const std::filesystem::path& from_pages) {
    std::filesystem::path path = from_pages;
    path.replace_extension();
    if (path.filename() == "index") path = path.parent_path();
    const std::string inner = generic(path);
    return inner.empty() || inner == "." ? "/" : std::format("/{}/", inner);
}

// Where a link written in `source` (a page, from the site's directory) leads, when that is inside the site: the
// path from the site's directory, and what follows it (`#anchor`, `?query`).
std::optional<std::pair<std::string, std::string>> inside(std::string_view source, std::string_view href) {
    if (href.empty() || href.starts_with('#') || href.starts_with('/') || href.find("://") != std::string_view::npos || href.starts_with("mailto:") ||
        href.starts_with("data:"))
        return std::nullopt;
    const std::size_t cut = href.find_first_of("#?");
    const std::string_view path = href.substr(0, cut), rest = cut == std::string_view::npos ? std::string_view{} : href.substr(cut);
    const std::filesystem::path target = (mcpplibs::os::fs::from_utf8(source).parent_path() / mcpplibs::os::fs::from_utf8(path)).lexically_normal();
    const std::string normal = generic(target);
    if (normal.starts_with("..")) return std::nullopt;
    return std::pair{normal, std::string(rest)};
}

// The default look of a page read before any engine is there: one readable column in the reader's own fonts and
// colours, light or dark as their system is, and plain on paper. A site's theme replaces it (ST4); the engine's
// own look is the canvas's (UI5 holds the two together).
constexpr std::string_view sheet =
    "html{color-scheme:light dark}"
    "body{margin:0;font:16px/1.6 system-ui,-apple-system,\"Segoe UI\",\"Noto Sans\",\"PingFang SC\",\"Microsoft YaHei\",sans-serif}"
    "[data-omui-target=app]{max-width:46rem;margin:0 auto;padding:1.5rem 1.25rem 4rem}"
    "nav{display:flex;flex-wrap:wrap;gap:.25rem 1rem}"
    "[data-omui-target=\"main/page/nav\"]{padding-bottom:1rem;border-bottom:1px solid color-mix(in srgb,currentColor 18%,transparent)}"
    "[data-omui-target=\"main/page/toc\"]{flex-direction:column;gap:.15rem;padding:.75rem 0;font-size:.9em;border-bottom:1px solid color-mix(in srgb,currentColor 18%,transparent)}"
    "[data-omui-target=\"main/page/turn\"]{justify-content:space-between;margin-top:2.5rem;padding-top:1rem;border-top:1px solid color-mix(in srgb,currentColor 18%,transparent)}"
    // A wide window: the site's pages at the left, the page's contents at the right, both staying as the page scrolls.
    "@media(min-width:72rem){"
    "[data-omui-target=app]{max-width:78rem;display:grid;grid-template-columns:14rem minmax(0,46rem) 14rem;column-gap:2rem;align-items:start}"
    "[data-omui-target=\"main/page/nav\"],[data-omui-target=\"main/page/toc\"]{flex-direction:column;flex-wrap:nowrap;position:sticky;top:1.5rem;max-height:calc(100vh - 3rem);overflow:auto;padding:0;border:0}"
    "[data-omui-target=\"main/page/nav\"]{grid-column:1;grid-row:1/span 2}"
    "[data-omui-target=\"main/page/main\"]{grid-column:2;grid-row:1}"
    "[data-omui-target=\"main/page/turn\"]{grid-column:2;grid-row:2}"
    "[data-omui-target=\"main/page/toc\"]{grid-column:3;grid-row:1/span 2}"
    "}"
    "pre{overflow:auto;padding:.75rem 1rem;border-radius:8px;background:color-mix(in srgb,currentColor 7%,transparent)}"
    "code{font:.9em ui-monospace,\"SF Mono\",Menlo,Consolas,\"Cascadia Mono\",\"DejaVu Sans Mono\",\"Liberation Mono\",\"Noto Sans Mono\",monospace}"
    "table{border-collapse:collapse}th,td{padding:.3rem .75rem;border:1px solid color-mix(in srgb,currentColor 18%,transparent)}"
    "blockquote{margin-left:0;padding-left:1rem;border-left:3px solid color-mix(in srgb,currentColor 25%,transparent)}"
    "img{max-width:100%}"
    "@media print{nav{display:none}a{color:inherit;text-decoration:none}}";

// The words the page's own parts are named by, in the page's language (a reader's tools say them; the pages' words
// are the pages'). A language without them here is named in English.
struct Named {
    std::string_view contents;  // the page's sections
    std::string_view turn;      // the page before and the page after
};
constexpr Named named_in(std::string_view language) {
    if (language.starts_with("zh")) return {"本页目录", "上一页与下一页"};
    if (language.starts_with("ja")) return {"このページの内容", "前のページと次のページ"};
    return {"On this page", "Previous and next"};
}

// The same page in another language: its address with the language's part changed — "/start/" and "/zh/start/";
// the site's first language has no part. Nothing where the site has no such page.
const Route* translated(std::span<const Route> routes, const Route& route, std::string_view language, const SiteOptions& options) {
    const std::string_view first = options.languages.empty() ? std::string_view("en") : std::string_view(options.languages.front());
    std::string_view bare = route.path;
    if (route.language != first && bare.starts_with(std::format("/{}/", route.language))) bare.remove_prefix(route.language.size() + 1);
    const std::string there = language == first ? std::string(bare) : std::format("/{}{}", language, bare);
    const auto found = std::ranges::find_if(routes, [&](const Route& other) { return other.path == there && other.language == language; });
    return found == routes.end() ? nullptr : &*found;
}

// When a page takes its program (D-260).
bool takes(std::string_view load) { return load == "eager" || load == "idle" || load == "interaction"; }

// A world's page (omui-gen dist-toy --world; omui.gen's dist_toy): a page waiting for its document. Its places, each
// there once.
constexpr std::string_view world_head = "<!--omui:head-->", world_document = "<!--omui:document-->", world_root = "var omuiRoot = \"\";",
                           world_takes = "var omuiTakes = \"eager\";";

std::filesystem::path world_of(const std::filesystem::path& site, const SiteOptions& options) {
    const std::filesystem::path stated = mcpplibs::os::fs::from_utf8(options.engine);
    return stated.is_absolute() ? stated : site / stated;
}

// What one page of a site puts at a world's places.
struct Filled {
    std::string language, title, head, document, root, load;
};

// The world's page as one page of a site; or why it is no world's page.
std::expected<std::string, std::string> from_world(std::string page, const Filled& filled, const std::filesystem::path& world) {
    const auto wrong = [&](std::string_view missing) {
        return std::unexpected(std::format("{}: no world's page — {} is not there once (a world is a program packed with `omui-gen dist-toy --world`: `omui build --web` of oweb.reader)",
                                           utf8(world / "index.html"), missing));
    };
    const auto put = [&](std::string_view place, std::string_view text) {
        const std::size_t at = page.find(place);
        if (at == std::string::npos || page.find(place, at + 1) != std::string::npos) return false;
        page.replace(at, place.size(), text);
        return true;
    };
    // The document last: what it says is its own, not a place.
    if (!put(world_head, filled.head)) return wrong(world_head);
    if (!put(world_root, std::format("var omuiRoot = \"{}\";", filled.root))) return wrong(world_root);
    if (!put(world_takes, std::format("var omuiTakes = \"{}\";", filled.load))) return wrong(world_takes);
    constexpr std::string_view opening = "<html lang=\"", title = "<title>";
    const std::size_t language = page.find(opening), language_end = language == std::string::npos ? language : page.find('"', language + opening.size());
    if (language_end == std::string::npos) return wrong("<html lang=…>");
    page.replace(language + opening.size(), language_end - language - opening.size(), escaped(filled.language));
    const std::size_t titled = page.find(title), titled_end = titled == std::string::npos ? titled : page.find("</title>", titled);
    if (titled_end == std::string::npos) return wrong("<title>");
    page.replace(titled + title.size(), titled_end - titled - title.size(), escaped(filled.title));
    if (!put(world_document, filled.document)) return wrong(world_document);
    return page;
}

// A drawing's own size as its file says it (SVG): its width and height where they are lengths in pixels, else its
// view box's, else what a browser gives one that says neither.
std::pair<int, int> drawing_size(std::string_view svg) {
    const std::size_t open = svg.find("<svg");
    const std::string_view tag = open == std::string_view::npos ? std::string_view{} : svg.substr(open, svg.find('>', open) - open);
    const auto stated = [&](std::string_view name) -> std::string_view {
        const std::string key = std::format(" {}=\"", name);
        const std::size_t at = tag.find(key);
        return at == std::string_view::npos ? std::string_view{} : tag.substr(at + key.size(), tag.find('"', at + key.size()) - at - key.size());
    };
    const auto pixels = [](std::string_view text) -> std::optional<float> {
        float value = 0;
        const auto read = std::from_chars(text.data(), text.data() + text.size(), value);
        const std::string_view unit(read.ptr, static_cast<std::size_t>(text.data() + text.size() - read.ptr));
        return read.ec == std::errc{} && value > 0 && (unit.empty() || unit == "px") ? std::optional(value) : std::nullopt;
    };
    std::array<float, 4> box{};
    std::size_t count = 0;
    for (const auto part : std::views::split(stated("viewBox"), ' '))
        if (!std::ranges::empty(part) && count < 4) std::from_chars(std::ranges::data(part), std::ranges::data(part) + std::ranges::size(part), box[count++]);
    const auto width = pixels(stated("width")), height = pixels(stated("height"));
    const bool viewed = count == 4 && box[2] > 0 && box[3] > 0;
    if (width && height) return {static_cast<int>(std::lround(*width)), static_cast<int>(std::lround(*height))};
    if (width && viewed) return {static_cast<int>(std::lround(*width)), static_cast<int>(std::lround(*width * box[3] / box[2]))};
    if (height && viewed) return {static_cast<int>(std::lround(*height * box[2] / box[3])), static_cast<int>(std::lround(*height))};
    if (viewed) return {static_cast<int>(std::lround(box[2])), static_cast<int>(std::lround(box[3]))};
    return {300, 150};
}

struct Evaluated {
    std::vector<Node> nodes;  // the whole page's, pre-order
    std::size_t       document = 0, document_end = 0;  // the document's subtree in them
};

}  // namespace

std::string relative(std::string_view from, std::string_view to) {
    const auto parts = [](std::string_view path) {
        std::vector<std::string_view> out;
        for (const auto part : std::views::split(path, '/'))
            if (!std::ranges::empty(part)) out.emplace_back(part.begin(), part.end());
        return out;
    };
    const auto a = parts(from), b = parts(to);
    std::size_t same = 0;
    while (same < a.size() && same < b.size() && a[same] == b[same]) ++same;
    std::string out;
    for (std::size_t up = same; up < a.size(); ++up) out += "../";
    for (std::size_t down = same; down < b.size(); ++down) out += std::format("{}/", b[down]);
    return out.empty() ? "./" : out;
}

std::expected<std::vector<Route>, std::string> collect(const std::filesystem::path& site, const SiteOptions& options) {
    // What the navigation and the tables say of a page, read from its document: no page is drawn for that. Its
    // language is its address's first part where that is one of the site's, the site's first otherwise.
    const auto described = [&](Route& route) -> std::expected<void, std::string> {
        const auto text = source_text(site, route);
        if (!text) return std::unexpected(text.error());
        const md::Document document = md::parse(*text);
        md::walk(document.root, [&](const md::Block& block) {
            if (block.kind == md::Kind::heading) route.headings.push_back({block.level, md::plain_text(block), block.anchor});
        });
        if (route.title.empty()) route.title = route.headings.empty() ? route.path : route.headings.front().text;
        const std::size_t end = route.path.find('/', 1);
        const std::string first = end == std::string::npos ? std::string{} : route.path.substr(1, end - 1);
        const auto language = std::ranges::find(options.languages, first);
        route.language = language != options.languages.end() ? *language : options.languages.empty() ? std::string("en") : options.languages.front();
        if (route.load.empty()) route.load = options.load;
        if (!takes(route.load))
            return std::unexpected(std::format("the route `{}` takes its program `{}`: a page takes it `eager`, `idle` or on `interaction`", route.path, route.load));
        return {};
    };
    std::vector<Route> routes;
    if (!options.routes.empty()) {
        // Stated: these pages, in this order.
        for (const Declared& declared : options.routes) {
            Route route;
            route.path = declared.path;
            route.source = declared.reference.empty() ? declared.source : declared.reference;
            route.reference = declared.reference;
            route.title = declared.title;
            route.load = declared.load;
            std::error_code missing;
            if (!std::filesystem::is_regular_file(site / mcpplibs::os::fs::from_utf8(route.source), missing))
                return std::unexpected(std::format("the route `{}` states the {} {}, which is not in {}: write it, or take the route out of oweb.config.ts", route.path,
                                                   route.reference.empty() ? "page" : "catalog", route.source, utf8(site)));
            if (std::ranges::contains(routes, route.path, &Route::path)) return std::unexpected(std::format("the route `{}` is stated twice: keep one", route.path));
            if (auto read_in = described(route); !read_in) return std::unexpected(read_in.error());
            routes.push_back(std::move(route));
        }
        return routes;
    }
    const std::filesystem::path pages = site / "pages";
    std::error_code error;
    if (!std::filesystem::is_directory(pages, error)) return std::unexpected(std::format("{} has no pages/: a site's pages are the Markdown files there", utf8(site)));
    for (const auto& entry : std::filesystem::recursive_directory_iterator(pages, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
        const std::filesystem::path from_pages = entry.path().lexically_relative(pages);
        if (generic(from_pages) == "404.md") continue;  // the page for a lost address: no route (lost_page)
        Route route;
        route.path = address_of(from_pages);
        route.source = std::format("pages/{}", generic(from_pages));
        if (auto read_in = described(route); !read_in) return std::unexpected(read_in.error());
        routes.push_back(std::move(route));
    }
    if (error) return std::unexpected(std::format("{}: {}", utf8(pages), error.message()));
    std::ranges::sort(routes, {}, &Route::path);
    for (std::size_t at = 1; at < routes.size(); ++at)
        if (routes[at].path == routes[at - 1].path)
            return std::unexpected(std::format("{} and {} are one address ({}): keep one", routes[at - 1].source, routes[at].source, routes[at].path));
    if (routes.empty()) return std::unexpected(std::format("{} has no page: a page is a Markdown file", utf8(pages)));
    return routes;
}

namespace {

// A page evaluated from its text and written. `base` is stated for the page a host answers a lost address with: it
// is served at any address, so its links are from the site's own and the document says where that is; it has no
// contents of its own to choose between and no page before or after.
std::expected<Page, std::string> written(const std::filesystem::path& site, std::span<const Route> routes, const Route& route, const SiteOptions& given, std::string_view page_text,
                                         std::string_view base) {
    using namespace omui;
    const bool lost = !base.empty();
    // A site with a program: its pages look as the program draws them. The page for a lost address is served at any
    // address and takes none: it reads, and leads home.
    SiteOptions options = given;
    if (!options.engine.empty()) options.look = true;
    const bool engine = !options.engine.empty() && !lost;

    // The page as an interface: the navigation the routes give (a site of one page has none) and its document.
    Headless host({.level = options.look ? HeadlessLevel::pixels : HeadlessLevel::layout, .size = {960, 1200}});
    if (options.look) {
        // Its look is read from what the frame drew: all of it, in each scheme a reader may prefer.
        host.runtime().set_draws_unseen(true);
        host.runtime().set_theme_by_preference([](bool dark) { return dark ? Theme::dark_theme() : Theme::light(); });
    }
    shell::Parts parts;
    if (routes.size() > 1) {
        const auto page_link = [&](const Route& to) {
            return shell::Link{std::format("r{}", &to - routes.data()), relative(route.path, to.path), to.title};
        };
        // The site's own language first, then each language's pages together.
        for (const Route& other : routes)
            if (other.language == route.language) parts.pages.push_back(page_link(other));
        // Each other language: this page in it where the site has it, that language's first page where not.
        for (const std::string& language : options.languages) {
            if (language == route.language) continue;
            const Route* there = translated(routes, route, language, options);
            if (!there) there = std::ranges::find(routes, language, &Route::language) == routes.end() ? nullptr : &*std::ranges::find(routes, language, &Route::language);
            if (there) parts.pages.push_back(page_link(*there));
        }
        parts.site = options.title.empty() ? std::string("Pages") : options.title;
    }
    // The page's own contents: its sections below its title, each a link to its anchor (a page of fewer than two has
    // nothing to choose between).
    if (!lost) {
        for (std::size_t index = 0; index < route.headings.size(); ++index) {
            const Heading& heading = route.headings[index];
            if (heading.level < 2 || heading.level > 3 || heading.anchor.empty()) continue;
            parts.sections.push_back({std::format("t{}", index), std::format("#{}", heading.anchor), heading.text});
        }
        if (parts.sections.size() < 2) parts.sections.clear();
        parts.contents = named_in(route.language).contents;
    }
    // The page before and the page after, among its language's in the routes' order.
    if (routes.size() > 1 && !lost) {
        const Route* before = nullptr;
        const Route* after = nullptr;
        bool passed = false;
        for (const Route& other : routes) {
            if (other.language != route.language) continue;
            if (other.path == route.path) passed = true;
            else if (!passed) before = &other;
            else if (!after) after = &other;
        }
        if (before) parts.beside.push_back({"before", relative(route.path, before->path), std::format("← {}", before->title)});
        if (after) parts.beside.push_back({"after", relative(route.path, after->path), std::format("{} →", after->title)});
        parts.turn = named_in(route.language).turn;
    }
    parts.document = std::string(page_text);
    // The page's pictures: its own files, found from where the page is written. What the frame needs of one is its
    // size (the page shows the file itself; with a program, the program takes the pixels the browser decoded): a
    // drawing's from its file, without its pixels.
    host.runtime().set_picture_reader([&](std::string_view source) -> std::optional<std::expected<omui::Picture, std::string>> {
        if (source.contains("://") || source.starts_with("data:") || source.starts_with("image:")) return std::nullopt;
        const std::filesystem::path from = (site / mcpplibs::os::fs::from_utf8(route.source)).parent_path();
        const std::filesystem::path file = (source.starts_with('/') ? site / "pages" / mcpplibs::os::fs::from_utf8(source.substr(1)) : from / mcpplibs::os::fs::from_utf8(source)).lexically_normal();
        std::error_code missing;
        if (!std::filesystem::is_regular_file(file, missing)) return std::unexpected(std::format("{} is not there", utf8(file)));
        if (file.extension() == ".svg") {
            const auto text = read(file);
            if (!text) return std::unexpected(text.error());
            const auto [width, height] = drawing_size(*text);
            return omui::Picture{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0), true};
        }
        const mlibs::image::Decoder* decoder = mlibs::image::decoder();
        if (!decoder || !decoder->load) return std::nullopt;
        auto pixels = decoder->load(file);
        if (!pixels) return std::unexpected(pixels.error());
        return omui::Picture{pixels->width, pixels->height, std::move(pixels->rgba), pixels->alpha == mlibs::image::Alpha::premultiplied};
    });
    host.mount(shell::mounted_at, options.view ? options.view(std::move(parts), {.scrolls = engine}) : shell::page(std::move(parts), {.scrolls = engine}));
    host.settle();

    semantic::Snapshot snapshot = host.tree().snapshot({});
    if (snapshot.nodes.empty()) return std::unexpected(std::format("{}: the page made no tree", route.source));
    // The document's subtree: its node and what follows it deeper.
    std::size_t document = snapshot.nodes.size(), document_end = snapshot.nodes.size();
    for (std::size_t at = 0; at < snapshot.nodes.size(); ++at)
        if (snapshot.nodes[at].role == Role::document) {
            document = at;
            const std::string under = snapshot.nodes[at].id + "/";
            document_end = at + 1;
            while (document_end < snapshot.nodes.size() && snapshot.nodes[document_end].id.starts_with(under)) ++document_end;
            break;
        }
    if (document == snapshot.nodes.size()) return std::unexpected(std::format("{}: the page has no document", route.source));

    // A link to another page is written as that writing's own address: the route's directory in HTML, its index.md in
    // Markdown. What stands beside the pages (a picture) is written where it stands in pages/ and found from the
    // route's directory. A link inside the site that leads nowhere — a page that is no route, an anchor its page has
    // not, a file that is not there — is a problem of the page, said once.
    std::vector<std::string> problems;
    const auto problem = [&](std::string text) {
        if (!std::ranges::contains(problems, text)) problems.push_back(std::move(text));
    };
    const auto rewritten = [&](std::string_view href, bool markdown) -> std::optional<std::string> {
        // A link that begins with `/` is the site's own address: a route's, or a file's beside the pages.
        std::optional<std::pair<std::string, std::string>> target;
        auto page = routes.end();
        if (href.starts_with('/') && !href.starts_with("//")) {
            const std::size_t cut = href.find_first_of("#?");
            std::string path(href.substr(0, cut));
            const std::string rest(cut == std::string_view::npos ? std::string_view{} : href.substr(cut));
            page = std::ranges::find_if(routes, [&](const Route& other) { return other.path == path || other.path == path + "/"; });
            target = page != routes.end() ? std::pair{page->source, rest} : std::pair{std::format("pages{}", path), rest};
            if (page == routes.end() && (path.ends_with('/') || !path.contains('.'))) {
                problem(std::format("{} links to {}, which is no route of the site", route.source, href));
                return std::nullopt;
            }
        } else {
            target = inside(route.source, href);
            if (!target) return std::nullopt;
            page = std::ranges::find(routes, target->first, &Route::source);
        }
        if (page != routes.end()) {
            if (target->second.starts_with('#') && target->second.size() > 1) {
                const std::string_view anchor = std::string_view(target->second).substr(1);
                // A heading's anchor, or a place the page's own markup names (id="…").
                if (!std::ranges::contains(page->headings, anchor, &Heading::anchor)) {
                    const auto text = source_text(site, *page);
                    if (!text || !text->contains(std::format("\"{}\"", anchor)))
                        problem(std::format("{} links to {}: {} has no heading of the anchor `{}`", route.source, href, page->source, anchor));
                }
            }
            return std::format("{}{}{}", relative(route.path, page->path), markdown ? "index.md" : "", target->second);
        }
        if (target->first.ends_with(".md")) {
            problem(std::format("{} links to {}, which is no page of the site ({})", route.source, href, target->first));
            return std::nullopt;
        }
        if (!target->first.starts_with("pages/")) return std::nullopt;
        std::error_code missing;
        if (!std::filesystem::is_regular_file(site / mcpplibs::os::fs::from_utf8(target->first), missing))
            problem(std::format("{} names {}, which is not there ({})", route.source, href, target->first));
        const std::filesystem::path beside = mcpplibs::os::fs::from_utf8(std::string_view(target->first).substr(6));
        const std::string directory = beside.has_parent_path() ? std::format("/{}/", generic(beside.parent_path())) : std::string("/");
        std::string to = relative(route.path, directory);
        if (to == "./") to.clear();
        return std::format("{}{}{}", to, generic(beside.filename()), target->second);
    };
    const auto addressed = [&](std::span<const Node> nodes, bool markdown) {
        std::vector<Node> out(nodes.begin(), nodes.end());
        for (Node& node : out)
            for (const std::string_view key : {"href", "source"})
                if (const Value* stated = node.properties.get(key); stated && stated->type() == Value::Type::text)
                    if (const auto address = rewritten(stated->as_string(), markdown)) node.properties.set(key, Value(*address));
        return out;
    };

    Page page;
    page.route = route;
    page.nodes = snapshot.nodes.size();
    // Its sections: a heading begins one; a block's own text is a line of the section it stands in. A node a text's
    // mark places (a link, a picture in a line) has its words in that text already.
    {
        std::unordered_set<std::string_view> placed;
        for (std::size_t at = document; at < document_end; ++at)
            for (const spec::Mark& mark : snapshot.nodes[at].marks)
                if (!mark.node.empty()) placed.insert(mark.node);
        std::vector<std::pair<int, std::string>> above;  // the headings the place is under: level, text
        page.sections.push_back({route.path, {}, route.title, {}, {}});
        for (std::size_t at = document + 1; at < document_end; ++at) {
            const Node& node = snapshot.nodes[at];
            if (node.role == Role::heading) {
                const Value* stated = node.properties.get("level");
                const int level = static_cast<int>(stated && stated->is_number() ? stated->as_int() : 2);
                while (!above.empty() && above.back().first >= level) above.pop_back();
                Section section{route.path, {}, node.name, {}, {}};
                if (const Value* anchor = node.properties.get("anchor"); anchor && anchor->type() == Value::Type::text) section.anchor = std::string(anchor->as_string());
                for (const auto& [_, text] : above) section.trail.push_back(text);
                above.emplace_back(level, node.name);
                // The page's first heading names the page's beginning: one section, not two.
                if (page.sections.size() == 1 && page.sections.front().text.empty() && level == 1) page.sections.front() = std::move(section);
                else page.sections.push_back(std::move(section));
                continue;
            }
            const bool worded = node.role == Role::paragraph || node.role == Role::code || node.role == Role::cell || node.role == Role::column_header ||
                                node.role == Role::row_header || node.role == Role::text || (node.role == Role::image && !placed.contains(node.id));
            if (!worded || node.name.empty() || node.properties.get("comment")) continue;
            std::string& text = page.sections.back().text;
            if (!text.empty()) text += '\n';
            text += node.name;
        }
    }
    const std::span<const Node> all = snapshot.nodes;
    page.markdown = markdown_of(addressed(all.subspan(document, document_end - document), true));
    // The navigation's links are addresses already; the document's are rewritten.
    semantic::Snapshot written = snapshot;
    {
        std::vector<Node> inner = addressed(all.subspan(document, document_end - document), false);
        std::ranges::move(inner, written.nodes.begin() + static_cast<std::ptrdiff_t>(document));
    }
    // What a view adds to operate its document (the button that copies a block of code) works only with the engine:
    // it is no part of the page as it reads without one.
    {
        const std::string under = written.nodes[document].id + "/";
        std::unordered_set<std::string> left_out;
        for (const Node& node : written.nodes)
            if (node.role == Role::button && node.id.starts_with(under)) left_out.insert(node.id);
        for (Node& node : written.nodes) std::erase_if(node.children, [&](const std::string& id) { return left_out.contains(id); });
        std::erase_if(written.nodes, [&](const Node& node) { return left_out.contains(node.id); });
    }
    semantic::Look look;
    std::string faces;
    if (options.look) {
        const bool preferred = host.runtime().dark_preferred();
        const auto read = [&](bool dark) {
            host.runtime().set_dark_preferred(dark);
            host.settle();
            return semantic::html_look(host.runtime(), snapshot);
        };
        semantic::Look light = read(false);
        look = semantic::by_schemes(std::move(light), read(true));
        host.runtime().set_dark_preferred(preferred);
        // What it shows, for the site's faces to be cut to.
        for (const Node& node : written.nodes) mlibs::text::subset::characters_of(node.name, page.characters);
        // The faces the frame set its text in: the site's own files, from wherever this page is served.
        const std::string top = lost ? std::string() : relative(route.path, "/");
        for (const semantic::Look::Font& font : look.fonts) {
            if (std::ranges::contains(page.faces, font.family)) continue;
            page.faces.push_back(font.family);
            const std::string key = semantic::font_key(font.family);
            faces += std::format("@font-face{{font-family:'{0}';src:url('{1}assets/{0}.ttf');font-weight:1 1000;font-display:swap}}", key, top == "./" ? std::string() : top);
        }
    }
    const std::string body = semantic::static_html(written, {.look = options.look ? &look : nullptr});
    // The page's sheet: the default one, or its look's — the document as tall as it is, the window scrolling it.
    const std::string style = options.look
                                  ? std::format("html{{color-scheme:light dark}}html,body{{margin:0}}{}{}[data-omui-look]{{width:auto!important;height:auto!important;min-height:100vh}}", faces,
                                                semantic::look_sheet(look))
                                  : std::string(sheet);
    // A route says where its Markdown is; the page for a lost address says where the site is, and is no page to index.
    std::string said = lost ? std::format("<base href=\"{}\">\n<meta name=\"robots\" content=\"noindex\">", escaped(base))
                            : std::string("<link rel=\"alternate\" type=\"text/markdown\" href=\"index.md\">");
    // The same page in the site's other languages, as a search engine is told of them: each language's, its own too.
    if (!lost) {
        std::string others;
        std::size_t count = 0;
        for (const std::string& language : options.languages) {
            const Route* there = language == route.language ? &route : translated(routes, route, language, options);
            if (!there) continue;
            others += std::format("\n<link rel=\"alternate\" hreflang=\"{}\" href=\"{}\">", escaped(language), escaped(relative(route.path, there->path)));
            count += language != route.language;
        }
        if (count > 0) said += others;
    }
    const std::string title = options.title.empty() || options.title == route.title ? route.title : std::format("{} · {}", route.title, options.title);
    if (engine) {
        // The world's page with this page's document in it: the same document, read with no script until the page
        // takes the program; the world's files from the site's one copy of them.
        const std::filesystem::path world = world_of(site, options);
        auto waiting = read(world / "index.html");
        if (!waiting) return std::unexpected(std::format("the site's engine: {}", waiting.error()));
        const std::string top = relative(route.path, "/");
        auto made = from_world(std::move(*waiting),
                               {.language = route.language,
                                .title = title,
                                .head = std::format("<meta name=\"generator\" content=\"oweb\">\n{}\n<style>html{{color-scheme:light dark}}{}{}</style>", said, faces, semantic::look_sheet(look)),
                                .document = body,
                                .root = std::format("{}engine/", top == "./" ? std::string() : top),
                                .load = route.load},
                               world);
        if (!made) return std::unexpected(made.error());
        page.html = std::move(*made);
    } else {
        page.html = std::format(
            "<!doctype html>\n<html lang=\"{}\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<title>{}</title>\n<meta name=\"generator\" content=\"oweb\">\n{}\n<style>{}</style>\n"
            "</head>\n<body>\n{}</body>\n</html>\n",
            escaped(route.language), escaped(title), said, style, body);
    }
    page.problems = std::move(problems);
    return page;
}

// The site's own address on its host: the path of where it is served ("https://example.org/notes/" → "/notes/").
std::string base_of(std::string_view url) {
    const std::size_t scheme = url.find("://");
    const std::size_t path = scheme == std::string_view::npos ? std::string_view::npos : url.find('/', scheme + 3);
    std::string base = path == std::string_view::npos ? std::string("/") : std::string(url.substr(path));
    if (const std::size_t cut = base.find_first_of("?#"); cut != std::string::npos) base.resize(cut);
    if (!base.ends_with('/')) base += '/';
    return base;
}

}  // namespace

std::expected<std::string, std::string> source_text(const std::filesystem::path& site, const Route& route) {
    if (route.reference.empty()) return read(site / mcpplibs::os::fs::from_utf8(route.source));
    // A reference: the catalog read now, its page what the catalog's own generator writes.
    const std::filesystem::path file = site / mcpplibs::os::fs::from_utf8(route.reference);
    const auto catalog = mlibs::control::gen::read_catalog_file(file);
    if (!catalog) {
        std::string problems;
        for (const std::string& problem : catalog.error()) problems += std::format("\n  {}", problem);
        return std::unexpected(std::format("the route `{}`: {} is no operation catalog:{}", route.path, utf8(file), problems));
    }
    return mlibs::control::gen::emit_catalog_markdown(*catalog);
}

std::expected<Page, std::string> evaluate(const std::filesystem::path& site, std::span<const Route> routes, const Route& route, const SiteOptions& options) {
    const auto text = source_text(site, route);
    if (!text) return std::unexpected(text.error());
    return written(site, routes, route, options, *text, {});
}

std::expected<Page, std::string> lost_page(const std::filesystem::path& site, std::span<const Route> routes, const SiteOptions& options) {
    if (routes.empty()) return std::unexpected("a site without a route has no page for a lost address");
    // The site's own (pages/404.md), or one that says so and leads home, in the site's first language.
    Route route;
    route.path = "/";
    route.source = "pages/404.md";
    route.language = options.languages.empty() ? std::string("en") : options.languages.front();
    std::string text;
    std::error_code missing;
    if (std::filesystem::is_regular_file(site / "pages" / "404.md", missing)) {
        auto own = read(site / "pages" / "404.md");
        if (!own) return std::unexpected(own.error());
        text = std::move(*own);
    } else {
        const Route& home = routes.front();
        const std::string to = home.source.starts_with("pages/") ? home.source.substr(6) : std::string{};
        const bool chinese = route.language.starts_with("zh");
        text = chinese ? std::format("# 没有这一页\n\n这个地址不通向本站的任何一页。{}\n", to.empty() ? std::string{} : std::format("回到[{}]({})。", home.title, to))
                       : std::format("# No such page\n\nThis address leads to no page of the site.{}\n", to.empty() ? std::string{} : std::format(" Back to [{}]({}).", home.title, to));
    }
    const md::Document document = md::parse(text);
    md::walk(document.root, [&](const md::Block& block) {
        if (block.kind == md::Kind::heading) route.headings.push_back({block.level, md::plain_text(block), block.anchor});
    });
    route.title = route.headings.empty() ? std::string("404") : route.headings.front().text;
    return written(site, routes, route, options, text, base_of(options.url));
}

std::string routes_json(std::span<const Route> routes, const SiteOptions& options) {
    using omui::Value;
    Value::List list;
    for (const Route& route : routes) {
        Value::List headings;
        for (const Heading& heading : route.headings)
            headings.push_back(Value{}.set("level", Value(static_cast<std::int64_t>(heading.level))).set("text", heading.text).set("anchor", heading.anchor));
        list.push_back(Value{}
                           .set("path", route.path)
                           .set("source", route.source)
                           .set("generated", !route.reference.empty())
                           .set("title", route.title)
                           .set("language", route.language)
                           .set("markdown", std::format("{}index.md", route.path))
                           .set("headings", Value(std::move(headings))));
        // When its page takes the site's program, where the site has one.
        if (!options.engine.empty()) list.back().set("load", route.load);
    }
    Value::List languages;
    for (const std::string& language : options.languages) languages.emplace_back(language);
    return Value{}
               .set("$format", "routes@1")
               .set("site", Value{}.set("title", options.title.empty() && !routes.empty() ? routes.front().title : options.title).set("languages", Value(std::move(languages))).set("url", options.url))
               .set("routes", Value(std::move(list)))
               .json() +
           "\n";
}

std::string llms_txt(std::span<const Route> routes, const SiteOptions& options) {
    std::string out = std::format("# {}\n\n", options.title.empty() && !routes.empty() ? routes.front().title : options.title);
    out += "Every page of this site is also plain Markdown: the address beside each title. The routes, with their headings: site.routes.xdef.json. Every section's text, to search: site.search.xdef.json.\n\n";
    const std::string base = options.url.empty() ? std::string() : options.url.ends_with('/') ? options.url.substr(0, options.url.size() - 1) : options.url;
    for (const Route& route : routes) out += std::format("- [{}]({}{}index.md)\n", route.title, base, options.url.empty() ? route.path.substr(1) : route.path);
    return out;
}

std::string sitemap_xml(std::span<const Route> routes, const SiteOptions& options) {
    const std::string base = options.url.ends_with('/') ? options.url.substr(0, options.url.size() - 1) : options.url;
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
    for (const Route& route : routes) out += std::format("  <url><loc>{}{}</loc></url>\n", escaped(base), escaped(route.path));
    return out + "</urlset>\n";
}

std::string search_json(std::span<const Section> sections) {
    using omui::Value;
    Value::List list;
    for (const Section& section : sections) {
        Value::List trail;
        for (const std::string& above : section.trail) trail.emplace_back(above);
        list.push_back(Value{}.set("route", section.route).set("anchor", section.anchor).set("title", section.title).set("trail", Value(std::move(trail))).set("text", section.text));
    }
    return Value{}.set("$format", "search@1").set("sections", Value(std::move(list))).json() + "\n";
}

namespace {

// A query's and a text's words: runs of letters and digits, lower case; each character outside ASCII a word of its
// own beside its neighbour (two in a row), so text written without spaces matches by characters in a row.
struct Words {
    std::vector<std::string> latin;   // "install", "omui"
    std::vector<std::string> joined;  // "安装", "装第"
};

Words words_of(std::string_view text) {
    Words out;
    std::string run;
    std::string previous;  // the character outside ASCII just before
    const auto end_run = [&] {
        if (!run.empty()) out.latin.push_back(std::move(run));
        run.clear();
    };
    for (std::size_t at = 0; at < text.size();) {
        const auto byte = static_cast<unsigned char>(text[at]);
        if (byte < 0x80) {
            previous.clear();
            if (std::isalnum(byte)) run += static_cast<char>(std::tolower(byte));
            else end_run();
            ++at;
            continue;
        }
        end_run();
        std::size_t length = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : 2;
        length = std::min(length, text.size() - at);
        const std::string character(text.substr(at, length));
        if (!previous.empty()) out.joined.push_back(previous + character);
        previous = character;
        at += length;
    }
    end_run();
    return out;
}

// How often a word of the query is in a text's words: whole, or as the beginning of one.
int count_of(const Words& text, std::string_view word, bool joined) {
    int count = 0;
    for (const std::string& one : joined ? text.joined : text.latin) count += joined ? one == word : one.starts_with(word);
    return count;
}

}  // namespace

std::vector<Hit> search(std::span<const Section> sections, std::string_view query, std::size_t limit) {
    const Words asked = words_of(query);
    // One character outside ASCII alone is a word too ("树").
    std::vector<std::string> single;
    if (asked.joined.empty() && asked.latin.empty()) {
        for (std::size_t at = 0; at < query.size();) {
            const auto byte = static_cast<unsigned char>(query[at]);
            const std::size_t length = std::min<std::size_t>(byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0x80 ? 2 : 1, query.size() - at);
            if (byte >= 0x80) single.emplace_back(query.substr(at, length));
            at += length;
        }
    }
    std::vector<Hit> hits;
    if (asked.latin.empty() && asked.joined.empty() && single.empty()) return hits;
    for (const Section& section : sections) {
        std::string trail;
        for (const std::string& above : section.trail) trail += above + "\n";
        const Words title = words_of(section.title), over = words_of(trail), body = words_of(section.text);
        double score = 0;
        bool all = true;
        const auto weigh = [&](std::string_view word, bool joined) {
            const int in_title = count_of(title, word, joined), in_trail = count_of(over, word, joined), in_body = count_of(body, word, joined);
            if (in_title + in_trail + in_body == 0) all = false;
            score += 3.0 * in_title + 1.5 * in_trail + std::min(in_body, 5);
        };
        for (const std::string& word : asked.latin) weigh(word, false);
        for (const std::string& word : asked.joined) weigh(word, true);
        for (const std::string& character : single)
            if (section.title.contains(character)) score += 3;
            else if (section.text.contains(character)) score += 1;
            else all = false;
        if (!all || score <= 0) continue;
        // The line that says so: the first of its text holding a word asked for, else its beginning.
        std::string excerpt;
        for (const auto line : std::views::split(std::string_view(section.text), '\n')) {
            const std::string_view one(line.begin(), line.end());
            const Words in_line = words_of(one);
            const bool holds = std::ranges::any_of(asked.latin, [&](const std::string& word) { return count_of(in_line, word, false) > 0; }) ||
                               std::ranges::any_of(asked.joined, [&](const std::string& word) { return count_of(in_line, word, true) > 0; }) ||
                               std::ranges::any_of(single, [&](const std::string& character) { return one.contains(character); });
            if (excerpt.empty() || holds) excerpt = std::string(one);
            if (holds) break;
        }
        if (excerpt.size() > 240) {
            std::size_t cut = 240;
            while (cut > 0 && (static_cast<unsigned char>(excerpt[cut]) & 0xC0) == 0x80) --cut;  // at a character's start
            excerpt = excerpt.substr(0, cut) + "…";
        }
        hits.push_back({section, score, std::move(excerpt)});
    }
    std::ranges::stable_sort(hits, [](const Hit& a, const Hit& b) { return a.score > b.score; });
    if (hits.size() > limit) hits.resize(limit);
    return hits;
}

std::expected<std::vector<Section>, std::string> sections_of(const std::filesystem::path& site, const SiteOptions& options) {
    const auto routes = collect(site, options);
    if (!routes) return std::unexpected(routes.error());
    std::vector<Section> sections;
    for (const Route& route : *routes) {
        auto page = evaluate(site, *routes, route, options);
        if (!page) return std::unexpected(page.error());
        std::ranges::move(page->sections, std::back_inserter(sections));
    }
    return sections;
}

std::expected<Built, std::string> build(const std::filesystem::path& site, const std::filesystem::path& out, const SiteOptions& given) {
    auto routes = collect(site, given);
    if (!routes) return std::unexpected(routes.error());
    SiteOptions options = given;
    if (options.title.empty()) options.title = routes->front().title;
    if (!options.engine.empty()) options.look = true;
    Built built;
    std::vector<Section> sections;
    std::vector<mlibs::delivery::File> files;
    const auto write = [&](const std::string& path, std::string_view bytes) -> std::expected<void, std::string> {
        const std::filesystem::path file = out / mcpplibs::os::fs::from_utf8(path);
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (auto written = mcpplibs::os::fs::write_atomic(file, bytes); !written) return std::unexpected(std::format("{}: {}", utf8(file), written.error().message()));
        files.push_back(mlibs::delivery::measured(path, bytes));
        return {};
    };
    // Every route evaluated before anything is written: a site with a link that leads nowhere is not built.
    std::vector<Page> pages;
    std::string problems;
    std::size_t found = 0;
    for (const Route& route : *routes) {
        auto page = evaluate(site, *routes, route, options);
        if (!page) return std::unexpected(page.error());
        for (const std::string& problem : page->problems)
            if (++found <= 12) problems += std::format("\n  {}", problem);
        pages.push_back(std::move(*page));
    }
    // What a static host answers a lost address with (GitHub Pages: 404.html at the site's top).
    auto lost = lost_page(site, *routes, options);
    if (!lost) return std::unexpected(lost.error());
    for (const std::string& problem : lost->problems)
        if (++found <= 12) problems += std::format("\n  {}", problem);
    if (found > 0)
        return std::unexpected(std::format("{} link{} inside the site lead{} nowhere; nothing was written:{}{}", found, found == 1 ? "" : "s", found == 1 ? "s" : "", problems,
                                           found > 12 ? std::format("\n  … and {} more", found - 12) : std::string{}));
    for (Page& page : pages) {
        const std::string directory = page.route.path.substr(1);
        if (auto written = write(directory + "index.html", page.html); !written) return std::unexpected(written.error());
        if (auto written = write(directory + "index.md", page.markdown); !written) return std::unexpected(written.error());
        std::ranges::move(page.sections, std::back_inserter(sections));
    }
    if (auto written = write("404.html", lost->html); !written) return std::unexpected(written.error());
    // With a look: the faces the pages' text is set in, once for the site.
    if (options.look) {
        std::vector<std::string> families = lost->faces;
        for (const Page& page : pages)
            for (const std::string& family : page.faces)
                if (!std::ranges::contains(families, family)) families.push_back(family);
        // Each cut to what the site shows: every character of its pages, and the letters, digits and marks of ASCII
        // (what a view draws beside its text: a list's numbers). A character a face is cut of is another face's, or
        // the reader's own fonts'.
        std::set<char32_t> shown = lost->characters;
        for (const Page& page : pages) shown.insert(page.characters.begin(), page.characters.end());
        for (char32_t character = 0x20; character < 0x7F; ++character) shown.insert(character);
        mlibs::text::FontCollection fonts;
        if (auto loaded = fonts.add_defaults(); !loaded) return std::unexpected(std::format("the faces of the site's look: {}", loaded.error()));
        for (const std::string& family : families) {
            const auto face = fonts.find_family(family);
            if (!face) return std::unexpected(std::format("the face `{}` a page is set in is not among the fonts here", family));
            const std::span<const std::uint8_t> whole = fonts.face_data(*face);
            // A face the cut cannot read (outlines that are not TrueType's) goes whole: the manifest says its bytes.
            std::vector<std::uint8_t> sent(whole.begin(), whole.end());
            if (auto cut = mlibs::text::subset::subset_font(whole, shown)) {
                sent = std::move(*cut);
                if (auto compact = mlibs::text::subset::compact_font_post(sent)) sent = std::move(*compact);
            }
            if (auto written = write(std::format("assets/{}.ttf", omui::semantic::font_key(family)), std::string_view(reinterpret_cast<const char*>(sent.data()), sent.size())); !written)
                return std::unexpected(written.error());
        }
    }
    // The program its pages load, once for the site: the world's files but its page, which every page was made from.
    if (!options.engine.empty()) {
        const std::filesystem::path world = world_of(site, options);
        std::error_code walking;
        std::vector<std::filesystem::path> held;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(world, walking))
            if (entry.is_regular_file() && entry.path() != world / "index.html") held.push_back(entry.path());
        if (walking) return std::unexpected(std::format("the site's engine {}: {}", utf8(world), walking.message()));
        std::ranges::sort(held);
        for (const std::filesystem::path& file : held) {
            const auto bytes = read(file);
            if (!bytes) return std::unexpected(bytes.error());
            if (auto written = write(std::format("engine/{}", generic(file.lexically_relative(world))), *bytes); !written) return std::unexpected(written.error());
        }
    }
    // What pages/ holds beside its pages (pictures, files to download), at the same place.
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(site / "pages", error)) {
        if (!entry.is_regular_file() || entry.path().extension() == ".md") continue;
        const auto bytes = read(entry.path());
        if (!bytes) return std::unexpected(bytes.error());
        if (auto written = write(generic(entry.path().lexically_relative(site / "pages")), *bytes); !written) return std::unexpected(written.error());
    }
    if (auto written = write("site.routes.xdef.json", routes_json(*routes, options)); !written) return std::unexpected(written.error());
    if (auto written = write("llms.txt", llms_txt(*routes, options)); !written) return std::unexpected(written.error());
    if (auto written = write("site.search.xdef.json", search_json(sections)); !written) return std::unexpected(written.error());
    if (!options.url.empty())
        if (auto written = write("sitemap.xml", sitemap_xml(*routes, options)); !written) return std::unexpected(written.error());
    // GitHub Pages serves the directory as it is (no Jekyll pass over it).
    if (auto written = write(".nojekyll", ""); !written) return std::unexpected(written.error());
    std::vector<mlibs::delivery::Budget> budgets;
    if (options.readable > 0) budgets.push_back({"a route readable", "**/index.html", mlibs::delivery::Measure::sent, mlibs::delivery::Over::each, options.readable});
    built.delivery = mlibs::delivery::held(std::move(files), budgets);
    // The manifest is of the site, not of itself: written last and not listed.
    {
        const std::filesystem::path file = out / "site.delivery.xdef.json";
        if (auto written = mcpplibs::os::fs::write_atomic(file, mlibs::delivery::to_json(built.delivery)); !written)
            return std::unexpected(std::format("{}: {}", utf8(file), written.error().message()));
    }
    built.routes = std::move(*routes);
    return built;
}

}  // namespace oweb
