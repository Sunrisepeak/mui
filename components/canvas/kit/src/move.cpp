// drag, resize and snap: moving nodes and changing their size, meeting
// their neighbours' lines on the way.
module openmui.ui.canvas.kit;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.canvas;
import :parts;

namespace omui::kit {

namespace {

// Snap's live instances (a behaviour named "snap" may be a program's own:
// only canvas-kit's is asked).
std::vector<const Snap*>& snaps() {
    static std::vector<const Snap*> all;
    return all;
}

// A press has to travel this far on screen before it moves anything.
constexpr float slop = 3;

WorldRect union_of(const Scene& s, std::span<const NodeRef> nodes) {
    WorldRect u;
    bool first = true;
    for (NodeRef n : nodes) {
        u = first ? s.rect(n) : u.united(s.rect(n));
        first = false;
    }
    return u;
}

// Dragging a node moves it, and the rest of the selection with it; a kind
// with a title bar is dragged by the bar (its body is the live component's).
// A press on a node chooses it first (⇧ adds); one that does not travel is
// a click: it chooses that node alone, or with ⇧ takes it out. Esc while
// dragging puts everything back.
class Drag final : public Behavior {
public:
    explicit Drag(std::string_view args) {
        if (!args.empty()) warn("canvas-kit: drag takes no arguments, not `{}`", args);
    }
    std::string_view name() const override { return "drag"; }
    int priority() const override { return 40; }

    Claim pointer(Host& h, const PointerInput& in) override {
        Scene& s = h.scene();
        switch (in.kind) {
            case PointerKind::down: {
                if (!in.hit || in.button != MouseButton::left || in.clicks >= 2 || in.mods.has(Modifier::alt)) return Claim::pass;
                const world::Kind& k = s.kind_of(in.hit->node);
                if (k.title_bar > 0 && !in.hit->title) return Claim::pass;
                pressed_ = std::string(s.key(in.hit->node));
                was_selected_ = selected(h, pressed_);
                shift_ = in.mods.has(Modifier::shift);
                if (!was_selected_) {
                    std::vector<std::string> keys = shift_ ? h.selection() : std::vector<std::string>{};
                    keys.push_back(pressed_);
                    h.select(std::move(keys));
                }
                // What moves: the selection, where each one was.
                moving_.clear();
                keys_.clear();
                for (const std::string& key : h.selection())
                    if (auto n = s.find(key)) {
                        const WorldRect r = s.rect(*n);
                        moving_.push_back({*n, r.x, r.y});
                        keys_.push_back(key);
                    }
                std::vector<NodeRef> nodes;
                for (const Moving& m : moving_) nodes.push_back(m.node);
                from_ = union_of(s, nodes);
                start_ = in.world;
                at_ = in.screen;
                travelled_ = cancelled_ = false;
                dx_ = dy_ = 0;
                return Claim::take;
            }
            case PointerKind::move: {
                if (cancelled_ || moving_.empty()) return Claim::take;
                if (!travelled_ && std::hypot(in.screen.x - at_.x, in.screen.y - at_.y) <= slop) return Claim::take;
                if (!travelled_) {
                    travelled_ = true;
                    // Raised above the rest, the pressed one on top.
                    for (const Moving& m : moving_)
                        if (s.valid(m.node) && s.key(m.node) != pressed_) s.raise(m.node);
                    if (auto p = s.find(pressed_)) s.raise(*p);
                }
                double dx = in.world[0] - start_[0], dy = in.world[1] - start_[1];
                if (Snap* snap = Snap::of(h)) {
                    const auto r = snap->snap(h, keys_, {from_.x + dx, from_.y + dy, from_.width, from_.height});
                    dx += r.dx, dy += r.dy;
                }
                place(s, dx, dy);
                return Claim::take;
            }
            default: {
                if (Snap* snap = Snap::of(h)) snap->done(h);
                if (in.kind == PointerKind::cancel && !cancelled_) place(s, 0, 0);
                else if (in.kind == PointerKind::up && travelled_ && !cancelled_ && (dx_ != 0 || dy_ != 0))
                    h.committed("moved", Value{}.set("keys", keys_value(keys_)).set("dx", Value(dx_)).set("dy", Value(dy_)));
                else if (in.kind == PointerKind::up && !travelled_) {
                    // A click on a node.
                    std::vector<std::string> keys = h.selection();
                    if (shift_ && was_selected_) std::erase(keys, pressed_);
                    else if (!shift_) keys = {pressed_};
                    h.select(std::move(keys));
                }
                moving_.clear();
                return Claim::take;
            }
        }
    }

