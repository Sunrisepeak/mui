module omui.site;

import std;
import omui;
import hanim.engine;
import hanim.render.vector;

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

const MuiArt& mui_art() {
    static const MuiArt art = [] {
        MuiArt out;
        const auto data = parse_json(files::mui_sprites);
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

Mui::~Mui() {
    // The host's engine outlives no runtime: touch it only while attached.
    if (figure_ && figure_->engine && attached()) {
        if (figure_->body) (void)figure_->engine->unsimulate(*figure_->body);
        if (figure_->showing) (void)figure_->engine->release(*figure_->showing);
        if (figure_->object) (void)figure_->engine->destroy_object(figure_->object);
    }
}

void Mui::mounted() {
    bubble_.set_style("label");
    bubble_.set_max_lines(1);
    services().set(node(), "pointer_events", Value("none"));  // over the page, it takes no pointer
    services().follow_actions(node(), true);                 // the agent's steps: painted again
}

void Mui::prop_changed(MuiProp) {
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size Mui::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah) {
    // As large as it is let be: it lives over the page.
    return {width.value_or(aw.is_definite() ? aw.value : 0), height.value_or(ah.is_definite() ? ah.value : 0)};
}

std::optional<Rect> Mui::box_of(std::string_view path) const {
    if (path.empty() || !attached()) return std::nullopt;
    const NodeId n = services().find(path);
    if (!n) return std::nullopt;
    const Rect window = services().bounds(n), self = services().bounds(node());
    if (window.empty()) return std::nullopt;
    return Rect{window.x - self.x, window.y - self.y, window.width, window.height};
}

// The figure made the first time it is painted with room: at the page's
// bottom right, sitting.
void Mui::ensure(const PaintContext& ctx) const {
    if (figure_ && figure_->room == ctx.size) return;
    const MuiArt& art = mui_art();
    if (art.frames.empty() || ctx.size.width <= 0 || ctx.size.height <= 0) return;
    if (figure_) {
        // A new size: the figure stays, its floor moves (steer gives the ledges).
        figure_->room = ctx.size;
        return;
    }
    auto made = std::make_unique<Figure>();
    made->engine = &services().engine();
    made->room = ctx.size;
    const float side = static_cast<float>(props.cell) * static_cast<float>(art.size);
    // Its hull's bottom (a pixel above the grid's) on the page's bottom: standing, not falling.
    made->spawn = {ctx.size.width - side - 32, ctx.size.height - 4 - (side - static_cast<float>(props.cell))};
    figure_ = std::move(made);
    show("sit", "indigo", false);
}

// Shows a frame, in a palette, facing a way: the object updated, shown anew,
// and carried by the same body (made the first time, under its hull).
void Mui::show(const std::string& frame, const std::string& palette, bool facing_right) const {
    Figure& f = *figure_;
    if (f.object && f.frame == frame && f.palette == palette && f.right == facing_right) return;
    const MuiArt& art = mui_art();
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
    f.drawn.reset();
    auto shown = engine.play(hanim::anim::morph(f.object, f.object));
    if (!shown) return;
    f.showing = *shown;
    if (!f.body) {
        // A little soft: it gives on landing and springs back to its shape.
        // The page's sides are walls (its root's space).
        const std::vector<xengine::simulation::Plane> walls{{.normal_x = 1, .normal_y = 0, .offset = -f.spawn.x},
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
void Mui::steer(const PaintContext& ctx) const {
    Figure& f = *figure_;
    hanim::Engine& engine = *f.engine;
    const double t = services().now().count();
    const double dt = f.last < 0 ? 0 : std::clamp(t - f.last, 0.0, 0.1);
    f.last = t;
    const bool still = services().motion_reduced();
    const bool dark = props.palette == MuiPalette::theme ? ctx.theme.dark : props.palette == MuiPalette::night;

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
    double centre_x = f.spawn.x + props.cell * mui_art().size / 2, bottom = f.spawn.y + props.cell * mui_art().size;
    double fall = 0;
    if (body) {
        const auto centre = body->centre();
        centre_x = f.spawn.x + centre[0];
        bottom = f.spawn.y + *std::ranges::max_element(body->y);
        for (const double v : body->velocity_y) fall += v;
        fall /= static_cast<double>(body->size());
    }

    // What it may stand on: the page's bottom, the nodes it was given, what it goes to.
    std::vector<xengine::simulation::Ledge> ledges;
    const auto ledge = [&](const Rect& r) {
        if (ledges.size() < xengine::simulation::max_ledges)
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
    bool facing_right = f.right;
    if (target) {
        const double dx = (target->x + target->width / 2) - centre_x;
        if (std::abs(dx) > 2) facing_right = dx > 0;
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
    if (f.body) {
        (void)engine.walk(*f.body, push);
        if (!still && dt > 0) (void)engine.advance_bodies(hanim::seconds(dt));
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
    } else {
        f.state = "quiet";
        if (t >= f.next_blink) f.next_blink = t + blink_every;
        if (!still && t >= f.next_blink - blink_for) frame = "blink";
    }
    const std::string palette = agent_here && !props.asleep ? (dark ? "agent_night" : "agent") : (dark ? "night" : "indigo");
    show(frame, palette, frame.starts_with("walk") ? facing_right : !facing_right);

    // Frames while it moves (its stride, its body settling) and not otherwise:
    // at rest the page draws nothing (D-70); it blinks in frames drawn anyway.
    double speed = 0;
    if (body)
        for (std::size_t i = 0; i < body->size(); ++i) speed = std::max(speed, std::abs(body->velocity_x[i]) + std::abs(body->velocity_y[i]));
    if (!still && (push != 0 || speed > 4)) {
        services().moving(node());
        services().invalidate_paint(node());  // the next frame draws it again
        services().invalidate_semantics(node());  // and says where it is: an agent reads it there
        if (!f.frames) f.frames = services().demand_frames(FrameDemand::fastest("mui"));
    } else if (f.frames) {
        f.frames.release();
        services().invalidate_semantics(node());  // at rest: said once more
    }
    // A new frame shown (a state changed): said.
    if (f.frame != f.said_frame) {
        f.said_frame = f.frame;
        services().invalidate_semantics(node());
    }
}

void Mui::paint(PaintContext& ctx) const {
    ensure(ctx);
    if (!figure_ || !figure_->showing) return;
    steer(ctx);
    Figure& f = *figure_;
    hanim::Engine& engine = *f.engine;
    const auto frame = f.showing ? engine.frame(*f.showing) : std::unexpected(hanim::EngineFailure{hanim::EngineError::stale_handle});
    if (!frame || !frame->animation) return;
    if (!f.drawn) {
        auto bound = hanim::vector_render::PreparedVector::bind(*frame->animation, frame->effects ? *frame->effects : hanim::PreparedEffects{});
        if (!bound) return;
        f.drawn = std::move(*bound);
    }
    const auto needs = f.drawn->requirements();
    xrender::DisplayList staging;
    std::vector<mlibs::geom::Affine> world(needs.world_transforms);
    std::vector<float> parameters(needs.backend_parameters);
    std::vector<mlibs::geom::Rect> layers(needs.effect_layers);
    ctx.part({.role = "figure", .kind = xrender::PartKind::content}, [&] {
        (void)f.drawn->record(frame->geometry, frame->effect_values, ctx.list, {&staging, world, parameters, layers});
    });

    // A line said, over its head, for a few seconds.
    if (!said_.empty() && services().now().count() < said_until_) {
        const Theme& th = ctx.theme;
        bubble_.set_text(said_);
        const Size t = bubble_.measure(ctx.services, th.color("text"), 280);
        const Now here = now();
        const float side = static_cast<float>(props.cell) * static_cast<float>(mui_art().size);
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

Mui::Now Mui::now() const {
    Now out;
    out.said = services().now().count() < said_until_ ? said_ : std::string();
    if (!figure_) return out;
    const Figure& f = *figure_;
    out.state = f.state;
    out.frame = f.frame;
    out.palette = f.palette;
    out.target = f.target;
    out.facing_right = f.frame.starts_with("walk") ? f.right : !f.right;
    if (f.body)
        if (const auto body = f.engine->body(*f.body); body && *body) {
            out.feet = {static_cast<float>(f.spawn.x + (*body)->centre()[0]), static_cast<float>(f.spawn.y + *std::ranges::max_element((*body)->y))};
        }
    return out;
}

void Mui::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = "Mui";
    const Now here = now();
    Value value;
    value.set("state", here.state).set("frame", here.frame).set("palette", here.palette).set("target", here.target).set("said", here.said);
    value.set("feet", Value(Value::List{Value(static_cast<double>(here.feet.x)), Value(static_cast<double>(here.feet.y))}));
    node.value = std::move(value);
}

ActResult Mui::sleep(ActContext&) {
    write_asleep(true);
    return ActResult::done().undo_by("wake");
}

ActResult Mui::wake(ActContext&) {
    write_asleep(false);
    return ActResult::done().undo_by("sleep");
}

ActResult Mui::move_to(ActContext&, const Value& node_path) {
    if (!box_of(node_path.as_string())) return ActResult::fail(std::format("no node '{}' on the page", node_path.as_string()));
    goal_ = std::string(node_path.as_string());
    goal_kind_ = "move";
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult Mui::look_at(ActContext&, const Value& node_path) {
    if (!box_of(node_path.as_string())) return ActResult::fail(std::format("no node '{}' on the page", node_path.as_string()));
    goal_ = std::string(node_path.as_string());
    goal_kind_ = "look";
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult Mui::point_at(ActContext&, const Value& node_path) {
    if (!box_of(node_path.as_string())) return ActResult::fail(std::format("no node '{}' on the page", node_path.as_string()));
    goal_ = std::string(node_path.as_string());
    goal_kind_ = "point";
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult Mui::say(ActContext&, const Value& text) {
    std::string line(text.as_string());
    if (const auto end = line.find('\n'); end != std::string::npos) line.resize(end);  // one line (§9.4)
    said_ = std::move(line);
    said_until_ = services().now().count() + say_for;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return ActResult::done();
}

}  // namespace omui::site
