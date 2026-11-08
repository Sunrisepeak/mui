module omui.site;

import std;
import omui;
import omui.ui.canvas;

namespace omui::site {
namespace {
// A round of the wall: each card a component demonstrating its state.
LiveWallPlay wall_play(Gallery& g, const GallerySite& site) {
    return {.play = [gallery = &g, site](const std::string& id) { gallery_demonstrate(*gallery, site, id); }, .few = 3, .every = 1.8};
}
}  // namespace
namespace {

using world::Tier;

// World units: a card, the gap between cards, a band's label strip and padding.
constexpr double card_width = 260, card_height = 180, gap = 28, band_strip = 48, band_pad = 20, band_gap = 32;
constexpr int per_row = 9;
// The bands, row by row: categories side by side where they fit, so the
// wall is about as wide as a screen and all of it is readable at once.
constexpr std::array<std::array<std::string_view, 2>, 4> band_rows{{{"input", ""}, {"action", "editing"}, {"navigation", "data"}, {"layout", ""}}};
constexpr std::string_view canvas_path = "main/body/view/components/page/wall/canvas";

// A category's ink on the wall.
Color group_ink(std::string_view group) {
    if (group == "input") return Color::hex(0x6366f1);
    if (group == "action") return Color::hex(0xf59e0b);
    if (group == "navigation") return Color::hex(0x14b8a6);
    if (group == "layout") return Color::hex(0x0ea5e9);
    if (group == "data") return Color::hex(0x8b5cf6);
    return Color::hex(0xf43f5e);
}

// A colour as part of a label's key: a style pack's other ink shapes it again.
std::string ink_key(Color ink) { return std::format("{:.3f},{:.3f},{:.3f},{:.3f}", ink.r, ink.g, ink.b, ink.a); }

auto words(const GallerySite& site, std::string_view en, std::string_view zh) {
    return computed([lang = site.lang, en = std::string(en), zh = std::string(zh)] { return lang && lang->get() == "zh" ? zh : en; });
}

void act_on_wall(const GallerySite& site, std::string_view action, Value args) {
    if (!site.runtime || !*site.runtime) return;
    Runtime& runtime = **site.runtime;
    Invocation call;
    call.args = std::move(args);
    (void)runtime.act(runtime.find(canvas_path), action, call);
}

// The scene, made once; its words again when the language changes.
struct WallScene {
    bool        built = false;
    std::string lang;
};
WallScene& wall_state() {
    static WallScene state;
    return state;
}

// How bands and cards look: registered again with each page's state (the
// scene outlives a page; a kind holds the page it shows).
void kinds(world::Scene& scene, Wall* wall, Gallery& g, const GallerySite& site) {
    // Shaped with this page's fonts: a page's labels, not the process's.
    auto labels = std::make_shared<world::Labels>();
    // A category's band: tinted, its name in its label column at a size fixed on screen.
    scene.kind("band", world::Kind{.tiers = {{0, Tier::Draw::custom}},
                                   .draw =
                                       [labels](world::DrawContext& d) {
                                           const Rect r = d.screen;
                                           const Color ink = d.scene.fill(d.node);
                                           const float radius = static_cast<float>(20 * d.zoom);
                                           d.list.fill_round_rect(r, CornerRadii::all(radius), xrender::Paint::solid(ink.with_alpha(0.06f)));
                                           if (!d.fonts) return;
                                           const std::string title(d.scene.text(d.node, "title"));
                                           const auto& label = labels->get(d, std::format("{}/{}/{}", d.scene.key(d.node), title, ink_key(ink)), title, 16, 650, ink);
                                           // In its strip where it fits, else just above the band.
                                           const Size size = label.size();
                                           const float strip = static_cast<float>(band_strip * d.zoom), pad = static_cast<float>(band_pad * d.zoom);
                                           const Point at = size.height + 4 <= strip ? Point{r.x + pad, r.y + (strip - size.height) / 2} : Point{r.x + 4, r.y - size.height - 4};
                                           world::Labels::paint(label, d.list, at);
                                       },
                                   .role = Role::group});
    // A component: far, a box in its category's ink; nearer, a card with its
    // name and purpose; close, the live component.
    Gallery* gallery = &g;
    scene.kind("component",
               world::Kind{.tiers = {{0, Tier::Draw::box}, {48, Tier::Draw::custom}, {100, Tier::Draw::promote}},
                           .draw =
                               [labels](world::DrawContext& d) {
                                   const Rect r = d.screen;
                                   const float radius = static_cast<float>(14 * d.zoom);
                                   // Not what a search found: a quiet outline, nothing to read.
                                   if (d.scene.number(d.node, "dim") > 0) {
                                       d.list.stroke_round_rect(r, CornerRadii::all(radius), 1, xrender::Paint::solid(d.theme.color("border").with_alpha(0.6f)));
                                       return;
                                   }
                                   d.list.fill_round_rect(r, CornerRadii::all(radius), xrender::Paint::solid(d.theme.color("surface")));
                                   d.list.stroke_round_rect(r, CornerRadii::all(radius), 1, xrender::Paint::solid(d.theme.color("border")));
                                   d.list.fill_round_rect({r.x, r.y, r.width, std::max(2.0f, static_cast<float>(6 * d.zoom))}, CornerRadii{radius, radius, 0, 0},
                                                          xrender::Paint::solid(d.scene.fill(d.node)));
                                   if (!d.fonts || r.width < 48) return;
                                   const std::string key(d.scene.key(d.node)), title(d.scene.text(d.node, "title")), purpose(d.scene.text(d.node, "purpose"));
                                   // Its name at the largest of a few sizes that fits (nothing, when none does).
                                   const float pad = std::max(6.0f, static_cast<float>(16 * d.zoom));
                                   const mlibs::text::Paragraph* fitting = nullptr;
                                   for (const float size : {15.0f, 12.0f, 10.0f}) {
                                       if (size == 15 && r.width < 160) continue;
                                       const auto& shaped = labels->get(d, std::format("{}/{}/{}/{}", key, title, size, ink_key(d.theme.color("text"))), title, size, 650, d.theme.color("text"));
                                       if (shaped.size().width + 2 * pad <= r.width) {
                                           fitting = &shaped;
                                           break;
                                       }
                                   }
                                   if (!fitting) return;
                                   const auto& name = *fitting;
                                   world::Labels::paint(name, d.list, {r.x + pad, r.y + pad});
                                   if (r.width < 160) return;
                                   const auto& line = labels->get(d, std::format("{}/{}/{}", key, purpose, ink_key(d.theme.color("text_muted"))), purpose, 12, 400, d.theme.color("text_muted"), "caption");
                                   if (line.size().width + 2 * pad <= r.width)
                                       world::Labels::paint(line, d.list, {r.x + pad, r.y + pad + name.size().height + 6});
                               },
                           .promote =
                               [gallery, site, wall](const world::Scene& s, world::NodeRef n) -> Element {
                                   const std::string id(s.key(n));
                                   const auto hover = [gallery, site, wall, id](bool on) {
                                       if (site.runtime && *site.runtime) live_wall_hover(wall->live, **site.runtime, wall_play(*gallery, site), id, on);
                                   };
                                   Element details = button({.id = "details",
                                                             .text = words(site, "Details", "详情"),
                                                             .variant = ButtonVariant::ghost,
                                                             .on_activated = [gallery, site, id] { open_component_detail(*gallery, site, id); }});
                                   return column({.id = "card",
                                                  .width = Dim(static_cast<float>(card_width)),
                                                  .height = Dim(static_cast<float>(card_height)),
                                                  .padding = Edges(12.0f, 14.0f),
                                                  .gap = 8,
                                                  .background = "surface",
                                                  .border_color = "border",
                                                  .border_width = 1,
                                                  .radius = 14,
                                                  .clip = true,
                                                  .on = {{"pointer_enter", [hover](const Value&) { hover(true); }}, {"pointer_leave", [hover](const Value&) { hover(false); }}}},
                                                 {row({.id = "head", .gap = 8, .align = "center"},
                                                      {text({.id = "name", .text = std::string(s.text(n, "title")), .style = "label", .wrap = false}), spacer(), std::move(details)}),
                                                  column({.id = "stage", .justify = "center", .align = "center", .grow = 1},
                                                         {gallery_specimen(*gallery, site, id, static_cast<float>(card_width - 28))})});
                               },
                           .role = Role::article});
}

// Where bands and cards stand: made once.
void build(world::Scene& scene) {
    scene.clear();
    double y = 0;
    for (const auto& names : band_rows) {
        double x = 0, row_height = 0;
        for (const std::string_view id : names) {
            if (id.empty()) continue;
            std::vector<const ComponentInfo*> members;
            for (const auto& info : component_catalog)
                if (info.group == id) members.push_back(&info);
            const int columns = std::min<int>(per_row, static_cast<int>(members.size()));
            const int rows = static_cast<int>((members.size() + per_row - 1) / per_row);
            const double width = 2 * band_pad + columns * (card_width + gap) - gap;
            const double height = band_strip + band_pad + rows * (card_height + gap) - gap;
            (void)scene.add({.kind = "band", .x = x, .y = y, .width = static_cast<float>(width), .height = static_cast<float>(height), .key = id,
                             .fill = group_ink(id), .radius = 20});
            for (std::size_t index = 0; index < members.size(); ++index) {
                const int column = static_cast<int>(index % per_row), row = static_cast<int>(index / per_row);
                (void)scene.add({.kind = "component",
                                 .x = x + band_pad + column * (card_width + gap),
                                 .y = y + band_strip + row * (card_height + gap),
                                 .width = static_cast<float>(card_width),
                                 .height = static_cast<float>(card_height),
                                 .key = members[index]->id,
                                 .fill = group_ink(id),
                                 .radius = 14});
            }
            x += width + band_gap;
            row_height = std::max(row_height, height);
        }
        y += row_height + band_gap;
    }
}

// The words on the wall in a language: each band's and card's title, each card's purpose.
void name(world::Scene& scene, bool zh) {
    scene.each([&](world::NodeRef n) {
        const std::string_view key = scene.key(n);
        for (const auto& group : component_groups)
            if (group.id == key) scene.set(n, "title", std::string(zh ? group.zh : group.en));
        if (const auto* info = find_component(key)) {
            scene.set(n, "title", std::string(zh ? info->zh : info->en));
            scene.set(n, "purpose", std::string(zh ? info->purpose_zh : info->purpose_en));
        }
    });
}

std::string folded(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// A search: what it does not find dimmed (and, far off, faint), the camera on what it does.
void search(Wall& wall, const GallerySite& site, const std::string& query) {
    world::Scene& scene = world::scene(wall_scene);
    const auto matches = wall_matches(query);
    scene.each([&](world::NodeRef n) {
        const auto* info = find_component(scene.key(n));
        if (!info) return;
        const bool dim = !query.empty() && std::ranges::find(matches, std::string(info->id)) == matches.end();
        scene.set(n, "dim", dim ? 1.0 : 0.0);
        scene.set_fill(n, group_ink(info->group).with_alpha(dim ? 0.18f : 1.0f), 14);
        scene.touch(n);
    });
    wall.found.set(query.empty() ? -1 : static_cast<std::int64_t>(matches.size()));
    Value::List keys;
    for (const auto& key : matches) keys.emplace_back(key);
    if (query.empty()) act_on_wall(site, "fit", Value{}.set("target", "all"));
    else if (!matches.empty()) act_on_wall(site, "fit", Value{}.set("target", Value(std::move(keys))));
}

}  // namespace

Element phone_wall(Gallery& g, const GallerySite& site, float width) {
    Gallery* gallery = &g;
    std::vector<Element> bands;
    for (const auto& group : component_groups) {
        std::vector<Element> cards;
        for (const auto& info : component_catalog) {
            if (info.group != group.id) continue;
            const std::string id(info.id);
            cards.push_back(card({.id = id,
                                  .interactive = true,
                                  .subtitle = words(site, info.purpose_en, info.purpose_zh),
                                  .title = words(site, info.en, info.zh),
                                  .variant = CardVariant::outlined,
                                  .on_activated = [gallery, site, id] { open_component_detail(*gallery, site, id); },
                                  .width = Dim(236.0f)}));
        }
        Element label = text({.id = "label", .text = words(site, group.en, group.zh), .style = "label", .color = Fill(group_ink(group.id))});
        bands.push_back(column({.id = std::string(group.id), .width = Dim(width), .gap = 8},
                               {std::move(label), scroll({.id = "cards", .horizontal = true, .vertical = false, .width = Dim(width), .label = std::string(group.en)},
                                                         {row({.id = "row", .gap = 10, .align = "stretch"}, std::move(cards))})}));
    }
    return column({.id = "wall", .width = Dim(width), .gap = 18}, std::move(bands));
}

std::vector<std::string> wall_matches(std::string_view query) {
    const std::string wanted = folded(query);
    std::vector<std::string> out;
    for (const auto& info : component_catalog)
        if (wanted.empty() ||
            folded(std::format("{} {} {} {} {}", info.id, info.en, info.zh, info.purpose_en, info.purpose_zh)).contains(wanted))
            out.emplace_back(info.id);
    return out;
}

Element component_wall(Wall& wall, Gallery& g, const GallerySite& site, float width, float height) {
    world::Scene& scene = world::scene(wall_scene);
    WallScene& state = wall_state();
    kinds(scene, &wall, g, site);
    wall.live.container = std::string(canvas_path);
    wall.live.cards.clear();
    for (const auto& info : component_catalog) wall.live.cards.emplace_back(info.id);
    if (!state.built) {
        build(scene);
        state.built = true;
    }
    const std::string lang = site.lang ? site.lang->peek() : std::string("en");
    if (lang != state.lang) {
        name(scene, lang == "zh");
        state.lang = lang;
    }
    Wall* model = &wall;
    Element find = text_box({.id = "search",
                             .label = words(site, "Search the wall", "在墙上搜索"),
                             .placeholder = words(site, "Name or purpose…", "名称或用途…"),
                             .text = wall.query,
                             .on_changed = [model, site](std::string query) { search(*model, site, query); },
                             .width = Dim(240.0f)});
    Element styles = segmented({.id = "style",
                                .label = words(site, "Style pack", "风格包"),
                                .labels = std::vector<std::string>{"Plain", "Glass", "Ink", "Liquid", "Neon", "Retro"},
                                .options = std::vector<std::string>{"plain", "glass", "ink", "liquid", "neon", "retro"},
                                .value = g.style,
                                .on_changed = [site](std::string pack) { if (site.style) site.style(std::move(pack)); }});
    Element hint = text({.id = "hint",
                               .text = computed([lang = site.lang, model] {
                                   const bool zh = lang && lang->get() == "zh";
                                   const auto found = model->found.get();
                                   if (found == 0) return std::string(zh ? "没有找到，换个词试试。" : "Nothing matched. Try another word.");
                                   if (found > 0) return zh ? std::format("找到 {} 个。", found) : std::format("{} found.", found);
                                   return std::string(zh ? "双击一张卡片放大，近看就能用。" : "Double-click a card to zoom in; up close it works.");
                               }),
                               .style = "caption",
                               .color = "text_muted"});
    Element tools = row({.id = "tools", .width = Dim(width), .gap = 12, .align = "center"},
                        {std::move(find), std::move(styles), spacer(),
                         live_wall_pause(wall.live, site.lang),
                         button({.id = "all",
                                 .text = words(site, "See all", "看全部"),
                                 .variant = ButtonVariant::ghost,
                                 .on_activated = [site] { act_on_wall(site, "fit", Value{}.set("target", "all")); }})});
    Element view = canvas({.id = "canvas",
                           .bounded = true,
                           .label = "Component wall",
                           .scene = std::string(wall_scene),
                           // Fitted the first time; after, where it was left (back from a detail: by that card).
                           .start = wall.fitted ? std::string() : std::string("fit"),
                           .x = wall.x,
                           .y = wall.y,
                           .zoom = wall.zoom,
                           .on_activated = [site](std::string key) { act_on_wall(site, "focus_node", Value{}.set("key", key)); },  // MOTION-OK: camera navigation
                           .on_camera_changed = [model](double, double, double) { model->fitted = true; },
                           .width = Dim(width),
                           .height = Dim(height)});
    // Drawn in the gallery's scope: a style pack chosen reskins every card, drawn or live.
    view.prop("theme_scope", Source(Value(std::string(gallery_scope))));
    return column({.id = "wall", .width = Dim(width), .gap = 10}, {std::move(tools), std::move(view), std::move(hint)});
}

void component_wall_tick(Wall& wall, Gallery& g, const GallerySite& site) {
    if (site.runtime && *site.runtime) live_wall_tick(wall.live, **site.runtime, wall_play(g, site));
}

}  // namespace omui::site
