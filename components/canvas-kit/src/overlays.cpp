// grid and minimap: what the canvas draws around the nodes.
module openmui.ui.canvas_kit;

import std;
import openmui.core;
import openmui.view;
import openmui.render;
import openmui.anim;
import openmui.raster;
import openmui.ui.canvas;
import :parts;

namespace omui::kit {

namespace {

class Grid;
std::vector<const Grid*>& grids() {
    static std::vector<const Grid*> all;
    return all;
}

// Lines under the nodes, a cell 16–32 pixels on screen whatever the zoom:
// the base spacing doubled or halved, every second line stronger, the
// finer ones fading in as the zoom grows so a step never pops.
class Grid final : public Behavior {
public:
    explicit Grid(std::string_view args) {
        if (!args.empty()) {
            double v = 0;
            const auto [p, ec] = std::from_chars(args.data(), args.data() + args.size(), v);
            if (ec == std::errc{} && p == args.data() + args.size() && v > 0) base_ = v;
            else warn("canvas-kit: grid takes a spacing above 0 (world units), not `{}`", args);
        }
        grids().push_back(this);
    }
    ~Grid() override { std::erase(grids(), this); }
    Grid(const Grid&) = delete;
    Grid& operator=(const Grid&) = delete;

    std::string_view name() const override { return "grid"; }
    double base() const noexcept { return base_; }

    void paint_under(Host& h, render::DisplayList& list) override {
        const world::Camera cam = h.camera();
        const Size vp = h.viewport();
        const double step = grid_step(base_, cam.zoom);
        if (!(step > 0)) return;
        const Color c = h.services().theme().color("border", Color::hex(0xcbd5e1));
        // The fine lines come in as a cell grows from 16 to 32 pixels.
        const float fade = static_cast<float>(std::clamp((step * cam.zoom - 16) / 16, 0.0, 1.0));
        const WorldRect view = cam.visible(vp);
        auto lines = [&](bool vertical) {
            const double lo = vertical ? view.x : view.y, hi = vertical ? view.right() : view.bottom();
            const auto first = static_cast<std::int64_t>(std::floor(lo / step)), last = static_cast<std::int64_t>(std::ceil(hi / step));
            for (std::int64_t i = first; i <= last; ++i) {
                const bool strong = i % 2 == 0;
                const float alpha = strong ? 0.7f : 0.7f * fade;
                if (alpha <= 0.01f) continue;
                const double w = static_cast<double>(i) * step;
                const Point at = vertical ? cam.to_screen(w, 0, vp) : cam.to_screen(0, w, vp);
                if (vertical) list.fill_rect({std::floor(at.x), 0, 1, vp.height}, c.with_alpha(c.a * alpha));
                else list.fill_rect({0, std::floor(at.y), vp.width, 1}, c.with_alpha(c.a * alpha));
            }
        };
        lines(true);
        lines(false);
    }

private:
    double base_ = 40;
};

// The whole scene in a corner, the view outlined; a press or a drag in it
// moves the view there. The scene is drawn into one small image — each pixel
// the mean colour of the nodes over it, more opaque where more of them are —
// through difference arrays, so making it costs the nodes plus the pixels
// whatever their sizes, and a frame draws it as one image. It is made again
// when the scene changes: for a large scene at most ten times a second (a
// drag changes it every frame, the image barely), in passes of 12,000 nodes
// a frame, and once more when the changes stop.
class Minimap final : public Behavior {
public:
    explicit Minimap(std::string_view args) {
        if (args.empty() || args == "bottom-right") corner_ = 3;
        else if (args == "top-left") corner_ = 0;
        else if (args == "top-right") corner_ = 1;
        else if (args == "bottom-left") corner_ = 2;
        else warn("canvas-kit: minimap takes a corner (top-left, top-right, bottom-left, bottom-right), not `{}`", args);
    }
    std::string_view name() const override { return "minimap"; }
    int priority() const override { return 100; }

    Claim pointer(Host& h, const PointerInput& in) override {
        switch (in.kind) {
            case PointerKind::down:
                if (in.button != MouseButton::left || !panel(h).contains(in.screen) || h.scene().size() == 0) return Claim::pass;
                // The map holds still while it is dragged in.
                held_ = map(h);
                active_ = true;
                go(h, in.screen);
                return Claim::take;
            case PointerKind::move:
                if (active_) go(h, in.screen);
                return Claim::take;
            default: active_ = false; return Claim::take;
        }
    }

