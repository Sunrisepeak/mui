// The hook and the title (0:00–0:08): a cyan pixel lights in the dark, and
// the label beside it says what drew it — read from the panel's object ids
// and semantic tree, not written. The camera pulls back from the pixel to
// its button, to the panel; the panel's semantic tree grows beside it, the
// path to the button lit. Then the tree's lines leave it and settle on the
// wordmark's outline, the wordmark fills in, and a band of light crosses it
// (a wipe within a wipe: openmui.render's Slang effects).
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;

namespace omui::promo {

namespace {

constexpr std::string_view wordmark_text = "OPENMUI";

// The semantic tree as the shot draws it: a column a level, from the root
// to the node that drew the pixel; at each level that node's siblings
// (a few), the one on the path lit.
struct TreeBox {
    std::string label;
    Rect        rect;      // screen
    int         level = 0;
    bool        lit = false;
};
struct TreeEdge {
    Point a, b;
    bool  lit = false;
    int   level = 0;  // its child's
};

// A point of the wordmark's outline and the outline's direction there.
struct Mark {
    Point at;
    float angle = 0;
};

struct State {
    double t = 0, zoom = 1, tree = 0, gather = 0, sweep = 0;
    Point  at, look;
    float  panel_opacity = 1, pixel_glow = 0, dark = 1;
    std::vector<TreeBox>  boxes;
    std::vector<TreeEdge> edges;
    std::vector<Mark>     marks;     // the wordmark's outline, in screen coordinates
    std::vector<Mark>     sources;   // where each dash starts: along the tree's lines
    std::vector<float>    delays;    // each dash's start in the gathering (0 … 0.45)
    Rect                  wordmark;  // the text's box on the screen
    Labels                labels;
};

class Origin final : public Shot {
public:
    explicit Origin(const ShotContext& c) : c_(c), s_(std::make_shared<State>()) {}