    bool key(Host& h, const KeyEvent& e) override {
        if (e.key != Key::escape || moving_.empty() || cancelled_) return false;
        place(h.scene(), 0, 0);
        cancelled_ = true;
        if (Snap* snap = Snap::of(h)) snap->done(h);
        return true;
    }

    void describe(Host&, NodeRef, spec::SemanticNode& node) override {
        node.actions.push_back({"move_to", spec::Effect::reversible, {}, {}, {{"x", "f64"}, {"y", "f64"}}});
    }

    std::optional<ActResult> act(Host& h, NodeRef n, std::string_view action, const Value& args) override {
        if (action != "move_to") return std::nullopt;
        const Value *x = args.get("x"), *y = args.get("y");
        if (!x || !y || !x->is_number() || !y->is_number()) return ActResult::fail("move_to takes x and y, numbers");
        Scene& s = h.scene();
        const WorldRect r = s.rect(n);
        const double dx = x->as_double() - r.x, dy = y->as_double() - r.y;
        // A kind that carries (a workspace) takes what lies on it along, as a person's drag of it does.
        std::vector<NodeRef> along;
        if (s.kind_of(n).carries) s.carried(n, along);
        std::vector<std::string> keys{std::string(s.key(n))};
        s.move(n, x->as_double(), y->as_double());
        for (NodeRef m : along) {
            const WorldRect q = s.rect(m);
            s.move(m, q.x + dx, q.y + dy);
            keys.emplace_back(s.key(m));
        }
        h.committed("moved", Value{}.set("keys", keys_value(keys)).set("dx", Value(dx)).set("dy", Value(dy)));
        return ActResult::done(Value(static_cast<std::int64_t>(keys.size())));
    }

private:
    void place(Scene& s, double dx, double dy) {
        for (const Moving& m : moving_)
            if (s.valid(m.node)) s.move(m.node, m.x + dx, m.y + dy);
        dx_ = dx, dy_ = dy;
    }

    struct Moving {
        NodeRef node;
        double  x, y;
    };
    std::vector<Moving>      moving_;
    std::vector<std::string> keys_;
    std::string              pressed_;
    WorldRect                from_;
    std::array<double, 2>    start_{};
    Point                    at_{};
    double                   dx_ = 0, dy_ = 0;
    bool                     travelled_ = false, cancelled_ = false, was_selected_ = false, shift_ = false;
};

// Handles on the one chosen node: its corners and sides. ⇧ on a corner
// keeps its proportions; no side passes the one across (a minimum of 8
// pixels on screen); Esc puts it back.
class Resize final : public Behavior {
public:
    explicit Resize(std::string_view args) {
        if (!args.empty()) warn("canvas-kit: resize takes no arguments, not `{}`", args);
    }
    std::string_view name() const override { return "resize"; }
    int priority() const override { return 60; }