    void paint_over(Host& h, render::DisplayList& list) override {
        Scene& s = h.scene();
        if (s.size() == 0) return;
        const Theme& theme = h.services().theme();
        const Rect p = panel(h);
        list.box_shadow(p, CornerRadii::all(8), {0, 2}, 8, 0, Color::hex(0x0f172a, 0.18f));
        list.fill_round_rect(p, CornerRadii::all(8), render::Paint::solid(theme.color("surface", Color::hex(0xffffff)).with_alpha(0.94f)));
        list.stroke_round_rect(p, CornerRadii::all(8), 1, render::Paint::solid(theme.color("border", Color::hex(0xcbd5e1))));
        refresh(h);
        const Map m = active_ ? held_ : map(h);
        list.push_clip(p, CornerRadii::all(8));
        if (image_.id != 0) list.draw_image(image_, {0, 0, static_cast<float>(image_w_), static_cast<float>(image_h_)}, m.to_panel(image_world_), render::Sampling::linear);
        const Rect view = m.to_panel(h.camera().visible(h.viewport()));
        const Color c = accent(h);
        list.fill_rect(view, c.with_alpha(0.08f));
        list.stroke_round_rect(view, {}, 1.5f, render::Paint::solid(c));
        list.pop_clip();
    }

    void unmounted(Host& h) override {
        if (image_.id != 0) h.services().images().remove(image_);
        image_ = {};
    }

private:
    // World → panel: the scene and the view, fitted with a margin.
    struct Map {
        WorldRect world;
        Rect      area;
        double    scale = 1;
        double    ox = 0, oy = 0;
        [[nodiscard]] Rect to_panel(const WorldRect& r) const {
            return {static_cast<float>(ox + (r.x - world.x) * scale), static_cast<float>(oy + (r.y - world.y) * scale),
                    static_cast<float>(r.width * scale), static_cast<float>(r.height * scale)};
        }
        [[nodiscard]] std::array<double, 2> to_world(Point p) const { return {world.x + (p.x - ox) / scale, world.y + (p.y - oy) / scale}; }
    };

    Rect panel(Host& h) const {
        const Size vp = h.viewport();
        const float w = std::min(200.0f, vp.width * 0.3f), hgt = std::min(136.0f, vp.height * 0.3f), m = 12;
        const float x = corner_ % 2 == 0 ? m : vp.width - w - m, y = corner_ < 2 ? m : vp.height - hgt - m;
        return {x, y, w, hgt};
    }

    Map map(Host& h) const {
        Map m;
        m.world = h.scene().bounds().united(h.camera().visible(h.viewport()));
        const Rect p = panel(h);
        m.area = {p.x + 6, p.y + 6, p.width - 12, p.height - 12};
        m.scale = m.world.empty() ? 1 : std::min(m.area.width / m.world.width, m.area.height / m.world.height);
        m.ox = m.area.x + (m.area.width - m.world.width * m.scale) / 2;
        m.oy = m.area.y + (m.area.height - m.world.height * m.scale) / 2;
        return m;
    }

    void go(Host& h, Point at) {
        const auto w = held_.to_world(at);
        world::Camera c = h.camera();
        c.x = w[0], c.y = w[1];
        h.set_camera(c);
    }

    // Made again when the scene changes — for a large scene at most ten
    // times a second, and in passes of a bounded number of nodes a frame, the
    // last image drawn meanwhile: a frame never pays for the whole scene.
    void refresh(Host& h) {
        const Scene& s = h.scene();
        constexpr anim::seconds every{0.1};
        const anim::seconds now = h.services().now();
        if (!building_) {
            if (built_ && s.revision() == revision_) return;
            if (built_ && s.size() > per_frame && now - built_at_ < every) {
                // Begun when the period is over (the canvas owns this behaviour: alive, so is its host).
                if (!pending_) {
                    pending_ = true;
                    h.services().after(every - (now - built_at_), [token = std::weak_ptr<int>(alive_), host = &h, self = this] {
                        if (token.expired()) return;
                        self->pending_ = false;
                        host->redraw();
                    });
                }
                return;
            }
            begin(h);
        }
        // A pass: up to `per_frame` slots.
        const std::size_t last = std::min(s.slots(), next_ + per_frame);
        const double fit = fit_;
        const WorldRect b = world_;
        const int w = w_, ht = h_;
        const std::size_t stride = static_cast<std::size_t>(w) + 1;
        const Color ink = h.services().theme().color("text_muted", Color::hex(0x64748b));
        auto add = [&](int x, int y, const std::array<float, 4>& v, float sign) {
            auto& c = d_[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x)];
            for (std::size_t k = 0; k < 4; ++k) c[k] += sign * v[k];
        };
        s.each_rect(
            [&](const WorldRect& r, Color f) {
                int x0 = static_cast<int>(std::floor((r.x - b.x) * fit)), y0 = static_cast<int>(std::floor((r.y - b.y) * fit));
                int x1 = static_cast<int>(std::ceil((r.right() - b.x) * fit)), y1 = static_cast<int>(std::ceil((r.bottom() - b.y) * fit));
                x0 = std::clamp(x0, 0, w - 1), y0 = std::clamp(y0, 0, ht - 1);
                x1 = std::clamp(std::max(x1, x0 + 1), 1, w), y1 = std::clamp(std::max(y1, y0 + 1), 1, ht);
                const Color c = f.a > 0 ? f : ink;
                const std::array<float, 4> v{1, c.r, c.g, c.b};
                add(x0, y0, v, 1), add(x1, y0, v, -1), add(x0, y1, v, -1), add(x1, y1, v, 1);
            },
            next_, last);
        next_ = last;
        if (next_ < s.slots()) {
            h.redraw();  // the next pass next frame
            return;
        }
        finish(h);
    }

