// The wall as a scene: workspaces and windows, and how each looks at each size.
module openmui.showcase.markdown_wall;

import std;
import openmui.hash;
import openmui;
import openmui.ui.canvas;
import openmui.ui.markdown;

namespace omui::wall {

namespace {

using omui::world::DrawContext;
using omui::world::NodeRef;
using omui::world::Scene;
using omui::world::Tier;

constexpr float title_bar = 30;  // a window's, world units

// A workspace's colour: its name's, from a set that reads on light and dark.
omui::Color tint(std::string_view name) {
    constexpr std::array<std::uint32_t, 8> tints{0x6366f1, 0x0ea5e9, 0x10b981, 0xf59e0b, 0xec4899, 0x8b5cf6, 0x14b8a6, 0xef4444};
    const std::uint64_t h = omui::hash::fnv1a(name);
    return omui::Color::hex(tints[h % tints.size()]);
}

// A window: a title bar and its page in a scroll view. The texture and the
// live window are this one element — values for the texture (it is laid out
// on the pool), the live text's signal for the window.
omui::Element window(const Document& d, double w, double h, omui::Prop<std::string> source, omui::Color accent,
                     std::function<void(std::string)> follow = {}, std::function<void()> ticked = {}) {
    using namespace omui;
    return column({.width = Dim(static_cast<float>(w)),
                   .height = Dim(static_cast<float>(h)),
                   .background = Fill("surface"),
                   .border_color = Fill("border"),
                   .border_width = 1,
                   .radius = 10,
                   .clip = true},
                  {row({.id = "bar", .height = Dim(title_bar), .padding = Edges(0.0f, 12.0f), .gap = 8, .align = "center", .background = Fill("surface_variant")},
                       {box({.width = 8, .height = 8, .background = Fill(accent), .radius = 4}),
                        text({.text = d.title, .style = "label", .wrap = false, .max_lines = 1}),
                        spacer(),
                        text({.text = d.path, .style = "caption", .color = Fill("text_muted"), .wrap = false, .max_lines = 1})}),
                   scroll({.id = "page", .grow = 1, .padding = Edges(14.0f, 18.0f)}, {markdown({.id = "doc", .source = std::move(source), .on_link_activated = std::move(follow),
                                                                                       .on_task_toggled = [ticked = std::move(ticked)](bool, std::int32_t) {
                                                                                           if (ticked) ticked();
                                                                                       }})})});
}

// Labels shaped once a node (and theme), drawn at a size fixed on screen.
struct Labels {
    std::unordered_map<std::string, omui::typeset::Paragraph> shaped;
    bool                                                      dark = false;
    const omui::typeset::Paragraph& get(DrawContext& d, const std::string& key, std::string text, float size, float weight, omui::Color colour) {
        if (d.theme.dark != dark) {
            shaped.clear();
            dark = d.theme.dark;
        }
        auto it = shaped.find(key);
        if (it == shaped.end()) {
            omui::typeset::ParagraphStyle ps;
            ps.style.size = size;
            ps.style.weight = weight;
            ps.style.color = colour;
            ps.style.families = d.theme.text_style("label").families;
            ps.wrap = false;
            omui::typeset::Paragraph p(*d.fonts, std::move(text), ps);
            p.layout(std::numeric_limits<float>::infinity());
            it = shaped.emplace(key, std::move(p)).first;
        }
        return it->second;
    }
};

// A document's outline: its section headings (## and ###), a line each, the
// first eight — what a window far enough away to be a summary shows.
std::string outline(std::string_view md) {
    std::string out;
    std::size_t lines = 0, at = 0;
    bool fenced = false;
    while (at < md.size() && lines < 8) {
        const std::size_t end = std::min(md.find('\n', at), md.size());
        std::string_view line = md.substr(at, end - at);
        at = end + 1;
        if (line.starts_with("```")) fenced = !fenced;
        if (fenced || !(line.starts_with("## ") || line.starts_with("### "))) continue;
        const bool sub = line.starts_with("### ");
        line.remove_prefix(sub ? 4 : 3);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        out += std::format("{}{}{}", out.empty() ? "" : "\n", sub ? "   " : "", line);
        ++lines;
    }
    return out;
}

}  // namespace

void build(Model& model, Scene& scene, const Geometry& g) {
    scene.clear();
    auto labels = std::make_shared<Labels>();

    // ── kinds ──
    scene.kind("workspace",
               omui::world::Kind{.tiers = {{0, Tier::Draw::custom}},
                                 .draw =
                                     [labels, g](DrawContext& d) {
                                         const omui::Rect r = d.screen;
                                         const omui::Color c = d.scene.fill(d.node);
                                         const float radius = static_cast<float>(24 * d.zoom);
                                         d.list.fill_round_rect(r, omui::CornerRadii::all(radius), omui::render::Paint::solid(c.with_alpha(0.07f)));
                                         const float strip = static_cast<float>(g.label * d.zoom);
                                         d.list.fill_round_rect({r.x, r.y, r.width, std::min(strip, r.height)},
                                                                omui::CornerRadii{radius, radius, 0, 0}, omui::render::Paint::solid(c.with_alpha(0.16f)));
                                         d.list.stroke_round_rect(r, omui::CornerRadii::all(radius), 1, omui::render::Paint::solid(c.with_alpha(0.35f)));
                                         // Its name, readable at any zoom: on the strip when it is tall enough, above the workspace when not.
                                         if (!d.fonts) return;
                                         const std::string key(d.scene.key(d.node));
                                         const auto& p = labels->get(d, key,
                                                                     std::format("{}  ·  {}", d.scene.text(d.node, "title"),
                                                                                 d.scene.text(d.node, "count")),
                                                                     15, 650, d.theme.color("text"));
                                         const omui::Size s = p.size();
                                         if (s.width + 24 > r.width) return;
                                         const float y = strip >= s.height + 8 ? r.y + (strip - s.height) / 2 : r.y - s.height - 6;
                                         p.paint(d.list, {r.x + 16, y});
                                     },
                                 .role = omui::Role::group,
                                 .title_bar = static_cast<float>(g.label)});
    scene.kind("window",
               omui::world::Kind{// A block with its title until the page would be too small to read (miniature text names nothing).
                                 .tiers = {{0, Tier::Draw::custom}, {g.texture_from, Tier::Draw::texture}, {900, Tier::Draw::promote}},
                                 // (a block, its title; nearer, its sections too: a summary; then its page)
                                 .draw =
                                     [labels](DrawContext& d) {
                                         // T0: a block and a title strip, two instanced boxes; its title, at a size fixed
                                         // on screen, where there is room — each document known at any distance.
                                         const omui::Rect r = d.screen;
                                         const float radius = static_cast<float>(10 * d.zoom);
                                         d.list.fill_round_rect(r, omui::CornerRadii::all(radius), omui::render::Paint::solid(d.theme.color("surface")));
                                         d.list.fill_round_rect({r.x, r.y, r.width, std::max(1.0f, static_cast<float>(title_bar * d.zoom))},
                                                                omui::CornerRadii{radius, radius, 0, 0}, omui::render::Paint::solid(d.scene.fill(d.node)));
                                         if (!d.fonts || r.width < 36 || r.height < 14) return;
                                         const std::string key(d.scene.key(d.node));
                                         // Near enough (a summary): the title larger, then the document's sections, then its path.
                                         const bool summary = r.width >= 150 && r.height >= 100;
                                         const auto& p = labels->get(d, summary ? key + "#t" : key, std::string(d.scene.text(d.node, "title")), summary ? 15 : 11, 650,
                                                                     d.theme.color("text"));
                                         const omui::Size s = p.size();
                                         d.list.push_clip({r.x + 4, r.y, r.width - 8, r.height});
                                         const float top = r.y + std::min(r.height / 2 - s.height / 2, 4.0f + static_cast<float>(title_bar * d.zoom));
                                         p.paint(d.list, {r.x + 8, top});
                                         if (summary) {
                                             float y = top + s.height + 8;
                                             for (auto part : std::views::split(d.scene.text(d.node, "outline"), '\n')) {
                                                 const std::string line{std::string_view(part)};
                                                 if (line.empty()) continue;
                                                 const auto& q = labels->get(d, std::format("{}#{}", key, line), line, 12, 450, d.theme.color("text_muted"));
                                                 if (y + q.size().height > r.y + r.height - 22) break;
                                                 q.paint(d.list, {r.x + 10, y});
                                                 y += q.size().height + 3;
                                             }
                                             const auto& path = labels->get(d, key + "#p", std::string(d.scene.text(d.node, "path")), 10, 400, d.theme.color("text_muted"));
                                             path.paint(d.list, {r.x + 8, r.y + r.height - path.size().height - 6});
                                         }
                                         d.list.pop_clip();
                                     },
                                 .promote =
                                     [&model](const Scene& s, NodeRef n) {
                                         const Document* doc = model.document(s.key(n));
                                         const auto r = s.rect(n);
                                         return window(*doc, r.width, r.height, model.live(doc->key), s.fill(n),
                                                       [&model, key = doc->key](std::string href) { (void)model.follow(key, href); },
                                                       // A task ticked: the file says so too (the folder's own documents).
                                                       [&model, key = doc->key] { (void)model.save_document(key); });
                                     },
                                 .view =
                                     [&model](const Scene& s, NodeRef n) {
                                         const Document* doc = model.document(s.key(n));
                                         const auto r = s.rect(n);
                                         return window(*doc, r.width, r.height, model.text(doc->key), s.fill(n));
                                     },
                                 .role = omui::Role::window,
                                 .title_bar = title_bar});

    // ── workspaces: a folder each, in path order; windows on shelves ──
    std::vector<std::pair<std::string, std::vector<const Document*>>> groups;
    for (const Document& d : model.docs) {
        if (groups.empty() || groups.back().first != d.group) groups.push_back({d.group, {}});
        groups.back().second.push_back(&d);
    }
    struct Placed {
        double                                 w = 0, h = 0;
        std::vector<std::array<double, 4>>     windows;  // x, y, w, h inside
    };
    std::vector<Placed> placed;
    for (const auto& [name, docs] : groups) {
        Placed p;
        const auto across = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(docs.size()) * 1.6)));
        double x = g.margin, y = g.label + g.margin, shelf = 0;
        for (std::size_t i = 0; i < docs.size(); ++i) {
            if (i > 0 && i % across == 0) {
                x = g.margin;
                y += shelf + g.gap;
                shelf = 0;
            }
            const double h = window_height(*docs[i]);
            p.windows.push_back({x, y, g.window, h});
            p.w = std::max(p.w, x + g.window + g.margin);
            shelf = std::max(shelf, h);
            x += g.window + g.gap;
        }
        p.h = y + shelf + g.margin;
        placed.push_back(std::move(p));
    }
    // Workspaces in rows of about the square root of their number.
    const auto columns = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(groups.size()))));
    double gx = 0, gy = 0, row = 0;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (i > 0 && i % columns == 0) {
            gx = 0;
            gy += row + g.between;
            row = 0;
        }
        const Placed& p = placed[i];
        const auto& [name, docs] = groups[i];
        const omui::Color c = tint(name);
        const auto ws = scene.add({.kind = "workspace",
                                   .x = gx,
                                   .y = gy,
                                   .width = static_cast<float>(p.w),
                                   .height = static_cast<float>(p.h),
                                   .key = key_of("workspace:" + name),
                                   .fill = c});
        scene.set(ws, "title", name == "." ? std::string("(top)") : name);
        scene.set(ws, "count", std::format("{} document{}", docs.size(), docs.size() == 1 ? "" : "s"));
        for (std::size_t k = 0; k < docs.size(); ++k) {
            const auto& [x, y, w, h] = p.windows[k];
            const auto n = scene.add({.kind = "window",
                                      .x = gx + x,
                                      .y = gy + y,
                                      .width = static_cast<float>(w),
                                      .height = static_cast<float>(h),
                                      .key = docs[k]->key,
                                      .fill = c,
                                      .radius = 10});
            scene.set(n, "title", docs[k]->title);
            scene.set(n, "path", docs[k]->path);
            scene.set(n, "outline", outline(docs[k]->source));
            scene.set(n, "workspace", name);
        }
        gx += p.w + g.between;
        row = std::max(row, p.h);
    }
    // The named views: the whole wall (for a 1920 × 1080 view), and its first window at 1:1.
    const auto b = scene.bounds();
    scene.preset("overview", {b.x + b.width / 2, b.y + b.height / 2, std::min(1872 / b.width, 1032 / b.height)});
    if (!model.docs.empty())
        if (const auto first = scene.find(model.docs.front().key)) {
            const auto r = scene.rect(*first);
            scene.preset("focus", {r.x + r.width / 2, r.y + r.height / 2, 1});
        }
}

}  // namespace omui::wall
