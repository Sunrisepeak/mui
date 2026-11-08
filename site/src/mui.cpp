module omui.site;

import std;
import omui;
import hanim.engine;
import omui.hanim;

namespace omui::site {

namespace {

constexpr double stride_speed = 180;  // px/s: its feet
constexpr double near_enough = 10;    // px from what it goes to: there
constexpr double blink_every = 3.7, blink_for = 0.12, stride_frame = 0.14, say_for = 3.0;
const std::string hull_id = "hull";

hanim::Rgba rgba_of(std::string_view hex) { return hanim::Rgba::parse_hex(hex).value_or(hanim::Rgba{0, 0, 0, 1}); }

// A frame as its grid: its feet on the grid's last row but one (every frame
// stands where the hull's bottom is), mirrored when it faces right (the art
// faces left when it walks).
std::vector<std::string> facing(const std::vector<std::string>& rows, bool right) {
    std::vector<std::string> out = rows;
    const auto filled = [](const std::string& row) { return row.find_first_not_of(". ") != std::string::npos; };
    std::size_t last = out.size();
    while (last > 0 && !filled(out[last - 1])) --last;
    if (last > 0 && last + 1 < out.size()) {
        const std::size_t by = out.size() - 1 - last;
        out.insert(out.begin(), by, std::string(out.front().size(), '.'));
        out.resize(rows.size());
    }
    if (right)
        for (auto& row : out) std::ranges::reverse(row);
    return out;
}

}  // namespace

const MascotArt& mascot_art() {
    static const MascotArt art = [] {
        MascotArt out;
        const auto data = parse_json(files::mascot_sprites);
        if (!data) return out;
        if (const Value* palettes = data->get("palettes"); palettes && palettes->map())
            for (const auto& [name, inks] : *palettes->map()) {
                std::vector<hanim::PixelInk> list;
                if (inks.map())
                    for (const auto& [key, colour] : *inks.map())
                        if (!key.empty()) list.push_back({key[0], rgba_of(colour.as_string())});
                out.palettes[name] = std::move(list);
            }
        if (const Value* body = data->get("sprites") ? data->get("sprites")->get("body24") : nullptr) {
            if (const Value* size = body->get("size")) out.size = static_cast<int>(size->as_int(24));
            if (const Value* frames = body->get("frames"); frames && frames->map())
                for (const auto& [name, rows] : *frames->map()) {
                    std::vector<std::string> list;
                    if (rows.list())
                        for (const Value& row : *rows.list()) list.emplace_back(row.as_string());
                    out.frames[name] = std::move(list);
                }
        }
        return out;
    }();
    return art;
}

Mascot::~Mascot() {
    // The host's engine outlives no runtime: touch it only while attached.
    if (figure_ && figure_->engine && attached()) {
        release_arrival();
        if (figure_->body) (void)figure_->engine->unsimulate(*figure_->body);
        if (figure_->showing) (void)figure_->engine->release(*figure_->showing);
        if (figure_->object) (void)figure_->engine->destroy_object(figure_->object);
    }
}

void Mascot::mounted() {
    bubble_.set_style("label");
    bubble_.set_max_lines(1);
    services().follow_actions(node(), true);                 // the agent's steps: painted again
}

void Mascot::prop_changed(MascotProp) {
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size Mascot::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah) {
    // As large as it is let be: it lives over the page.
    return {width.value_or(aw.is_definite() ? aw.value : 0), height.value_or(ah.is_definite() ? ah.value : 0)};
}

std::optional<Rect> Mascot::box_of(std::string_view path) const {
    if (path.empty() || !attached()) return std::nullopt;
    const NodeId n = services().find(path);
    if (!n) return std::nullopt;
    const Rect window = services().bounds(n), self = services().bounds(node());
    if (window.empty()) return std::nullopt;
    return Rect{window.x - self.x, window.y - self.y, window.width, window.height};
}

// The figure made the first time it is painted with room: at the page's
// bottom right, sitting.
void Mascot::ensure(const PaintContext& ctx) const {
    if (figure_ && figure_->room == ctx.size) return;
    const bool had_figure = figure_ != nullptr;
    const MascotArt& art = mascot_art();
    if (art.frames.empty() || ctx.size.width <= 0 || ctx.size.height <= 0) return;
    // A new size (another place, a window resized): the figure made again at
    // its place on the new floor, not falling to it — a page's layout is not an event.
    std::string frame = "sit", palette = "indigo";
    bool right = true, facing_right = false;  // sitting, facing left: the art's sit mirrored
    if (figure_) {
        frame = figure_->frame, palette = figure_->palette, right = figure_->right, facing_right = figure_->facing_right;
        Figure& old = *figure_;
        release_arrival();
        if (old.body) (void)old.engine->unsimulate(*old.body);
        if (old.showing) (void)old.engine->release(*old.showing);
        if (old.object) (void)old.engine->destroy_object(old.object);
        figure_.reset();
    }
    auto made = std::make_unique<Figure>();
    made->engine = &services().engine();
    made->room = ctx.size;
    const float side = static_cast<float>(props.cell) * static_cast<float>(art.size);
    // Its hull's bottom (a pixel above the grid's) on the page's bottom: standing, not falling.
    made->spawn = {ctx.size.width - side - 32, ctx.size.height - 4 - (side - static_cast<float>(props.cell))};
    made->facing_right = facing_right;
    // The first time: from where it was told it comes from, its pixels gathering there (not with less motion).
    const bool first = !had_figure;
    if (first && !props.arrive.empty() && !services().motion_reduced())
        if (const auto from = box_of(props.arrive)) {
            made->spawn = {std::round(from->x + from->width / 2 - side / 2), std::round(from->y + from->height / 2 - side / 2)};
            made->arrived = false;
            made->arrive_at = services().now().count() + std::max(0.0, props.arrive_after);
        }
    figure_ = std::move(made);
    if (figure_->arrived) show(frame, palette, right);
}

void Mascot::release_arrival() const {
    if (!figure_) return;
    Figure& f = *figure_;
    if (f.gathering) (void)f.engine->release(*f.gathering);
    f.gathering.reset();
    f.gathering_drawing.reset();
    if (f.pixels) (void)f.engine->destroy_object(f.pixels);
    f.pixels = {};
}

// Its pixels gathering where it comes from (hanim's assemble_in on its pixel
// picture), then — gathered — its body made there, in the air: it drops to
// the page's bottom, landing soft.
void Mascot::arrive(PaintContext& ctx) const {
    Figure& f = *figure_;
    hanim::Engine& engine = *f.engine;
    const double t = services().now().count();
    services().moving(node());
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    f.state = "arriving";
    if (t < f.arrive_at) return;
    const MascotArt& art = mascot_art();
    const std::string palette = ctx.theme.dark ? "night" : "indigo";
    const auto rows = art.frames.find("sit");
    const auto inks = art.palettes.find(palette);
    if (rows == art.frames.end() || inks == art.palettes.end()) { f.arrived = true; return; }
    const hanim::PixelGrid grid{inks->second, facing(rows->second, true), props.cell};
    if (!f.gathering) {
        auto description = hanim::pixel_object("arrival", grid, {.form = hanim::PixelForm::pixels});
        if (!description) { f.arrived = true; return; }
        description->parts[0].local = {.tx = f.spawn.x, .ty = f.spawn.y};
        auto object = engine.create_object(*description);
        if (!object) { f.arrived = true; return; }
        f.pixels = *object;
        const double side = props.cell * art.size;
        auto played = engine.play(hanim::anim::assemble_in(f.pixels, grid, {.seed = 7, .distance = side * 1.4, .spin = 2.4}));
        if (!played) { release_arrival(); f.arrived = true; return; }
        f.gathering = *played;
        (void)engine.pause(*f.gathering);
        f.gather_length = engine.inspect(*f.gathering) ? engine.inspect(*f.gathering)->duration.count() : 0;
    }
    const double elapsed = std::min(t - f.arrive_at, f.gather_length);
    (void)engine.seek(*f.gathering, hanim::seconds(elapsed));
    (void)f.gathering_drawing.record(ctx, engine, *f.gathering, "figure", 0);
    if (t - f.arrive_at < f.gather_length) return;
    // Gathered: its body, there, in the air — awake, so it falls.
    release_arrival();
    f.arrived = true;
    f.started = true;
    f.asleep = false;
    f.last = t;
    show("sit", palette, true);
}

// Shows a frame, in a palette, facing a way: the object updated, shown anew,
// and carried by the same body (made the first time, under its hull).
void Mascot::show(const std::string& frame, const std::string& palette, bool facing_right) const {
    Figure& f = *figure_;
    if (f.object && f.frame == frame && f.palette == palette && f.right == facing_right) return;
    const MascotArt& art = mascot_art();
    const auto rows = art.frames.find(frame);
    const auto inks = art.palettes.find(palette);
    if (rows == art.frames.end() || inks == art.palettes.end()) return;
    const double cell = props.cell;
    hanim::PixelGrid grid{inks->second, facing(rows->second, facing_right), cell};
    auto description = hanim::pixel_object("mui", grid);
    if (!description) return;
    description->parts[0].local = {.tx = f.spawn.x, .ty = f.spawn.y};
    // The hull: a rounded box round the figure, never drawn — what the body is.
    {
        // Round the whole grid (every frame inside it), its bottom at the feet.
        const double side = cell * art.size, r = cell * 5;
        const double left = 0, top = cell, right = side, bottom = side - cell;
        const double k = r * 0.5522847498307936;
        const std::array segments{
            hanim::ShapeSegment::line({right - r, top}), hanim::ShapeSegment::cubic({right - r + k, top}, {right, top + r - k}, {right, top + r}),
            hanim::ShapeSegment::line({right, bottom - r}), hanim::ShapeSegment::cubic({right, bottom - r + k}, {right - r + k, bottom}, {right - r, bottom}),
            hanim::ShapeSegment::line({left + r, bottom}), hanim::ShapeSegment::cubic({left + r - k, bottom}, {left, bottom - r + k}, {left, bottom - r}),
            hanim::ShapeSegment::line({left, top + r}), hanim::ShapeSegment::cubic({left, top + r - k}, {left + r - k, top}, {left + r, top})};
        auto hull = hanim::Contour::copy({{left + r, top}, segments, true});
        if (!hull) return;
        description->parts.insert(description->parts.begin() + 1,
                                  {.id = hull_id, .parent = "mui", .contours = {*hull}, .fill = hanim::Rgba{0, 0, 0, 0}});
    }
    std::vector<std::string> ink_ids;
    for (const auto& part : description->parts)
        if (part.id.starts_with("ink_")) ink_ids.push_back(part.id);
    hanim::Engine& engine = *f.engine;
    if (!f.object) {
        auto object = engine.create_object(*description);
        if (!object) return;
        f.object = *object;
    } else if (!engine.update_object(f.object, *description)) {
        return;
    }
    if (f.showing) (void)engine.release(*f.showing);
    f.showing.reset();
    f.drawing.reset();
    auto shown = engine.play(hanim::anim::morph(f.object, f.object));
    if (!shown) return;
    f.showing = *shown;
    if (!f.body) {
        // A little soft: it gives on landing and springs back to its shape.
        // The page's sides are walls (its root's space).
        const std::vector<xengine::dynamics::Plane> walls{{.normal_x = 1, .normal_y = 0, .offset = -f.spawn.x},
                                                            {.normal_x = -1, .normal_y = 0, .offset = -(f.room.width - f.spawn.x)}};
        hanim::BodyOptions options{.params = {.mass = 1, .gravity_y = 1800, .area_compliance = 0.0002, .shape_stiffness = 0.12,
                                              .upright = 1, .restitution = 0.15, .friction = 0.4, .damping = 0.6},
                                   .colliders = walls,
                                   .points = 24,
                                   .carries = ink_ids};
        if (auto body = engine.simulate({f.object, hull_id}, options)) f.body = *body;
        else std::println(std::cerr, "mui: no body: {}", body.error().text());
    } else {
        (void)engine.carry(*f.body, ink_ids);
    }
    f.frame = frame;
    f.palette = palette;
    f.right = facing_right;
    f.inks = std::move(ink_ids);
}

// Where it goes, what it stands on, which frame: from the agent's presence,
// what it was asked, and the time.
void Mascot::steer(const PaintContext& ctx) const {
    Figure& f = *figure_;
    hanim::Engine& engine = *f.engine;
    const double t = services().now().count();
    const double dt = f.last < 0 ? 0 : std::clamp(t - f.last, 0.0, 0.1);
    f.last = t;
    const bool still = services().motion_reduced();
    const bool dark = props.palette == MascotPalette::theme ? ctx.theme.dark : props.palette == MascotPalette::night;

    // Whom it embodies, and what it goes to.
    const AgentPresenceState agent = agent_presence_state(services(), props.agents, 0);
    const bool agent_here = agent.now.has_value();
    std::optional<Rect> target;
    std::string kind;
    if (props.asleep) {
        kind = "sleep";
    } else if (agent_here && !agent.now->bounds.empty()) {
        const Rect self = services().bounds(node());
        target = Rect{agent.now->bounds.x - self.x, agent.now->bounds.y - self.y, agent.now->bounds.width, agent.now->bounds.height};
        kind = agent.asking ? "ask" : "point";
        f.target = agent.now->path;
    } else if (const auto goal = box_of(goal_)) {
        target = goal;
        kind = goal_kind_;
        f.target = goal_;
    } else {
        f.target.clear();
    }

    // Where its body is now (its root's space) and its feet in its box.
    const auto* body = f.body ? engine.body(*f.body).value_or(nullptr) : nullptr;
    double centre_x = f.spawn.x + props.cell * mascot_art().size / 2, bottom = f.spawn.y + props.cell * mascot_art().size;
    double fall = 0;
    if (body) {
        const auto centre = body->centre();
        centre_x = f.spawn.x + centre[0];
        bottom = f.spawn.y + *std::ranges::max_element(body->y);
        for (const double v : body->velocity_y) fall += v;
        fall /= static_cast<double>(body->size());
    }

    // What it may stand on: the page's bottom, the nodes it was given, what it goes to.
    std::vector<xengine::dynamics::Ledge> ledges;
    const auto ledge = [&](const Rect& r) {
        if (ledges.size() < xengine::dynamics::max_ledges)
            ledges.push_back({r.x - f.spawn.x, r.right() - f.spawn.x, r.y - f.spawn.y});
    };
    ledge({0, ctx.size.height - 4, ctx.size.width, 0});
    if (target && std::ranges::any_of(props.surfaces.list() ? *props.surfaces.list() : Value::List{}, [&](const Value& path) { return f.target == path.as_string(); }))
        ledge(*target);
    if (const auto* list = props.surfaces.list())
        for (const Value& path : *list)
            if (const auto box = box_of(path.as_string())) ledge(*box);
    if (f.body) (void)engine.surfaces(*f.body, ledges);

    // Walk to it (unless it only looks, or less motion is asked); under it, stop and hop up onto it.
    double push = 0;
    if (target) {
        const double dx = (target->x + target->width / 2) - centre_x;
        if (std::abs(dx) > 2) f.facing_right = dx > 0;
        const bool goes = kind != "look" && kind != "sleep" && !still;
        const double reach = std::max(near_enough, target->width / 2 * 0.6);
        // It climbs only onto what it may stand on (a card); a control it points at from below.
        bool surface = false;
        if (const auto* list = props.surfaces.list())
            for (const Value& path : *list) surface = surface || f.target == path.as_string() || f.target.starts_with(std::string(path.as_string()) + "/");
        const bool above = surface && target->y < bottom - 8;  // its top higher than its feet
        const bool grounded = std::abs(fall) < 25 && t - f.hopped > 0.5;
        if (goes && above && std::abs(dx) < target->width / 2 + 20) {
            if (grounded && f.body) {
                const double rise = bottom - target->y + 16, up = std::sqrt(2 * 1800 * rise), flight = 2 * up / 1800;
                (void)engine.hop(*f.body, {std::clamp(dx / flight, -220.0, 220.0), -up});
                f.hopped = t;
            }
        } else if (goes && std::abs(dx) > reach) {
            push = dx > 0 ? stride_speed : -stride_speed;
        }
    }
    // Its body sleeps once it has settled (as a physics engine's does): frames
    // drawn for anything else then leave it exactly as it is. A walk, a hop or
    // what it stands on changing wakes it.
    double speed = 0;
    if (body)
        for (std::size_t i = 0; i < body->size(); ++i) speed = std::max(speed, std::abs(body->velocity_x[i]) + std::abs(body->velocity_y[i]));
    // Made standing on its floor, it starts asleep: its first surfaces are not a change.
    if (!f.started) f.started = true, f.asleep = true, f.ledges_seen = ledges.size();
    const bool woken = push != 0 || t - f.hopped < 0.6 || ledges.size() != f.ledges_seen;
    if (woken || (!f.asleep && speed > 4)) {
        f.asleep = false;
        f.settled_at = -1;
    } else if (!f.asleep) {
        if (f.settled_at < 0) f.settled_at = t;
        if (t - f.settled_at > 0.3) {
            f.asleep = true;
            // Asleep in its canonical pose: the same pixels whatever brought it here.
            if (f.body && !still) {
                // On whole pixels of its box (its root stands on whole pixels too: see ensure).
                (void)engine.settle(*f.body, 1);
                (void)engine.advance_body(*f.body, hanim::seconds(1.0 / 120));
            }
        }
    }
    const bool sleeping = f.asleep;
    f.ledges_seen = ledges.size();
    if (f.body) {
        (void)engine.walk(*f.body, push);
        if (!still && !sleeping && dt > 0) (void)engine.advance_body(*f.body, hanim::seconds(dt));
    }

    // The frame: asleep, holding the card, striding, pointing, looking, or sitting and now and then blinking.
    std::string frame = "sit";
    if (kind == "sleep") {
        frame = "sleep", f.state = "asleep";
    } else if (push != 0) {
        f.stride += dt;
        frame = static_cast<int>(f.stride / stride_frame) % 2 == 0 ? "walk" : "walk2";
        f.state = "walking";
    } else if (kind == "ask") {
        frame = "hold", f.state = "asking";
    } else if (kind == "point") {
        frame = "point", f.state = "pointing";
    } else if (kind == "look" || kind == "move") {
        frame = "look", f.state = kind == "look" ? "looking" : "quiet";
    } else if (services().agent_link().state == "waiting") {
        frame = "look", f.state = "listening";  // its ears up: your agent is on its way
    } else {
        f.state = "quiet";
        // A blink now and then while it is awake; asleep in body, its drawing stays exactly as it is.
        if (t >= f.next_blink) f.next_blink = t + blink_every;
        if (!still && !sleeping && t >= f.next_blink - blink_for) frame = "blink";
    }
    const std::string palette = agent_here && !props.asleep ? (dark ? "agent_night" : "agent") : (dark ? "night" : "indigo");
    // The art walks facing left and sits facing right: mirrored to face the way it faces.
    show(frame, palette, frame.starts_with("walk") ? f.facing_right : !f.facing_right);

    // Frames while it moves (its stride, its body settling) and not otherwise:
    // at rest the page draws nothing (D-70); it blinks in frames drawn anyway.
    if (!still && !sleeping) {
        services().moving(node());
        services().invalidate_paint(node());  // the next frame draws it again
        services().invalidate_semantics(node());  // and says where it is: an agent reads it there
        f.was_moving = true;
    } else if (f.was_moving) {
        f.was_moving = false;
        services().invalidate_semantics(node());  // at rest: said once more
    }
    // A new frame shown (a state changed): said.
    if (f.frame != f.said_frame) {
        f.said_frame = f.frame;
        services().invalidate_semantics(node());
    }
}

void Mascot::paint(PaintContext& ctx) const {
    ensure(ctx);
    if (figure_ && !figure_->arrived) {
        arrive(ctx);
        if (!figure_->arrived) return;
    }
    if (!figure_ || !figure_->showing) return;
    steer(ctx);
    Figure& f = *figure_;
    hanim::Engine& engine = *f.engine;
    if (!f.drawing.record(ctx, engine, *f.showing, "figure")) return;

    // A line said, over its head, for a few seconds.
    if (!said_.empty() && services().now().count() < said_until_) {
        const Theme& th = ctx.theme;
        bubble_.set_text(said_);
        const Size t = bubble_.measure(ctx.services, th.color("text"), 280);
        const Now here = now();
        const float side = static_cast<float>(props.cell) * static_cast<float>(mascot_art().size);
        const Rect bubble{std::clamp(here.feet.x - t.width / 2 - 10, 4.0f, std::max(4.0f, ctx.size.width - t.width - 24)), here.feet.y - side - t.height - 18,
                          t.width + 20, t.height + 10};
        ctx.part({.role = "bubble", .allocation = bubble, .kind = xrender::PartKind::box}, [&] {
            if (auto it = th.shadows.find("sm"); it != th.shadows.end()) paint_shadow(ctx.list, bubble, th.radii(bubble.height / 2), it->second);
            ctx.list.fill_round_rect(bubble, th.radii(bubble.height / 2), xrender::Paint::solid(th.color("surface")));
            bubble_.paint(ctx, {bubble.x + 10, bubble.y + 5}, th.color("text"));
        });
        services().moving(node());
        services().invalidate_paint(node());
    }
}

Mascot::Now Mascot::now() const {
    Now out;
    out.said = services().now().count() < said_until_ ? said_ : std::string();
    if (!figure_) return out;
    const Figure& f = *figure_;
    out.state = f.state;
    out.frame = f.frame;
    out.palette = f.palette;
    out.target = f.target;
    out.facing_right = f.facing_right;
    if (f.body)
        if (const auto body = f.engine->body(*f.body); body && *body) {
            out.feet = {static_cast<float>(f.spawn.x + (*body)->centre()[0]), static_cast<float>(f.spawn.y + *std::ranges::max_element((*body)->y))};
        }
    return out;
}

void Mascot::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = "Mui";
    const Now here = now();
    Value value;
    value.set("state", here.state).set("frame", here.frame).set("palette", here.palette).set("target", here.target).set("said", here.said);
    value.set("feet", Value(Value::List{Value(static_cast<double>(here.feet.x)), Value(static_cast<double>(here.feet.y))}));
    node.value = std::move(value);
}

