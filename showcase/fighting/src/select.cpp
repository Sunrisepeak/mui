module openmui.showcase.fighting;

import std;
import openmui.core;
import openmui.view;
import gongfu.present2d;
import hanim;

namespace omui::fighting {

namespace {

namespace p2 = gongfu::present2d;

constexpr float picture_w = 960, picture_h = 540;
constexpr double frame_period = 1.0 / 60;
// Frames between both players being ready and the fight being chosen: the VS has its moment.
constexpr std::int32_t chosen_after = 45;

Color tone(std::uint32_t hex, float alpha = 1) { return Color::hex(hex, alpha); }

// The menu art's regions (hud/menu-*.webp, generated and keyed by gongfu-studio).
namespace region {
constexpr Rect logo{68, 78, 690, 309};  // menu-title.webp
constexpr Rect versus{9, 5, 372, 108};  // menu-type.webp
constexpr Rect training{381, 9, 380, 105};
constexpr Rect select{20, 117, 738, 86};
constexpr Rect ready{15, 204, 300, 90};
constexpr Rect vs{10, 285, 245, 192};
constexpr Rect p1{244, 300, 150, 92};
constexpr Rect p2{387, 300, 156, 92};
constexpr Rect duizhan{0, 7, 240, 108};  // menu-cn.webp: 对战, 训练
constexpr Rect xunlian{254, 7, 211, 108};
constexpr Rect tile{31, 12, 245, 217};  // menu-frames.webp
constexpr Rect tile_lit{298, 12, 245, 217};
constexpr Rect banner{581, 12, 245, 442};
constexpr Rect bar{19, 262, 562, 75};
constexpr Rect bar_lit{15, 345, 568, 90};
constexpr Rect ink_fill{32, 104, 928, 62};  // fill.webp
// names.webp: each fighter's name in calligraphy, with its seal.
constexpr std::array<std::pair<std::string_view, Rect>, 6> name{{{"mo", {40, 18, 206, 198}},
                                                                  {"ni", {325, 17, 213, 197}},
                                                                  {"kun", {596, 15, 184, 222}},
                                                                  {"li", {38, 249, 223, 194}},
                                                                  {"yue", {311, 249, 219, 193}},
                                                                  {"ke", {597, 250, 215, 198}}}};
}  // namespace region

float wide(const Rect& src, float h) { return src.width * h / src.height; }

void picture(render::DisplayList& out, render::ImageRef image, const Rect& src, const Rect& dest, float opacity = 1) {
    if (image.id == 0 || opacity <= 0) return;
    out.draw_image(image, src, dest, render::Sampling::linear, opacity);
}
void shape(render::DisplayList& out, render::ImageRef image, const Rect& src, const Rect& dest, Color colour) {
    if (image.id == 0 || colour.a <= 0) return;
    const std::array<Point, 4> at{Point{dest.x, dest.y}, {dest.x + dest.width, dest.y}, {dest.x + dest.width, dest.y + dest.height}, {dest.x, dest.y + dest.height}};
    const std::array<Point, 4> uv{Point{src.x, src.y}, {src.x + src.width, src.y}, {src.x + src.width, src.y + src.height}, {src.x, src.y + src.height}};
    constexpr std::array<std::uint16_t, 6> tri{0, 1, 2, 0, 2, 3};
    out.draw_mesh_mask(image, at, uv, tri, colour);
}

// The stage's keys (the same as a fight's): player 1 W A S D, U to confirm, J back; player 2 the arrows, Insert, Delete.
struct Keys {
    Key up, down, left, right, confirm, back;
};
constexpr std::array<Keys, 2> keys{{{Key::w, Key::s, Key::a, Key::d, Key::u, Key::j}, {Key::up, Key::down, Key::left, Key::right, Key::insert, Key::delete_}}};

}  // namespace

std::span<const std::string> Select::choices() {
    static const std::vector<std::string> order = [] {
        std::vector<std::string> out;
        for (std::string_view n : {"mo", "ni", "kun", "li", "yue", "ke"})
            if (std::ranges::contains(roster().names, n)) out.emplace_back(n);
        return out;
    }();
    return order;
}

Select::~Select() {
    if (attached() && timer_) services().cancel(timer_);
}

void Select::mounted() {
    list_.reserve(256);
    art_ = registered(services().images());
    const Roster& r = roster();
    for (const std::string& name : choices()) {
        const std::size_t i = fighter_index(name);
        for (std::size_t palette = 0; palette < 2; ++palette) {
            p2::Look look = r.looks[2 * i + palette];
            art_->bind(look.pictures);
            looks_.push_back(std::move(look));
        }
        if (const auto home = r.homes.find(name); home != r.homes.end() && !stages_.contains(home->second))
            if (const auto st = r.stages.find(home->second); st != r.stages.end()) {
                p2::Stage s = st->second;
                art_->bind(s.pictures, "stages/");
                stages_.emplace(home->second, std::move(s));
            }
    }
    if (hearing() && !heard_) heard_ = hearing()(sounds_);  // the program hears the menus (a device on their mixer)
    begin(props.phase);
    services().focus(node());
    pace_ = services().demand_frames({.min_hz = 60, .max_hz = 60, .preferred_hz = 60, .reason = "menu"});
    schedule();
}

void Select::schedule() {
    timer_ = services().after(seconds(frame_period), [this] {
        timer_ = 0;
        if (!props.active) {  // behind the fight: still, until it is up again
            schedule();
            return;
        }
        ++frame_;
        if (both_ready_ >= 0) music();  // the theme gives way to the VS
        if (both_ready_ >= 0 && frame_ - both_ready_ == chosen_after) {
            const auto& c = choices();
            const std::string& a = c[static_cast<std::size_t>(cursor_[0])];
            const std::string& b = c[static_cast<std::size_t>(cursor_[1])];
            const auto home = roster().homes.find(a);
            emit_chosen(home == roster().homes.end() ? std::string() : home->second, mode_, a, b);
        }
        services().invalidate_paint(node());
        schedule();
    });
}

void Select::begin(std::string_view phase) {
    phase_ = phase == "fighters" ? "fighters" : "title";
    ready_ = {};
    both_ready_ = -1;
    if (attached()) services().focus(node());
    music();
    changed();
}

// The main theme while the menus are up: from its intro when they come up,
// calm on the title, the beat on the select screen, all of it once a player
// is ready; fading under the VS; finished behind the fight.
void Select::music() {
    if (!attached()) return;
    if (!props.active) {
        sounds_.finish();
        return;
    }
    if (sounds_.playing() != "menu") {
        sounds_.restart("menu");
        heard_from_ = frame_;
    }
    const float intensity = phase_ == "title" ? 0.25f : ready_[0] || ready_[1] ? 0.9f : 0.6f;
    const float gain = both_ready_ < 0 ? 1.0f : 1.0f - static_cast<float>(std::min(frame_ - both_ready_, chosen_after)) / static_cast<float>(chosen_after);
    sounds_.theme(intensity, gain);
}

void Select::cue(std::string_view name, float pan) {
    if (attached() && props.active) sounds_.cue(name, frame_ - heard_from_, 1, pan);
}

void Select::prop_changed(SelectProp p) {
    if (!attached()) return;
    if (p == SelectProp::phase || (p == SelectProp::active && props.active)) begin(props.phase);
    else if (p == SelectProp::active) music();
}

void Select::changed() {
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size Select::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, layout::Available, layout::Available) {
    return {width.value_or(picture_w), height.value_or(picture_h)};
}

void Select::step(std::int32_t player, std::int32_t by) {
    if (phase_ == "title") {
        item_ = ((item_ + by) % 2 + 2) % 2;
    } else {
        const auto p = static_cast<std::size_t>(player & 1);
        if (ready_[p]) return;
        const auto n = static_cast<std::int32_t>(choices().size());
        if (n > 0) cursor_[p] = ((cursor_[p] + by) % n + n) % n;
    }
    if (by != 0) cue("menu_move", phase_ == "title" ? 0.0f : player == 0 ? -0.4f : 0.4f);
    changed();
}

void Select::press(std::int32_t player) {
    if (phase_ == "title") {
        mode_ = item_ == 0 ? "versus" : "training";
        cue("menu_confirm");
        begin("fighters");
        return;
    }
    if (ready_[static_cast<std::size_t>(player & 1)]) return;
    ready_[static_cast<std::size_t>(player & 1)] = true;
    cue("menu_ready", player == 0 ? -0.4f : 0.4f);
    if (ready_[0] && ready_[1] && both_ready_ < 0) {
        both_ready_ = frame_;
        cue("menu_vs");
    }
    music();
    changed();
}

void Select::unpress(std::int32_t player) {
    if (phase_ == "title") return;
    const auto p = static_cast<std::size_t>(player & 1);
    if (both_ready_ >= 0) both_ready_ = -1;
    cue("menu_back", p == 0 ? -0.4f : 0.4f);
    if (ready_[p]) ready_[p] = false;
    else if (!ready_[1 - p]) begin("title");
    music();
    changed();
}

ActResult Select::move(ActContext&, const MoveArgs& args) {
    const std::int64_t player = args.player.as_int(0), by = args.by.as_int(0);
    if (player != 0 && player != 1) return ActResult::fail("`player` is 0 or 1");
    step(static_cast<std::int32_t>(player), static_cast<std::int32_t>(by));
    return ActResult::done();
}

ActResult Select::confirm(ActContext&, const Value& player) {
    const std::int64_t p = player.as_int(0);
    if (p != 0 && p != 1) return ActResult::fail("`player` is 0 or 1");
    press(static_cast<std::int32_t>(p));
    return ActResult::done();
}

ActResult Select::back(ActContext&, const Value& player) {
    const std::int64_t p = player.as_int(0);
    if (p != 0 && p != 1) return ActResult::fail("`player` is 0 or 1");
    unpress(static_cast<std::int32_t>(p));
    return ActResult::done();
}

std::optional<std::int32_t> Select::seat_of(std::uint32_t device, bool take) {
    for (std::size_t i = 0; i < pads_.size(); ++i)
        if (pads_[i] == device) return static_cast<std::int32_t>(i);
    if (!take) return std::nullopt;
    for (std::size_t i = 0; i < pads_.size(); ++i)
        if (!pads_[i]) {
            pads_[i] = device;
            return static_cast<std::int32_t>(i);
        }
    return std::nullopt;
}

bool Select::on_event(EventContext& ctx, const InputEvent& e) {
    if (!props.active) return false;
    if (const auto* g = std::get_if<GamepadEvent>(&e)) {
        if (g->kind == GamepadKind::disconnected) {
            for (auto& p : pads_)
                if (p == g->device) p.reset();
            return true;
        }
        const auto seat = seat_of(g->device, true);
        if (!seat) return false;
        const bool across = phase_ == "fighters";
        if (g->kind == GamepadKind::down) {
            switch (g->button) {
                case GamepadButton::south:
                case GamepadButton::start: press(*seat); break;
                case GamepadButton::east:
                case GamepadButton::back: unpress(*seat); break;
                case GamepadButton::dpad_left: if (across) step(*seat, -1); break;
                case GamepadButton::dpad_right: if (across) step(*seat, 1); break;
                case GamepadButton::dpad_up: if (!across) step(*seat, -1); else step(*seat, -3); break;
                case GamepadButton::dpad_down: if (!across) step(*seat, 1); else step(*seat, 3); break;
                default: break;
            }
        } else if (g->kind == GamepadKind::axis && (g->axis == GamepadAxis::left_x || g->axis == GamepadAxis::left_y)) {
            // A push past half moves once; back under a quarter, it may move again.
            float& held = stick_[static_cast<std::size_t>(*seat)];
            const bool x = g->axis == GamepadAxis::left_x;
            if (std::abs(g->value) > 0.5f && std::abs(held) < 0.25f) {
                const std::int32_t by = g->value > 0 ? 1 : -1;
                if (x && across) step(*seat, by);
                if (!x) step(*seat, across ? 3 * by : by);
                held = g->value;
            } else if (std::abs(g->value) < 0.25f) {
                held = 0;
            }
        }
        ctx.handled = true;
        return true;
    }
    const auto* k = std::get_if<KeyEvent>(&e);
    if (!k || k->kind != KeyKind::down) return false;
    for (std::int32_t p = 0; p < 2; ++p) {
        const Keys& l = keys[static_cast<std::size_t>(p)];
        const bool across = phase_ == "fighters";
        std::optional<std::int32_t> by;
        if (k->key == l.left && across) by = -1;
        if (k->key == l.right && across) by = 1;
        if (k->key == l.up) by = across ? -3 : -1;
        if (k->key == l.down) by = across ? 3 : 1;
        if (by) {
            step(p, *by);
        } else if (k->key == l.confirm || (p == 0 && k->key == Key::enter)) {
            press(p);
        } else if (k->key == l.back || (p == 0 && k->key == Key::escape)) {
            unpress(p);
        } else {
            continue;
        }
        ctx.handled = true;
        return true;
    }
    return false;
}

void Select::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.name = phase_ == "title" ? "Title" : "Select fighters";
    Value v;
    v.set("phase", phase_).set("mode", phase_ == "title" ? std::string(item_ == 0 ? "versus" : "training") : mode_);
    if (phase_ == "fighters") {
        const auto& c = choices();
        for (std::int32_t p = 0; p < 2; ++p) {
            const auto i = static_cast<std::size_t>(cursor_[static_cast<std::size_t>(p)]);
            v.set(p == 0 ? "p1" : "p2", Value{}.set("on", i < c.size() ? c[i] : std::string()).set("ready", ready_[static_cast<std::size_t>(p)]));
        }
    }
    node.value = std::move(v);
}

