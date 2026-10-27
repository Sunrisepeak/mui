module space;

import std;
import openmui;
import openmui.ui.canvas;

namespace space {

namespace {

using omui::world::NodeRef;
using omui::world::Scene;

constexpr std::array<std::pair<std::string_view, Category>, 64> extensions{{
    {".c", Category::code},       {".cc", Category::code},       {".cpp", Category::code},     {".cxx", Category::code},
    {".h", Category::code},       {".hpp", Category::code},      {".cppm", Category::code},    {".ixx", Category::code},
    {".rs", Category::code},      {".go", Category::code},       {".py", Category::code},      {".js", Category::code},
    {".ts", Category::code},      {".tsx", Category::code},      {".lua", Category::code},     {".java", Category::code},
    {".kt", Category::code},      {".swift", Category::code},    {".cs", Category::code},      {".slang", Category::code},
    {".glsl", Category::code},    {".sh", Category::code},       {".md", Category::docs},      {".txt", Category::docs},
    {".rst", Category::docs},     {".pdf", Category::docs},      {".html", Category::docs},    {".htm", Category::docs},
    {".adoc", Category::docs},    {".json", Category::data},     {".toml", Category::data},    {".yaml", Category::data},
    {".yml", Category::data},     {".xml", Category::data},      {".csv", Category::data},     {".lock", Category::data},
    {".jsonl", Category::data},   {".png", Category::media},     {".jpg", Category::media},    {".jpeg", Category::media},
    {".gif", Category::media},    {".webp", Category::media},    {".svg", Category::media},    {".mp4", Category::media},
    {".mov", Category::media},    {".wav", Category::media},     {".mp3", Category::media},    {".ttf", Category::media},
    {".otf", Category::media},    {".woff2", Category::media},   {".zip", Category::archive},  {".gz", Category::archive},
    {".tar", Category::archive},  {".xz", Category::archive},    {".7z", Category::archive},   {".zst", Category::archive},
    {".o", Category::binary},     {".a", Category::binary},      {".so", Category::binary},    {".dll", Category::binary},
    {".dylib", Category::binary}, {".exe", Category::binary},    {".lib", Category::binary},   {".gcm", Category::binary},
}};

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// One folder's entries, its children's totals summed into it.
void walk(const std::filesystem::path& dir, Entry& into, std::size_t& skipped) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        ++skipped;
        return;
    }
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) {
            ++skipped;
            break;
        }
        const std::filesystem::directory_entry& e = *it;
        // Links are not followed: a folder linked into itself is not counted twice (or forever).
        if (e.is_symlink(ec)) continue;
        Entry child;
        child.name = omui::utf8(e.path().filename());
        child.path = into.path.empty() ? child.name : into.path + "/" + child.name;
        if (e.is_directory(ec)) {
            child.folder = true;
            walk(e.path(), child, skipped);
        } else if (e.is_regular_file(ec)) {
            child.bytes = e.file_size(ec);
            if (ec) {
                ++skipped;
                continue;
            }
            child.files = 1;
            child.category = category_of(child.name);
        } else {
            continue;
        }
        into.bytes += child.bytes;
        into.files += child.files;
        into.children.push_back(std::move(child));
    }
    std::ranges::sort(into.children, [](const Entry& a, const Entry& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.name < b.name; });
}

// ── the squarified treemap ───────────────────────────────────────────────

struct Box {
    double x, y, w, h;
};

// How far from square a row of areas laid along a side is: the worst ratio.
double worst(double sum, double largest, double smallest, double side) {
    const double s2 = sum * sum, w2 = side * side;
    return std::max(w2 * largest / s2, s2 / (w2 * smallest));
}

// Areas (largest first, summing to the box's) into boxes, rows along the
// shorter side, each row added to while it makes the row squarer.
std::vector<Box> squarify(std::span<const double> areas, Box r) {
    std::vector<Box> out(areas.size());
    std::size_t i = 0;
    while (i < areas.size()) {
        const double side = std::min(r.w, r.h);
        if (!(side > 0)) break;
        std::size_t j = i + 1;
        double sum = areas[i], ratio = worst(sum, areas[i], areas[i], side);
        while (j < areas.size()) {
            const double next = worst(sum + areas[j], areas[i], areas[j], side);
            if (next > ratio) break;
            sum += areas[j];
            ratio = next;
            ++j;
        }
        // The row: a strip `thick` deep along the shorter side.
        const double thick = sum / side;
        double along = 0;
        for (std::size_t k = i; k < j; ++k) {
            const double len = areas[k] / thick;
            out[k] = r.w >= r.h ? Box{r.x, r.y + along, thick, len} : Box{r.x + along, r.y, len, thick};
            along += len;
        }
        if (r.w >= r.h) r = {r.x + thick, r.y, std::max(0.0, r.w - thick), r.h};
        else r = {r.x, r.y + thick, r.w, std::max(0.0, r.h - thick)};
        i = j;
    }
    return out;
}

