module omui.site;

import std;
import omui;
import mlibs.math;
import omui.ui.canvas;

namespace omui::site {
namespace {

// The cards at least partly on screen (under the header, above the footer).
std::vector<std::string> on_screen(const LiveWall& wall, Runtime& runtime) {
    std::vector<std::string> out;
    const Size window = runtime.size();
    const auto shown = [&](const Rect& box) { return box.width > 0 && box.bottom() > 64 && box.y < window.height - 32 && box.right() > 0 && box.x < window.width; };
    if (!wall.container.empty()) {
        const NodeId id = runtime.find(wall.container);
        if (runtime.valid(id) && shown(runtime.screen_bounds(id))) out = wall.cards;
        return out;
    }
    for (const std::string& card : wall.cards) {
        const NodeId id = runtime.find(card);
        if (!runtime.valid(id)) continue;
        if (shown(runtime.screen_bounds(id))) out.push_back(card);
    }
    return out;
}

bool motion_full(const Runtime& runtime) { return !runtime.reduce_motion() && runtime.motion_level() == MotionLevel::full; }

// A round: a few visible cards at random, and the one under the pointer.
void round(LiveWall& wall, Runtime& runtime, const LiveWallPlay& how) {
    std::vector<std::string> visible = on_screen(wall, runtime);
    mlibs::math::SplitMix64 random{wall.stream};
    wall.picked.clear();
    const auto few = std::min<std::size_t>(static_cast<std::size_t>(std::max(how.few, 0)), visible.size());
    while (wall.picked.size() < few) {
        const std::size_t index = static_cast<std::size_t>(random.next() % visible.size());
        wall.picked.push_back(visible[index]);
        visible.erase(visible.begin() + static_cast<std::ptrdiff_t>(index));
    }
    wall.stream = random.state;
    if (!wall.hovered.empty() && std::ranges::find(wall.picked, wall.hovered) == wall.picked.end()) wall.picked.push_back(wall.hovered);
    for (const std::string& card : wall.picked) how.play(card);
}

std::string_view last_segment(std::string_view path) { return path.substr(path.rfind('/') + 1); }

}  // namespace

void live_wall_stop(LiveWall& wall, Runtime& runtime) {
    if (wall.timer) runtime.cancel(std::exchange(wall.timer, 0));
}

void live_wall_tick(LiveWall& wall, Runtime& runtime, const LiveWallPlay& how) {
    if (wall.paused.peek() || !motion_full(runtime) || on_screen(wall, runtime).empty()) {
        live_wall_stop(wall, runtime);
        return;
    }
    if (wall.timer) return;
    wall.timer = runtime.after(seconds(how.every), [&wall, &runtime, how] {
        wall.timer = 0;
        round(wall, runtime, how);
        live_wall_tick(wall, runtime, how);
    });
}

void live_wall_hover(LiveWall& wall, Runtime& runtime, const LiveWallPlay& how, const std::string& card, bool on) {
    if (!on) {
        if (wall.hovered == card) wall.hovered.clear();
        return;
    }
    if (wall.hovered == card) return;
    wall.hovered = card;
    if (motion_full(runtime)) how.play(card);
    live_wall_tick(wall, runtime, how);
}

Element live_wall_pause(LiveWall& wall, Signal<std::string>* lang) {
    Signal<bool> paused = wall.paused;
    Element pause = button({.id = "pause",
                            .text = computed([paused, lang] {
                                const bool zh = lang && lang->get() == "zh";
                                return std::string(paused.get() ? (zh ? "播放动效" : "Play motion") : (zh ? "暂停动效" : "Pause motion"));
                            }),
                            .variant = ButtonVariant::ghost,
                            .on_activated = [paused] { paused.set(!paused.peek()); }});
    pause.prop("pressed", derive([paused] { return paused.get(); }));
    return pause;
}

Value motion_case_config(const MotionCaseInfo& info) {
    if (info.authored_program) return Value("none");  // only the root's state-morph slot; its own program plays in the gallery
    Value main = Value{}.set("use", info.use).set("order", info.order).set("stagger", info.stagger).set("rise", info.rise).set("grow", info.grow).set("stretch", info.stretch);
    if (!info.curve.empty()) main.set("curve", info.curve);
    if (info.vertex_path) main.set("vertex_path", Value{}.set("bend", info.bend));
    if (info.use == "liquid" || info.use == "ink") return Value(Value::List{main, Value("simple")});
    return main;
}

namespace {

LiveWallPlay motion_play(MotionWall& wall) {
    return {.play = [&wall](const std::string& card) {
                // A round flips the scene between its two states: it moves as its motion says.
                if (const auto found = wall.states.find(last_segment(card)); found != wall.states.end())
                    found->second->end.set(!found->second->end.peek());
            },
            .few = 2,
            .every = 2.2};
}

}  // namespace

void motion_wall_tick(MotionWall& wall, const MotionWallSite& site) {
    if (site.runtime && *site.runtime) live_wall_tick(wall.live, **site.runtime, motion_play(wall));
}

namespace {

// A case's card (live on the canvas, or in the phone's column): its scene in its own state and
// scope, its name (opening its page) and its purpose; the pointer on it plays it.
Element motion_card(MotionWall& wall, const MotionWallSite& site, const MotionCaseInfo& info, bool creative, float card_width, float scene_height,
                    std::function<void(bool)> hover) {
    const bool zh = site.lang && site.lang->peek() == "zh";
    const std::string id(info.id);
    const float scene_width = card_width - 24;
    auto& state = wall.states[id];
    if (!state) state = std::make_unique<MotionSceneState>();
    const std::string scope = std::format("wall-{}-{}", creative ? "creative" : "basic", id);
    if (site.style) site.style(scope, std::string(info.style));
    MotionSceneContext context{.state = state.get(), .language = site.lang, .width = scene_width, .height = scene_height, .changed = [] {},
                               .clipboard = [runtime = site.runtime](std::string text) {
                                   if (runtime && *runtime) (*runtime)->set_clipboard_text(std::move(text));
                               }};
    // Built again as its state changes (what the scene reads, it follows).
    Element scene = box({.id = "scene", .width = Dim(scene_width), .height = Dim(scene_height)},
                        {dynamic([context, creative, id] { return std::vector<Element>{creative ? creative_motion_scene(id, context) : basic_motion_scene(id, context)}; })});
    scene.key = id;
    scene.prop("label", Source(Value(std::string(zh ? info.zh : info.en)))).prop("motion", Source(motion_case_config(info)));
    Element stage = column({.id = "stage", .width = Dim(scene_width), .height = Dim(scene_height), .background = "surface_variant", .radius = 12, .clip = true},
                           {std::move(scene)});
    stage.prop("theme_scope", Source(Value(scope))).prop("overlay_boundary", Source(Value(true)));
    return column({.id = id, .width = Dim(card_width), .padding = Edges(12.0f), .gap = 8, .background = "surface", .border_color = "border",
                   .border_width = 1, .radius = 16,
                   .on = {{"pointer_enter", [hover](const Value&) { hover(true); }}, {"pointer_leave", [hover](const Value&) { hover(false); }}}},
                  {std::move(stage),
                   button({.id = "open", .text = std::string(zh ? info.zh : info.en), .variant = ButtonVariant::ghost,
                           .on_activated = [open = site.open, id] { if (open) open(id); }, .align_self = "start"}),
                   text({.id = "purpose", .text = std::string(zh ? info.purpose_zh : info.purpose_en), .style = "caption", .color = "text_muted",
                         .max_width = Dim(card_width - 24)})});
}

// The canvas's world: a card a case, in rows.
constexpr double creative_card = 520, basic_card = 400, card_gap = 28;
constexpr int creative_columns = 5, basic_columns = 7;
double card_height(bool creative) { return creative ? 420 : 340; }
double scene_height_on_canvas(bool creative) { return card_height(creative) - 104; }

}  // namespace

Element motion_wall(MotionWall& wall, const MotionWallSite& site, bool creative, float width) {
    const bool zh = site.lang && site.lang->peek() == "zh";
    const auto entries = creative ? creative_motions() : basic_motions();
    MotionWall* const model = &wall;
    if (width >= 900) {
        // On a canvas, all of it at once: each card live (its scene), a few playing at random and
        // the one under the pointer; double-clicked, the camera flies to it (MOTION-OK: camera navigation).
        const std::string scene_name = creative ? "motion_wall_creative" : "motion_wall_basic", canvas_path = site.root + "/canvas";
        world::Scene& scene = world::scene(scene_name);
        const double card = creative ? creative_card : basic_card;
        scene.kind("motion_case",
                   world::Kind{.tiers = {{0, world::Tier::Draw::box}, {100, world::Tier::Draw::promote}},
                               .promote =
                                   [model, site, creative, card](const world::Scene& s, world::NodeRef n) -> Element {
                                       const auto* info = find_motion_case(s.key(n), creative);
                                       if (!info) return text({.id = "missing", .text = std::string(s.key(n))});
                                       const auto hover = [model, site, id = std::string(info->id)](bool on) {
                                           if (site.runtime && *site.runtime) live_wall_hover(model->live, **site.runtime, motion_play(*model), id, on);
                                       };
                                       return motion_card(*model, site, *info, creative, static_cast<float>(card), static_cast<float>(scene_height_on_canvas(creative)), hover);
                                   },
                               .role = Role::article});
        if (scene.bounds().empty()) {
            const int columns = creative ? creative_columns : basic_columns;
            for (std::size_t index = 0; index < entries.size(); ++index)
                (void)scene.add({.kind = "motion_case",
                                 .x = static_cast<double>(index % static_cast<std::size_t>(columns)) * (card + card_gap),
                                 .y = static_cast<double>(index / static_cast<std::size_t>(columns)) * (card_height(creative) + card_gap),
                                 .width = static_cast<float>(card),
                                 .height = static_cast<float>(card_height(creative)),
                                 .key = entries[index].id,
                                 .fill = Color::hex(0xe7e7ef),
                                 .radius = 16});
        }
        scene.each([&](world::NodeRef n) {
            if (const auto* info = find_motion_case(scene.key(n), creative)) scene.set(n, "title", std::string(zh ? info->zh : info->en));
        });
        wall.live.container = canvas_path;
        wall.live.cards.clear();
        for (const auto& info : entries) wall.live.cards.emplace_back(info.id);
        const auto act = [runtime = site.runtime, canvas_path](std::string_view action, Value args) {
            if (!runtime || !*runtime) return;
            Invocation call;
            call.args = std::move(args);
            (void)(*runtime)->act((*runtime)->find(canvas_path), action, call);
        };
        Element heading = text({.id = "heading",
                                .text = creative ? std::string(zh ? "动效组合" : "Compositions") : std::string(zh ? "动效规格样本" : "Motion specimens"),
                                .style = "title"});
        heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{creative ? 2 : 1})));
        Element view = canvas({.id = "canvas",
                               .bounded = true,
                               .label = creative ? "Compositions" : "Motion specimens",
                               .scene = scene_name,
                               .start = "fit",
                               .on_activated = [act](std::string key) { act("focus_node", Value{}.set("key", key)); },  // MOTION-OK: camera navigation
                               .width = Dim(width),
                               .height = Dim(640.0f)});
        return column({.id = "wall", .width = Dim(width), .gap = 12},
                      {std::move(heading),
                       row({.id = "tools", .width = Dim(width), .gap = 12, .align = "center"},
                           {text({.id = "line",
                                  .text = std::string(zh ? "全部在这里，随时有几个在动；指到哪个，哪个就动；双击放大，近看就能用。"
                                                         : "All of them here, a few moving at any time; the one you point at moves; double-click to zoom in and use it."),
                                  .style = "body", .color = "text_muted"}),
                            spacer(), live_wall_pause(wall.live, site.lang),
                            button({.id = "all", .text = std::string(zh ? "看全部" : "See all"), .variant = ButtonVariant::ghost,
                                    .on_activated = [act] { act("fit", Value{}.set("target", "all")); }})}),
                       std::move(view)});
    }
    // Narrower (a phone, a tablet): a column of cards, each a node of its own.
    const int columns = width >= 640 ? 2 : 1;
    const float gap = 20, card_width = std::floor((width - static_cast<float>(columns - 1) * gap) / static_cast<float>(columns));
    const float scene_height = std::round(std::clamp((card_width - 24) * 0.62f, 240.0f, 340.0f));
    wall.live.container.clear();
    wall.live.cards.clear();
    std::vector<Element> cards;
    for (const auto& info : entries) {
        const std::string card_path = std::format("{}/grid/{}", site.root, info.id);
        wall.live.cards.push_back(card_path);
        const auto hover = [model, runtime = site.runtime, card_path](bool on) {
            if (runtime && *runtime) live_wall_hover(model->live, **runtime, motion_play(*model), card_path, on);
        };
        cards.push_back(motion_card(wall, site, info, creative, card_width, scene_height, hover));
    }
    Element heading = text({.id = "heading",
                            .text = creative ? std::string(zh ? "动效组合" : "Compositions") : std::string(zh ? "动效规格样本" : "Motion specimens"),
                            .style = "title"});
    heading.prop("role", Source(Value("heading"))).prop("level", Source(Value(std::int64_t{creative ? 2 : 1})));
    const std::string line = creative ? (zh ? "真实的组件组合在一起，各用各的动效和材质——全部在这里，随时有几个在动；指到哪个，哪个就动。"
                                            : "Real components composed, each with its own motion and material — all of them here, a few moving at any time; the one you point at moves.")
                                      : (zh ? "规范里的每个动效样本，现场运行：指到哪个，哪个就动。" : "Every motion sample of the specification, live: the one you point at moves.");
    return column({.id = "wall", .width = Dim(width), .gap = 16},
                  {std::move(heading), text({.id = "line", .text = line, .style = "body", .color = "text_muted", .max_width = Dim(width)}),
                   row({.id = "tools", .gap = 8}, {live_wall_pause(wall.live, site.lang)}),
                   row({.id = "grid", .width = Dim(width), .gap = gap, .align = "start", .wrap = "wrap"}, std::move(cards))});
}

}  // namespace omui::site