bool Mascot::hits(Point local, Size) const {
    if (!figure_ || !figure_->arrived || probing_) return false;
    const Now here = now();
    const float side = static_cast<float>(props.cell) * static_cast<float>(mascot_art().size);
    if (!Rect{here.feet.x - side / 2, here.feet.y - side, side, side}.contains(local)) return false;
    // Over a control, the control's: what is beneath it, asked with its own hits saying no.
    const Rect box = services().bounds(node());
    probing_ = true;
    const NodeId control = services().control_at({box.x + local.x, box.y + local.y});
    probing_ = false;
    return !control || services().state(control).disabled;
}

bool Mascot::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || !hits(ctx.local, {})) return false;
    if (p->kind == PointerKind::down) return true;  // its own press: a click on it, not on the page beneath
    if (p->kind != PointerKind::up) return false;
    emit_activated();
    return true;
}

ActResult Mascot::activate(ActContext&) {
    emit_activated();
    return ActResult::done();
}

ActResult Mascot::sleep(ActContext&) {
    write_asleep(true);
    return ActResult::done().undo_by("wake");
}

ActResult Mascot::wake(ActContext&) {
    write_asleep(false);
    return ActResult::done().undo_by("sleep");
}

ActResult Mascot::move_to(ActContext&, const Value& node_path) {
    if (!box_of(node_path.as_string())) return ActResult::fail(std::format("no node '{}' on the page", node_path.as_string()));
    goal_ = std::string(node_path.as_string());
    goal_kind_ = "move";
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult Mascot::look_at(ActContext&, const Value& node_path) {
    if (!box_of(node_path.as_string())) return ActResult::fail(std::format("no node '{}' on the page", node_path.as_string()));
    goal_ = std::string(node_path.as_string());
    goal_kind_ = "look";
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult Mascot::point_at(ActContext&, const Value& node_path) {
    if (!box_of(node_path.as_string())) return ActResult::fail(std::format("no node '{}' on the page", node_path.as_string()));
    goal_ = std::string(node_path.as_string());
    goal_kind_ = "point";
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult Mascot::say(ActContext&, const Value& text) {
    std::string line(text.as_string());
    if (const auto end = line.find('\n'); end != std::string::npos) line.resize(end);  // one line (§9.4)
    said_ = std::move(line);
    said_until_ = services().now().count() + say_for;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return ActResult::done();
}

}  // namespace omui::site
