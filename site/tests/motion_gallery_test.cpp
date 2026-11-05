import std;
import openmui;
import openmui.site;
import openmui.theme;
import openmui.theme.styles;
import openmui.testing;
using namespace omui;
using namespace omui::testing;

namespace {
const std::filesystem::path& media() { static const auto dir = scratch_dir("motion-gallery"); return dir; }
struct Probe {
    site::MotionGallery gallery;
    Signal<std::string> language = signal(std::string("en"));
    bool creative;
    float width;
    Headless h;
    Runtime* rt;
    site::MotionGalleryContext context;
    std::vector<std::string> details, styles;
    Probe(std::string id = "toggle-thumb", bool combinations = false, float room = 700, bool autoplay = false)
        : creative(combinations), width(room), h(HeadlessOptions{.size = {room + 48, 1240}, .parallel_raster = false}), rt(&h.runtime()) {
        gallery.selected.set(std::move(id)); gallery.loop.set(autoplay);
        context = {.runtime = &rt, .lang = &language,
            .open_detail = [this](std::string id) { details.push_back(std::move(id)); },
            .style = [this](std::string pack) {
                styles.push_back(pack);
                if (pack == "plain") rt->set_scope(context.scope, rt->theme());
                else check(theme::apply_style(*rt, pack, context.scope, false).has_value(), pack);
            }, .root = "main/motion_gallery", .scope = combinations ? "creative-preview" : "motion-preview"};
        h.mount("main", column({.width = Dim(room + 48), .padding = 24}, {dynamic([this] {
            return std::vector<Element>{site::motion_gallery(gallery, context, creative, width)};
        })}));
        step(); step();
    }
    void step(seconds elapsed = seconds(1.0 / 60)) { h.advance(elapsed); site::motion_gallery_tick(gallery, context); }
    std::string path(std::string_view tail) const { return context.root + "/" + std::string(tail); }
    std::uint64_t pixels() {
        const auto& px = h.pixels();
        const Rect area = rt->drawn_bounds(rt->find(path("stage")));
        Hash hash;
        const int left = std::clamp(static_cast<int>(area.x), 0, px.width()), top = std::clamp(static_cast<int>(area.y), 0, px.height());
        const int right = std::clamp(static_cast<int>(area.right()), left, px.width()), bottom = std::clamp(static_cast<int>(area.bottom()), top, px.height());
        for (int y = top; y < bottom; ++y) hash.bytes(std::as_bytes(std::span(px.row(y) + left, static_cast<std::size_t>(right - left))));
        return hash.value();
    }
    void act(std::string_view tail, std::string_view action = "activate", Value args = {}) {
        Invocation call; call.args = std::move(args);
        check(h.tree().act(path(tail), action, call).exit_code() == std::optional<int>(0), std::string(tail)); step();
    }
    bool exposed(std::string_view tail) {
        const auto id = path(tail);
        for (const auto& node : h.tree().snapshot().nodes) if (node.id == id) return true;
        return false;
    }
    bool exposed_under(std::string_view tail) {
        const auto prefix = path(tail) + "/";
        for (const auto& node : h.tree().snapshot().nodes) if (node.id.starts_with(prefix)) return true;
        return false;
    }
    void play() { site::motion_gallery_play(gallery, context); step(); }
    ~Probe() { site::motion_gallery_cancel(gallery, context); }
};
}