    View view() override {
        const PanelPicture& panel = panel_picture();
        (void)panel;
        const bool portrait = c_.portrait();
        word_size_ = portrait ? 150.0f : 210.0f;
        auto s = s_;
        auto refs = std::make_shared<std::pair<render::ImageRef, render::ImageRef>>();
        refs_ = refs;
        Element lens = painter({.id = "lens",
                                .width = Dim(layout::pct(100)),
                                .height = Dim(layout::pct(100)),
                                .role = Role::figure,
                                .label = "A pixel of the settings panel, magnified, and the panel's semantic tree",
                                .paint = [s, refs](PaintContext& p) { paint(p, *s, *refs); }});
        // The typography: the pixel's label, the wordmark and its band of light.
        Element label = text({.id = "provenance", .text = panel_picture().provenance, .style = "code", .color = Fill(hex(ink::ice)), .wrap = false,
                              .size = portrait ? 28.0f : 36.0f, .weight = 500.0f});
        GlyphMotion typed = GlyphMotion::typewriter(hanim::seconds(0.022));
        typed.transition.delay = hanim::seconds(0.55);
        Element coords = text({.id = "where",
                               .text = std::format("1 px · x {} · y {} · {}", static_cast<int>(panel_picture().pixel.x), static_cast<int>(panel_picture().pixel.y),
                                                   panel_picture().path),
                               .style = "code",
                               .color = Fill(hex(ink::muted)),
                               .wrap = false,
                               .size = portrait ? 15.0f : 17.0f});
        GlyphMotion later = typed;
        later.transition.delay = hanim::seconds(1.25);
        later.stagger = hanim::seconds(0.012);
        auto mark = [&](std::string id, Color c) {
            return text({.id = std::move(id), .text = std::string(wordmark_text), .style = "display", .color = Fill(c), .wrap = false, .size = word_size_,
                         .weight = 800.0f});
        };
        Element light = box({.id = "sweep-from"}, {box({.id = "sweep-to"}, {mark("light", hex(0xffffff))})});
        Element word = stack({.id = "word", .opacity = 0.0f}, {mark("wordmark", hex(ink::cyan)), std::move(light)});
        GlyphMotion lift = GlyphMotion::rise(hanim::seconds(0.04));
        lift.transition.delay = hanim::seconds(6.35);
        Element tagline = animate_glyphs(text({.id = "tagline", .text = "an open UI specification · its reference implementation in C++ modules", .style = "code",
                                               .color = Fill(hex(ink::muted)), .wrap = false, .size = portrait ? 15.0f : 19.0f}),
                                         lift);
        Element type = stack({.id = "type", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                             {column({.id = "label", .gap = 6, .opacity = 0.0f}, {animate_glyphs(std::move(label), typed), animate_glyphs(std::move(coords), later)}),
                              std::move(word), box({.id = "under", .opacity = 0.0f}, {std::move(tagline)})});
        return stack({.id = "origin", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"}, {std::move(lens), std::move(type)});
    }

    void mounted(Runtime& rt) override {
        const PanelPicture& panel = panel_picture();
        refs_->first = rt.images().add(panel.x1);
        refs_->second = rt.images().add(panel.x2);
        lens_ = rt.find("main/lens");
        label_ = rt.find("main/type/label");
        word_ = rt.find("main/type/word");
        under_ = rt.find("main/type/under");
        sweep_from_ = rt.find("main/type/word/sweep-from");
        sweep_to_ = rt.find("main/type/word/sweep-from/sweep-to");
        layout_tree();
        layout_wordmark(rt);
    }

    void at(Runtime& rt, double t) override {
        State& s = *s_;
        const PanelPicture& panel = panel_picture();
        const Size screen = c_.size;
        s.t = t;
        const double magnify = c_.value("magnify", t, 0);
        s.zoom = hanim::dm::exp(magnify * hanim::dm::ln2);
        s.tree = c_.value("tree", t, 0);
        s.gather = c_.value("gather", t, 0);
        s.sweep = c_.value("sweep", t, 0);
        // Where the camera looks: the pixel, sliding to the panel's middle as it pulls back; the panel moving aside for its tree.
        const Point pixel{panel.pixel.x + 0.5f, panel.pixel.y + 0.5f};
        const Point middle{panel.size.width / 2, panel.size.height / 2};
        const double u = smooth(magnify, 5.5, 0.4);
        s.look = {static_cast<float>(mix(pixel.x, middle.x, u)), static_cast<float>(mix(pixel.y, middle.y, u))};
        const double aside = smooth(s.tree, 0, 1);
        const Point centre{screen.width / 2, screen.height / 2};
        const Point side = c_.portrait() ? Point{screen.width / 2, screen.height * 0.27f} : Point{screen.width * 0.3f, screen.height / 2};
        s.at = {static_cast<float>(mix(centre.x, side.x, aside)), static_cast<float>(mix(centre.y, side.y, aside))};
        // The dark: only the pixel at first, then the panel around it as the camera pulls back.
        // Lit on the first frame, with the ping: a flash round it that settles.
        s.pixel_glow = static_cast<float>(1 + 1.6 * (1 - smooth(t, 0.0, 0.6)));
        s.dark = static_cast<float>(1 - smooth(t, 1.4, 2.6));
        s.panel_opacity = static_cast<float>(1 - smooth(s.gather, 0.05, 0.6));
        // The label: beside the pixel while it is the whole screen, gone as the panel comes.
        const float off = c_.portrait() ? 0.18f : 0.22f;
        const Point p = panel_to_screen({panel.pixel.x + 1, panel.pixel.y + 1}, s.at, s.look, s.zoom);
        const double label_on = smooth(t, 0.45, 0.6) * (1 - smooth(t, 2.4, 2.9));
        rt.set(label_, "opacity", Value(label_on));
        const Rect lb = rt.bounds(label_);
        rt.set(label_, "translate_x", Value(std::clamp(static_cast<double>(p.x) + screen.width * 0.04, screen.width * 0.06,
                                                       std::max(screen.width * 0.06, screen.width * 0.94 - lb.width))));
        // Clear of the lines along the bottom.
        rt.set(label_, "translate_y", Value(std::clamp(static_cast<double>(p.y) + screen.height * off * 0.25, screen.height * 0.08, screen.height * 0.66 - lb.height)));
        // The wordmark: filled in as the dashes settle, then the band of light.
        const double filled = smooth(s.gather, 0.82, 1.0);
        rt.set(word_, "opacity", Value(filled));
        rt.set(word_, "translate_x", Value(static_cast<double>(s.wordmark.x)));
        rt.set(word_, "translate_y", Value(static_cast<double>(s.wordmark.y)));
        // The tagline under the rule, centred (its width once laid out).
        const Rect tag = rt.bounds(rt.find("main/type/under/tagline"));
        rt.set(under_, "opacity", Value(filled));
        rt.set(under_, "translate_x", Value(static_cast<double>((screen.width - tag.width) / 2)));
        rt.set(under_, "translate_y", Value(static_cast<double>(s.wordmark.y + s.wordmark.height + 34)));
        const double band = 0.16, at_x = mix(-band, 1 + band, s.sweep);
        const bool sweeping = s.sweep > 0 && s.sweep < 1;
        rt.set(sweep_from_, "pixel_effect",
               sweeping ? Value{}.set("name", "wipe").set("progress", std::clamp(at_x - band, 0.0, 1.0)).set("angle", 0.0).set("softness", 0.06) : Value{});
        rt.set(sweep_to_, "pixel_effect",
               sweeping ? Value{}.set("name", "wipe").set("progress", std::clamp(1 - at_x, 0.0, 1.0)).set("angle", 180.0).set("softness", 0.06) : Value{});
        rt.set(rt.find("main/type/word/sweep-from/sweep-to/light"), "opacity", Value(sweeping ? 0.9 : 0.0));
        rt.invalidate_paint(lens_);
    }

    std::vector<Cue> cues(double) override { return {}; }

private:
    static void paint(PaintContext& p, State& s, const std::pair<render::ImageRef, render::ImageRef>& refs);
    void layout_tree();
    void layout_wordmark(Runtime& rt);

    ShotContext                                                  c_;
    std::shared_ptr<State>                                       s_;
    std::shared_ptr<std::pair<render::ImageRef, render::ImageRef>> refs_;
    NodeId lens_{}, label_{}, word_{}, under_{}, sweep_from_{}, sweep_to_{};
    float  word_size_ = 210;
};

// The tree from the panel's root to the node that drew the pixel.
void Origin::layout_tree() {
    State& s = *s_;
    const PanelPicture& panel = panel_picture();
    const Size screen = c_.size;
    const bool portrait = c_.portrait();
    std::map<std::string, const spec::SemanticNode*> by_id;
    for (const auto& n : panel.tree) by_id[n.id] = &n;
    // The path: from the root, the child whose id the drawing node's begins with, down to it.
    std::vector<const spec::SemanticNode*> path;
    if (!panel.tree.empty()) path.push_back(&panel.tree.front());
    while (!path.empty() && path.back()->id != panel.path) {
        const spec::SemanticNode* next = nullptr;
        for (const std::string& child : path.back()->children)
            if (panel.path == child || panel.path.starts_with(child + "/"))
                if (const auto it = by_id.find(child); it != by_id.end()) next = it->second;
        if (!next) break;
        path.push_back(next);
    }
    if (path.empty()) return;
    const Rect area = portrait ? Rect{screen.width * 0.06f, screen.height * 0.5f, screen.width * 0.88f, screen.height * 0.38f}
                               : Rect{screen.width * 0.6f, screen.height * 0.14f, screen.width * 0.36f, screen.height * 0.72f};
    const std::size_t levels = path.size();
    const float box_w = portrait ? 150 : 170, box_h = 24;
    std::vector<Rect> lit_rects;
    for (std::size_t l = 0; l < levels; ++l) {
        // The level's nodes: the path's node among its siblings (at most five shown, it among them).
        std::vector<std::string> ids;
        if (l == 0) ids.push_back(path[0]->id);
        else
            for (const std::string& child : path[l - 1]->children) ids.push_back(child);
        std::size_t lit = std::ranges::find(ids, path[l]->id) - ids.begin();
        const std::size_t keep = 5;
        std::size_t from = lit >= keep / 2 ? lit - keep / 2 : 0;
        from = std::min(from, ids.size() > keep ? ids.size() - keep : 0);
        const std::size_t to = std::min(ids.size(), from + keep);
        const float col = portrait ? area.y + area.height * static_cast<float>(l) / static_cast<float>(std::max<std::size_t>(levels - 1, 1))
                                   : area.x + (area.width - box_w) * static_cast<float>(l) / static_cast<float>(std::max<std::size_t>(levels - 1, 1));
        for (std::size_t i = from; i < to; ++i) {
            const auto it = by_id.find(ids[i]);
            if (it == by_id.end()) continue;
            const spec::SemanticNode& n = *it->second;
            const std::string id = n.id.substr(n.id.rfind('/') + 1);
            const float k = static_cast<float>(i - from) - static_cast<float>(lit - from);
            Rect r = portrait ? Rect{area.x + area.width / 2 - box_w / 2 + k * (box_w + 14), col, box_w, box_h}
                              : Rect{col, area.y + area.height / 2 - box_h / 2 + k * (box_h + 16), box_w, box_h};
            s.boxes.push_back({std::format("{} {}", spec::name(n.role), id), r, static_cast<int>(l), i == lit});
            if (i == lit) lit_rects.push_back(r);
        }
    }
    // Edges: from each lit box to the boxes of the next level.
    for (const TreeBox& b : s.boxes) {
        if (b.level == 0) continue;
        const Rect& parent = lit_rects[static_cast<std::size_t>(b.level - 1)];
        if (portrait) s.edges.push_back({{parent.x + parent.width / 2, parent.y + parent.height}, {b.rect.x + b.rect.width / 2, b.rect.y}, b.lit, b.level});
        else s.edges.push_back({{parent.x + parent.width, parent.y + parent.height / 2}, {b.rect.x, b.rect.y + b.rect.height / 2}, b.lit, b.level});
    }
}

// The wordmark's outline: its text drawn on a host of its own, the edge of its alpha sampled.
void Origin::layout_wordmark(Runtime& rt) {
    State& s = *s_;
    const Size screen = c_.size;
    Headless h(HeadlessOptions{.size = {screen.width, word_size_ * 1.4f}, .background = Color::transparent(), .fonts = &rt.fonts(), .app = "openmui-promo-mark"});
    h.mount("main", row({.width = Dim(layout::pct(100)), .justify = "center"},
                        {text({.id = "mark", .text = std::string(wordmark_text), .style = "display", .color = Fill(Color::white()), .wrap = false, .size = word_size_,
                               .weight = 800.0f})}));
    h.settle();
    const Rect b = h.runtime().bounds(h.runtime().find("main/mark"));
    const float top = (c_.portrait() ? screen.height * 0.42f : screen.height * 0.44f) - b.height / 2;
    s.wordmark = {b.x, top, b.width, b.height};
    const raster::Surface& px = h.pixels();
    std::vector<Mark> edge;
    for (int y = 1; y + 1 < px.height(); y += 2)
        for (int x = 1; x + 1 < px.width(); ++x) {
            const int a = px.at(x, y).a;
            if (a < 128) continue;
            const int l = px.at(x - 1, y).a, r = px.at(x + 1, y).a, u = px.at(x, y - 1).a, d = px.at(x, y + 1).a;
            if (l >= 128 && r >= 128 && u >= 128 && d >= 128) continue;
            const double gx = r - l, gy = d - u;  // the outline runs across its gradient
            edge.push_back({{static_cast<float>(x), top - b.y + static_cast<float>(y)}, static_cast<float>(hanim::dm::atan2(gx, -gy))});
        }
    // About seven hundred dashes, spread evenly over the outline.
    const std::size_t want = 720;
    const std::size_t step = std::max<std::size_t>(1, edge.size() / want);
    for (std::size_t i = 0; i < edge.size(); i += step) s.marks.push_back(edge[i]);
    // Where they start: along the tree's lines and round its boxes, as many points.
    std::vector<std::pair<Point, Point>> segs;
    for (const TreeEdge& e : s.edges) segs.push_back({e.a, e.b});
    for (const TreeBox& bx : s.boxes) {
        const Rect r = bx.rect;
        segs.push_back({{r.x, r.y}, {r.x + r.width, r.y}});
        segs.push_back({{r.x + r.width, r.y}, {r.x + r.width, r.y + r.height}});
        segs.push_back({{r.x + r.width, r.y + r.height}, {r.x, r.y + r.height}});
        segs.push_back({{r.x, r.y + r.height}, {r.x, r.y}});
    }
    double total = 0;
    for (const auto& [a, z] : segs) total += hanim::dm::sqrt((z.x - a.x) * (z.x - a.x) + (z.y - a.y) * (z.y - a.y));
    const std::size_t n = s.marks.size();
    if (n == 0 || total <= 0) return;
    double along = 0, next = 0;
    const double gap = total / static_cast<double>(n);
    for (const auto& [a, z] : segs) {
        const double len = hanim::dm::sqrt((z.x - a.x) * (z.x - a.x) + (z.y - a.y) * (z.y - a.y));
        const float angle = static_cast<float>(hanim::dm::atan2(z.y - a.y, z.x - a.x));
        while (next < along + len && s.sources.size() < n) {
            const double u = (next - along) / std::max(len, 1e-9);
            s.sources.push_back({{static_cast<float>(a.x + (z.x - a.x) * u), static_cast<float>(a.y + (z.y - a.y) * u)}, angle});
            next += gap;
        }
        along += len;
    }
    while (s.sources.size() < n) s.sources.push_back(s.sources.empty() ? Mark{{screen.width / 2, screen.height / 2}, 0} : s.sources.back());
    // Matched by their order across the screen, so they fly mostly one way; staggered left to right.
    std::vector<std::size_t> from(n), to(n);
    std::iota(from.begin(), from.end(), 0);
    std::iota(to.begin(), to.end(), 0);
    const bool portrait = c_.portrait();
    std::ranges::sort(from, [&](std::size_t i, std::size_t j) {
        const auto key = [&](const Mark& m) { return portrait ? std::pair{m.at.y, m.at.x} : std::pair{m.at.x, m.at.y}; };
        return key(s.sources[i]) < key(s.sources[j]);
    });
    std::ranges::sort(to, [&](std::size_t i, std::size_t j) { return std::pair{s.marks[i].at.x, s.marks[i].at.y} < std::pair{s.marks[j].at.x, s.marks[j].at.y}; });
    std::vector<Mark> sources(n);
    s.delays.assign(n, 0);
    for (std::size_t k = 0; k < n; ++k) {
        sources[to[k]] = s.sources[from[k]];
        s.delays[to[k]] = 0.42f * static_cast<float>(k) / static_cast<float>(n);
    }
    s.sources = std::move(sources);
}

void Origin::paint(PaintContext& p, State& s, const std::pair<render::ImageRef, render::ImageRef>& refs) {
    const PanelPicture& panel = panel_picture();
    const Size screen = p.size;
    auto& list = p.list;
    auto& fonts = p.services.fonts();
    // The paper, its grid coming up with the light; the grid at the panel's scale, so it moves with it.
    list.fill_rect({0, 0, screen.width, screen.height}, hex(ink::paper_deep));
    if (s.dark < 1) {
        list.push_layer(1 - s.dark);
        const float grid = static_cast<float>(std::clamp(s.zoom, 0.25, 4.0));
        const Point o = panel_to_screen({0, 0}, s.at, s.look, s.zoom);
        paint_paper(list, screen, o, grid);
        list.pop_layer();
    }
    // The panel's real pixels, magnified.
    if (s.panel_opacity > 0) {
        const Point a = panel_to_screen({0, 0}, s.at, s.look, s.zoom), z = panel_to_screen({panel.size.width, panel.size.height}, s.at, s.look, s.zoom);
        if (s.zoom < 2 && s.dark < 1)
            list.box_shadow({a.x, a.y, z.x - a.x, z.y - a.y}, CornerRadii::all(0), {0, 18}, 48, 0, hex(0x000000, 0.55f * s.panel_opacity * (1 - s.dark)));
        paint_panel(list, p.services.images(), refs, panel, screen, s.at, s.look, s.zoom, s.panel_opacity);
        if (s.zoom < 3 && s.dark < 1) paint_ticks(list, {a.x, a.y, z.x - a.x, z.y - a.y}, hex(ink::cyan, 0.7f * s.panel_opacity * (1 - s.dark)), 18, 1.5f, 0.4f);
    }
    // The dark over everything but the pixel (a hole the pixel's size, widening as the light comes).
    const Point p0 = panel_to_screen(panel.pixel, s.at, s.look, s.zoom);
    const Point p1 = panel_to_screen({panel.pixel.x + 1, panel.pixel.y + 1}, s.at, s.look, s.zoom);
    const Rect pix{p0.x, p0.y, p1.x - p0.x, p1.y - p0.y};
    if (s.dark > 0) {
        const float a = s.dark;
        const Color dark = hex(ink::paper_deep, a);
        list.fill_rect({0, 0, screen.width, std::max(0.0f, pix.y)}, dark);
        list.fill_rect({0, pix.y + pix.height, screen.width, std::max(0.0f, screen.height - pix.y - pix.height)}, dark);
        list.fill_rect({0, pix.y, std::max(0.0f, pix.x), pix.height}, dark);
        list.fill_rect({pix.x + pix.width, pix.y, std::max(0.0f, screen.width - pix.x - pix.width), pix.height}, dark);
        // The pixel itself, lighting up: dark, then its colour, a halo round it.
        const float glow = std::min(1.0f, s.pixel_glow * 0.5f);
        const std::array<render::GradientStop, 3> halo{{{0, hex(ink::cyan, 0.32f * glow * a)}, {0.45f, hex(ink::cyan, 0.1f * glow * a)}, {1, hex(ink::cyan, 0)}}};
        render::Paint g;
        g.kind = render::Paint::Kind::gradient;
        g.gradient.kind = render::GradientKind::radial;
        g.gradient.start = g.gradient.end = {pix.x + pix.width / 2, pix.y + pix.height / 2};
        g.gradient.radius = std::max(pix.width, pix.height) * (0.8f + 0.5f * s.pixel_glow);
        g.gradient.stops = halo;
        list.fill_rect({0, 0, screen.width, screen.height}, g);
    }
    // The pixel's frame and dimension, while it is large.
    if (pix.width > 60) {
        const float on = static_cast<float>(smooth(s.t, 0.35, 0.7) * (1 - smooth(s.t, 2.4, 2.9)));
        if (on > 0) {
            paint_ticks(list, {pix.x - 10, pix.y - 10, pix.width + 20, pix.height + 20}, hex(ink::ice, 0.8f * on), 26, 2, 0);
            const auto& dim = s.labels.get(fonts, "1 px", 18, hex(ink::muted, on), 500, "JetBrains Mono");
            dim.paint(list, {pix.x + pix.width / 2 - dim.size().width / 2, pix.y - 44});
        }
    }
    // The Save button's frame, as the panel comes into view.
    {
        const double on = smooth(s.t, 2.6, 3.1) * (1 - smooth(s.tree, 0.6, 1.0));
        if (on > 0) {
            const Point a = panel_to_screen({panel.button.x, panel.button.y}, s.at, s.look, s.zoom);
            const Point z = panel_to_screen({panel.button.x + panel.button.width, panel.button.y + panel.button.height}, s.at, s.look, s.zoom);
            paint_ticks(list, {a.x - 6, a.y - 6, z.x - a.x + 12, z.y - a.y + 12}, hex(ink::cyan, static_cast<float>(on)), 10, 2, 0.6f);
        }
    }
    // The semantic tree, growing level by level; the lit path to the button.
    const float grow = static_cast<float>(s.tree);
    const float solid = static_cast<float>(1 - smooth(s.gather, 0.0, 0.12));
    if (grow > 0 && solid > 0) {
        int levels = 1;
        for (const TreeBox& b : s.boxes) levels = std::max(levels, b.level + 1);
        auto shown = [&](int level) { return static_cast<float>(smooth(grow * (levels + 1.5), level, level + 1.2)); };
        for (const TreeEdge& e : s.edges) {
            const float on = shown(e.level);
            if (on <= 0) continue;
            const Point m{e.a.x + (e.b.x - e.a.x) * on, e.a.y + (e.b.y - e.a.y) * on};
            paint_line(list, e.a, m, hex(e.lit ? ink::cyan : ink::line, solid * (e.lit ? 1.0f : 0.8f)), e.lit ? 2 : 1.2f);
        }
        for (const TreeBox& b : s.boxes) {
            const float on = shown(b.level) * solid;
            if (on <= 0) continue;
            list.fill_round_rect(b.rect, CornerRadii::all(4), render::Paint::solid(hex(b.lit ? 0x0d3b55 : ink::panel, 0.92f * on)));
            list.stroke_round_rect(b.rect, CornerRadii::all(4), b.lit ? 1.6f : 1, render::Paint::solid(hex(b.lit ? ink::cyan : ink::line, on)));
            const auto& l = s.labels.get(fonts, b.label, 12.5f, hex(b.lit ? ink::ice : ink::muted, on), 500, "JetBrains Mono");
            list.push_clip({b.rect.x + 6, b.rect.y, b.rect.width - 12, b.rect.height});
            l.paint(list, {b.rect.x + 9, b.rect.y + b.rect.height / 2 - l.size().height / 2});
            list.pop_clip();
        }
        // The lit node to the button it is: a leader line.
        const TreeBox* last = nullptr;
        for (const TreeBox& b : s.boxes)
            if (b.lit) last = &b;
        if (last) {
            const float on = static_cast<float>(smooth(grow, 0.85, 1.0)) * solid;
            const Point z = panel_to_screen({panel.button.x + panel.button.width, panel.button.y + panel.button.height / 2}, s.at, s.look, s.zoom);
            const Point a = screen.height > screen.width ? Point{last->rect.x + last->rect.width / 2, last->rect.y} : Point{last->rect.x, last->rect.y + last->rect.height / 2};
            if (on > 0) paint_line(list, a, {a.x + (z.x - a.x) * on, a.y + (z.y - a.y) * on}, hex(ink::cyan, 0.9f), 1.5f);
        }
    }
    // The gathering: the tree's lines as dashes, flying to the wordmark's outline.
    if (s.gather > 0 && !s.marks.empty()) {
        const float fade = static_cast<float>(1 - smooth(s.gather, 0.9, 1.0));
        if (fade > 0) {
            render::Path dashes;
            for (std::size_t i = 0; i < s.marks.size(); ++i) {
                const double g = hanim::ease::in_out_cubic(std::clamp((s.gather - s.delays[i]) / 0.55, 0.0, 1.0));
                const Mark& a = s.sources[i];
                const Mark& z = s.marks[i];
                const double arc = hanim::dm::sin(g * hanim::dm::pi) * 40 * (static_cast<double>(i % 7) - 3) / 3;
                const double x = mix(a.at.x, z.at.x, g) + arc * 0.3, y = mix(a.at.y, z.at.y, g) + arc;
                double da = z.angle - a.angle;
                while (da > hanim::dm::pi) da -= hanim::dm::two_pi;
                while (da < -hanim::dm::pi) da += hanim::dm::two_pi;
                const double angle = a.angle + da * g, len = mix(7, 4.5, g);
                const double cx = hanim::dm::cos(angle) * len / 2, cy = hanim::dm::sin(angle) * len / 2;
                dashes.move_to({static_cast<float>(x - cx), static_cast<float>(y - cy)});
                dashes.line_to({static_cast<float>(x + cx), static_cast<float>(y + cy)});
            }
            render::StrokeStyle st;
            st.width = 2.2f;
            st.cap = render::LineCap::round;
            list.stroke_path(dashes, st, render::Paint::solid(hex(ink::cyan, fade)));
        }
        // The drawing's construction lines round the wordmark: its axis, its edges and their dimension, a circle drawn round it.
        const float lines = static_cast<float>(smooth(s.t, 5.9, 6.8));
        if (lines > 0) {
            const Rect w = s.wordmark;
            const float cx = w.x + w.width / 2, cy = w.y + w.height / 2;
            const Color faint = hex(ink::cyan, 0.16f * lines), line = hex(ink::cyan, 0.45f * lines);
            const float reach = static_cast<float>(smooth(s.t, 5.9, 7.0));
            list.fill_rect({cx - screen.width * 0.48f * reach, cy, screen.width * 0.96f * reach, 1}, faint);
            list.fill_rect({cx, cy - screen.height * 0.42f * reach, 1, screen.height * 0.84f * reach}, faint);
            for (const float x : {w.x, w.x + w.width}) list.fill_rect({x, w.y - 46, 1, w.height + 60}, line);
            const float dy = w.y - 30;
            list.fill_rect({w.x, dy, w.width * reach, 1}, line);
            for (const float x : {w.x, w.x + w.width}) list.fill_rect({x - 4, dy - 4, 9, 1}, line), list.fill_rect({x, dy - 4, 1, 9}, line);
            const auto& dim = s.labels.get(fonts, "one semantic tree · one wordmark", 14, hex(ink::muted, lines), 500, "JetBrains Mono");
            list.fill_rect({cx - dim.size().width / 2 - 8, dy - 9, dim.size().width + 16, 18}, hex(ink::paper, lines));
            dim.paint(list, {cx - dim.size().width / 2, dy - dim.size().height / 2});
            const float radius = w.width * 0.6f;
            const double sweep = hanim::dm::two_pi * smooth(s.t, 6.0, 7.4);
            render::Path arc;
            const int steps = 180;
            for (int k = 0; k <= steps; ++k) {
                const double a = -hanim::dm::half_pi + sweep * k / steps;
                const Point q{cx + static_cast<float>(hanim::dm::cos(a)) * radius, cy + static_cast<float>(hanim::dm::sin(a)) * radius};
                if (k == 0) arc.move_to(q);
                else arc.line_to(q);
            }
            render::StrokeStyle st;
            st.width = 1;
            if (sweep > 0) list.stroke_path(arc, st, render::Paint::solid(faint));
        }
        // A rule under the wordmark, drawn out once it stands.
        const float rule = static_cast<float>(smooth(s.gather, 0.9, 1.0) * smooth(s.t, 6.0, 6.6));
        if (rule > 0) {
            const float y = s.wordmark.y + s.wordmark.height + 8, cx = screen.width / 2, half = s.wordmark.width * 0.5f * rule;
            list.fill_rect({cx - half, y, half * 2, 2}, hex(ink::cyan, 0.8f));
        }
    }
}

}  // namespace

std::unique_ptr<Shot> origin_shot(const ShotContext& c) { return std::make_unique<Origin>(c); }

}  // namespace omui::promo