// A folder's header strip and margins, for its box.
double header_of(const Box& b) { return std::min(b.h * 0.08, b.w * 0.25); }

void place(const Entry& e, Box b, Scene& scene, const Entry* parent) {
    const std::string key = key_of(e.path.empty() ? std::string_view(".") : std::string_view(e.path));
    const auto n = scene.add({.kind = e.folder ? "folder" : "file",
                              .x = b.x,
                              .y = b.y,
                              .width = static_cast<float>(b.w),
                              .height = static_cast<float>(b.h),
                              .key = key,
                              .fill = e.folder ? omui::Color::hex(0x64748b, 0.10f) : colour(e.category),
                              .radius = static_cast<float>(std::min(b.w, b.h) * (e.folder ? 0.012 : 0.04))});
    scene.set(n, "title", e.name.empty() ? std::string(".") : e.name);
    scene.set(n, "path", e.path.empty() ? std::string(".") : e.path);
    scene.set(n, "size", human(e.bytes));
    scene.set(n, "bytes", static_cast<double>(e.bytes));
    scene.set(n, "share", parent && parent->bytes ? static_cast<double>(e.bytes) / static_cast<double>(parent->bytes) : 1.0);
    scene.set(n, "parent", parent ? (parent->name.empty() ? std::string(".") : parent->name) : std::string{});
    scene.set(n, "category", std::string(name(e.category)));
    if (!e.folder || e.children.empty() || e.bytes == 0) return;
    const double hdr = header_of(b), pad = hdr * 0.2;
    const Box inner{b.x + pad, b.y + hdr, b.w - 2 * pad, b.h - hdr - pad};
    if (!(inner.w > 0) || !(inner.h > 0)) return;
    // Every child at least a sliver: an empty file still has a place.
    std::vector<double> areas;
    double total = 0;
    for (const Entry& c : e.children) total += static_cast<double>(std::max<std::uint64_t>(c.bytes, 1));
    for (const Entry& c : e.children) areas.push_back(static_cast<double>(std::max<std::uint64_t>(c.bytes, 1)) / total * inner.w * inner.h);
    const std::vector<Box> boxes = squarify(areas, inner);
    for (std::size_t i = 0; i < e.children.size(); ++i) place(e.children[i], boxes[i], scene, &e);
}

// ── how the kinds draw ───────────────────────────────────────────────────

// Labels are shaped once a node (and theme), and drawn when they fit: a map
// labels what it can, it does not shrink type into blocks.
struct Labels {
    std::unordered_map<std::string, omui::typeset::Paragraph> shaped;
    bool                                                      dark = false;

    const omui::typeset::Paragraph& get(omui::typeset::FontCollection& fonts, const omui::Theme& theme, const std::string& key, std::string text,
                                        float size, float weight, omui::Color colour) {
        if (theme.dark != dark) {
            shaped.clear();
            dark = theme.dark;
        }
        auto it = shaped.find(key);
        if (it == shaped.end()) {
            omui::typeset::ParagraphStyle ps;
            ps.style.size = size;
            ps.style.weight = weight;
            ps.style.color = colour;
            ps.style.families = theme.text_style("label").families;
            ps.wrap = false;
            omui::typeset::Paragraph p(fonts, std::move(text), ps);
            p.layout(std::numeric_limits<float>::infinity());
            it = shaped.emplace(key, std::move(p)).first;
        }
        return it->second;
    }
};

// Draws a label at `at` if it fits `within`; whether it did.
bool label(omui::world::DrawContext& d, Labels& labels, const std::string& key, std::string text, float size, float weight, omui::Color colour,
           omui::Point at, omui::Rect within) {
    if (!d.fonts) return false;
    const auto& p = labels.get(*d.fonts, d.theme, key, std::move(text), size, weight, colour);
    const omui::Size s = p.size();
    if (at.x + s.width > within.right() || at.y + s.height > within.bottom()) return false;
    p.paint(d.list, at);
    return true;
}