int main(int argc, char** argv) {
    Suite suite{"site.motion_gallery"};
    (void)themes::add_styles();

    suite.test("the opening morph uses public path configuration with optional details and local style", [] {
        Probe p("", false, 300), linear("", false, 300);
        check_eq(p.gallery.active_id, std::string("shape-morph"));
        check_near(p.rt->screen_bounds(p.rt->find(p.path("stage/scene"))).height, 300, .01);
        check(!p.exposed("details/bend") && !p.exposed("details/style"));
        p.act("transport/buttons/details");
        p.act("details/bend", "set_value", Value{}.set("value", -.2));
        check_near(p.gallery.bend.peek(), -.2, .001);
        p.act("details/copy");
        const auto config = parse_json(p.rt->clipboard_text());
        check(config.has_value());
        if (config) {
            const auto spec = parse_motion(*config, p.rt->theme());
            check(spec.has_value());
            if (spec) check_near(spec->vertex_path.control1.y, hanim::PointPath::bend(-.2).control1.y, 1e-6);
        }
        const auto shown = p.h.tree().node(p.path("details/config/text"));
        check(shown && shown->value.as_string().find("vertex_path") != std::string::npos);
        site::motion_gallery_reset(p.gallery, p.context); p.step(); p.step();
        linear.gallery.bend.set(0); linear.step();
        p.step(seconds(.1)); linear.step(seconds(.1));
        p.play(); linear.play();
        check(!p.gallery.held.empty() && !linear.gallery.held.empty());
        // The slow ease reaches most of its geometry by half the duration;
        // record an earlier, visibly intermediate pose for human review.
        site::motion_gallery_seek(p.gallery, p.context, .25); p.step();
        site::motion_gallery_seek(linear.gallery, linear.context, .25); linear.step();
        check_near(p.gallery.progress.peek(), .25, .001);
        const auto middle = p.pixels();
        check(middle != linear.pixels(), "the path control changes the real middle drawing");
        check(p.h.save_png(media() / "shape-path-middle.png", p.rt->drawn_bounds(p.rt->find(p.path("stage")))).has_value());
        site::motion_gallery_seek(p.gallery, p.context, 0); p.step();
        check(p.pixels() != middle, "the sample is not the start drawing");
        site::motion_gallery_seek(p.gallery, p.context, 1); p.step();
        site::motion_gallery_seek(linear.gallery, linear.context, 1); linear.step();
        check(p.pixels() != middle && p.pixels() == linear.pixels(), "the sample is not the end and the modifier preserves the endpoint");
        p.act("details/style", "set_value", Value{}.set("value", "neon"));
        check(p.styles.back() == "neon" && p.gallery.style.peek() == "neon");
        check(p.rt->get(p.rt->find(p.path("stage/scene")), "theme_scope").as_string() == "motion-preview");
        check(p.rt->get(p.rt->find(p.path("transport")), "theme_scope").as_string().empty(), "style stays inside the scene");
        p.act("transport/buttons/details");
        p.h.move(p.rt->screen_bounds(p.rt->find(p.path("stage"))).center()); p.step();
        const auto before = p.gallery.scene.end.peek();
        check(p.gallery.interactive.peek() && p.gallery.held.empty());
        check(p.h.click(p.path("stage/scene/composition/change"))); p.step();
        check(p.gallery.scene.end.peek() != before, "a real pointer changes the shape endpoint");
        p.h.key(Key::space); p.step();
        check(p.gallery.scene.end.peek() == before, "keyboard operates the same endpoint control");
        p.act("stage/scene/composition/change");
        check(p.gallery.scene.end.peek() != before, "Agent action uses that same component operation");
    });

    suite.test("bounded browsing exposes one canvas, eight choices, search and details only on demand", [] {
        Probe p("list-grid");
        const auto heading = p.h.tree().node(p.path("heading/title"));
        check(heading && heading->role == Role::heading, "the page has a semantic main heading");
        check(p.rt->get(p.rt->find(p.path("heading/title")), "level").as_int() == 1);
        check(site::basic_motions().size() >= 40);
        check(site::creative_motions().size() >= 20);
        check(!p.exposed("details/config"), "configuration is not in the overview");
        check(!p.exposed_under("browse") && !p.gallery.browser_open.peek(), "the picker starts closed above the canvas");
        check(p.exposed("transport/buttons/play") && p.exposed("transport/buttons/try") && p.exposed("transport/buttons/details"));
        for (const auto id : {"reverse", "reset", "state-a", "state-b"})
            check(!p.exposed(std::string("transport/buttons/") + id), "advanced transport is not exposed by default");
        check(!p.exposed("transport/seek"));
        p.act("choose");
        check(p.gallery.browser_open.peek() && p.exposed_under("browse"));
        check(p.rt->children(p.rt->find(p.path("browse/choices"))).size() <= 8, "the catalogue is bounded, not forty fixtures down a page");
        p.act("browse/pagination/next");
        check_eq(p.gallery.page.peek(), std::int64_t{1});
        p.gallery.query.set("zzz-no-design"); p.step();
        check(p.exposed("browse/choices/empty"), "search has an honest empty state");
        p.gallery.query.set("thumb"); p.step();
        check(p.exposed("browse/choices/toggle-thumb"));
        p.act("browse/choices/toggle-thumb");
        check_eq(p.gallery.active_id, std::string("toggle-thumb"));
        check(!p.gallery.browser_open.peek() && !p.exposed_under("browse"), "selection closes the picker");
        const auto english = p.h.tree().node(p.path("stage/scene"));
        check(english && english->name == "A setting changes", "the fixed scene path identifies the currently selected design");
        p.language.set("zh"); p.step();
        const auto chinese = p.h.tree().node(p.path("stage/scene"));
        check(chinese && chinese->name == "开关滑块", "static-to-live handoff can match the translated design name");
        p.step(seconds(0.1)); // locale replacement is a separate drawing change, not a continuous stream with playback
        p.play(); check(p.gallery.held.size() >= 2, "selection mounts a fresh, playable scene");
        p.act("transport/buttons/details");
        check(p.gallery.detail.peek() && !p.details.empty());
        check(p.exposed("details/config/text"));
        for (const auto id : {"reverse", "reset", "state-a", "state-b"}) check(p.exposed(std::string("transport/buttons/") + id));
        check(p.exposed("transport/seek"));
        p.act("details/settings/curve", "set_value", Value{}.set("value", "motion.fast"));
        const auto shown_config = p.h.tree().node(p.path("details/config/text"));
        check(shown_config && shown_config->value.as_string().find("motion.fast") != std::string::npos, "the displayed reusable configuration follows the live parameters");
        p.act("details/copy");
        check(p.rt->clipboard_text().find("motion.fast") != std::string::npos);
        check(p.rt->clipboard_text().find("stagger") != std::string::npos);
        check(site::find_motion_case("not-a-case", false) == nullptr);
        check(site::motion_gallery_html(true, false, "list-grid").find("列表转卡片") != std::string::npos);
        p.gallery.query.set("slide"); p.act("choose"); p.act("browse/choices/slide-card");
        check(p.gallery.detail.peek() && !p.details.empty() && p.details.back() == "slide-card", "selection in Details updates the address callback");
        check(!p.gallery.browser_open.peek() && !p.exposed_under("browse"));
        p.act("transport/buttons/details");
        check(!p.exposed("transport/seek") && !p.exposed("transport/buttons/reset"), "closing Details hides advanced controls from compact semantics");
    });

    suite.test("the player holds every active scene motion on common seconds and can pause seek reverse reset", [] {
        Probe p;
        p.play();
        check(p.gallery.held.size() >= 2, "scene root and toggle's default animation both participate");
        check(p.gallery.duration > 0 && p.gallery.anchor);
        const auto collections = p.gallery.collections;
        p.step(seconds(0.06));
        site::motion_gallery_pause(p.gallery, p.context); p.step();
        const auto still = p.pixels();
        for (int i = 0; i < 4; ++i) p.step(seconds(0.04));
        check_eq(p.pixels(), still);
        check_eq(p.gallery.collections, collections);
        p.step(seconds(0.6));
        const auto paused = p.h.step(); site::motion_gallery_tick(p.gallery, p.context);
        check(!paused.animating && paused.demand.idle(), "paused held motions do not keep the host rendering");
        site::motion_gallery_seek(p.gallery, p.context, 0.2); p.step(); const auto early = p.pixels();
        for (const auto& handle : p.gallery.held) if (const auto clock = p.rt->motion_time(handle.node))
            check_near(clock->second, std::min(handle.duration, p.gallery.duration * 0.2), 0.001);
        site::motion_gallery_seek(p.gallery, p.context, 0.7); p.step(); const auto late = p.pixels();
        check(early != late, "seeking changes real intermediate pixels");
        site::motion_gallery_reverse(p.gallery, p.context);
        site::motion_gallery_play(p.gallery, p.context); p.step(seconds(0.04));
        check(p.gallery.direction == -1 && p.gallery.progress.peek() < 0.7, "reverse survives pause/play");
        site::motion_gallery_reset(p.gallery, p.context); p.step(); p.step();
        check(!p.gallery.scene.end.peek() && !p.gallery.playing.peek());
        check(p.gallery.held.empty(), "reset releases the previous animation instead of leaving a stale drawing");
    });

    suite.test("hover and keyboard focus finish the preview and hand real controls back to their default motion", [] {
        Probe p;
        p.play(); site::motion_gallery_seek(p.gallery, p.context, 0.25); p.step();
        const auto collections = p.gallery.collections;
        const auto stage = p.rt->find(p.path("stage"));
        p.h.move(p.rt->drawn_bounds(stage).center()); p.step();
        check(p.gallery.interactive.peek() && p.gallery.held.empty());
        p.act("stage/scene/composition/setting/quiet", "toggle");
        check(p.gallery.scene.checked.peek(), "actual component editing is live");
        check_eq(p.gallery.collections, collections);
        check(p.rt->motion_time(p.rt->find(p.path("stage/scene/composition/setting/quiet"))).has_value(), "default toggle feedback runs naturally without a preview hold");
        p.step(seconds(1));
        check(!p.rt->motion_time(p.rt->find(p.path("stage/scene/composition/setting/quiet"))), "the default feedback is released, not frozen");
        Probe keyboard;
        keyboard.h.focus_next(); keyboard.step();
        check(!keyboard.gallery.interactive.peek(), "the picker precedes the canvas in keyboard order");
        keyboard.h.focus_next(); keyboard.step();
        check(keyboard.gallery.interactive.peek(), "Tab into the canvas makes it interactive");
        keyboard.rt->focus_visibly(keyboard.rt->find(keyboard.path("transport/buttons/play"))); keyboard.step();
        check(!keyboard.gallery.interactive.peek(), "moving focus outside returns the canvas to preview mode");
    });

    suite.test("the first move and press in one input frame and the first touch both reach the real control", [] {
        for (bool touch : {false, true}) {
            Probe p;
            p.play(); site::motion_gallery_seek(p.gallery, p.context, 0.25); p.step();
            const Point at = p.rt->screen_bounds(p.rt->find(p.path("stage/scene/composition/setting/quiet"))).center();
            PointerEvent move; move.kind = PointerKind::move; move.position = at;
            PointerEvent down; down.kind = PointerKind::down; down.position = at; down.button = MouseButton::left; down.buttons = 1;
            down.type = touch ? PointerType::touch : PointerType::mouse;
            PointerEvent up = down; up.kind = PointerKind::up; up.buttons = 0;
            if (touch) p.h.step({down, up}); else p.h.step({move, down, up});
            site::motion_gallery_tick(p.gallery, p.context); p.step();
            check(p.gallery.interactive.peek(), touch ? "first touch enters interaction" : "same-frame pointer entry enters interaction");
            check(p.gallery.scene.checked.peek(), touch ? "first touch activates the control" : "the first move+press activates the control");
            check(p.gallery.held.empty(), "preview handles are released for the first interaction");
        }
    });

    suite.test("looping requires full motion and visibility; offscreen and page removal release all handles", [] {
        Probe automatic("list-grid", false, 700, true);
        check(automatic.gallery.playing.peek() && !automatic.gallery.held.empty(), "a visible full-motion sample starts automatically");
        Probe p("list-grid");
        p.gallery.loop.set(true); p.play();
        for (int i = 0; i < 12; ++i) p.step(seconds(0.1));
        check(p.gallery.playing.peek() && !p.gallery.held.empty());
        const auto stage = p.rt->find(p.path("stage"));
        p.rt->set(stage, "translate_y", Value(-2000.0)); p.step();
        check(!p.gallery.playing.peek() && p.gallery.held.empty(), "an offscreen canvas releases motion handles");
        p.rt->set(stage, "translate_y", Value(0.0)); p.step(); p.step();
        check(p.gallery.playing.peek() && !p.gallery.held.empty(), "visible full-motion preview resumes");
        p.h.mount("main", box({.id = "another-page", .width = 400, .height = 200})); p.step();
        check(!p.gallery.playing.peek() && p.gallery.held.empty() && !p.gallery.pending);
        Probe reduced;
        reduced.rt->set_motion_level(MotionLevel::reduced); reduced.gallery.loop.set(true); reduced.step();
        check(!reduced.gallery.playing.peek() && reduced.gallery.held.empty(), "reduced motion does not automatically loop");
        reduced.rt->set_motion_level(MotionLevel::full); reduced.rt->set_reduce_motion(true); reduced.step();
        check(!reduced.gallery.playing.peek() && reduced.gallery.held.empty(), "the host's reduce-motion preference also prevents preview loops");
        reduced.rt->set_reduce_motion(false); reduced.rt->set_motion_level(MotionLevel::off); reduced.step();
        reduced.act("transport/buttons/details");
        reduced.act("transport/buttons/state-b"); reduced.step();
        check(reduced.gallery.scene.end.peek() && reduced.gallery.held.empty(), "B is available with motion off");
        reduced.act("transport/buttons/state-a"); reduced.step();
        check(!reduced.gallery.scene.end.peek() && reduced.gallery.held.empty(), "A is available with motion off");
        site::motion_gallery_interact(reduced.gallery, reduced.context); reduced.act("stage/scene/composition/setting/quiet", "toggle");
        check(reduced.gallery.scene.checked.peek(), "off still permits real controls");
    });

    suite.test("leaving and remounting resumes playing intent but preserves an explicit pause", [] {
        for (bool playing : {false, true}) {
            Probe p("list-grid", false, 700, true);
            if (!playing) site::motion_gallery_pause(p.gallery, p.context);
            site::motion_gallery_cancel(p.gallery, p.context);
            p.h.mount("main", box({.width = 400, .height = 200})); p.step(); p.step();
            check(p.gallery.held.empty() && !p.gallery.playing.peek());
            p.h.mount("main", column({.width = 748, .padding = 24}, {dynamic([&p] {
                return std::vector<Element>{site::motion_gallery(p.gallery, p.context, false, p.width)};
            })}));
            p.step(); p.step();
            check(p.gallery.playing.peek() == playing, playing ? "previously playing preview resumes" : "the person's pause survives navigation");
            check(p.gallery.held.empty() != playing, "only the resumed preview owns active handles");
            p.gallery.selected.set("slide-card"); p.step(); p.step();
            check(p.gallery.playing.peek() == playing, "choosing another design also respects the person's transport intent");
        }
    });

    suite.test("catalogue links use canonical routes and choosing a design brings its canvas into view", [] {
        check(site::motion_gallery_html(false, true).find("#/demos/packing-board") != std::string::npos);
        check(site::motion_gallery_html(true, true).find("#/zh/demos/packing-board") != std::string::npos);
        check(site::motion_gallery_html(false, false).find("#/motion/list-grid") != std::string::npos);
        check(site::motion_gallery_html(true, false).find("#/zh/motion/list-grid") != std::string::npos);
        Probe p("list-grid");
        auto offset = signal(0.0f);
        p.h.mount("main", scroll({.y = offset, .width = 748, .height = 600}, {dynamic([&p] {
            return std::vector<Element>{site::motion_gallery(p.gallery, p.context, false, p.width)};
        })}));
        p.step(); p.act("choose"); offset.set(650); p.step(); const float before = offset.peek();
        p.act("browse/choices/slide-card");
        check(offset.peek() < before, "the person's catalogue selection scrolls back to the canvas");
        check(!p.rt->drawn_bounds(p.rt->find(p.path("stage"))).intersection({0, 0, 748, 600}).empty());
    });

    suite.test("the compact phone opening fits both list-grid endpoints and defaults creative to palette", [] {
        for (const std::string_view language : {"en", "zh"}) {
            Probe p("list-grid", false, 300);
            p.rt->set_motion_level(MotionLevel::off); p.language.set(std::string(language)); p.step();
            const Rect canvas = p.rt->screen_bounds(p.rt->find(p.path("stage/scene")));
            check_near(canvas.height, 280, 0.01);
            for (bool end : {false, true}) {
                p.gallery.scene.end.set(end); p.step();
                for (const auto& node : p.h.tree().snapshot().nodes) {
                    if (!node.id.starts_with(p.path("stage/scene/composition/")) || !node.bounds || node.states.has(State::hidden)) continue;
                    check(node.bounds->x >= canvas.x - 0.5f && node.bounds->right() <= canvas.right() + 0.5f &&
                          node.bounds->y >= canvas.y - 0.5f && node.bounds->bottom() <= canvas.bottom() + 0.5f,
                          std::format("{} {} {} stays within compact canvas", language, end, node.id));
                }
            }
        }
        Probe creative("", true, 300);
        check_eq(creative.gallery.active_id, std::string("palette-studio"));
        check_near(creative.rt->screen_bounds(creative.rt->find(creative.path("stage/scene"))).height, 300, 0.01);
    });

    suite.test("creative materials are scoped on the scene and low tiers retain a simple motion candidate", [] {
        Probe p("palette-studio", true);
        check(!p.styles.empty() && p.styles.back() == "liquid");
        check(p.rt->get(p.rt->find(p.path("stage/scene")), "theme_scope").as_string() == "creative-preview");
        p.rt->set_device_tier(pipeline::Tier::t1, "low-tier player reference"); p.step(seconds(0.1)); p.play();
        const auto* info = site::find_motion_case("palette-studio", true);
        check(info != nullptr);
        if (info) {
            const auto config = site::motion_gallery_config(p.gallery, *info);
            check(config.list() && config.list()->size() == 2 && config.list()->back().as_string() == "simple");
        }
        check(!p.gallery.held.empty(), "low tier retains actual active scene motions");
        // The style makes swatches and controls independent nested roots;
        // the unchanged outer wrapper need not own a motion of its own.
        for (const auto& handle : p.gallery.held) check_near(handle.duration, 0.16, 0.001);
        const auto start = p.pixels(); p.step(seconds(0.07));
        check(start != p.pixels(), "the fallback paints changing frames");
    });

    suite.test("every basic configuration is reusable on another target rather than tied to its gallery scene", [] {
        site::MotionGallery gallery;
        for (const auto& info : site::basic_motions()) {
            // Authored Hanim programs use the public binding API instead of a
            // state-motion JSON slot; component_motion_test covers reuse on a
            // second UI hierarchy, outside the site's note scene.
            if(info.authored_program)continue;
            const auto config = site::motion_gallery_config(gallery, info);
            check(parse_motions(config, Theme::light()).has_value(), std::string(info.id));
            Headless h(HeadlessOptions{.size = {260, 100}, .parallel_raster = false});
            auto expanded = signal(false);
            auto target = box({.id = "target", .width = 240, .height = 70}, {dynamic([expanded] {
                return std::vector<Element>{box({.id = "fill", .width = expanded.get() ? 200 : 60, .height = 50, .background = "accent", .radius = 12})};
            })});
            target.prop("motion", Source(config));
            h.mount("main", column({.padding = 12}, {std::move(target)})); h.settle();
            const auto before = site::frame_hash(h.pixels()); expanded.set(true); h.step();
            check(h.runtime().motion_time(h.runtime().find("main/target")).has_value(), std::string(info.id));
            h.advance(seconds(0.05)); const auto middle = site::frame_hash(h.pixels());
            h.settle(); const auto after = site::frame_hash(h.pixels());
            check(middle != before && middle != after, std::string(info.id) + " affects another target's actual paint");
        }
    });

    suite.test("the running shared player produces a real dynamic recording and phone/desktop references", [] {
        for (bool creative : {false, true}) for (float width : {300.0f, 700.0f}) {
            Probe p(creative ? "packing-board" : "list-grid", creative, width);
            p.play();
            for (int i = 0; i < 22; ++i) { p.step(seconds(1.0 / 20)); p.h.record(); }
            const auto file = media() / std::format("{}-player-{}.webp", creative ? "creative" : "basic", static_cast<int>(width));
            check(p.h.save_recording(file, {.delay_num = 1, .delay_den = 20, .quality = 82}).has_value());
            const auto decoded = imageio::load_animated_webp(file);
            check(decoded && decoded->frames.size() > 2, "the recording contains actual player frames");
            check(std::filesystem::file_size(file) < 2 * 1024 * 1024);
            check(p.h.save_png(media() / std::format("{}-gallery-{}.png", creative ? "creative" : "basic", static_cast<int>(width))).has_value());
        }
    });
    return suite.run(argc, argv);
}