    Claim pointer(Host& h, const PointerInput& in) override {
        Scene& s = h.scene();
        switch (in.kind) {
            case PointerKind::down: {
                if (in.button != MouseButton::left) return Claim::pass;
                const auto n = target(h);
                if (!n) return Claim::pass;
                const auto handle = handle_at(screen_of(h, s.rect(*n)), in.screen);
                if (!handle) return Claim::pass;
                node_ = *n;
                handle_ = *handle;
                from_ = s.rect(*n);
                start_ = in.world;
                active_ = true;
                changed_ = false;
                return Claim::take;
            }
            case PointerKind::move: {
                if (!active_ || !s.valid(node_)) return Claim::take;
                const auto [ex, ey] = edges(handle_);
                const double dx = in.world[0] - start_[0], dy = in.world[1] - start_[1];
                double l = from_.x, t = from_.y, r = from_.right(), b = from_.bottom();
                if (ex < 0) l += dx;
                if (ex > 0) r += dx;
                if (ey < 0) t += dy;
                if (ey > 0) b += dy;
                if (Snap* snap = Snap::of(h)) {
                    const std::array<std::string, 1> self{std::string(s.key(node_))};
                    const auto c = snap->snap(h, self, {l, t, r - l, b - t}, {ex < 0, false, ex > 0}, {ey < 0, false, ey > 0});
                    if (ex < 0) l += c.dx;
                    if (ex > 0) r += c.dx;
                    if (ey < 0) t += c.dy;
                    if (ey > 0) b += c.dy;
                }
                // ⇧ on a corner: the proportions it had.
                if (in.mods.has(Modifier::shift) && ex != 0 && ey != 0 && from_.height > 0) {
                    const double aspect = from_.width / from_.height;
                    const double w = r - l, hgt = b - t;
                    if (std::abs(w) / aspect > std::abs(hgt)) {
                        const double nh = std::abs(w) / aspect;
                        if (ey < 0) t = b - nh;
                        else b = t + nh;
                    } else {
                        const double nw = std::abs(hgt) * aspect;
                        if (ex < 0) l = r - nw;
                        else r = l + nw;
                    }
                }
                const double least = 8 / h.camera().zoom;
                if (r - l < least) {
                    if (ex < 0) l = r - least;
                    else r = l + least;
                }
                if (b - t < least) {
                    if (ey < 0) t = b - least;
                    else b = t + least;
                }
                s.move(node_, l, t);
                s.resize(node_, static_cast<float>(r - l), static_cast<float>(b - t));
                changed_ = true;
                return Claim::take;
            }
            default: {
                if (Snap* snap = Snap::of(h)) snap->done(h);
                if (active_ && s.valid(node_)) {
                    if (in.kind == PointerKind::cancel) put_back(s);
                    else if (changed_) commit(h, node_);
                }
                active_ = false;
                return Claim::take;
            }
        }
    }

    bool key(Host& h, const KeyEvent& e) override {
        if (e.key != Key::escape || !active_) return false;
        if (h.scene().valid(node_)) put_back(h.scene());
        active_ = false;
        if (Snap* snap = Snap::of(h)) snap->done(h);
        return true;
    }

    void paint_over(Host& h, render::DisplayList& list) override {
        const auto n = target(h);
        if (!n) return;
        const Rect r = screen_of(h, h.scene().rect(*n));
        if (std::min(r.width, r.height) < 24) return;  // too small to hold handles
        const Color c = accent(h), fill = h.services().theme().color("surface", Color::hex(0xffffff));
        for (int i = 0; i < 8; ++i) {
            const Point p = handle_point(r, i);
            const Rect box{p.x - 4, p.y - 4, 8, 8};
            list.fill_rect(box, fill);
            list.stroke_round_rect(box, {}, 1.5f, render::Paint::solid(c));
        }
    }

    void describe(Host&, NodeRef, spec::SemanticNode& node) override {
        node.actions.push_back({"resize", spec::Effect::reversible, {}, {}, {{"height", "f64"}, {"width", "f64"}}});
    }