// A file, close up: a live card — what it is, how big, its share of its folder.
omui::Element file_card(const Scene& s, NodeRef n) {
    const auto r = s.rect(n);
    const Category c = category_of(s.text(n, "title"));
    const double share = s.number(n, "share", 0);
    return omui::column({.width = omui::Dim(static_cast<float>(r.width)),
                         .height = omui::Dim(static_cast<float>(r.height)),
                         .padding = omui::Edges(std::clamp(static_cast<float>(std::min(r.width, r.height)) * 0.06f, 2.0f, 20.0f)),
                         .gap = 6,
                         .background = omui::Fill("surface"),
                         .border_color = omui::Fill(colour(c)),
                         .border_width = 3,
                         .radius = 8,
                         .clip = true},
                        {omui::text({.text = std::string(s.text(n, "title")), .style = "heading", .wrap = false}),
                         omui::text({.text = std::format("{} · {}", s.text(n, "size"), name(c)), .style = "body"}),
                         omui::text({.text = std::format("{:.1f}% of {}", share * 100, s.text(n, "parent")), .style = "caption"}),
                         omui::text({.text = std::string(s.text(n, "path")), .style = "caption", .color = omui::Fill("text_muted")})});
}

}  // namespace

Category category_of(std::string_view file) noexcept {
    const auto dot = file.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return Category::other;
    const std::string ext = lower(file.substr(dot));
    for (const auto& [e, c] : extensions)
        if (e == ext) return c;
    return Category::other;
}

std::string_view name(Category c) noexcept {
    constexpr std::array<std::string_view, 7> names{"code", "docs", "data", "media", "archive", "binary", "other"};
    return names[static_cast<std::size_t>(c)];
}

omui::Color colour(Category c) noexcept {
    constexpr std::array<std::uint32_t, 7> colours{0x6366f1, 0x10b981, 0xf59e0b, 0xec4899, 0x8b5cf6, 0x475569, 0x94a3b8};
    return omui::Color::hex(colours[static_cast<std::size_t>(c)]);
}

std::expected<Scan, std::string> scan(const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return std::unexpected(std::format("`{}` is not a folder", omui::utf8(folder)));
    Scan s;
    s.root.folder = true;
    s.root.name = omui::utf8(std::filesystem::absolute(folder, ec).filename());
    if (s.root.name.empty()) s.root.name = omui::utf8(folder);
    walk(folder, s.root, s.skipped);
    return s;
}