void Select::paint(PaintContext& ctx) const {
    render::DisplayList& out = list_;
    out.clear();
    const Roster& r = roster();
    const auto& c = choices();
    const auto image = [&](std::string_view file) { return art_ ? art_->picture(file) : render::ImageRef{}; };
    const render::ImageRef title_art = image("hud/menu-title.webp"), type = image("hud/menu-type.webp"), cn = image("hud/menu-cn.webp"),
                           frames = image("hud/menu-frames.webp"), names = image("hud/names.webp"), fill = image("hud/fill.webp");
    const double t = frame_ / 60.0;

    // Behind it all: the home of whom player 1 is on (the study on the title), panning slowly, darkened.
    out.fill_rect({0, 0, picture_w, picture_h}, tone(0x0b0a0e));
    std::string_view home = "study";
    if (phase_ == "fighters" && !c.empty())
        if (const auto h = r.homes.find(c[static_cast<std::size_t>(cursor_[0])]); h != r.homes.end()) home = h->second;
    if (const auto st = stages_.find(home); st != stages_.end()) {
        const p2::Scene scene{.width = picture_w, .height = picture_h, .ground = 470, .stage = &st->second};
        const p2::Camera cam{960 + 380 * hanim::dm::sin(t * 0.12), 0, 0.82};
        painter_.stage_layers(st->second, false, scene, cam, 960, frame_, 1, out);
    }
    {  // the room darkened toward the lacquer, more at the edges
        std::array<render::GradientStop, 3> stops{{{0, tone(0x07060a, 0.62f)}, {0.5f, tone(0x07060a, 0.42f)}, {1, tone(0x07060a, 0.8f)}}};
        render::Paint g;
        g.kind = render::Paint::Kind::gradient;
        g.gradient = {.kind = render::GradientKind::linear, .start = {0, 0}, .end = {0, picture_h}, .stops = stops};
        out.fill_rect({0, 0, picture_w, picture_h}, g);
    }
    // An ink wash drifting across the bottom.
    if (fill.id != 0) {
        out.push_layer(0.18f, render::BlendMode::screen);
        const float drift = static_cast<float>(std::fmod(t * 14, 400.0));
        picture(out, fill, {region::ink_fill.x + drift, region::ink_fill.y, region::ink_fill.width - 400, region::ink_fill.height}, {0, 430, picture_w, 110});
        out.pop_layer();
    }

    if (phase_ == "title") {
        const float lw = 560, lh = lw * region::logo.height / region::logo.width;
        const float breathe = 1 + 0.012f * static_cast<float>(hanim::dm::sin(t * 1.3));
        picture(out, title_art, region::logo, {picture_w / 2 - lw * breathe / 2, 40 + lh * (1 - breathe) / 2, lw * breathe, lh * breathe});
        // The modes: a lacquer bar each, the one under the cursor lit, its Chinese and its word.
        for (std::int32_t m = 0; m < 2; ++m) {
            const bool lit = m == item_;
            const Rect& bar_src = lit ? region::bar_lit : region::bar;
            const float bw = 430, bh = bw * bar_src.height / bar_src.width;
            const float x = picture_w / 2 - bw / 2 + (lit ? 0.0f : 12.0f), y = 318 + static_cast<float>(m) * 74;
            picture(out, frames, bar_src, {x, y, bw, bh}, lit ? 1.0f : 0.75f);
            const Rect& zh = m == 0 ? region::duizhan : region::xunlian;
            const Rect& en = m == 0 ? region::versus : region::training;
            const float zh_h = 38, en_h = 34;
            shape(out, cn, zh, {x + 70, y + bh / 2 - zh_h / 2, wide(zh, zh_h), zh_h}, lit ? tone(0xf6ecd6) : tone(0xb9ad94));
            picture(out, type, en, {x + 70 + wide(zh, zh_h) + 18, y + bh / 2 - en_h / 2, wide(en, en_h), en_h}, lit ? 1.0f : 0.6f);
        }
    } else {
        picture(out, type, region::select, {picture_w / 2 - 210, 12, 420, 420 * region::select.height / region::select.width});
        // The roster: three across, two down; each tile its fighter's face (or their name), the players' cursors lit.
        constexpr float tw = 104, th = tw * 217 / 245, gap = 10;
        const float gx = picture_w / 2 - (3 * tw + 2 * gap) / 2, gy = 92;
        for (std::size_t i = 0; i < c.size(); ++i) {
            const float x = gx + static_cast<float>(i % 3) * (tw + gap), y = gy + static_cast<float>(i / 3) * (th + gap);
            const Rect inner{x + tw * 0.12f, y + th * 0.1f, tw * 0.76f, th * 0.8f};
            out.fill_rect(inner, tone(0x120e14, 0.92f));
            const p2::Look& look = looks_[2 * i];
            const auto head = std::ranges::find(look.parts, std::string_view("head"), &p2::Part::name);
            out.push_clip(inner);
            if (head != look.parts.end() && head->shape == p2::Shape::image && head->picture >= 0) {
                const p2::Picture& pic = look.pictures[static_cast<std::size_t>(head->picture)];
                const float h = inner.height * 1.15f, w = head->source.width * h / head->source.height;
                picture(out, pic.ref, head->source, {inner.x + inner.width / 2 - w / 2, inner.y + 2, w, h});
            } else if (const auto n = std::ranges::find(region::name, std::string_view(c[i]), &std::pair<std::string_view, Rect>::first); n != region::name.end()) {
                const float h = inner.height * 0.8f, w = wide(n->second, h);
                shape(out, names, n->second, {inner.x + inner.width / 2 - w / 2, inner.y + inner.height / 2 - h / 2, w, h}, tone(0xe8dcc4));
            }
            out.pop_clip();
            const bool on = static_cast<std::int32_t>(i) == cursor_[0] || static_cast<std::int32_t>(i) == cursor_[1];
            picture(out, frames, on ? region::tile_lit : region::tile, {x, y, tw, th});
            for (std::int32_t p = 0; p < 2; ++p)
                if (static_cast<std::int32_t>(i) == cursor_[static_cast<std::size_t>(p)]) {
                    const Rect& badge = p == 0 ? region::p1 : region::p2;
                    const float bh = 22, bw = wide(badge, bh);
                    picture(out, type, badge, {p == 0 ? x - 6 : x + tw - bw + 6, y - 12, bw, bh});
                }
        }
        // Each side's fighter in a tall banner: their idle, lit, facing the other; their name under it.
        for (std::int32_t p = 0; p < 2; ++p) {
            const auto i = static_cast<std::size_t>(cursor_[static_cast<std::size_t>(p)]);
            if (i >= c.size()) continue;
            const bool left = p == 0;
            const float bh = 430, bw = bh * region::banner.width / region::banner.height;
            const float bx = left ? 26 : picture_w - 26 - bw, by = 70;
            render::Path window;  // the banner's slanted inside
            const float lean = bw * 0.16f, inset = bw * 0.1f;
            window.move_to({bx + inset + lean, by + bh * 0.06f})
                .line_to({bx + bw - inset, by + bh * 0.06f})
                .line_to({bx + bw - inset - lean, by + bh * 0.94f})
                .line_to({bx + inset, by + bh * 0.94f})
                .close();
            out.fill_path(window, render::FillRule::nonzero, render::Paint::solid(tone(left ? 0x2a1014 : 0x0f2230, 0.82f)));
            out.push_clip(window);
            const std::size_t fighter = fighter_index(c[i]);
            const p2::Look& look = looks_[2 * i + static_cast<std::size_t>(p)];
            gongfu::FighterState st;
            st.facing = left ? 1 : -1;
            const p2::Scene scene{.width = picture_w, .height = picture_h, .ground = by + bh * 0.9f};
            const float zoom = 1.75f;
            const float centre = bx + bw / 2;
            const p2::Camera cam{(picture_w / 2 - centre) / zoom, 0, zoom};
            painter_.body(look, r.fighters[fighter], st, frame_, scene, cam, out);
            out.pop_clip();
            picture(out, frames, region::banner, {bx, by, bw, bh});
            if (const auto n = std::ranges::find(region::name, std::string_view(c[i]), &std::pair<std::string_view, Rect>::first); n != region::name.end()) {
                // On a slanted lacquer plate across the banner's foot, toward the middle.
                const float h = 58, w = wide(n->second, h);
                const float nx = left ? bx + bw - w * 0.35f : bx - w * 0.65f, ny = by + bh - h - 6;
                const float pw = w + 30, lean = 10;
                render::Path plate;
                plate.move_to({nx - 15 + lean, ny}).line_to({nx - 15 + pw + lean, ny}).line_to({nx - 15 + pw - lean, ny + h}).line_to({nx - 15 - lean, ny + h}).close();
                out.fill_path(plate, render::FillRule::nonzero, render::Paint::solid(tone(0x07080b, 0.9f)));
                out.stroke_path(plate, {.width = 1.5f}, render::Paint::solid(tone(0xc9a24a, 0.9f)));
                shape(out, names, n->second, {nx, ny, w, h}, tone(0xf6ecd6));
            }
            if (ready_[static_cast<std::size_t>(p)]) {  // stamped READY across the banner
                const float rh = 46, rw = wide(region::ready, rh);
                picture(out, type, region::ready, {bx + bw / 2 - rw / 2, by + bh * 0.42f, rw, rh});
            }
        }
        // VS between them; struck larger as both are ready.
        float vs = 120;
        if (both_ready_ >= 0) vs *= 1 + 0.25f * static_cast<float>(hanim::ease::out_back(std::min(1.0, (frame_ - both_ready_) / 10.0)));
        picture(out, type, region::vs, {picture_w / 2 - wide(region::vs, vs) / 2, 330 + (120 - vs) / 2, wide(region::vs, vs), vs});
    }

    // Into the node's box: scaled to fit, centred, letterboxed.
    const float s = std::min(ctx.size.width / picture_w, ctx.size.height / picture_h);
    const float ox = (ctx.size.width - picture_w * s) / 2, oy = (ctx.size.height - picture_h * s) / 2;
    ctx.list.fill_rect({0, 0, ctx.size.width, ctx.size.height}, tone(0x0b0d12));
    ctx.list.push_clip({0, 0, ctx.size.width, ctx.size.height});
    ctx.list.push_transform(Affine::translate(ox, oy) * Affine::scale(s, s));
    ctx.list.append(out);
    ctx.list.pop_transform();
    ctx.list.pop_clip();
}

}  // namespace omui::fighting