    // A new image begins: the scene's bounds fitted into the panel's inner
    // area at the screen's scale; difference arrays of (count, r, g, b) — +v
    // at a rectangle's top-left and bottom-right corners, −v at the others —
    // that, summed, hold what is over each pixel.
    void begin(Host& h) {
        const Scene& s = h.scene();
        building_ = true;
        next_ = 0;
        revision_ = s.revision();
        built_at_ = h.services().now();
        world_ = s.bounds();
        if (world_.empty()) world_ = {0, 0, 1, 1};
        const Rect p = panel(h);
        const double px = std::max(1.0f, h.services().pixel_scale());
        fit_ = std::min((p.width - 12) * px / world_.width, (p.height - 12) * px / world_.height);
        w_ = std::max(1, static_cast<int>(std::ceil(world_.width * fit_)));
        h_ = std::max(1, static_cast<int>(std::ceil(world_.height * fit_)));
        d_.assign((static_cast<std::size_t>(w_) + 1) * (static_cast<std::size_t>(h_) + 1), std::array<float, 4>{});
    }

    void finish(Host& h) {
        building_ = false;
        built_ = true;
        const int w = w_, ht = h_;
        const std::size_t stride = static_cast<std::size_t>(w) + 1;
        auto at = [&](int x, int y) -> std::array<float, 4>& { return d_[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x)]; };
        // Prefix sums, in place: along the rows, then down the columns.
        for (int y = 0; y < ht; ++y)
            for (int x = 1; x < w; ++x)
                for (std::size_t k = 0; k < 4; ++k) at(x, y)[k] += at(x - 1, y)[k];
        for (int y = 1; y < ht; ++y)
            for (int x = 0; x < w; ++x)
                for (std::size_t k = 0; k < 4; ++k) at(x, y)[k] += at(x, y - 1)[k];
        float most = 0;
        for (int y = 0; y < ht; ++y)
            for (int x = 0; x < w; ++x) most = std::max(most, at(x, y)[0]);
        std::vector<raster::Rgba8> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(ht));
        const float depth = std::log1p(std::max(1.0f, most));
        for (int y = 0; y < ht; ++y)
            for (int x = 0; x < w; ++x) {
                const auto& c = at(x, y);
                const float count = std::round(c[0]);  // sums of ±1: whole, but for rounding
                if (count < 0.5f) continue;
                const float alpha = most <= 1.5f ? 0.9f : 0.35f + 0.6f * std::log1p(count) / depth;
                pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                    raster::premultiply(Color{c[1] / c[0], c[2] / c[0], c[3] / c[0], alpha});
            }
        if (image_.id != 0) h.services().images().remove(image_);
        image_ = h.services().images().add(w, ht, std::move(pixels));
        image_w_ = w, image_h_ = ht;
        image_world_ = world_;
    }

    static constexpr std::size_t per_frame = 12000;  // slots a frame's pass visits

    int              corner_ = 3;  // top-left, top-right, bottom-left, bottom-right
    bool             active_ = false;
    Map              held_;
    render::ImageRef image_;
    int              image_w_ = 0, image_h_ = 0;
    WorldRect        image_world_;
    std::uint64_t    revision_ = 0;
    bool             built_ = false, pending_ = false, building_ = false;
    anim::seconds    built_at_{0};
    // The image being made.
    std::vector<std::array<float, 4>> d_;
    std::size_t      next_ = 0;
    WorldRect        world_;
    double           fit_ = 1;
    int              w_ = 0, h_ = 0;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);  // the pending redraw's: expired once this is gone
};

}  // namespace

std::optional<double> grid_base(Host& h) {
    Behavior* b = h.behavior("grid");
    if (!b || std::ranges::find(grids(), b) == grids().end()) return std::nullopt;
    return static_cast<const Grid*>(b)->base();
}

std::unique_ptr<Behavior> make_grid(std::string_view args) { return std::make_unique<Grid>(args); }
std::unique_ptr<Behavior> make_minimap(std::string_view args) { return std::make_unique<Minimap>(args); }

}  // namespace omui::kit