std::string human(std::uint64_t bytes) {
    constexpr std::array<std::string_view, 6> units{"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double v = static_cast<double>(bytes);
    std::size_t u = 0;
    while (v >= 1024 && u + 1 < units.size()) v /= 1024, ++u;
    return u == 0 ? std::format("{} B", bytes) : std::format("{:.1f} {}", v, units[u]);
}

std::string key_of(std::string_view path) {
    std::uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a
    for (unsigned char c : path) h = (h ^ c) * 0x100000001b3ull;
    return std::format("n{:016x}", h);
}

void layout(const Entry& root, Scene& scene, double width, double height) {
    scene.clear();
    auto labels = std::make_shared<Labels>();
    scene.kind("folder", omui::world::Kind{.tiers = {{0, omui::world::Tier::Draw::box}, {60, omui::world::Tier::Draw::custom}},
                                           .draw =
                                               [labels](omui::world::DrawContext& d) {
                                                   const omui::Rect r = d.screen;
                                                   const float radius = static_cast<float>(d.scene.radius(d.node) * d.zoom);
                                                   d.list.fill_round_rect(r, omui::CornerRadii::all(radius), omui::render::Paint::solid(d.scene.fill(d.node)));
                                                   d.list.stroke_round_rect(r, omui::CornerRadii::all(radius), 1,
                                                                            omui::render::Paint::solid(d.theme.color("border")));
                                                   // The header strip, when it is tall enough to read.
                                                   const auto w = d.scene.rect(d.node);
                                                   const float hdr = static_cast<float>(header_of({w.x, w.y, w.width, w.height}) * d.zoom);
                                                   if (hdr < 15) return;
                                                   const std::string key(d.scene.key(d.node));
                                                   const float size = std::min(13.0f, hdr * 0.62f) < 12 ? 11 : 13;
                                                   (void)label(d, *labels, key, std::format("{}  {}", d.scene.text(d.node, "title"), d.scene.text(d.node, "size")), size,
                                                         600, d.theme.color("text"), {r.x + 6, r.y + (hdr - size * 1.3f) / 2},
                                                         {r.x, r.y, r.width - 4, hdr});
                                               },
                                           .role = omui::Role::group});
    scene.kind("file", omui::world::Kind{.tiers = {{0, omui::world::Tier::Draw::box}, {44, omui::world::Tier::Draw::custom}, {360, omui::world::Tier::Draw::promote}},
                                         .draw =
                                             [labels](omui::world::DrawContext& d) {
                                                 const omui::Rect r = d.screen;
                                                 const float radius = static_cast<float>(d.scene.radius(d.node) * d.zoom);
                                                 d.list.fill_round_rect(r, omui::CornerRadii::all(radius), omui::render::Paint::solid(d.scene.fill(d.node)));
                                                 const std::string key(d.scene.key(d.node));
                                                 const omui::Color ink = omui::Color::hex(0xffffff, 0.95f);
                                                 // The size only beside the name: a block is never just a number.
                                                 if (!label(d, *labels, key, std::string(d.scene.text(d.node, "title")), 12, 600, ink, {r.x + 6, r.y + 5}, r)) return;
                                                 (void)label(d, *labels, key + "/size", std::string(d.scene.text(d.node, "size")), 11, 400,
                                                       omui::Color::hex(0xffffff, 0.8f), {r.x + 6, r.y + 22}, r);
                                             },
                                         .promote = file_card,
                                         .role = omui::Role::document});
    place(root, {0, 0, width, height}, scene, nullptr);
    scene.preset("overview", {width / 2, height / 2, 1});
}

std::vector<const Entry*> largest(const Entry& root, std::size_t n) {
    std::vector<const Entry*> files;
    std::function<void(const Entry&)> visit = [&](const Entry& e) {
        if (!e.folder) files.push_back(&e);
        for (const Entry& c : e.children) visit(c);
    };
    visit(root);
    std::ranges::sort(files, [](const Entry* a, const Entry* b) { return a->bytes != b->bytes ? a->bytes > b->bytes : a->path < b->path; });
    if (files.size() > n) files.resize(n);
    return files;
}

void Model::go(std::string_view key) const {
    if (!runtime) return;
    omui::Invocation call;
    call.args = omui::Value{}.set("key", omui::Value(key));
    call.actor = {"user", "space", {}};
    (void)runtime->act(runtime->find("main/map"), "focus_node", call);
}

omui::AppSpec app(Model& model) {
    omui::AppSpec a;
    a.id = "org.openmui.space";
    a.title = std::format("space — {}", model.folder);
    a.size = {1120, 760};
    a.setup = [&model](omui::Runtime& rt) {
        model.runtime = &rt;
        omui::register_canvas_component();
        layout(model.tree.root, omui::world::scene("space"));
    };
    a.view = [&model] {
        using namespace omui;
        std::vector<Element> legend;
        for (int c = 0; c < 7; ++c) {
            const auto cat = static_cast<Category>(c);
            legend.push_back(row({.gap = 4, .align = "center"},
                                 {box({.width = 10, .height = 10, .background = Fill(colour(cat)), .radius = 2}),
                                  text({.text = std::string(name(cat)), .style = "caption"})}));
        }
        std::vector<Element> top;
        int i = 0;
        for (const Entry* e : largest(model.tree.root, 5)) {
            const std::string key = key_of(e->path);
            top.push_back(button({.id = std::format("largest-{}", i++),
                                  .text = std::format("{} · {}", e->name, human(e->bytes)),
                                  .variant = ButtonVariant::ghost,
                                  .on_activated = [&model, key] { model.go(key); }}));
        }
        const Entry& root = model.tree.root;
        return column({.id = "app", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .background = "background"},
                      {column({.id = "header", .padding = Edges(8.0f, 14.0f), .gap = 6, .background = Fill("surface"), .border_color = Fill("border"),
                               .border_width = 1},
                              {row({.gap = 12, .align = "center"},
                                   {text({.text = model.folder, .style = "title", .wrap = false}),
                                    text({.text = std::format("{} in {} files{}", human(root.bytes), root.files,
                                                              model.tree.skipped ? std::format(" ({} unreadable)", model.tree.skipped) : std::string{}),
                                          .style = "body",
                                          .color = Fill("text_muted")}),
                                    text({.id = "zoom",
                                          .text = computed([&model] { return std::format("{:.3g}%", model.zoom.get() * 100); }),
                                          .style = "caption",
                                          .color = Fill("text_muted")}),
                                    spacer(),
                                    row({.gap = 10, .align = "center"}, std::move(legend))}),
                               row({.id = "largest", .gap = 4, .align = "center", .wrap = "wrap"},
                                   [&] {
                                       std::vector<Element> r{text({.text = "Largest", .style = "label"})};
                                       for (Element& b : top) r.push_back(std::move(b));
                                       return r;
                                   }())}),
                       canvas({.id = "map",
                               .label = "Folder map",
                               .max_zoom = 1e7,
                               .min_zoom = 0.02,
                               .scene = "space",
                               .start = "fit",
                               .x = model.x,
                               .y = model.y,
                               .zoom = model.zoom,
                               .grow = 1})});
    };
    return a;
}

}  // namespace space