    std::optional<ActResult> act(Host& h, NodeRef n, std::string_view action, const Value& args) override {
        if (action != "resize") return std::nullopt;
        const Value *w = args.get("width"), *hg = args.get("height");
        if (!w || !hg || !w->is_number() || !hg->is_number() || !(w->as_double() > 0) || !(hg->as_double() > 0))
            return ActResult::fail("resize takes a width and a height above 0");
        h.scene().resize(n, static_cast<float>(w->as_double()), static_cast<float>(hg->as_double()));
        commit(h, n);
        return ActResult::done();
    }

private:
    // The node with handles: the selection, when it is one node.
    static std::optional<NodeRef> target(Host& h) {
        if (h.selection().size() != 1) return std::nullopt;
        return h.scene().find(h.selection().front());
    }

    // Handles clockwise from the top-left: which edges each moves (-1: left /
    // top, 1: right / bottom).
    static std::pair<int, int> edges(int handle) {
        constexpr std::array<std::pair<int, int>, 8> e{{{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}}};
        return e[static_cast<std::size_t>(handle)];
    }
    static Point handle_point(Rect r, int handle) {
        const auto [ex, ey] = edges(handle);
        return {r.x + r.width * static_cast<float>(ex + 1) / 2, r.y + r.height * static_cast<float>(ey + 1) / 2};
    }
    static std::optional<int> handle_at(Rect r, Point p) {
        if (std::min(r.width, r.height) < 24) return std::nullopt;
        // Corners first: they overlap the sides' near the ends.
        for (int i : {0, 2, 4, 6, 1, 3, 5, 7}) {
            const Point c = handle_point(r, i);
            if (std::abs(p.x - c.x) <= 6 && std::abs(p.y - c.y) <= 6) return i;
        }
        return std::nullopt;
    }

    void put_back(Scene& s) {
        s.move(node_, from_.x, from_.y);
        s.resize(node_, static_cast<float>(from_.width), static_cast<float>(from_.height));
    }

    static void commit(Host& h, NodeRef n) {
        const WorldRect r = h.scene().rect(n);
        h.committed("resized", Value{}
                                   .set("key", Value(h.scene().key(n)))
                                   .set("x", Value(r.x))
                                   .set("y", Value(r.y))
                                   .set("width", Value(r.width))
                                   .set("height", Value(r.height)));
    }

