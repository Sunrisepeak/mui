module omui.ui.canvas;

import std;
import omui.core;
import omui.view;
import xrender.vector;
import xrender.raster;
import hanim.core;
import mlibs.trace;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::fatal;
using mlibs::log::warn;
}

namespace omui {

namespace {

using world::Camera;
using world::NodeRef;

double number_of(const Value& v, double fallback) { return v.is_number() ? v.as_double() : fallback; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

// ── the camera ───────────────────────────────────────────────────────────

world::Scene* Canvas::scene() const { return props.scene.empty() ? nullptr : world::find_scene(props.scene); }

Size Canvas::viewport() const {
    if (!attached()) return {};
    const Rect b = services().bounds(node());
    return {b.width, b.height};
}

Camera Canvas::camera() const { return camera_; }

// The scene watched from mount on: one named and not made yet is made (empty)
// here, so whoever fills it later fills the one this canvas watches.
void Canvas::observe() {
    if (!observed_.empty())
        if (world::Scene* old = world::find_scene(observed_); old && old == watched_) old->unobserve(observer_);
    observed_ = props.scene;
    observer_ = 0;
    watched_ = props.scene.empty() ? nullptr : &world::scene(props.scene);
    if (!watched_) return;
    observer_ = watched_->observe([this] {
        if (resync_) return;
        resync_ = true;
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
        // The live nodes: placed again when the next frame starts (the flag
        // is the step's to clear, never the painting's: a change made before
        // this frame's paint is still placed by the next frame's step).
        schedule();
    });
}

void Canvas::mounted() {
    self_ = this;
    host_.c = this;
    build_behaviors();
    observe();
    camera_ = {props.x, props.y, std::clamp(props.zoom, props.min_zoom, props.max_zoom)};
    textures_.configure({.budget = static_cast<std::size_t>(std::max(1.0, props.cache_mb) * 1024 * 1024),
                         .upload = static_cast<std::size_t>(std::max(0.25, props.upload_mb) * 1024 * 1024),
                         .glyphs = static_cast<std::size_t>(std::max(1.0, props.arrive_glyphs))});
}

void Canvas::prop_changed(CanvasProp p) {
    if (!attached()) return;
    switch (p) {
        case CanvasProp::x:
        case CanvasProp::y:
        case CanvasProp::zoom: {
            if (writing_) return;
            // A binding moved it — not our own write-back coming round again,
            // perhaps a frame later, while the camera has moved on (a spring).
            if ((p == CanvasProp::x && props.x == written_.x) || (p == CanvasProp::y && props.y == written_.y) ||
                (p == CanvasProp::zoom && props.zoom == written_.zoom))
                return;
            const Camera c{props.x, props.y, std::clamp(props.zoom, props.min_zoom, props.max_zoom)};
            if (c == camera_) return;
            motion_.reset();
            glide_.reset();
            refit_ = false;
            camera_ = c;
            remember();
            place();
            break;
        }
        case CanvasProp::cache_mb:
        case CanvasProp::upload_mb:
        case CanvasProp::arrive_glyphs:
            textures_.configure({.budget = static_cast<std::size_t>(std::max(1.0, props.cache_mb) * 1024 * 1024),
                                 .upload = static_cast<std::size_t>(std::max(0.25, props.upload_mb) * 1024 * 1024),
                         .glyphs = static_cast<std::size_t>(std::max(1.0, props.arrive_glyphs))});
            break;
        case CanvasProp::behaviors: build_behaviors(); break;
        case CanvasProp::scene:
            selected_.clear();
            started_ = false;
            textures_.clear(services().images());
            observe();
            place();
            break;
        case CanvasProp::promotion: place(); break;
        case CanvasProp::min_zoom:
        case CanvasProp::max_zoom: camera_.zoom = std::clamp(camera_.zoom, props.min_zoom, props.max_zoom); break;
        default: break;
    }
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void Canvas::set_camera(const Camera& c) {
    motion_.reset();
    glide_.reset();
    camera_ = {c.x, c.y, std::clamp(c.zoom, props.min_zoom, props.max_zoom)};
    changed();
}

void Canvas::animate_to(const Camera& c, hanim::SpringParams params) {
    refit_ = false;
    const Camera to{c.x, c.y, std::clamp(c.zoom, props.min_zoom, props.max_zoom)};
    // Retargeted mid-flight, it keeps its velocity.
    const auto v = motion_ ? motion_->velocity(services().now()) : std::array<double, 3>{};
    glide_.reset();
    motion_.emplace(camera_, to, services().now(), params, v);
    remember();
    pace();
    schedule();
}

void Canvas::remember() {
    const hanim::seconds now = services().now();
    // A course taken from now on: what lay ahead of now (the page's time
    // taken back, then the camera moved) is no longer its past.
    while (!past_.empty() && past_.back().from > now) past_.pop_back();
    if (!past_.empty()) {
        Course& last = past_.back();
        const bool same = motion_ ? last.motion && last.motion->start() == motion_->start()
                        : glide_  ? last.glide && last.glide->start() == glide_->start()
                                  : !last.motion && !last.glide && last.at == camera_;
        if (same) return;
        if (last.from == now) {  // two changes in one instant: the later one
            last = {now, camera_, motion_, glide_};
            return;
        }
    }
    past_.push_back({now, camera_, motion_, glide_});
    // 30 s back, as the runtime's channels keep theirs (the oldest kept answers for before it).
    const auto old = std::ranges::find_if(past_, [&](const Course& c) { return c.from >= now - hanim::seconds(30); });
    if (old - past_.begin() > 1) past_.erase(past_.begin(), old - 1);
}

const Canvas::Course* Canvas::course_at(hanim::seconds t) const {
    if (past_.empty()) return nullptr;
    const auto after = std::ranges::upper_bound(past_, t, {}, &Course::from);
    return after == past_.begin() ? &past_.front() : &*(after - 1);
}

void Canvas::retimed(hanim::seconds t) {
    const Course* c = course_at(t);
    if (!c) return;
    // Before its course began (the oldest kept answers for earlier), where it began.
    const bool begun = t >= c->from;
    camera_ = !begun ? c->at : c->motion ? c->motion->at(t) : c->glide ? c->glide->at(t) : c->at;
    motion_.reset();
    glide_.reset();
    if (begun && c->motion && !c->motion->settled(t)) motion_ = c->motion;
    else if (begun && c->glide && !c->glide->settled(t)) glide_ = c->glide;
    // At rest there, it has rested: a jump in time is not a movement, so the live nodes are placed now.
    const bool moving = motion_ || glide_;
    changed(false);
    if (!moving) {
        last_motion_ = t - hanim::seconds(2 * promote_after);
        place();
    }
    if (moving && !services().time_paused()) schedule();
}

// The camera after input or an animation step: the promoted nodes placed, the
// view redrawn, the binding and listeners told.
// While the camera moves on its own (a spring, a fling), frames as fast as
// the display goes (D-70); none asked for once it rests.
void Canvas::pace() {
    // Not while the page's time stands still: the camera stands with it.
    const bool moving = (motion_.has_value() || glide_.has_value()) && !services().time_paused();
    if (moving && !pace_) pace_ = services().demand_frames(FrameDemand::fastest(motion_ ? "camera spring" : "camera fling"));
    else if (!moving && pace_) pace_.release();
}

// Bounded: no farther out than the whole scene (with a margin), its centre
// where the view never shows past the scene's edges (centred on an axis the
// scene does not fill).
void Canvas::keep_in(world::Camera& camera) const {
    world::Scene* s = props.bounded ? scene() : nullptr;
    if (!s) return;
    const world::WorldRect bounds = s->bounds();
    const Size vp = viewport();
    if (bounds.empty() || vp.width <= 0 || vp.height <= 0) return;
    constexpr float margin = 24;
    camera.zoom = std::max(camera.zoom, world::Camera::fit(bounds, vp, margin, props.min_zoom, props.max_zoom).zoom);
    const auto keep = [&](double& centre, double from, double length, double view) {
        const double pad = margin / camera.zoom, half = view / 2 / camera.zoom;
        if (length + 2 * pad <= 2 * half) centre = from + length / 2;
        else centre = std::clamp(centre, from - pad + half, from + length + pad - half);
    };
    keep(camera.x, bounds.x, bounds.width, vp.width);
    keep(camera.y, bounds.y, bounds.height, vp.height);
}

void Canvas::changed(bool record) {
    keep_in(camera_);
    if (record) remember();
    last_motion_ = services().now();
    pace();
    place();
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    writing_ = true;
    written_ = camera_;
    services().write_back(node(), "x", Value(camera_.x));
    services().write_back(node(), "y", Value(camera_.y));
    services().write_back(node(), "zoom", Value(camera_.zoom));
    writing_ = false;
    emit_camera_changed(camera_.x, camera_.y, camera_.zoom);
}

// An animation steps at the start of each frame (a timer, before input and
// layout), so the promoted nodes move with the camera in the same frame.
void Canvas::schedule() {
    if (tick_) return;
    tick_ = services().after(hanim::seconds(0), [this] {
        tick_ = 0;
        step();
    });
    services().request_frame();
}

void Canvas::step() {
    if (!started_ && !props.start.empty()) laid_out();  // the scene was empty when it was laid out
    if (resync_) {  // the scene changed: its live nodes again
        resync_ = false;
        place();
    }
    // Textures that came back: drawn this frame.
    if (textures_.collect(services().images(), frame_, services().deterministic())) services().invalidate_paint(node());
    const bool moving = motion_ || glide_;
    if (!moving) {
        if (textures_.busy()) schedule();
        return;
    }
    // The page's time stands still: the camera with it, until it goes on (retimed).
    if (services().time_paused()) return;
    const hanim::seconds t = services().now();
    if (motion_) {
        camera_ = motion_->at(t);
        if (motion_->settled(t)) motion_.reset();
    } else if (glide_) {
        camera_ = glide_->at(t);
        if (glide_->settled(t)) glide_.reset();
    }
    changed();
    if (motion_ || glide_ || textures_.busy()) schedule();
}

Canvas::~Canvas() {
    if (attached())
        for (const auto& b : behaviors_) b->unmounted(host_);
    if (!observed_.empty())
        if (world::Scene* s = world::find_scene(observed_); s && s == watched_) s->unobserve(observer_);
    if (tick_ && attached()) services().cancel(tick_);
    if (promote_timer_ && attached()) services().cancel(promote_timer_);
    if (attached()) textures_.clear(services().images());
}

// ── behaviours ───────────────────────────────────────────────────────────

void Canvas::build_behaviors() {
    gesture_ = nullptr;
    if (attached())
        for (const auto& b : behaviors_) b->unmounted(host_);
    behaviors_.clear();
    for (const std::string& name : props.behaviors) {
        if (auto b = world::make_behavior(name)) behaviors_.push_back(std::move(b));
        else warn("canvas: no behaviour is named '{}' (world::behaviors() lists them; canvas-kit registers its own)", name);
    }
    // Offered from the highest priority; equal ones in the order named.
    std::ranges::stable_sort(behaviors_, std::greater{}, [](const auto& b) { return b->priority(); });
    if (attached()) services().invalidate_paint(node());
}

world::Behavior* Canvas::behavior(std::string_view name) const {
    for (const auto& b : behaviors_)
        if (b->name() == name) return b.get();
    return nullptr;
}

world::Scene& Canvas::HostAdapter::scene() {
    world::Scene* s = c->scene();
    if (!s) fatal("canvas: a behaviour asked for the scene of a canvas that shows none");
    return *s;
}

void Canvas::HostAdapter::committed(std::string_view what, const Value& args) {
    // A drag of nodes (a behaviour's, done as the pointer went): one step of
    // the history, its opposite the undo (S-12).
    if (what == "moved")
        if (const Value *dx = args.get("dx"), *dy = args.get("dy"), *keys = args.get("keys"); dx && dy && keys && (dx->as_double() != 0 || dy->as_double() != 0))
            c->services().performed(c->node(), "move_by", args,
                                    {{"move_by", Value{}.set("keys", *keys).set("dx", -dx->as_double()).set("dy", -dy->as_double())}});
    c->services().emit(c->node(), what, args);
}

void Canvas::select(std::vector<std::string> keys) {
    if (keys == selected_) return;
    selected_ = std::move(keys);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    emit_selection_changed(selected_);
}

world::PointerInput Canvas::pointer_input(const PointerEvent& p, Point at) const {
    world::PointerInput in;
    in.kind = p.kind;
    in.screen = at;
    in.world = camera_.to_world(at, viewport());
    in.button = p.button;
    in.mods = p.mods;
    in.clicks = p.clicks;
    if (world::Scene* s = scene()) in.hit = s->hit(in.world[0], in.world[1]);
    return in;
}

// ── tiers and promotion ──────────────────────────────────────────────────

std::size_t Canvas::tier_of(const world::Kind& k, Rect screen, bool focused) const {
    const float side = std::max(screen.width, screen.height);
    std::size_t t = 0;
    for (std::size_t i = 0; i < k.tiers.size(); ++i)
        if (side >= k.tiers[i].from) t = i;
    // The focused node is live as soon as the tier before it would show (§3.2 T3).
    if (focused && t + 1 < k.tiers.size() && k.tiers[t + 1].draw == world::Tier::Draw::promote && k.promote) t = t + 1;
    return t;
}

// The nodes the camera makes live, composed when the set changes and moved
// with the camera otherwise.
void Canvas::place() {
    world::Scene* s = scene();
    const Size vp = viewport();
    // A canvas given no room for a while (a panel over the whole of a phone) keeps what was live as it was:
    // a page being read keeps its place, an edit its state, when the room comes back.
    if (vp.width <= 0 || vp.height <= 0) return;
    std::vector<std::string> keys;
    std::vector<std::uint64_t> revisions;  // each one's content revision: a touched node is composed again
    // While the camera moves, no node goes live (a live node is laid out on
    // the UI thread: a zoom through a wall would stall on each one); those
    // already live stay, the focused one goes at once, the rest when it rests.
    // Frames drawn complete (an offline render) never wait for the camera to rest.
    // `promotion`: at_rest (as above), always (moving or not), never (textures only but the focused node).
    const bool never = props.promotion == "never";
    const bool moving = props.promotion != "always" && !services().complete_frames() && (services().now() - last_motion_).count() < promote_after;
    bool deferred = false;
    if (s && vp.width > 0) {
        std::vector<NodeRef> seen;
        s->query(camera_.visible(vp), seen);
        for (NodeRef n : seen) {
            const world::Kind& k = s->kind_of(n);
            if (!k.promote) continue;
            const std::string_view key = s->key(n);
            const std::size_t t = tier_of(k, camera_.to_screen(s->rect(n), vp), key == focused_);
            if (k.tiers[t].draw != world::Tier::Draw::promote) continue;
            if (never && key != focused_) continue;
            if (moving && key != focused_ && std::ranges::find(promoted_, key) == promoted_.end()) deferred = true;
            else {
                keys.emplace_back(key);
                revisions.push_back(s->content_revision(n));
            }
        }
    }
    if (deferred && !promote_timer_)
        promote_timer_ = services().after(hanim::seconds(promote_after), [this] {
            promote_timer_ = 0;
            place();
            services().invalidate_paint(node());
            services().invalidate_semantics(node());
        });
    if (keys != promoted_ || revisions != promoted_revisions_) {
        // A node going back to its texture is drawn again as it is now: what was done in it while live shows.
        std::vector<std::string> left;
        for (const std::string& k : promoted_)
            if (std::ranges::find(keys, k) == keys.end()) left.push_back(k);
        promoted_ = std::move(keys);
        promoted_revisions_ = std::move(revisions);
        if (s)
            for (const std::string& k : left)
                if (const auto n = s->find(k)) s->touch(*n);
        services().recompose(node());
        return;
    }
    // The same nodes: only their transforms follow the camera.
    if (promoted_.empty() || !s) return;
    const auto live = services().children(node());
    if (live.size() < promoted_.size()) {
        // Composed and not built yet (the camera moved in the same frame, as
        // a start view does): composed again where the camera is now.
        services().recompose(node());
        return;
    }
    for (const NodeId child : live) {
        const std::string path = services().path(child);
        const std::string_view key = std::string_view(path).substr(path.rfind('/') + 1);
        const auto n = s->find(key);
        if (!n) continue;
        const auto [tx, ty, scale] = placement(*s, *n, vp);
        services().set(child, "translate_x", Value(static_cast<double>(tx)));
        services().set(child, "translate_y", Value(static_cast<double>(ty)));
        services().set(child, "scale", Value(static_cast<double>(scale)));
    }
}

// Laid out at its world size; its top-left on the node's screen corner, under
// the camera's scale (a visual transform scales about the centre).
std::array<float, 3> Canvas::placement(const world::Scene& s, NodeRef n, Size vp) const {
    const world::WorldRect r = s.rect(n);
    const Point at = camera_.to_screen(r.x, r.y, vp);
    const auto z = static_cast<float>(camera_.zoom), w = static_cast<float>(r.width), h = static_cast<float>(r.height);
    return {at.x - w * (1 - z) / 2, at.y - h * (1 - z) / 2, z};
}

std::vector<Element> Canvas::compose() {
    std::vector<Element> out;
    world::Scene* s = scene();
    if (!s) return out;
    const Size vp = viewport();
    for (const std::string& key : promoted_) {
        const auto n = s->find(key);
        if (!n) continue;
        const world::Kind& k = s->kind_of(*n);
        if (!k.promote) continue;
        const world::WorldRect r = s->rect(*n);
        const auto [tx, ty, scale] = placement(*s, *n, vp);
        out.push_back(box({.id = key,
                           .width = static_cast<float>(r.width),
                           .height = static_cast<float>(r.height),
                           .translate_x = tx,
                           .translate_y = ty,
                           .scale = scale},
                          {k.promote(*s, *n)}));
    }
    return out;
}

// `start`: the camera placed once the canvas has its size (layout gives it)
// and its scene something to show.
void Canvas::laid_out() {
    const Size vp = viewport();
    // A new size moves every screen position: the live nodes follow at once.
    world::Scene* s = scene();
    if (vp.width != placed_for_.width || vp.height != placed_for_.height) {
        placed_for_ = vp;
        // Fitted at the start and not moved since: still the whole scene (the
        // window's first layouts settle its size over a frame or two).
        if (refit_ && s && vp.width > 0 && vp.height > 0) set_camera(Camera::fit(start_rect(*s), vp, 24, props.min_zoom, props.max_zoom));
        else place();
    }
    if (started_ || props.start.empty()) return;
    if (!s || s->size() == 0 || vp.width <= 0 || vp.height <= 0) return;
    if (props.start == "fit" || props.start.starts_with("fit:")) {
        started_ = true;
        set_camera(Camera::fit(start_rect(*s), vp, 24, props.min_zoom, props.max_zoom));
        refit_ = true;
    } else if (props.start.starts_with("preset:")) {
        started_ = true;
        if (const auto p = s->preset(std::string_view(props.start).substr(7))) set_camera({p->x, p->y, p->zoom});
        else warn("canvas: the scene has no view named `{}` to start at", std::string_view(props.start).substr(7));
    } else {
        started_ = true;
        warn("canvas: start is `fit`, `fit:<key>`, `preset:<name>` or empty, not `{}`", props.start);
    }
}

// What `start` fits: the node it names (`fit:<key>`), else the whole scene.
world::WorldRect Canvas::start_rect(const world::Scene& s) const {
    if (props.start.starts_with("fit:"))
        if (const auto n = s.find(std::string_view(props.start).substr(4))) return s.rect(*n);
    return s.bounds();
}

// The textures were drawn in the old theme: drawn again (the stale ones
// meanwhile are gone, so the nodes are boxes for a moment, then sharp).
void Canvas::restyle() {
    if (!attached()) return;
    textures_.clear(services().images());
    services().invalidate_paint(node());
}

void Canvas::style(const Theme&, mlibs::layout::Style& s) const {
    // The promoted nodes overlap at the origin; their transforms place them.
    s.display = mlibs::layout::Display::stack;
    // Its size is the layout's to give, never its live nodes' (laid out at
    // their world size, they may be far larger than the view).
    s.min_width = s.min_height = mlibs::layout::pt(0);
    s.overflow_x = s.overflow_y = mlibs::layout::Overflow::clip;
}

// ── drawing ──────────────────────────────────────────────────────────────

void Canvas::paint(PaintContext& ctx) const {
    mlibs::trace::Span span("canvas.paint", "canvas");
    const auto from = std::chrono::steady_clock::now();
    const Size vp = ctx.size;
    ctx.list.fill_rect({0, 0, vp.width, vp.height}, ctx.theme.color(props.background, ctx.theme.color("background")));
    stats_ = {};
    visible_.clear();
    world::Scene* s = scene();
    if (!s || vp.width <= 0 || vp.height <= 0) return;
    s->query(camera_.visible(vp), visible_);
    const auto queried = std::chrono::steady_clock::now();
    for (const auto& b : behaviors_) b->paint_under(host_, ctx.list);
    paint_edges(*s, ctx.list, vp);
    stats_.visible = visible_.size();
    const int level = world::Textures::level_for(camera_.zoom * services().pixel_scale());
    auto tier = [&](NodeRef n, const world::Kind& k, Rect screen) {
        std::size_t t = tier_of(k, screen, s->key(n) == focused_);
        // Not live yet (the camera is moving): the tier below it meanwhile.
        if (k.tiers[t].draw == world::Tier::Draw::promote && t > 0 && std::ranges::find(promoted_, s->key(n)) == promoted_.end()) --t;
        return t;
    };
    const bool lists = props.content == "lists";
    // Drawn complete (Services::complete_frames): every texture the frame
    // shows made at its level first, so the frame is what it would be at rest.
    if (services().complete_frames()) {
        for (const NodeRef n : visible_) {
            const world::Kind& k = s->kind_of(n);
            const Rect screen = camera_.to_screen(s->rect(n), vp);
            if (k.tiers[tier(n, k, screen)].draw != world::Tier::Draw::texture) continue;
            const std::uint64_t rev = s->content_revision(n);
            if (lists) {
                const auto got = textures_.listed(s->key(n), rev);
                if (!got.unfit) {
                    if (!got.exact && !k.content) textures_.want_list(s->key(n), rev, n, 1);
                    continue;
                }
            }
            if (const auto pick = textures_.pick(s->key(n), rev, level, frame_); !pick || !pick->exact) textures_.want(s->key(n), rev, level, n, 1);
        }
        textures_.complete(*s, ctx.services.images(), &ctx.services.fonts(), ctx.theme, frame_);
    }
    for (const NodeRef n : visible_) {
        const world::Kind& k = s->kind_of(n);
        const Rect screen = camera_.to_screen(s->rect(n), vp);
        const std::size_t t = tier(n, k, screen);
        switch (k.tiers[t].draw) {
            case world::Tier::Draw::box: {
                const Color fill = s->fill(n);
                if (fill.a > 0) {
                    const float r = static_cast<float>(s->radius(n) * camera_.zoom);
                    if (r > 0) ctx.list.fill_round_rect(screen, CornerRadii::all(r), xrender::Paint::solid(fill));
                    else ctx.list.fill_rect(screen, fill);
                }
                ++stats_.boxes;
                break;
            }
            case world::Tier::Draw::custom:
                if (k.draw) {
                    world::DrawContext dc{*s, n, ctx.list, screen, camera_.zoom, t, ctx.theme, &ctx.services.fonts()};
                    k.draw(dc);
                }
                ++stats_.custom;
                break;
            case world::Tier::Draw::texture: {
                // The content's texture at the screen's resolution, the nearest
                // ready one meanwhile, a box before any is.
                const std::string_view key = s->key(n);
                const std::uint64_t rev = s->content_revision(n);
                if (lists) {
                    // The content's own list under the camera: made once a revision, the same at every zoom.
                    auto got = textures_.listed(key, rev);
                    if (!got.exact && !got.unfit && k.content) got = textures_.list_now(*s, n, key, rev);
                    if (!got.unfit) {
                        if (!got.exact) {
                            const float cx = screen.x + screen.width / 2 - vp.width / 2, cy = screen.y + screen.height / 2 - vp.height / 2;
                            textures_.want_list(key, rev, n, screen.width * screen.height / (1.0f + (cx * cx + cy * cy) / (vp.width * vp.width + vp.height * vp.height)));
                        }
                        const world::WorldRect world = s->rect(n);
                        if (got.list && world.width > 0 && world.height > 0) {
                            ctx.list.push_clip(screen);
                            ctx.list.push_transform(Affine::translate(screen.x, screen.y) *
                                                    Affine::scale(screen.width / static_cast<float>(world.width), screen.height / static_cast<float>(world.height)));
                            ctx.list.append(*got.list, got.serial, 0);
                            ctx.list.pop_transform();
                            ctx.list.pop_clip();
                            ++stats_.listed;
                        } else {
                            const Color fill = s->fill(n);
                            if (fill.a > 0) ctx.list.fill_round_rect(screen, CornerRadii::all(static_cast<float>(s->radius(n) * camera_.zoom)), xrender::Paint::solid(fill));
                            ++stats_.boxes;
                        }
                        break;
                    }
                }
                const auto pick = textures_.pick(key, rev, level, frame_);
                if (!pick || !pick->exact) {
                    const float cx = screen.x + screen.width / 2 - vp.width / 2, cy = screen.y + screen.height / 2 - vp.height / 2;
                    const float centrality = 1.0f / (1.0f + (cx * cx + cy * cy) / (vp.width * vp.width + vp.height * vp.height));
                    textures_.want(key, rev, level, n, screen.width * screen.height * centrality);
                }
                const xrender::raster::Surface* img = pick ? services().images().get(pick->image) : nullptr;
                if (img) {
                    ctx.list.draw_image(pick->image, {0, 0, static_cast<float>(img->width()), static_cast<float>(img->height())}, screen,
                                        xrender::Sampling::linear);
                    ++stats_.textured;
                    if (!pick->exact) ++stats_.stretched;
                } else {
                    const Color fill = s->fill(n);
                    if (fill.a > 0) ctx.list.fill_round_rect(screen, CornerRadii::all(static_cast<float>(s->radius(n) * camera_.zoom)), xrender::Paint::solid(fill));
                    ++stats_.boxes;
                }
                break;
            }
            case world::Tier::Draw::promote: ++stats_.promoted; break;  // a live child draws it
        }
    }
    rehearse(*s, ctx, vp, from);
    // What the frame wants and has not: rasterised on the pool, collected at
    // the start of a frame (the step), drawn when it is in.
    textures_.issue(*s, ctx.services.images(), &ctx.services.fonts(), ctx.theme);
    ++frame_;
    if (textures_.busy() && self_ && !tick_) self_->schedule();
    const auto done = std::chrono::steady_clock::now();
    stats_.query_ms = std::chrono::duration<double, std::milli>(queried - from).count();
    stats_.paint_ms = std::chrono::duration<double, std::milli>(done - queried).count();
    mlibs::trace::count("canvas.visible", static_cast<double>(stats_.visible), "canvas");
}

void Canvas::rehearse(world::Scene& scene, PaintContext& ctx, Size viewport, std::chrono::steady_clock::time_point painting_since) const {
    using clock = std::chrono::steady_clock;
    // How far on, in the camera's last steps: the nearest first — what is needed soonest is made first.
    constexpr std::array<int, rehearsal_horizons> frames_ahead{2, 4, 6, 8, 12};
    // A few nodes of each a frame, in turn, to find what is not made yet; where one's drawing made
    // something (it took this long), the rest at that distance are made within the paint's time.
    constexpr std::size_t probes = 48;
    constexpr auto        made_something = std::chrono::microseconds{4};
    constexpr auto        paint_within = std::chrono::microseconds{2000}, at_least = std::chrono::microseconds{300};
    const world::Camera before = std::exchange(camera_before_, camera_);
    if (!(before.zoom > 0) || before == camera_) return;
    const auto began = clock::now();
    const auto until = std::max(painting_since + paint_within, began + at_least);
    for (std::size_t which = 0; which < frames_ahead.size(); ++which) {
        // Where the camera is after as many steps again as the last one.
        const int     steps = frames_ahead[which];
        world::Camera then = camera_;
        then.x += (camera_.x - before.x) * steps;
        then.y += (camera_.y - before.y) * steps;
        then.zoom = std::clamp(camera_.zoom * std::pow(camera_.zoom / before.zoom, steps), props.min_zoom, props.max_zoom);
        ahead_.clear();
        scene.query(then.visible(viewport), ahead_);
        if (ahead_.empty()) continue;
        std::size_t& at = rehearse_at_[which];
        std::size_t  done = 0;
        bool         cold = false;
        for (; done < ahead_.size() && (cold || done < probes); ++done) {
            const NodeRef      node = ahead_[(at + done) % ahead_.size()];
            const world::Kind& kind = scene.kind_of(node);
            const Rect         screen = then.to_screen(scene.rect(node), viewport);
            const std::size_t  tier = tier_of(kind, screen, false);
            if (kind.tiers[tier].draw != world::Tier::Draw::custom || !kind.draw) continue;
            rehearsal_.clear();
            world::DrawContext drawing{scene, node, rehearsal_, screen, then.zoom, tier, ctx.theme, &ctx.services.fonts()};
            const auto         from = clock::now();
            kind.draw(drawing);
            const auto now = clock::now();
            ++stats_.rehearsed;
            if (now - from >= made_something) cold = true;
            if (now >= until) {
                ++done;
                break;
            }
        }
        at = (at + done) % ahead_.size();
        if (cold || clock::now() >= until) break;  // the farther ones another frame
    }
}

void Canvas::paint_over(PaintContext& ctx) const {
    world::Scene* s = scene();
    if (!s) return;
    for (const auto& b : behaviors_) b->paint_over(host_, ctx.list);
    // The item the keyboard is on: its ring.
    if (focused_.empty() || !services().state(node()).focus_visible) return;
    if (const auto n = s->find(focused_)) {
        const Rect r = camera_.to_screen(s->rect(*n), ctx.size);
        constexpr float gap = 3, width = 2;
        const float radius = static_cast<float>(s->radius(*n) * camera_.zoom) + gap;
        ctx.list.stroke_round_rect({r.x - gap, r.y - gap, r.width + 2 * gap, r.height + 2 * gap}, CornerRadii::all(radius), width,
                                   xrender::Paint::solid(ctx.theme.color("focus", Color::hex(0x6366f1, 0.55f))));
    }
}

// Edges between their nodes' facing sides: a curve, or a straight line when
// far out; none when both ends are dots.
void Canvas::paint_edges(const world::Scene& s, xrender::DisplayList& list, Size vp) const {
    const auto edges = s.edges();
    if (edges.empty()) return;
    const world::WorldRect view = camera_.visible(vp);
    const auto z = static_cast<float>(camera_.zoom);
    for (const world::Edge& e : edges) {
        const auto a = s.find(e.from), b = s.find(e.to);
        if (!a || !b) continue;
        const world::WorldRect ra = s.rect(*a), rb = s.rect(*b);
        if (!ra.united(rb).overlaps(view)) continue;
        const Rect sa = camera_.to_screen(ra, vp), sb = camera_.to_screen(rb, vp);
        if (std::max(sa.width, sa.height) < 2 && std::max(sb.width, sb.height) < 2) continue;
        const Point ca{sa.x + sa.width / 2, sa.y + sa.height / 2}, cb{sb.x + sb.width / 2, sb.y + sb.height / 2};
        // Out of the sides that face each other.
        const bool horizontal = std::abs(cb.x - ca.x) * sa.height >= std::abs(cb.y - ca.y) * sa.width;
        Point p0, p1, n0, n1;
        if (horizontal) {
            const float dir = cb.x >= ca.x ? 1.0f : -1.0f;
            p0 = {dir > 0 ? sa.right() : sa.x, ca.y};
            p1 = {dir > 0 ? sb.x : sb.right(), cb.y};
            n0 = {dir, 0}, n1 = {-dir, 0};
        } else {
            const float dir = cb.y >= ca.y ? 1.0f : -1.0f;
            p0 = {ca.x, dir > 0 ? sa.bottom() : sa.y};
            p1 = {cb.x, dir > 0 ? sb.y : sb.bottom()};
            n0 = {0, dir}, n1 = {0, -dir};
        }
        xrender::Path path;
        path.move_to(p0);
        const float bend = std::hypot(p1.x - p0.x, p1.y - p0.y) * 0.4f;
        if (camera_.zoom < 0.15 || bend < 6) path.line_to(p1);
        else path.cubic_to({p0.x + n0.x * bend, p0.y + n0.y * bend}, {p1.x + n1.x * bend, p1.y + n1.y * bend}, p1);
        list.stroke_path(path, {.width = std::max(1.0f, e.width * z), .cap = xrender::LineCap::round}, xrender::Paint::solid(e.color));
    }
}

// ── input: drag pans, the wheel and a pinch zoom, a fling glides ─────────

bool Canvas::on_event(EventContext& ctx, const InputEvent& e) {
    const Size vp = viewport();
    refit_ = false;  // the person moves the camera from here on
    const Point origin = [&] {
        const Rect b = services().bounds(node());
        return Point{b.x, b.y};
    }();
    if (const auto* w = std::get_if<WheelEvent>(&e)) {
        const Point at{w->position.x - origin.x, w->position.y - origin.y};
        // Over a live node (a page being read) the wheel is the page's: what
        // it does not scroll stops there, the camera stays (⌃ or ⌘ held still
        // zooms) — a page's end is not a zoom.
        if (!w->mods.has(Modifier::ctrl) && !w->mods.has(Modifier::meta))
            if (world::Scene* s = scene()) {
                const auto wp = camera_.to_world(at, vp);
                if (const auto h = s->hit(wp[0], wp[1]); h && std::ranges::find(promoted_, s->key(h->node)) != promoted_.end()) return true;
            }
        // A mouse wheel zooms about the cursor; a trackpad pans, and zooms
        // with ⌃ or ⌘ held (how a pinch reaches a window).
        world::Camera next = camera_;
        if (!w->precise || w->mods.has(Modifier::ctrl) || w->mods.has(Modifier::meta)) next.zoom_at(at, std::exp(-static_cast<double>(w->delta.y) * (w->precise ? 0.01 : 0.0025)), vp, props.min_zoom, props.max_zoom);
        else next.pan({-w->delta.x, -w->delta.y});
        keep_in(next);
        // Bounded and already as far as it goes: the page's to scroll.
        if (props.bounded && std::abs(next.x - camera_.x) < 1e-6 && std::abs(next.y - camera_.y) < 1e-6 && std::abs(next.zoom - camera_.zoom) < 1e-9) return false;
        motion_.reset();
        glide_.reset();
        camera_ = next;
        changed();
        return true;
    }
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down && scene())
        for (const auto& b : behaviors_)
            if (b->key(host_, *k)) return true;
    // Tab and Shift+Tab: from item to item in reading order, the camera going
    // to each (its focus ring drawn); past the last, or before the first, the
    // page's own order goes on. Enter activates the one focused.
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down && scene() && (k->key == Key::tab || k->key == Key::enter)) {
        world::Scene& s = *scene();
        if (k->key == Key::enter) {
            if (focused_.empty() || !s.find(focused_)) return false;
            emit_activated(focused_);
            return true;
        }
        std::vector<NodeRef> items;
        s.each([&](NodeRef n) {
            // Its items, not what holds them (a group, a region: a landmark, not a stop).
            const Role role = s.kind_of(n).role;
            if (role != Role::generic && role != Role::group && role != Role::region) items.push_back(n);
        });
        // Reading order, region by region: the regions (role region) in reading order, each one's
        // items in reading order within it (what lies in none, after them all).
        std::vector<world::WorldRect> regions;
        s.each([&](NodeRef n) {
            if (s.kind_of(n).role == Role::region) regions.push_back(s.rect(n));
        });
        std::ranges::sort(regions, [](const world::WorldRect& a, const world::WorldRect& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
        const auto region_of = [&](NodeRef n) {
            const auto r = s.rect(n);
            for (std::size_t index = 0; index < regions.size(); ++index)
                if (regions[index].contains(r.x + r.width / 2, r.y + r.height / 2)) return index;
            return regions.size();
        };
        std::ranges::sort(items, [&](NodeRef a, NodeRef b) {
            const auto ga = region_of(a), gb = region_of(b);
            if (ga != gb) return ga < gb;
            const auto ra = s.rect(a), rb = s.rect(b);
            return ra.y != rb.y ? ra.y < rb.y : ra.x < rb.x;
        });
        const bool back = k->mods.has(Modifier::shift);
        const auto at = std::ranges::find_if(items, [&](NodeRef n) { return s.key(n) == focused_; });
        std::ptrdiff_t next = at == items.end() ? (back ? static_cast<std::ptrdiff_t>(items.size()) - 1 : 0) : (at - items.begin()) + (back ? -1 : 1);
        if (next < 0 || next >= static_cast<std::ptrdiff_t>(items.size())) {
            focus_only({});
            return false;
        }
        const NodeRef n = items[static_cast<std::size_t>(next)];
        focused_ = std::string(s.key(n));
        animate_to(Camera::fit(s.rect(n), vp, 24, props.min_zoom, props.max_zoom));
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
        return true;
    }
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down) {
        const float step = 80;
        switch (k->key) {
            case Key::left: camera_.pan({step, 0}); break;
            case Key::right: camera_.pan({-step, 0}); break;
            case Key::up: camera_.pan({0, step}); break;
            case Key::down: camera_.pan({0, -step}); break;
            case Key::equal: camera_.zoom_at({vp.width / 2, vp.height / 2}, 1.25, vp, props.min_zoom, props.max_zoom); break;
            case Key::minus: camera_.zoom_at({vp.width / 2, vp.height / 2}, 0.8, vp, props.min_zoom, props.max_zoom); break;
            case Key::digit0:
                if (world::Scene* s = scene()) animate_to(Camera::fit(s->bounds(), vp, 24, props.min_zoom, props.max_zoom));
                return true;
            default: return false;
        }
        motion_.reset();
        glide_.reset();
        changed();
        return true;
    }
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    const Point at{p->position.x - origin.x, p->position.y - origin.y};
    // The gesture arena: a press goes to the behaviours, the highest first,
    // until one takes it; that one has the pointer until it is released.
    if (gesture_) {
        if (p->kind == PointerKind::move || p->kind == PointerKind::up || p->kind == PointerKind::cancel) {
            world::Behavior* b = gesture_;
            (void)b->pointer(host_, pointer_input(*p, at));
            if (p->kind != PointerKind::move) {
                gesture_ = nullptr;
                services().release_pointer();
            }
            return true;
        }
    } else if (p->kind == PointerKind::down && pointers_.empty() && scene()) {
        const world::PointerInput in = pointer_input(*p, at);
        for (const auto& b : behaviors_)
            if (b->pointer(host_, in) == world::Claim::take) {
                gesture_ = b.get();
                motion_.reset();
                glide_.reset();
                services().capture_pointer(node());
                services().focus(node());
                return true;
            }
    }
    auto pointer = std::ranges::find(pointers_, p->pointer, &Pointer::id);
    switch (p->kind) {
        case PointerKind::down: {
            if (p->type == PointerType::mouse && p->button != MouseButton::left && p->button != MouseButton::middle) return false;
            if (p->clicks >= 2) {
                // A double press on a node: activated; on the background: fit.
                if (world::Scene* s = scene()) {
                    const auto w = camera_.to_world(at, vp);
                    if (auto hit = s->hit(w[0], w[1])) emit_activated(std::string(s->key(hit->node)));
                    else animate_to(Camera::fit(s->bounds(), vp, 24, props.min_zoom, props.max_zoom));
                }
                return true;
            }
            // Over a live node (a page being read, worked in) a drag is the page's, as its wheel is: what it
            // does not take (a scroll at its end, text) does not move the camera under it. Two fingers still do.
            if (pointers_.empty())
                if (world::Scene* s = scene()) {
                    const auto w = camera_.to_world(at, vp);
                    if (const auto h = s->hit(w[0], w[1]); h && std::ranges::find(promoted_, s->key(h->node)) != promoted_.end() && p->type != PointerType::touch)
                        return false;
                }
            motion_.reset();
            glide_.reset();
            if (pointer == pointers_.end()) pointers_.push_back({p->pointer, at});
            else pointer->at = at;
            dragging_ = true;
            pressed_at_ = at;
            travelled_ = false;
            velocity_ = {};
            last_move_ = services().now();
            pinch_distance_ = 0;
            // Not captured yet: a press that stays put is a click, and the
            // node under it (a live window's link, button, field) takes it.
            // The pointer is the canvas's once it travels (a pan).
            return true;
        }
        case PointerKind::move: {
            if (dragging_ && p->type == PointerType::mouse && p->buttons == 0) {
                // A release a node inside took (a link's click): the press is over.
                dragging_ = false;
                pointers_.clear();
                return false;
            }
            if (!dragging_ || pointer == pointers_.end()) return false;
            const Point before = pointer->at;
            pointer->at = at;
            if (pointers_.size() >= 2) {
                // Two pointers: the distance between them zooms about their midpoint.
                const Point a = pointers_[0].at, b = pointers_[1].at;
                const float d = std::hypot(a.x - b.x, a.y - b.y);
                const Point mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
                if (pinch_distance_ > 0 && d > 0) camera_.zoom_at(mid, d / pinch_distance_, vp, props.min_zoom, props.max_zoom);
                pinch_distance_ = d;
                if (pointer == pointers_.begin() || pointer == pointers_.begin() + 1) camera_.pan({(at.x - before.x) / 2, (at.y - before.y) / 2});
            } else {
                const Point delta{at.x - before.x, at.y - before.y};
                if (!travelled_ && std::hypot(at.x - pressed_at_.x, at.y - pressed_at_.y) > 3) {
                    travelled_ = true;
                    services().capture_pointer(node());  // a pan: the pointer is the canvas's until released
                    services().focus(node());
                }
                if (!travelled_) return true;  // a tap's jitter does not pan
                camera_.pan(delta);
                // The pointer's velocity, smoothed, for the fling.
                const hanim::seconds now = services().now();
                const double dt = std::max(1e-3, (now - last_move_).count());
                last_move_ = now;
                velocity_ = {static_cast<float>(velocity_.x * 0.5 + delta.x / dt * 0.5), static_cast<float>(velocity_.y * 0.5 + delta.y / dt * 0.5)};
            }
            changed();
            return true;
        }
        case PointerKind::up:
        case PointerKind::cancel: {
            if (pointer == pointers_.end()) return false;
            pointers_.erase(pointer);
            if (!pointers_.empty()) {
                pinch_distance_ = 0;
                return true;
            }
            dragging_ = false;
            services().release_pointer();
            // A click no behaviour took: offered to them as a tap.
            if (!travelled_ && p->kind == PointerKind::up && scene()) {
                const world::PointerInput in = pointer_input(*p, at);
                for (const auto& b : behaviors_)
                    if (b->tap(host_, in)) return true;
                return true;
            }
            // A fling: the last movement was recent and fast.
            const bool recent = (services().now() - last_move_).count() < 0.1;
            if (props.inertia && recent && p->kind == PointerKind::up && std::hypot(velocity_.x, velocity_.y) > 50) {
                glide_.emplace(camera_, velocity_, services().now());
                remember();
                pace();
                schedule();
            }
            return true;
        }
        default: return false;
    }
}

// ── semantics (OMUI_EXT_canvas) ──────────────────────────────────────────

void Canvas::semantics(SemanticsContext&, spec::SemanticNode& n) const {
    if (!props.label.empty()) n.name = props.label;
    if (world::Scene* s = scene()) n.description = std::format("{} nodes, {} on screen; zoom {:.3g}", s->size(), visible_.size(), camera_.zoom);
    // The camera flying or gliding, or nodes waiting for it to rest to go
    // live: not settled yet (an agent's wait for a still waits for this too).
    if (motion_ || glide_ || promote_timer_) n.states.set(State::animating);
}

spec::SemanticNode Canvas::item(const world::Scene& s, NodeRef n, std::string_view path, Size vp, Rect b) const {
    const world::Kind& k = s.kind_of(n);
    spec::SemanticNode it;
    const std::string_view key = s.key(n);
    it.id = std::format("{}/{}", path, key);
    it.role = k.role;
    it.name = std::string(s.text(n, k.name_column));
    const Rect r = camera_.to_screen(s.rect(n), vp);
    it.bounds = spec::Rect{b.x + r.x, b.y + r.y, r.width, r.height};
    it.states.set(State::offscreen, !camera_.visible(vp).overlaps(s.rect(n)));
    it.states.set(State::focused, key == focused_);
    it.states.set(State::selected, std::ranges::find(selected_, key) != selected_.end());
    it.actions.push_back({"focus", Effect::read, {}, {}, {}});
    // What the behaviours make of it (its `move` because drag is on).
    for (const auto& beh : behaviors_) beh->describe(host_, n, it);
    return it;
}

std::vector<spec::SemanticNode> Canvas::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    world::Scene* s = scene();
    if (!s || !attached()) return out;
    const Size vp = viewport();
    const Rect b = services().bounds(node());
    std::vector<NodeRef> stated;
    s->query(camera_.visible(vp), stated);
    // The focused and the selected are stated wherever they are.
    for (const std::string& k : selected_)
        if (auto n = s->find(k); n && std::ranges::find(stated, *n) == stated.end()) stated.push_back(*n);
    if (!focused_.empty())
        if (auto f = s->find(focused_); f && std::ranges::find(stated, *f) == stated.end()) stated.push_back(*f);
    out.reserve(stated.size());
    for (const NodeRef n : stated) out.push_back(item(*s, n, path, vp, b));
    return out;
}

std::optional<spec::SemanticNode> Canvas::virtual_item(std::string_view path, std::string_view key) const {
    world::Scene* s = scene();
    if (!s || !attached()) return std::nullopt;
    const auto n = s->find(key);
    if (!n) return std::nullopt;
    return item(*s, *n, path, viewport(), services().bounds(node()));
}

ActResult Canvas::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    world::Scene* s = scene();
    const auto n = s ? s->find(key) : std::nullopt;
    if (!n) return ActResult::fail(std::format("no node has the key `{}`", key));
    if (action == "focus") return focus_node(ctx, Value(std::string(key)));
    for (const auto& b : behaviors_)
        if (auto r = b->act(host_, *n, action, ctx.args)) return *r;
    return ActResult::fail(std::format("`{}` has no action `{}`", key, action));
}

bool Canvas::reveal(std::string_view key) {
    world::Scene* s = scene();
    if (!s) return false;
    const auto n = s->find(key);
    if (!n) return false;
    focused_ = std::string(key);
    set_camera(Camera::fit(s->rect(*n), viewport(), 24, props.min_zoom, props.max_zoom));
    return true;
}

// ── the viewport's actions ───────────────────────────────────────────────

ActResult Canvas::move_by(ActContext&, const MoveByArgs& args) {
    world::Scene* s = scene();
    if (!s) return ActResult::fail("the canvas shows no scene");
    if (!args.keys.list() || !args.dx.is_number() || !args.dy.is_number()) return ActResult::fail("move_by takes keys (a list) and dx, dy (numbers)");
    const double dx = args.dx.as_double(), dy = args.dy.as_double();
    std::vector<std::string> moved;
    std::vector<world::NodeRef> along;
    auto move = [&](world::NodeRef n) {
        if (std::ranges::find(moved, s->key(n)) != moved.end()) return;  // once, though named and carried both
        const auto r = s->rect(n);
        s->move(n, r.x + dx, r.y + dy);
        moved.emplace_back(s->key(n));
    };
    for (const Value& k : *args.keys.list())
        if (auto n = s->find(k.as_string())) {
            // A kind that carries (a workspace) takes what lies on it along, as a drag of it does.
            if (s->kind_of(*n).carries) s->carried(*n, along);
            else along.clear();
            move(*n);
            for (world::NodeRef m : along) move(m);
        }
    if (moved.empty()) return ActResult::fail("no node with those keys");
    services().invalidate_paint(node());
    emit_moved(dx, dy, moved);
    Value::List all;
    for (const std::string& k : moved) all.emplace_back(k);
    return ActResult::done().undo_by("move_by", Value{}.set("keys", Value(std::move(all))).set("dx", -dx).set("dy", -dy));
}

ActResult Canvas::pan_to(ActContext&, const PanToArgs& args) {
    animate_to({number_of(args.x, camera_.x), number_of(args.y, camera_.y), camera_.zoom});
    return ActResult::done();
}

ActResult Canvas::zoom_to(ActContext&, const Value& zoom) {
    if (!zoom.is_number() || !(zoom.as_double() > 0)) return ActResult::fail("zoom_to takes a zoom above 0");
    animate_to({camera_.x, camera_.y, zoom.as_double()});
    return ActResult::done();
}

ActResult Canvas::fit(ActContext&, const Value& target) {
    world::Scene* s = scene();
    if (!s) return ActResult::fail("the canvas shows no scene");
    world::WorldRect r;
    if (const auto* keys = target.list()) {
        // Several: all of them in view (the keys not in the scene left out).
        std::optional<world::WorldRect> around;
        for (const Value& key : *keys)
            if (auto n = s->find(key.as_string())) around = around ? around->united(s->rect(*n)) : s->rect(*n);
        if (!around) return ActResult::fail("none of these keys is in the scene");
        r = *around;
    } else {
        const std::string t = target.is_null() ? std::string("all") : std::string(target.as_string());
        if (t == "all") r = s->bounds();
        else if (auto n = s->find(t)) r = s->rect(*n);
        else return ActResult::fail(std::format("no node has the key `{}`", t));
    }
    animate_to(Camera::fit(r, viewport(), 24, props.min_zoom, props.max_zoom));
    return ActResult::done();
}

ActResult Canvas::focus_node(ActContext&, const Value& key) {
    world::Scene* s = scene();
    const auto n = s ? s->find(key.as_string()) : std::nullopt;
    if (!n) return ActResult::fail(std::format("no node has the key `{}`", key.as_string()));
    focused_ = std::string(key.as_string());
    animate_to(Camera::fit(s->rect(*n), viewport(), 24, props.min_zoom, props.max_zoom));
    services().invalidate_semantics(node());
    return ActResult::done();
}

void Canvas::focus_only(std::string key) {
    focused_ = std::move(key);
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

ActResult Canvas::camera_preset(ActContext&, const Value& name) {
    world::Scene* s = scene();
    if (!s) return ActResult::fail("the canvas shows no scene");
    const auto p = s->preset(name.as_string());
    if (!p) {
        std::string known;
        for (const auto& k : s->presets()) known += (known.empty() ? "" : ", ") + k;
        return ActResult::fail(std::format("no view named `{}` (the scene has: {})", name.as_string(), known.empty() ? "none" : known));
    }
    animate_to({p->x, p->y, p->zoom});
    return ActResult::done();
}

ActResult Canvas::find(ActContext&, const Value& query) {
    world::Scene* s = scene();
    if (!s) return ActResult::fail("the canvas shows no scene");
    std::vector<NodeRef> candidates;
    if (const Value* r = query.get("in_rect"); r && r->list() && r->list()->size() == 4) {
        const auto& l = *r->list();
        s->query({number_of(l[0], 0), number_of(l[1], 0), number_of(l[2], 0), number_of(l[3], 0)}, candidates);
    } else if (const Value* near = query.get("near"); near && near->list() && near->list()->size() == 2) {
        const double radius = number_of(query.get("radius") ? *query.get("radius") : Value{}, 1000);
        const double cx = number_of((*near->list())[0], 0), cy = number_of((*near->list())[1], 0);
        s->query({cx - radius, cy - radius, 2 * radius, 2 * radius}, candidates);
    } else {
        s->each([&](NodeRef n) { candidates.push_back(n); });
    }
    const std::string name = query.get("name") ? lower(query.get("name")->as_string()) : std::string{};
    const std::string kind = query.get("kind") ? std::string(query.get("kind")->as_string()) : std::string{};
    const std::string role = query.get("role") ? std::string(query.get("role")->as_string()) : std::string{};
    std::erase_if(candidates, [&](NodeRef n) {
        const world::Kind& k = s->kind_of(n);
        if (!kind.empty() && s->kind_name(n) != kind) return true;
        if (!role.empty() && spec::name(k.role) != role) return true;
        if (!name.empty() && !lower(s->text(n, k.name_column)).contains(name)) return true;
        return false;
    });
    if (const Value* near = query.get("near"); near && near->list() && near->list()->size() == 2) {
        const double cx = number_of((*near->list())[0], 0), cy = number_of((*near->list())[1], 0);
        auto distance = [&](NodeRef n) {
            const world::WorldRect r = s->rect(n);
            const double dx = std::max({r.x - cx, 0.0, cx - r.right()}), dy = std::max({r.y - cy, 0.0, cy - r.bottom()});
            return dx * dx + dy * dy;
        };
        std::ranges::stable_sort(candidates, {}, distance);
    }
    Value::List keys;
    for (NodeRef n : candidates) keys.emplace_back(std::string(s->key(n)));
    return ActResult::done(Value(std::move(keys)));
}

void register_canvas_component() { register_type(component_type<Canvas>()); }

}  // namespace omui
