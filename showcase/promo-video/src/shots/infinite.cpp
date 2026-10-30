// The infinite canvas (0:16–0:28): one shot, no cut — from a page of the
// film's own script, read live, out to the Markdown wall of a thousand
// documents, and in again to another page. The wall is S1's (its scene,
// its windows, its tiers): far away a window is two boxes, nearer a texture
// of its page drawn on the task pool at the screen's resolution, close up
// the live document. The camera is the clip's keys; every frame is drawn
// complete (Services::complete_frames), so any one renders on its own. The
// corner counts what the frame draws, by tier.
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;
import openmui.theme;
import openmui.ui.canvas;
import openmui.showcase.markdown_wall;

namespace omui::promo {

namespace {

constexpr std::size_t wall_size = 1000;
constexpr double live_zoom = 1.52;

std::string grouped(std::size_t n) {  // 1,000
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}  // a 640-unit window 970 px wide: past the wall's 900 px, so it is live

struct Anchors {
    world::Camera a, b;      // the two pages
    world::Camera overview;  // the whole wall
};

class Infinite final : public Shot {
public:
    explicit Infinite(const ShotContext& c) : c_(c) {
        static std::atomic<std::uint64_t> made = 0;
        model_.scene = std::format("promo.wall.{}", made++);
        model_.title = "The wall";
        // The film's script, first; the benchmark's documents; the film's overview, last: far apart.
        std::vector<wall::Document> docs;
        const Files files;
        auto page = [&](std::string_view group, std::string_view name) {
            wall::Document d;
            d.path = std::format("{}/{}", group, name);
            d.group = std::string(group);
            d.source = files.read(std::format("script/{}", name)).value_or(std::string{});
            const std::size_t hash = d.source.find("# ");
            const std::size_t end = d.source.find('\n', hash);
            d.title = hash == std::string::npos ? std::string(name) : d.source.substr(hash + 2, end - hash - 2);
            d.key = wall::key_of(d.path);
            return d;
        };
        for (std::string_view n : {"hook.md", "title.md", "code.md", "infinite.md", "styles.md", "agent.md", "time.md", "end.md"})
            docs.push_back(page("script", n));
        for (wall::Document& d : wall::synthetic(wall_size - 9, 7)) docs.push_back(std::move(d));
        docs.push_back(page("film", "film.md"));
        model_.docs = std::move(docs);
        model_.hud.set(false);
        a_key_ = wall::key_of("script/infinite.md");
        b_key_ = wall::key_of("film/film.md");
    }

    View view() override {
        return stack({.id = "infinite", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                     {canvas({.id = "wall",
                              .behaviors = std::vector<std::string>{"minimap"},
                              .cache_mb = 96,
                              .label = "A wall of a thousand documents",
                              .max_zoom = 8,
                              .min_zoom = 0.002,
                              .scene = model_.scene,
                              .x = model_.x,
                              .y = model_.y,
                              .zoom = model_.zoom,
                              .width = Dim(layout::pct(100)),
                              .height = Dim(layout::pct(100))}),
                      column({.id = "type", .width = Dim(layout::pct(100)), .padding = Edges(Insets{c_.size.height * 0.05f + 44, c_.size.width * 0.05f, 0, 0}),
                              .align = "end"},
                             {column({.id = "hud", .padding = Edges(12.0f, 16.0f), .gap = 4, .align = "end", .background = Fill(hex(ink::paper, 0.82f)),
                                      .border_color = Fill(hex(ink::cyan, 0.3f)), .border_width = 1, .radius = 6},
                                     {text({.id = "count", .text = "", .style = "code", .color = Fill(hex(ink::ice)), .wrap = false, .size = 30.0f, .weight = 700.0f}),
                                      text({.id = "tiers", .text = "", .style = "code", .color = Fill(hex(ink::muted)), .wrap = false, .size = 14.0f}),
                                      text({.id = "zoom", .text = "", .style = "code", .color = Fill(hex(ink::cyan)), .wrap = false, .size = 14.0f}),
                                      text({.id = "budget", .text = "≤ 1 ms of CPU a frame · tests/bench", .style = "code", .color = Fill(hex(ink::muted, 0.8f)),
                                            .wrap = false, .size = 12.0f})})})});
    }

    void mounted(Runtime& rt) override {
        (void)theme::apply_style(rt, look_style, false);
        model_.runtime = &rt;
        world::Scene& scene = world::scene(model_.scene);
        wall::build(model_, scene);
        const Size vp = c_.size;
        auto at = [&](std::string_view key) {
            const auto n = scene.find(key);
            const auto r = n ? scene.rect(*n) : world::WorldRect{};
            return world::Camera{r.x + r.width / 2, r.y + r.height / 2, live_zoom};
        };
        anchors_.a = at(a_key_);
        anchors_.b = at(b_key_);
        anchors_.overview = world::Camera::fit(scene.bounds(), vp, std::min(vp.width, vp.height) * 0.04f, 0.002, 8);
        hud_ = rt.find("main/type/hud");
    }

    void at(Runtime& rt, double t) override {
        const world::Camera c = camera(t);
        model_.x.set(c.x);
        model_.y.set(c.y);
        model_.zoom.set(c.zoom);
        // What the frame draws, by tier (the wall's: under 40 px two boxes, to 900 a texture, past it live).
        world::Scene& scene = world::scene(model_.scene);
        std::vector<world::NodeRef> seen;
        scene.query(c.visible(c_.size), seen);
        std::size_t windows = 0, boxes = 0, textured = 0, live = 0;
        for (const world::NodeRef n : seen) {
            if (scene.kind_name(n) != "window") continue;
            ++windows;
            const Rect r = c.to_screen(scene.rect(n), c_.size);
            const float side = std::max(r.width, r.height);
            (side >= 900 ? live : side >= 40 ? textured : boxes) += 1;
        }
        rt.set(rt.find("main/type/hud/count"), "text", Value(std::format("{} of {} on screen", grouped(windows), grouped(wall_size))));
        rt.set(rt.find("main/type/hud/tiers"), "text", Value(std::format("boxes {} · textures {} · live {}", boxes, textured, live)));
        rt.set(rt.find("main/type/hud/zoom"), "text", Value(std::format("zoom {:.1f}%", c.zoom * 100)));
        rt.set(hud_, "opacity", Value(smooth(t, 0.4, 0.9)));
    }

    std::vector<Cue> cues(double) override { return {}; }

private:
    // The camera at a time: `pan` 0 the first page, 1 the wall's middle, 2 the second page; `out` 0 a page at
    // live size, 1 the whole wall (between, the zoom's logarithm), and the look point eased to match.
    world::Camera camera(double t) const {
        const double pan = std::clamp(c_.value("pan", t, 0), 0.0, 2.0), out = std::clamp(c_.value("out", t, 0), 0.0, 1.0);
        const world::Camera& o = anchors_.overview;
        const world::Camera& from = pan <= 1 ? anchors_.a : o;
        const world::Camera& to = pan <= 1 ? o : anchors_.b;
        const double u = pan <= 1 ? pan : pan - 1;
        const double lz = mix(hanim::dm::log(live_zoom), hanim::dm::log(o.zoom), out);
        return {mix(from.x, to.x, u), mix(from.y, to.y, u), hanim::dm::exp(lz)};
    }

    ShotContext  c_;
    wall::Model  model_;
    std::string  a_key_, b_key_;
    Anchors      anchors_;
    NodeId       hud_{};
};

}  // namespace

std::unique_ptr<Shot> infinite_shot(const ShotContext& c) { return std::make_unique<Infinite>(c); }

}  // namespace omui::promo