    NodeRef               node_;
    int                   handle_ = 0;
    WorldRect             from_;
    std::array<double, 2> start_{};
    bool                  active_ = false, changed_ = false;
};

}  // namespace

// ── snap ─────────────────────────────────────────────────────────────────

Snap::Snap(std::string_view args) {
    // "snap": to the other nodes; "snap:grid": and the grid's lines; "snap:20": and every 20 units.
    if (args == "grid") grid_ = -1;
    else if (!args.empty()) {
        double v = 0;
        const auto [p, ec] = std::from_chars(args.data(), args.data() + args.size(), v);
        if (ec == std::errc{} && p == args.data() + args.size() && v > 0) grid_ = v;
        else warn("canvas-kit: snap takes `grid`, a spacing above 0 or nothing, not `{}`", args);
    }
    snaps().push_back(this);
}

Snap::~Snap() { std::erase(snaps(), this); }

Snap* Snap::of(Host& h) {
    Behavior* b = h.behavior("snap");
    if (!b || std::ranges::find(snaps(), b) == snaps().end()) return nullptr;
    return static_cast<Snap*>(b);
}

Snap::Result Snap::snap(Host& h, std::span<const std::string> moving, const WorldRect& p, std::array<bool, 3> xs, std::array<bool, 3> ys) {
    Scene& s = h.scene();
    const double zoom = h.camera().zoom;
    const double reach = threshold_ / zoom;
    const std::array<double, 3> px{p.x, p.x + p.width / 2, p.right()}, py{p.y, p.y + p.height / 2, p.bottom()};
    double bx = reach, by = reach;  // the nearest found, within reach
    std::optional<double> cx, cy;
    // The other nodes in view.
    near_.clear();
    s.query(h.camera().visible(h.viewport()), near_);
    std::erase_if(near_, [&](NodeRef n) { return std::ranges::find(moving, s.key(n)) != moving.end(); });
    auto consider = [](double line, double at, double& best, std::optional<double>& c) {
        const double d = line - at;
        if (std::abs(d) < best) {
            best = std::abs(d);
            c = d;
        }
    };
    for (NodeRef n : near_) {
        const WorldRect r = s.rect(n);
        const std::array<double, 3> rx{r.x, r.x + r.width / 2, r.right()}, ry{r.y, r.y + r.height / 2, r.bottom()};
        for (std::size_t i = 0; i < 3; ++i) {
            if (xs[i])
                for (double line : rx) consider(line, px[i], bx, cx);
            if (ys[i])
                for (double line : ry) consider(line, py[i], by, cy);
        }
    }
    // The grid's lines (its edges only: a centre on a line is not what a grid is for).
    double step = grid_ > 0 ? grid_ : 0;
    if (grid_ < 0)
        if (auto base = grid_base(h)) step = grid_step(*base, zoom);
    if (step > 0)
        for (std::size_t i : {std::size_t{0}, std::size_t{2}}) {
            if (xs[i]) consider(std::round(px[i] / step) * step, px[i], bx, cx);
            if (ys[i]) consider(std::round(py[i] / step) * step, py[i], by, cy);
        }
    const Result out{cx.value_or(0), cy.value_or(0)};
    // The guides: every node line the snapped rectangle now lies on.
    guides_.clear();
    const WorldRect q{p.x + out.dx, p.y + out.dy, p.width, p.height};
    const std::array<double, 3> qx{q.x, q.x + q.width / 2, q.right()}, qy{q.y, q.y + q.height / 2, q.bottom()};
    const double eps = 1e-6 * std::max(1.0, std::abs(q.x) + std::abs(q.y));
    for (NodeRef n : near_) {
        const WorldRect r = s.rect(n);
        const std::array<double, 3> rx{r.x, r.x + r.width / 2, r.right()}, ry{r.y, r.y + r.height / 2, r.bottom()};
        for (std::size_t i = 0; i < 3; ++i) {
            if (cx && xs[i])
                for (double line : rx)
                    if (std::abs(line - qx[i]) <= eps) guides_.push_back({true, line, std::min(q.y, r.y), std::max(q.bottom(), r.bottom())});
            if (cy && ys[i])
                for (double line : ry)
                    if (std::abs(line - qy[i]) <= eps) guides_.push_back({false, line, std::min(q.x, r.x), std::max(q.right(), r.right())});
        }
    }
    if (!guides_.empty()) h.redraw();
    return out;
}

void Snap::done(Host& h) {
    if (guides_.empty()) return;
    guides_.clear();
    h.redraw();
}

void Snap::paint_over(Host& h, render::DisplayList& list) {
    const Color c = h.services().theme().color("danger", Color::hex(0xdc2626));
    const world::Camera cam = h.camera();
    const Size vp = h.viewport();
    for (const Guide& g : guides_) {
        Point a = g.vertical ? cam.to_screen(g.at, g.from, vp) : cam.to_screen(g.from, g.at, vp);
        Point b = g.vertical ? cam.to_screen(g.at, g.to, vp) : cam.to_screen(g.to, g.at, vp);
        // On a pixel's centre: one crisp pixel wide.
        if (g.vertical) a.x = b.x = std::floor(a.x) + 0.5f;
        else a.y = b.y = std::floor(a.y) + 0.5f;
        line(list, a, b, 1, c);
    }
}

std::unique_ptr<Behavior> make_drag(std::string_view args) { return std::make_unique<Drag>(args); }
std::unique_ptr<Behavior> make_resize(std::string_view args) { return std::make_unique<Resize>(args); }
std::unique_ptr<Behavior> make_snap(std::string_view args) { return std::make_unique<Snap>(args); }

}  // namespace omui::kit
