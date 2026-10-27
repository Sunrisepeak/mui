module openmui.ui.canvas;

import std;
import openmui.core;
import openmui.view;
import openmui.render;
import openmui.raster;
import openmui.anim;
import openmui.trace;

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

void Canvas::observe() {
    if (!observed_.empty())
        if (world::Scene* old = world::find_scene(observed_)) old->unobserve(observer_);
    observed_ = props.scene;
    observer_ = 0;
    world::Scene* s = scene();
    if (!s) return;
    observer_ = s->observe([this] {
        if (scene_dirty_) return;
        scene_dirty_ = true;
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
        // The live nodes: placed again when the frame starts.
        schedule();
    });
}

void Canvas::mounted() {
    self_ = this;
    observe();
    camera_ = {props.x, props.y, std::clamp(props.zoom, props.min_zoom, props.max_zoom)};
    textures_.configure({.budget = static_cast<std::size_t>(std::max(1.0, props.cache_mb) * 1024 * 1024),
                         .upload = static_cast<std::size_t>(std::max(0.25, props.upload_mb) * 1024 * 1024)});
}

void Canvas::prop_changed(CanvasProp p) {
    if (!attached()) return;
    switch (p) {
        case CanvasProp::x:
        case CanvasProp::y:
        case CanvasProp::zoom: {
            if (writing_) return;
            // A binding moved it (not our own write-back, which equals the camera).
            const Camera c{props.x, props.y, std::clamp(props.zoom, props.min_zoom, props.max_zoom)};
            if (c == camera_) return;
            motion_.reset();
            glide_.reset();
            camera_ = c;
            place();
            break;
        }
        case CanvasProp::cache_mb:
        case CanvasProp::upload_mb:
            textures_.configure({.budget = static_cast<std::size_t>(std::max(1.0, props.cache_mb) * 1024 * 1024),
                                 .upload = static_cast<std::size_t>(std::max(0.25, props.upload_mb) * 1024 * 1024)});
            break;
        case CanvasProp::scene:
            textures_.clear(services().images());
            observe();
            place();
            break;
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

void Canvas::animate_to(const Camera& c, anim::SpringParams params) {
    const Camera to{c.x, c.y, std::clamp(c.zoom, props.min_zoom, props.max_zoom)};
    // Retargeted mid-flight, it keeps its velocity.
    const auto v = motion_ ? motion_->velocity(services().now()) : std::array<double, 3>{};
    glide_.reset();
    motion_.emplace(camera_, to, services().now(), params, v);
    schedule();
}

// The camera after input or an animation step: the promoted nodes placed, the
// view redrawn, the binding and listeners told.
void Canvas::changed() {
    place();
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    writing_ = true;
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
    tick_ = services().after(anim::seconds(0), [this] {
        tick_ = 0;
        step();
    });
    services().request_frame();
}

void Canvas::step() {
    if (scene_dirty_) place();  // the scene changed: its live nodes again
    // Textures that came back: drawn this frame.
    if (textures_.collect(services().images(), frame_)) services().invalidate_paint(node());
    const bool moving = motion_ || glide_;
    if (!moving) {
        if (textures_.busy()) schedule();
        return;
    }
    const anim::seconds t = services().now();
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
    if (!observed_.empty())
        if (world::Scene* s = world::find_scene(observed_)) s->unobserve(observer_);
    if (tick_ && attached()) services().cancel(tick_);
    if (attached()) textures_.clear(services().images());
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
    std::vector<std::string> keys;
    if (s && vp.width > 0) {
        std::vector<NodeRef> seen;
        s->query(camera_.visible(vp), seen);
        for (NodeRef n : seen) {
            const world::Kind& k = s->kind_of(n);
            if (!k.promote) continue;
            const std::size_t t = tier_of(k, camera_.to_screen(s->rect(n), vp), s->key(n) == focused_);
            if (k.tiers[t].draw == world::Tier::Draw::promote) keys.emplace_back(s->key(n));
        }
    }
    if (keys != promoted_) {
        promoted_ = std::move(keys);
        services().recompose(node());
        return;
    }
    // The same nodes: only their transforms follow the camera.
    if (promoted_.empty() || !s) return;
    for (const NodeId child : services().children(node())) {
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

void Canvas::style(const Theme&, layout::Style& s) const {
    // The promoted nodes overlap at the origin; their transforms place them.
    s.display = layout::Display::stack;
}

// ── drawing ──────────────────────────────────────────────────────────────

void Canvas::paint(PaintContext& ctx) const {
    trace::Span span("canvas.paint", "canvas");
    const auto from = std::chrono::steady_clock::now();
    const Size vp = ctx.size;
    scene_dirty_ = false;
    ctx.list.fill_rect({0, 0, vp.width, vp.height}, ctx.theme.color(props.background, ctx.theme.color("background")));
    stats_ = {};
    visible_.clear();
    world::Scene* s = scene();
    if (!s || vp.width <= 0 || vp.height <= 0) return;
    s->query(camera_.visible(vp), visible_);
    const auto queried = std::chrono::steady_clock::now();
    stats_.visible = visible_.size();
    for (const NodeRef n : visible_) {
        const world::Kind& k = s->kind_of(n);
        const Rect screen = camera_.to_screen(s->rect(n), vp);
        const std::size_t t = tier_of(k, screen, s->key(n) == focused_);
        switch (k.tiers[t].draw) {
            case world::Tier::Draw::box: {
                const Color fill = s->fill(n);
                if (fill.a > 0) {
                    const float r = static_cast<float>(s->radius(n) * camera_.zoom);
                    if (r > 0) ctx.list.fill_round_rect(screen, CornerRadii::all(r), render::Paint::solid(fill));
                    else ctx.list.fill_rect(screen, fill);
                }
                ++stats_.boxes;
                break;
            }
            case world::Tier::Draw::custom:
                if (k.draw) {
                    world::DrawContext dc{*s, n, ctx.list, screen, camera_.zoom, t, ctx.theme};
                    k.draw(dc);
                }
                ++stats_.custom;
                break;
            case world::Tier::Draw::texture: {
                // The content's texture at the screen's resolution, the nearest
                // ready one meanwhile, a box before any is.
                const std::string_view key = s->key(n);
                const std::uint64_t rev = s->content_revision(n);
                const int level = world::Textures::level_for(camera_.zoom * services().pixel_scale());
                const auto pick = textures_.pick(key, rev, level, frame_);
                if (!pick || !pick->exact) {
                    const float cx = screen.x + screen.width / 2 - vp.width / 2, cy = screen.y + screen.height / 2 - vp.height / 2;
                    const float centrality = 1.0f / (1.0f + (cx * cx + cy * cy) / (vp.width * vp.width + vp.height * vp.height));
                    textures_.want(key, rev, level, n, screen.width * screen.height * centrality);
                }
                const raster::Surface* img = pick ? services().images().get(pick->image) : nullptr;
                if (img) {
                    ctx.list.draw_image(pick->image, {0, 0, static_cast<float>(img->width()), static_cast<float>(img->height())}, screen,
                                        render::Sampling::linear);
                    ++stats_.textured;
                    if (!pick->exact) ++stats_.stretched;
                } else {
                    const Color fill = s->fill(n);
                    if (fill.a > 0) ctx.list.fill_round_rect(screen, CornerRadii::all(static_cast<float>(s->radius(n) * camera_.zoom)), render::Paint::solid(fill));
                    ++stats_.boxes;
                }
                break;
            }
            case world::Tier::Draw::promote: ++stats_.promoted; break;  // a live child draws it
        }
    }
    // What the frame wants and has not: rasterised on the pool, collected at
    // the start of a frame (the step), drawn when it is in.
    textures_.issue(*s, ctx.services.images(), &ctx.services.fonts());
    ++frame_;
    if (textures_.busy() && self_ && !tick_) self_->schedule();
    const auto done = std::chrono::steady_clock::now();
    stats_.query_ms = std::chrono::duration<double, std::milli>(queried - from).count();
    stats_.paint_ms = std::chrono::duration<double, std::milli>(done - queried).count();
    trace::count("canvas.visible", static_cast<double>(stats_.visible), "canvas");
}

// ── input: drag pans, the wheel and a pinch zoom, a fling glides ─────────

bool Canvas::on_event(EventContext& ctx, const InputEvent& e) {
    const Size vp = viewport();
    const Point origin = [&] {
        const Rect b = services().bounds(node());
        return Point{b.x, b.y};
    }();
    if (const auto* w = std::get_if<WheelEvent>(&e)) {
        const Point at{w->position.x - origin.x, w->position.y - origin.y};
        motion_.reset();
        glide_.reset();
        // A mouse wheel zooms about the cursor; a trackpad pans, and zooms
        // with ⌃ or ⌘ held (how a pinch reaches a window).
        if (!w->precise || w->mods.has(Modifier::ctrl) || w->mods.has(Modifier::meta)) camera_.zoom_at(at, std::exp(-static_cast<double>(w->delta.y) * (w->precise ? 0.01 : 0.0025)), vp, props.min_zoom, props.max_zoom);
        else camera_.pan({-w->delta.x, -w->delta.y});
        changed();
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
            motion_.reset();
            glide_.reset();
            if (pointer == pointers_.end()) pointers_.push_back({p->pointer, at});
            else pointer->at = at;
            dragging_ = true;
            velocity_ = {};
            last_move_ = services().now();
            pinch_distance_ = 0;
            services().capture_pointer(node());
            services().focus(node());
            return true;
        }
        case PointerKind::move: {
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
                camera_.pan(delta);
                // The pointer's velocity, smoothed, for the fling.
                const anim::seconds now = services().now();
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
            // A fling: the last movement was recent and fast.
            const bool recent = (services().now() - last_move_).count() < 0.1;
            if (props.inertia && recent && p->kind == PointerKind::up && std::hypot(velocity_.x, velocity_.y) > 50) {
                glide_.emplace(camera_, velocity_, services().now());
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
}

std::vector<spec::SemanticNode> Canvas::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    world::Scene* s = scene();
    if (!s || !attached()) return out;
    const Size vp = viewport();
    const Rect b = services().bounds(node());
    std::vector<NodeRef> on_screen;
    s->query(camera_.visible(vp), on_screen);
    if (!focused_.empty())
        if (auto f = s->find(focused_); f && std::ranges::find(on_screen, *f) == on_screen.end()) on_screen.push_back(*f);
    out.reserve(on_screen.size());
    for (const NodeRef n : on_screen) {
        const world::Kind& k = s->kind_of(n);
        spec::SemanticNode item;
        item.id = std::format("{}/{}", path, s->key(n));
        item.role = k.role;
        item.name = std::string(s->text(n, k.name_column));
        const Rect r = camera_.to_screen(s->rect(n), vp);
        item.bounds = spec::Rect{b.x + r.x, b.y + r.y, r.width, r.height};
        item.states.set(State::offscreen, !camera_.visible(vp).overlaps(s->rect(n)));
        item.states.set(State::focused, s->key(n) == focused_);
        item.actions.push_back({"focus", Effect::read, {}, {}, {}});
        out.push_back(std::move(item));
    }
    return out;
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

ActResult Canvas::pan_to(ActContext&, const Value& x, const Value& y) {
    animate_to({number_of(x, camera_.x), number_of(y, camera_.y), camera_.zoom});
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
    const std::string t = target.is_null() ? std::string("all") : std::string(target.as_string());
    world::WorldRect r;
    if (t == "all") r = s->bounds();
    else if (auto n = s->find(t)) r = s->rect(*n);
    else return ActResult::fail(std::format("no node has the key `{}`", t));
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
