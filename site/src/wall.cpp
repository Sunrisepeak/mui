module omui.site;

import std;
import omui;
import omui.ui.canvas;
import hanim.engine;

namespace omui::site {
namespace {

using world::Tier;

// World units: a component's card, the gap between cards, a band's label strip and padding.
constexpr double card_width = 260, card_height = 180, gap = 28, band_strip = 64, band_pad = 20, band_gap = 44;
constexpr int per_row = 9;
// The bands, row by row: categories side by side where they fit, so the
// components' region is about as wide as a screen and readable at once.
constexpr std::array<std::array<std::string_view, 2>, 4> band_rows{{{"input", ""}, {"action", "editing"}, {"navigation", "data"}, {"layout", ""}}};
// The other regions' cards (world units), and how regions stand apart.
constexpr double work_width = 560, work_height = 520, composition_width = 520, composition_height = 420, recipe_width = 400, recipe_height = 340;
constexpr double region_pad = 48, region_strip = 110, region_gap = 220;
constexpr int work_columns = 4, composition_columns = 5, recipe_columns = 8;
// The closest the camera comes: a card a little larger than life.
constexpr double closest = 2.5;

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

void act_on(Runtime** runtime, const std::string& path, std::string_view action, Value args = {}) {
    if (!runtime || !*runtime) return;
    Invocation call;
    call.args = std::move(args);
    (void)(*runtime)->act((*runtime)->find(path), action, call);
}

// A round's card played, by its key: a component demonstrating its state, a work going, a
// composition's scene flipped, a recipe played on its card.
LiveWallPlay studio_play(Wall* wall, const StudioSite& site, const std::string& canvas) {
    return {.play =
                [wall, site, canvas](const std::string& key) {
                    Runtime** runtime = site.gallery_site.runtime;
                    if (key.starts_with("work-")) {
                        Works& works = *site.works;
                        const std::string id = key.substr(5), stage = std::format("{}/{}/{}/stage", canvas, key, id);
                        if (id == "mark") works.mark_playing.set(true);
                        else if (id == "gather") works.gather_playing.set(true);
                        else if (id == "soft") act_on(runtime, stage, "drop");
                        else if (id == "letters") works.other.set(!works.other.peek());
                        else if (id == "spring") act_on(runtime, stage, "fling", Value{}.set("vx", 1400.0).set("vy", -700.0));
                        else if (id == "agent") works.rise_playing.set(true);
                        else if (id == "glass") works.together.set(!works.together.peek());
                        else if (id == "layout") works.cards.set(!works.cards.peek());
                    } else if (key.starts_with("composition-")) {
                        motion_case_flip(wall->compositions, std::string_view(key).substr(12));
                    } else if (key.starts_with("recipe-")) {
                        act_on(runtime, std::format("{}/{}/{}/stage", canvas, key, key.substr(7)), "play");
                    } else {
                        gallery_demonstrate(*site.gallery, site.gallery_site, key);
                    }
                },
            .few = 3,
            .every = 1.8};
}

// The pointer on a card plays it (and keeps it playing while it stays).
std::function<void(bool)> hover_for(Wall* wall, const StudioSite& site, const std::string& canvas, const std::string& key) {
    return [wall, site, canvas, key](bool on) {
        Runtime** runtime = site.gallery_site.runtime;
        if (runtime && *runtime) live_wall_hover(wall->live, **runtime, studio_play(wall, site, canvas), key, on);
    };
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

// How regions, bands and cards look: registered again with each page's state
// (the scene outlives a page; a kind holds the page it shows).
void kinds(world::Scene& scene, Wall* wall, const StudioSite& studio_site, const std::string& canvas) {
    const GallerySite& site = studio_site.gallery_site;
    Gallery* gallery = studio_site.gallery;
    Works* works = studio_site.works;
    // Shaped with this page's fonts: a page's labels, not the process's.
    auto labels = std::make_shared<world::Labels>();
    // A region: a quiet field under its cards, its name at a size fixed on screen.
    scene.kind("region", world::Kind{.tiers = {{0, Tier::Draw::custom}},
                                     .draw =
                                         [labels](world::DrawContext& d) {
                                             const Rect r = d.screen;
                                             d.list.fill_round_rect(r, CornerRadii::all(static_cast<float>(40 * d.zoom)),
                                                                    xrender::Paint::solid(d.theme.color("surface_variant").with_alpha(0.55f)));
                                             if (!d.fonts) return;
                                             const std::string title(d.scene.text(d.node, "title"));
                                             const Color ink = d.theme.color("text");
                                             const auto& label = labels->get(d, std::format("{}/{}/{}", d.scene.key(d.node), title, ink_key(ink)), title, 22, 700, ink);
                                             const float pad = static_cast<float>(region_pad * d.zoom), strip = static_cast<float>(region_strip * d.zoom);
                                             // In its strip where it fits; far off, just above the region.
                                             const Size size = label.size();
                                             const Point at = strip >= size.height + 12 ? Point{r.x + pad, r.y + pad / 2 + (strip - size.height) / 2} : Point{r.x, r.y - size.height - 8};
                                             world::Labels::paint(label, d.list, at);
                                         },
                                     .role = Role::region});
    // A card far off: a box; nearer, a card with its name (and, wide enough, its purpose); a
    // search's misses a quiet outline. Close enough, each kind's card goes live (its promote).
    const auto titled = [labels](world::DrawContext& d) {
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
                               };
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
                                           const auto& label = labels->get(d, std::format("{}/{}/{}", d.scene.key(d.node), title, ink_key(ink)), title, 14, 650, ink);
                                           // In its strip where it fits, else just above the band.
                                           const Size size = label.size();
                                           const float strip = static_cast<float>(band_strip * d.zoom), pad = static_cast<float>(band_pad * d.zoom);
                                           const Point at = size.height + 4 <= strip ? Point{r.x + pad, r.y + (strip - size.height) / 2} : Point{r.x + 4, r.y - size.height - 4};
                                           world::Labels::paint(label, d.list, at);
                                       },
                                   .role = Role::group});
    // A component: far, a box in its category's ink; nearer, a card with its
    // name and purpose; from a hundred pixels across, the live component.
    scene.kind("component",
               world::Kind{.tiers = {{0, Tier::Draw::box}, {48, Tier::Draw::custom}, {72, Tier::Draw::promote}},
                           .draw = titled,
                           .promote =
                               [gallery, site, wall, studio_site, canvas](const world::Scene& s, world::NodeRef n) -> Element {
                                   const std::string id(s.key(n));
                                   const auto hover = hover_for(wall, studio_site, canvas, id);
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
    // A signature work: its stage and controls (live); the pointer on it plays it.
    scene.kind("work", world::Kind{.tiers = {{0, Tier::Draw::box}, {40, Tier::Draw::custom}, {72, Tier::Draw::promote}},
                                   .draw = titled,
                                   .promote =
                                       [works, studio_site, wall, canvas](const world::Scene& s, world::NodeRef n) -> Element {
                                           const std::string key(s.key(n)), id = key.substr(5);
                                           const auto found = std::ranges::find(work_ids, std::string_view(id));
                                           if (found == work_ids.end()) return text({.id = "missing", .text = id});
                                           auto cards = work_cards(*works, studio_site.works_site, static_cast<float>(work_width),
                                                                   [canvas](std::string_view work) { return std::format("{}/work-{}/{}", canvas, work, work); });
                                           Element card = std::move(cards[static_cast<std::size_t>(found - work_ids.begin())]);
                                           const auto hover = hover_for(wall, studio_site, canvas, key);
                                           card.handlers.emplace_back("pointer_enter", Handler([hover](const Value&) { hover(true); }));
                                           card.handlers.emplace_back("pointer_leave", Handler([hover](const Value&) { hover(false); }));
                                           return card;
                                       },
                                   .role = Role::article});
    // A composition: its live scene in its own state and material.
    scene.kind("composition", world::Kind{.tiers = {{0, Tier::Draw::box}, {40, Tier::Draw::custom}, {72, Tier::Draw::promote}},
                                   .draw = titled,
                                          .promote =
                                              [studio_site, wall, canvas](const world::Scene& s, world::NodeRef n) -> Element {
                                                  const std::string key(s.key(n));
                                                  const auto* info = find_motion_case(std::string_view(key).substr(12), true);
                                                  if (!info) return text({.id = "missing", .text = key});
                                                  return motion_case_card(wall->compositions, studio_site.compositions_site, *info, true, static_cast<float>(composition_width),
                                                                          static_cast<float>(composition_height - 104), hover_for(wall, studio_site, canvas, key));
                                              },
                                          .role = Role::article});
    // A recipe of hanim's library on its card; its Tune opens the editor beside the canvas.
    scene.kind("recipe", world::Kind{.tiers = {{0, Tier::Draw::box}, {40, Tier::Draw::custom}, {72, Tier::Draw::promote}},
                                   .draw = titled,
                                     .promote =
                                         [works, studio_site, wall, canvas](const world::Scene& s, world::NodeRef n) -> Element {
                                             const std::string key(s.key(n));
                                             const auto* info = hanim::anim::find_recipe(std::string_view(key).substr(7));
                                             if (!info) return text({.id = "missing", .text = key});
                                             return recipe_card(*works, studio_site.works_site, *info, static_cast<float>(recipe_width), std::format("{}/{}/{}", canvas, key, info->name),
                                                                hover_for(wall, studio_site, canvas, key), [wall] { wall->tuning.set(true); });
                                         },
                                     .role = Role::article});
}

// A node to be placed (laid out first, so the regions go under their cards).
struct Planned {
    std::string kind, key;
    double      x = 0, y = 0, width = 0, height = 0;
    Color       fill = Color::transparent();
    float       radius = 0;
};
struct Extent {
    double right = 0, bottom = 0;
};

// The components, a band a category, from (left, top).
Extent plan_components(std::vector<Planned>& out, double left, double top) {
    double y = top, right = left;
    for (const auto& names : band_rows) {
        double x = left, row_height = 0;
        for (const std::string_view id : names) {
            if (id.empty()) continue;
            std::vector<const ComponentInfo*> members;
            for (const auto& info : component_catalog)
                if (info.group == id) members.push_back(&info);
            const int columns = std::min<int>(per_row, static_cast<int>(members.size()));
            const int rows = static_cast<int>((members.size() + per_row - 1) / per_row);
            const double width = 2 * band_pad + columns * (card_width + gap) - gap;
            const double height = band_strip + band_pad + rows * (card_height + gap) - gap;
            out.push_back({"band", std::string(id), x, y, width, height, group_ink(id), 20});
            for (std::size_t index = 0; index < members.size(); ++index) {
                const int column = static_cast<int>(index % per_row), row = static_cast<int>(index / per_row);
                out.push_back({"component", std::string(members[index]->id), x + band_pad + column * (card_width + gap), y + band_strip + row * (card_height + gap),
                               card_width, card_height, group_ink(id), 14});
            }
            right = std::max(right, x + width);
            x += width + band_gap;
            row_height = std::max(row_height, height);
        }
        y += row_height + band_gap;
    }
    return {right, y - band_gap};
}

// Cards of a kind in rows, from (left, top).
Extent plan_cards(std::vector<Planned>& out, std::string_view kind, const std::vector<std::string>& keys, double left, double top, double width, double height,
                  int columns) {
    Extent extent{left, top};
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const double x = left + static_cast<double>(index % static_cast<std::size_t>(columns)) * (width + gap);
        const double y = top + static_cast<double>(index / static_cast<std::size_t>(columns)) * (height + gap);
        out.push_back({std::string(kind), keys[index], x, y, width, height, Color::hex(0xe7e7ef), 16});
        extent.right = std::max(extent.right, x + width);
        extent.bottom = std::max(extent.bottom, y + height);
    }
    return extent;
}

// The studio: the components and the works side by side, the compositions and the recipes under them.
void build(world::Scene& scene) {
    scene.clear();
    std::vector<Planned> cards, regions;
    const auto region = [&](std::string_view name, double left, double top, Extent extent) {
        regions.push_back({"region", std::format("region-{}", name), left - region_pad, top - region_pad - region_strip, extent.right - left + 2 * region_pad,
                           extent.bottom - top + 2 * region_pad + region_strip, Color::transparent(), 40});
    };
    const Extent components = plan_components(cards, 0, 0);
    region("components", 0, 0, components);
    const double second = components.right + 2 * region_pad + region_gap;
    std::vector<std::string> works, compositions, recipes;
    for (const auto id : work_ids) works.push_back(std::format("work-{}", id));
    for (const auto& info : creative_motions()) compositions.push_back(std::format("composition-{}", info.id));
    for (const auto& info : hanim::anim::recipes()) recipes.push_back(std::format("recipe-{}", info.name));
    const Extent signature = plan_cards(cards, "work", works, second, 0, work_width, work_height, work_columns);
    region("works", second, 0, signature);
    const double below = std::max(components.bottom, signature.bottom) + 2 * region_pad + region_strip + region_gap;
    const Extent scenes = plan_cards(cards, "composition", compositions, 0, below, composition_width, composition_height, composition_columns);
    region("compositions", 0, below, scenes);
    const Extent library = plan_cards(cards, "recipe", recipes, second, below, recipe_width, recipe_height, recipe_columns);
    region("recipes", second, below, library);
    for (const auto* list : {&regions, &cards})
        for (const Planned& planned : *list)
            (void)scene.add({.kind = planned.kind, .x = planned.x, .y = planned.y, .width = static_cast<float>(planned.width), .height = static_cast<float>(planned.height),
                             .key = planned.key, .fill = planned.fill, .radius = planned.radius});
}

// The words on the canvas in a language: each region's, band's and card's title, each component's purpose.
void name(world::Scene& scene, bool zh) {
    scene.each([&](world::NodeRef n) {
        const std::string_view key = scene.key(n);
        for (const auto& group : component_groups)
            if (group.id == key) scene.set(n, "title", std::string(zh ? group.zh : group.en));
        if (key.starts_with("region-")) {
            const std::string_view region = key.substr(7);
            const std::string_view title = region == "components"     ? (zh ? "组件" : "Components")
                                           : region == "works"        ? (zh ? "招牌作品" : "Signature works")
                                           : region == "compositions" ? (zh ? "动效组合" : "Compositions")
                                                                      : (zh ? "配方库" : "Recipes");
            scene.set(n, "title", std::string(title));
        }
        if (key.starts_with("work-")) scene.set(n, "title", std::string(key.substr(5)));
        if (key.starts_with("recipe-")) scene.set(n, "title", std::string(key.substr(7)));
        if (key.starts_with("composition-"))
            if (const auto* info = find_motion_case(key.substr(12), true)) scene.set(n, "title", std::string(zh ? info->zh : info->en));
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
void search(Wall& wall, Runtime** runtime, const std::string& canvas, const std::string& query) {
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
    if (query.empty()) act_on(runtime, canvas, "fit", Value{}.set("target", "region-components"));
    else if (!matches.empty()) act_on(runtime, canvas, "fit", Value{}.set("target", Value(std::move(keys))));
}

// The cards the camera shows now, large enough to be live (the rounds' choice).
std::vector<std::string> on_camera(const Wall& wall, Runtime& runtime, const std::string& canvas) {
    std::vector<std::string> out;
    const NodeId id = runtime.find(canvas);
    if (!runtime.valid(id)) return out;
    const Rect box = runtime.screen_bounds(id);
    const double zoom = wall.zoom.peek();
    if (box.width <= 0 || box.height <= 0 || zoom <= 0) return out;
    const world::WorldRect view{wall.x.peek() - box.width / 2 / zoom, wall.y.peek() - box.height / 2 / zoom, box.width / zoom, box.height / zoom};
    world::Scene& scene = world::scene(wall_scene);
    scene.each([&](world::NodeRef n) {
        const std::string_view kind = scene.kind_name(n);
        if (kind == "region" || kind == "band") return;
        const world::WorldRect rect = scene.rect(n);
        if (rect.overlaps(view) && rect.width * zoom >= 72) out.emplace_back(scene.key(n));
    });
    return out;
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
    Element heading = text({.id = "heading", .text = words(site, "Components", "组件"), .style = "heading"});
    heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1})));
    bands.insert(bands.begin(), std::move(heading));
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

Element studio(Wall& wall, const StudioSite& site, std::string_view region, float width, float height) {
    world::Scene& scene = world::scene(wall_scene);
    WallScene& state = wall_state();
    const std::string canvas_path = site.root + "/canvas";
    Wall* const model = &wall;
    kinds(scene, model, site, canvas_path);
    if (!state.built) {
        build(scene);
        state.built = true;
    }
    const GallerySite& gallery_site = site.gallery_site;
    const std::string lang = gallery_site.lang ? gallery_site.lang->peek() : std::string("en");
    if (lang != state.lang) {
        name(scene, lang == "zh");
        state.lang = lang;
    }
    const bool zh = lang == "zh";
    Runtime** runtime = gallery_site.runtime;
    wall.live.container.clear();
    wall.live.cards.clear();
    wall.live.visible = [model, runtime, canvas_path] { return runtime && *runtime ? on_camera(*model, **runtime, canvas_path) : std::vector<std::string>{}; };
    if (wall.region.peek() != region) wall.region.set(std::string(region));
    // Another place's button: it arrives at that place's region (back from a detail, where it was left).
    if (wall.arrived != region) {
        wall.arrived = std::string(region);
        wall.fitted = false;
    }
    // The place's name, the page's heading.
    Element heading = text({.id = "heading", .text = std::string(region == "components" ? (zh ? "组件" : "Components") : (zh ? "动效" : "Motion")), .style = "heading"});
    heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{1})));
    // Its regions, a chip each: the camera flies there (MOTION-OK: camera navigation).
    Element regions = segmented({.id = "region",
                                 .label = words(gallery_site, "Region", "区域"),
                                 .labels = zh ? std::vector<std::string>{"组件", "招牌作品", "动效组合", "配方库"}
                                              : std::vector<std::string>{"Components", "Works", "Compositions", "Recipes"},
                                 .options = std::vector<std::string>{studio_regions.begin(), studio_regions.end()},
                                 .value = wall.region,
                                 .on_changed = [runtime, canvas_path](std::string chosen) {
                                     act_on(runtime, canvas_path, "fit", Value{}.set("target", std::format("region-{}", chosen)));
                                 }});
    Element find = text_box({.id = "search",
                             .label = words(gallery_site, "Search the components", "搜索组件"),
                             .placeholder = words(gallery_site, "Name or purpose…", "名称或用途…"),
                             .text = wall.query,
                             .on_changed = [model, runtime, canvas_path](std::string query) { search(*model, runtime, canvas_path, query); },
                             .width = Dim(200.0f)});
    Element styles = segmented({.id = "style",
                                .label = words(gallery_site, "Style pack", "风格包"),
                                .labels = std::vector<std::string>{"Plain", "Glass", "Ink", "Liquid", "Neon", "Retro"},
                                .options = std::vector<std::string>{"plain", "glass", "ink", "liquid", "neon", "retro"},
                                .value = site.gallery->style,
                                .on_changed = [gallery_site](std::string pack) { if (gallery_site.style) gallery_site.style(std::move(pack)); }});
    Element tools = row({.id = "tools", .width = Dim(width), .gap = 12, .align = "center", .wrap = "wrap"},
                        {std::move(heading), std::move(regions), std::move(find), std::move(styles), spacer(), live_wall_pause(wall.live, gallery_site.lang),
                         button({.id = "all",
                                 .text = words(gallery_site, "See all", "看全部"),
                                 .variant = ButtonVariant::ghost,
                                 .on_activated = [runtime, canvas_path] { act_on(runtime, canvas_path, "fit", Value{}.set("target", "all")); }})});
    Element hint = text({.id = "hint",
                         .text = computed([lang = gallery_site.lang, model] {
                             const bool chinese = lang && lang->get() == "zh";
                             const auto found = model->found.get();
                             if (found == 0) return std::string(chinese ? "没有找到，换个词试试。" : "Nothing matched. Try another word.");
                             if (found > 0) return chinese ? std::format("找到 {} 个。", found) : std::format("{} found.", found);
                             return std::string(chinese ? "拖动平移，滚轮缩放；双击一张卡片放大，近看就能用；双击空白回到全部。"
                                                        : "Drag to pan, scroll to zoom; double-click a card to zoom in and use it; double-click empty space for all of it.");
                         }),
                         .style = "caption",
                         .color = "text_muted"});
    // The canvas, and beside it the recipe being tuned (an inspector) while it is open.
    Element body = dynamic([model, site, runtime, canvas_path, region = std::string(region), zh, width, height] {
        const bool inspector = model->tuning.get() && width >= 1000;
        Element view = canvas({.id = "canvas",
                               .bounded = true,
                               .label = zh ? "核心特性画布" : "Core features canvas",
                               .max_zoom = closest,
                               .scene = std::string(wall_scene),
                               // At its region the first time; after, where it was left (back from a detail: by that card).
                               .start = model->fitted ? std::string() : std::format("fit:region-{}", region),
                               .x = model->x,
                               .y = model->y,
                               .zoom = model->zoom,
                               // A region double-clicked: all of it in view; a card: close up (MOTION-OK: camera navigation).
                               .on_activated = [runtime, canvas_path](std::string key) {
                                   if (key.starts_with("region-")) act_on(runtime, canvas_path, "fit", Value{}.set("target", key));
                                   else act_on(runtime, canvas_path, "focus_node", Value{}.set("key", key));
                               },
                               .on_camera_changed = [model](double, double, double) { model->fitted = true; },
                               .width = Dim(inspector ? width - 380 : width),
                               .height = Dim(height)});
        // Drawn in the gallery's scope: a style pack chosen reskins every component, drawn or live.
        view.prop("theme_scope", Source(Value(std::string(gallery_scope))));
        if (!inspector) return std::vector<Element>{std::move(view)};
        Element editor = column({.id = "inspector", .width = Dim(360.0f), .gap = 10},
                                {row({.id = "head", .gap = 8, .align = "center"},
                                     {text({.id = "title", .text = std::string(zh ? "调整配方" : "Tune a recipe"), .style = "label"}), spacer(),
                                      button({.id = "close", .text = std::string(zh ? "关闭" : "Close"), .variant = ButtonVariant::ghost,
                                              .on_activated = [model] { model->tuning.set(false); }})}),
                                 recipe_editor(*site.works, site.works_site, 360)});
        return std::vector<Element>{row({.id = "studio", .width = Dim(width), .gap = 20, .align = "start"}, {std::move(view), std::move(editor)})};
    });
    return column({.id = "wall", .width = Dim(width), .gap = 10}, {std::move(tools), std::move(body), std::move(hint)});
}

void studio_tick(Wall& wall, const StudioSite& site) {
    if (site.gallery_site.runtime && *site.gallery_site.runtime)
        live_wall_tick(wall.live, **site.gallery_site.runtime, studio_play(&wall, site, site.root + "/canvas"));
}

}  // namespace omui::site
