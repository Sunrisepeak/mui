// What the field draws: under its bricks, the plates, shutters and the
// circuits' wires; over them, in the simulation's units under the camera,
// what the machine is doing (kickers' arrows, capacitors' charge and fuse,
// the core's glow and cracks, hidden caches when found or near), the beams,
// sparks, capsules, the aim's forecast, a rewound shot's ghost, trails, balls
// squashed by their bounces, paddles by their returns, the copilot's word
// and intent; the dark room's darkness lit round the balls; then the HUD on
// the screen: score, balls, sparks, the chain, a hint, the room's end, and
// the inspector. Gameplay's colours are the same under every style.
module openmui.showcase.widget_breakout;

import std;
import openmui.hash;
import openmui;
import openmui.showcase.widget_breakout.sim;

namespace omui::breakout {

namespace {

Color hex(std::uint32_t c, float a = 1) { return Color::hex(c, a); }
constexpr std::uint32_t rim = 0x10131c;  // round the ball and the paddles: readable on a light style too
render::Paint solid(Color c) { return render::Paint::solid(c); }
Rect rect(const mgame::Box& b) { return {static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)}; }
Point mid(const mgame::Box& b) { return {static_cast<float>(b.x + b.w / 2), static_cast<float>(b.y + b.h / 2)}; }
Point point(mgame::V2 v) { return {static_cast<float>(v.x), static_cast<float>(v.y)}; }
Rect grow(Rect r, float by) { return {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by}; }
// 0 … 1 … 0 over `period` frames: a pulse that is the same every run.
float pulse(std::int64_t frame, std::int64_t period) {
    const std::int64_t p = frame % period;
    return static_cast<float>(p < period / 2 ? p : period - p) / static_cast<float>(period / 2);
}
std::uint64_t hash_text(std::string_view text, float size, Color c, float width) {
    omui::hash::Fnv1a h;
    h.text(text);
    for (const float f : {size, c.r, c.g, c.b, c.a, width}) h.word(std::bit_cast<std::uint32_t>(f));
    return h.value();
}
const char* gift_letter(Gift g) {
    switch (g) {
        case Gift::wide: return "W";
        case Gift::multi: return "M";
        case Gift::slow: return "S";
        case Gift::pierce: return "D";
        case Gift::magnet: return "U";
        case Gift::none: break;
    }
    return "";
}
Color gift_color(Gift g) {
    switch (g) {
        case Gift::wide: return hex(ink::wide);
        case Gift::multi: return hex(0xffd54f);
        case Gift::slow: return hex(0xb39ddb);
        case Gift::pierce: return hex(ink::pierce);
        case Gift::magnet: return hex(ink::magnet);
        case Gift::none: break;
    }
    return hex(0xffffff);
}
// The circuits: from each switch, one wire to each group of what is on it
// (things side by side, a shield of four shutters, are one group), square
// cornered: up (or down) from the switch's edge to just short of the group,
// across, and into the middle of the group's near side.
void draw_wires(render::DisplayList& out, render::Path& path, const State& s, Color colour, float width) {
    s.world.each<Body, Brick>([&](mgame::Entity, const Body& sb, const Brick& sw) {
        if (sw.kind != Kind::toggle || sw.wire == 0) return;
        std::array<Rect, 8> groups{};
        std::size_t n = 0;
        s.world.each<Body, Brick>([&](mgame::Entity, const Body& tb, const Brick& t) {
            if (t.wire != sw.wire || t.kind == Kind::toggle) return;
            const Rect r = rect(tb.box);
            for (std::size_t k = 0; k < n; ++k) {
                const Rect& g = groups[k];
                if (r.x < g.x + g.width + 24 && r.x + r.width + 24 > g.x && r.y < g.y + g.height + 24 && r.y + r.height + 24 > g.y) {
                    const float x0 = std::min(g.x, r.x), y0 = std::min(g.y, r.y);
                    groups[k] = {x0, y0, std::max(g.x + g.width, r.x + r.width) - x0, std::max(g.y + g.height, r.y + r.height) - y0};
                    return;
                }
            }
            if (n < groups.size()) groups[n++] = r;
        });
        // Groups that grew into each other are one.
        for (bool merged = true; merged;) {
            merged = false;
            for (std::size_t a = 0; a < n && !merged; ++a)
                for (std::size_t b = a + 1; b < n && !merged; ++b) {
                    const Rect& g = groups[a];
                    const Rect& r = groups[b];
                    if (r.x < g.x + g.width + 24 && r.x + r.width + 24 > g.x && r.y < g.y + g.height + 24 && r.y + r.height + 24 > g.y) {
                        const float x0 = std::min(g.x, r.x), y0 = std::min(g.y, r.y);
                        groups[a] = {x0, y0, std::max(g.x + g.width, r.x + r.width) - x0, std::max(g.y + g.height, r.y + r.height) - y0};
                        groups[b] = groups[--n];
                        merged = true;
                    }
                }
        }
        const Rect from = rect(sb.box);
        const float fx = from.x + from.width / 2;
        for (std::size_t k = 0; k < n; ++k) {
            const Rect& g = groups[k];
            const bool up = g.y + g.height / 2 < from.y;
            const Point start{fx, up ? from.y : from.y + from.height};
            path.clear();
            path.move_to(start);
            const Point end{fx > g.x && fx < g.x + g.width ? fx : g.x + g.width / 2, up ? g.y + g.height : g.y};
            const float bus = up ? end.y + 14 : end.y - 14;
            if (end.x != fx) path.line_to({fx, bus}).line_to({end.x, bus});
            path.line_to(end);
            out.stroke_path(path, {.width = width, .cap = render::LineCap::round, .join = render::LineJoin::round}, solid(colour));
            out.fill_round_rect({end.x - 4, end.y - 4, 8, 8}, CornerRadii::all(4), solid(colour));
            out.fill_round_rect({start.x - 4, start.y - 4, 8, 8}, CornerRadii::all(4), solid(sw.on ? hex(ink::live) : colour));
        }
    });
}

const char* role_of(Kind k) {
    switch (k) {
        case Kind::button: return "button";
        case Kind::toggle: return "switch";
        case Kind::slider: return "slider";
        case Kind::card: return "region";
        case Kind::hidden: return "button (hidden)";
        case Kind::wall: return "plate";
        case Kind::gate: return "progress";
        case Kind::shutter: return "plate";
        case Kind::kicker: return "button";
        case Kind::core: return "button";
    }
    return "";
}

}  // namespace

void Field::paint(PaintContext& ctx) const {
    const Theme& theme = services().theme();
    ctx.list.fill_rect({0, 0, ctx.size.width, ctx.size.height}, theme.color("background", hex(0x0d111c)));
    const State* s = state();
    const RoomDef& def = room(props.room);
    if (!s || def.canvas) return;  // a canvas room: its canvas draws the room, the overlays come after it
    auto& out = ctx.list;
    out.push_transform(Affine::scale(ctx.size.width / static_cast<float>(field_w), ctx.size.height / static_cast<float>(field_h)));
    const Color plate = theme.color("surface_variant", hex(0x1b2235));
    draw_wires(out, path_, *s, hex(ink::wire), 3);
    // Plates: walls, shutters (hatched, closed; a dashed track, open), and under the components that are not buttons.
    s->world.each<Body, Brick>([&](mgame::Entity, const Body& body, const Brick& b) {
        const Rect r = rect(body.box);
        switch (b.kind) {
            case Kind::wall:
                out.fill_round_rect(r, CornerRadii::all(4), solid(plate));
                out.fill_rect({r.x, r.y, r.width, 2}, theme.color("outline", hex(0x3a4660)).with_alpha(0.8f));
                break;
            case Kind::shutter: {
                const float lit = b.id < fx_.flash.size() ? fx_.flash[b.id] : 0;
                const float shown = b.on == 0 ? 1 - lit : lit;  // sliding shut, or away
                if (b.on != 0) out.stroke_round_rect(r, CornerRadii::all(3), 1.5f, solid(hex(ink::shutter, 0.35f)));
                if (shown > 0) {
                    const bool across = r.width > r.height;
                    const Rect part = across ? Rect{r.x + r.width * (1 - shown) / 2, r.y, r.width * shown, r.height}
                                             : Rect{r.x, r.y + r.height * (1 - shown) / 2, r.width, r.height * shown};
                    out.fill_round_rect(part, CornerRadii::all(3), solid(hex(ink::shutter, 0.9f)));
                    out.push_clip(part);
                    path_.clear();
                    for (float x = r.x - r.height; x < r.x + r.width; x += 10) path_.move_to({x, r.y + r.height}).line_to({x + r.height, r.y});
                    out.stroke_path(path_, {.width = 2}, solid(hex(0x1b2235, 0.55f)));
                    out.pop_clip();
                }
                break;
            }
            case Kind::toggle:
            case Kind::slider:
            case Kind::card:
            case Kind::gate: out.fill_round_rect(r, CornerRadii::all(10), solid(plate)); break;
            case Kind::button:
            case Kind::hidden:
            case Kind::kicker:
            case Kind::core: break;
        }
    });
    out.pop_transform();
}

void Field::paint_over(PaintContext& ctx) const {
    auto& out = ctx.list;
    auto& fonts = services().fonts();
    const Theme& theme = services().theme();
    const Color text_ink = theme.color("text", hex(0xe8eaf6));
    const Color on_dark = hex(0xe8eaf6);  // words on the dark panels the HUD lays down (the hint, the inspector): light in every room's theme
    // On a light style the bright cues (gold, cyan) are drawn darker, so they read as well as on a dark one.
    const Color ground = theme.color("background", hex(0x0d111c));
    const bool light = 0.2126f * ground.r + 0.7152f * ground.g + 0.0722f * ground.b > 0.5f;
    auto cue = [light](std::uint32_t c, float a = 1) {
        const Color k = hex(c, a);
        return light ? Color{k.r * 0.62f, k.g * 0.62f, k.b * 0.62f, k.a} : k;
    };
    if (words_.shaped.size() > 512) words_.shaped.clear();  // changing text (a clock) is shaped again, not kept for ever
    // Text shaped once (`width`: wrapped to it).
    auto label = [&](std::string_view text, float size, Color color, float width = 0, float weight = 600) -> const typeset::Paragraph& {
        const std::uint64_t key = hash_text(text, size, color, width) ^ static_cast<std::uint64_t>(weight);
        auto it = words_.shaped.find(key);
        if (it == words_.shaped.end()) {
            typeset::ParagraphStyle ps;
            ps.style.size = size;
            ps.style.weight = weight;
            ps.style.color = color;
            ps.wrap = width > 0;
            typeset::Paragraph p(fonts, std::string(text), std::move(ps));
            p.layout(width > 0 ? width : std::numeric_limits<float>::infinity());
            it = words_.shaped.emplace(key, std::move(p)).first;
        }
        return it->second;
    };
    auto centred = [&](std::string_view text, float size, Color color, Point at) {
        const auto& p = label(text, size, color);
        p.paint(out, {at.x - p.size().width / 2, at.y - p.size().height / 2});
    };
    const State* sp = state();
    if (!sp) return;
    const State& s = *sp;
    const RoomDef& def = room(props.room);
    const float sx = ctx.size.width / static_cast<float>(field_w), sy = ctx.size.height / static_cast<float>(field_h);
    const float kx = sx * static_cast<float>(s.zoom), ky = sy * static_cast<float>(s.zoom);
    // A restrained shake: a few pixels, dying away, the same every run.
    const float shake = fx_.shake;
    const Point jolt{shake * (pulse(s.frame * 7 + 3, 10) - 0.5f), shake * (pulse(s.frame * 5 + 1, 14) - 0.5f)};
    // The camera: the field's bottom on the screen's, zoomed out to keep the balls in sight.
    const Affine camera = Affine::translate(ctx.size.width / 2 - static_cast<float>(field_w / 2) * kx + jolt.x,
                                            ctx.size.height / 2 - static_cast<float>(s.height - field_h / s.zoom / 2) * ky + jolt.y) *
                          Affine::scale(kx, ky);
    const float px = 1 / kx;  // a screen pixel, in the field's units
    out.push_transform(camera);
    if (s.zoom < 1) {  // the field's edges, once more than a screen of it shows
        out.stroke_round_rect({0, 0, static_cast<float>(field_w), static_cast<float>(s.height)}, CornerRadii::all(4), 1.5f * px,
                              solid(text_ink.with_alpha(0.35f * static_cast<float>(std::min(1.0, (1 - s.zoom) * 8)))));
    }
    const bool scan = scanning();
    // A canvas room's circuits (the canvas covers what is drawn under it); any room's while the X-ray shows them.
    if (def.canvas || scan) draw_wires(out, path_, s, hex(scan ? ink::live : ink::wire, scan ? 0.8f : 1), 3);
    // The machine at work, brick by brick.
    s.world.each<Body, Brick>([&](mgame::Entity, const Body& body, const Brick& b) {
        const Rect r = rect(body.box);
        const Point c = mid(body.box);
        const float lit = b.id < fx_.flash.size() ? fx_.flash[b.id] : 0;
        switch (b.kind) {
            case Kind::kicker: {  // its arrow: where it sends the ball
                const mgame::V2 d = kick_direction(b);
                const Point tip{c.x + static_cast<float>(d.x) * 26, c.y + static_cast<float>(d.y) * 26 - 8};
                const Point base{c.x - static_cast<float>(d.x) * 6, c.y - static_cast<float>(d.y) * 6 - 8};
                const Point side{static_cast<float>(-d.y) * 8, static_cast<float>(d.x) * 8};
                path_.clear();
                path_.move_to(base).line_to(tip).move_to({tip.x - static_cast<float>(d.x) * 10 + side.x, tip.y - static_cast<float>(d.y) * 10 + side.y}).line_to(tip)
                    .line_to({tip.x - static_cast<float>(d.x) * 10 - side.x, tip.y - static_cast<float>(d.y) * 10 - side.y});
                out.stroke_path(path_, {.width = 4, .cap = render::LineCap::round, .join = render::LineJoin::round}, solid(hex(ink::kicker, 0.75f + 0.25f * lit)));
                if (lit > 0) out.stroke_round_rect(grow(r, 3 + 6 * (1 - lit)), CornerRadii::all(8), 2, solid(hex(ink::kicker, lit)));
                break;
            }
            case Kind::slider: {  // a capacitor: its charge as pips; full, its fuse glowing; spent, cooling
                const float pip = 10, gap = 4, w = static_cast<float>(b.durability) * (pip + gap) - gap;
                for (std::int32_t k = 0; k < b.durability; ++k)
                    out.fill_round_rect({c.x - w / 2 + static_cast<float>(k) * (pip + gap), r.y - 9, pip, 5}, CornerRadii::all(2),
                                        solid(hex(ink::charge, k < b.charge ? 1.0f : 0.22f)));
                if (b.fuse > 0) out.stroke_round_rect(grow(r, 3), CornerRadii::all(10), 3, solid(hex(ink::charge, 0.5f + 0.5f * pulse(s.frame, 6))));
                if (b.fuse < 0) out.fill_round_rect(r, CornerRadii::all(10), solid(hex(0x000000, 0.25f)));
                break;
            }
            case Kind::core: {  // the goal: a glow that breathes, cracks for each hit taken
                const float breathe = pulse(s.frame, 90);
                out.stroke_round_rect(grow(r, 4 + 3 * breathe), CornerRadii::all(12), 2.5f, solid(hex(ink::core, 0.35f + 0.35f * breathe + 0.3f * lit)));
                const std::int32_t full = b.id < def.bricks.size() ? def.bricks[b.id].durability : b.durability;
                path_.clear();
                for (std::int32_t k = 0; k < full - b.durability; ++k) {
                    const float x = r.x + r.width * (0.2f + 0.6f * static_cast<float>((k * 37) % 10) / 10);
                    path_.move_to({x, r.y}).line_to({x + 6, r.y + r.height * 0.45f}).line_to({x - 4, r.y + r.height * 0.7f}).line_to({x + 3, r.y + r.height});
                }
                if (!path_.empty()) out.stroke_path(path_, {.width = 2, .cap = render::LineCap::round}, solid(hex(0x1a0505, 0.7f)));
                if (lit > 0) out.fill_round_rect(r, CornerRadii::all(8), solid(hex(0xffffff, 0.55f * lit)));
                break;
            }
            case Kind::hidden: {  // a cache: found (or the X-ray on), outlined; a ball near, it glints first
                double near = 1e9;
                s.world.each<Ball>([&](mgame::Entity, const Ball& ball) {
                    near = std::min(near, mgame::length(ball.at - mgame::V2{body.box.x + body.box.w / 2, body.box.y + body.box.h / 2}));
                });
                const bool found = b.id < discovered_.size() && discovered_[b.id];
                const float glint = near < 130 ? static_cast<float>(1 - near / 130) * pulse(s.frame, 16) : 0;
                if (scan || found || glint > 0)
                    out.stroke_round_rect(r, CornerRadii::all(6), 2, solid(hex(ink::cache, scan ? 0.95f : found ? 0.55f : glint)));
                if (scan) out.fill_round_rect(r, CornerRadii::all(6), solid(hex(ink::cache, 0.18f)));
                break;
            }
            case Kind::toggle:
                if (lit > 0) out.stroke_round_rect(grow(r, 2 + 6 * (1 - lit)), CornerRadii::all(12), 2, solid(hex(ink::live, lit)));
                break;
            case Kind::gate:  // its timer: after the darkness (it is what to act on)
                if (gates_open(s)) out.stroke_round_rect(r, CornerRadii::all(6), 2, solid(hex(ink::live, 0.6f + 0.4f * pulse(s.frame, 20))));
                break;
            case Kind::button:
            case Kind::card:
                if (lit > 0) out.fill_round_rect(r, CornerRadii::all(8), solid(hex(0xffffff, 0.35f * lit)));
                break;
            case Kind::wall:
            case Kind::shutter: break;
        }
    });
    // The dark room: darkness, lit round each ball and the paddle (cut out of it).
    if (dark() && !s.over) {
        const Rect all{-20, -20, static_cast<float>(field_w) + 40, static_cast<float>(s.height) + 40};
        out.push_layer(1);
        out.fill_rect(all, hex(0x04060b, 0.95f));
        out.push_layer(1, render::BlendMode::erase);
        auto light = [&](Point at, float radius) {
            const std::array<render::GradientStop, 3> stops{{{0, hex(0xffffff, 1)}, {0.55f, hex(0xffffff, 0.92f)}, {1, hex(0xffffff, 0)}}};
            render::Paint p;
            p.kind = render::Paint::Kind::gradient;
            p.gradient = {render::GradientKind::radial, at, at, radius, render::Spread::pad, stops};
            out.fill_rect({at.x - radius, at.y - radius, 2 * radius, 2 * radius}, p);
        };
        s.world.each<Ball>([&](mgame::Entity, const Ball& b) { light(point(b.at), 220); });
        light({static_cast<float>(s.paddles[0].x), static_cast<float>(s.paddles[0].y)}, 160);
        for (const Fx::Beam& b : fx_.beams)
            if (b.life > 0) light({(b.from.x + b.to.x) / 2, (b.from.y + b.to.y) / 2}, 420 * b.life);
        out.pop_layer();
        out.pop_layer();
        // Its switch glows a little in the dark: the lights can be earned.
        s.world.each<Body, Brick>([&](mgame::Entity, const Body& body, const Brick& b) {
            if (b.kind != Kind::toggle) return;
            const Rect r = rect(body.box);
            out.stroke_round_rect(grow(r, 2), CornerRadii::all(12), 1.5f, solid(hex(ink::live, 0.25f + 0.2f * pulse(s.frame, 120))));
            out.fill_round_rect({r.x + 10, r.y + r.height / 2 - 4, 8, 8}, CornerRadii::all(4), solid(hex(ink::live, 0.5f + 0.4f * pulse(s.frame, 120))));
        });
        if (scan)  // the X-ray sees through it
            s.world.each<Body, Brick>([&](mgame::Entity, const Body& body, const Brick& b) {
                if (b.kind == Kind::hidden || b.kind == Kind::core || b.kind == Kind::gate)
                    out.stroke_round_rect(rect(body.box), CornerRadii::all(6), 2, solid(hex(b.kind == Kind::hidden ? ink::cache : ink::live, 0.9f)));
            });
    }
    // What to act on, even in the dark: a gate's timer, the core's outline.
    s.world.each<Body, Brick>([&](mgame::Entity, const Body& body, const Brick& b) {
        const Rect r = rect(body.box);
        if (b.kind == Kind::core && dark() && !s.over) out.stroke_round_rect(grow(r, 4), CornerRadii::all(12), 1.5f, solid(hex(ink::core, 0.45f)));
        // In the dark, every brick that is not hidden is still a faint outline: where the ball can go is never a guess.
        if (dark() && !s.over && b.kind != Kind::hidden && b.kind != Kind::core && b.kind != Kind::gate)
            out.stroke_round_rect(r, CornerRadii::all(8), 1.0f, solid(text_ink.with_alpha(0.22f)));
        // A slider's name over it (the component draws a track alone): what the beam it charges is called.
        if (b.kind == Kind::slider && b.id < def.bricks.size())
            centred(def.bricks[b.id].label, 12, text_ink.with_alpha(dark() && !scanning() ? 0.45f : 0.8f), {r.x + r.width / 2, r.y - 9});
        if (b.kind != Kind::gate) return;
        const bool open = gates_open(s);
        if (dark() && !s.over) out.stroke_round_rect(r, CornerRadii::all(6), 1.5f, solid(hex(ink::live, open ? 0.9f : 0.35f)));
        std::int64_t left = 0;
        if (s.frame < s.gate_from) left = s.gate_from - s.frame;
        else if (open) left = s.gate_open - (s.frame - s.gate_from) % s.gate_period;
        else left = s.gate_period - (s.frame - s.gate_from) % s.gate_period;
        std::array<char, 32> t{};
        const double secs = static_cast<double>(left) / steps_per_second;
        const auto e = open ? std::format_to_n(t.data(), t.size() - 1, "open · {:.1f} s", secs) : std::format_to_n(t.data(), t.size() - 1, "opens in {:.1f} s", secs);
        centred({t.data(), e.out}, 13, open ? cue(ink::live) : text_ink.with_alpha(0.8f), {r.x + r.width / 2, r.y - 11});
    });
    // Beams: a white core in a glow, fading; thin enough that what it strikes shows.
    for (const Fx::Beam& b : fx_.beams) {
        if (b.life <= 0) continue;
        const bool across = b.from.y == b.to.y;
        const float t = (5 + 3 * static_cast<float>(b.n)) * b.life;
        auto band = [&](float w) {
            return across ? Rect{std::min(b.from.x, b.to.x), b.from.y - w / 2, std::abs(b.to.x - b.from.x), w}
                          : Rect{b.from.x - w / 2, std::min(b.from.y, b.to.y), w, std::abs(b.to.y - b.from.y)};
        };
        out.fill_round_rect(band(t * 2.2f), CornerRadii::all(t), solid(hex(ink::beam, 0.18f * b.life)));
        out.fill_round_rect(band(t), CornerRadii::all(t / 2), solid(hex(ink::beam, 0.8f * b.life)));
        out.fill_round_rect(band(t * 0.35f), CornerRadii::all(t / 4), solid(hex(0xffffff, b.life)));
    }
    // A circuit's pulse, running from the switch along its wire.
    for (const Fx::Pulse& p : fx_.pulses) {
        if (p.life <= 0) continue;
        const float u = 1 - p.life;
        const float first = std::abs(p.to.y - p.from.y), second = std::abs(p.to.x - p.from.x), along = u * (first + second);
        const Point at = along < first ? Point{p.from.x, p.from.y + (p.to.y > p.from.y ? along : -along)}
                                       : Point{p.from.x + (p.to.x > p.from.x ? along - first : first - along), p.to.y};
        out.fill_round_rect({at.x - 7, at.y - 7, 14, 14}, CornerRadii::all(7), solid(hex(ink::live, 0.35f)));
        out.fill_round_rect({at.x - 4, at.y - 4, 8, 8}, CornerRadii::all(4), solid(hex(0xffffff, 0.95f)));
    }
    // Sparks.
    for (const Fx::Particle& p : fx_.particles) {
        if (p.life <= 0) continue;
        out.fill_round_rect({p.x - p.size / 2, p.y - p.size / 2, p.size, p.size}, CornerRadii::all(p.size / 2), solid(p.color.with_alpha(std::min(1.0f, p.life * 1.4f))));
    }
    // Capsules: a pill and its letter.
    s.world.each<Capsule>([&](mgame::Entity, const Capsule& k) {
        const Rect r{static_cast<float>(k.at.x) - 18, static_cast<float>(k.at.y) - 9, 36, 18};
        out.fill_round_rect(grow(r, 3), CornerRadii::all(12), solid(gift_color(k.gift).with_alpha(0.3f + 0.2f * pulse(s.frame, 30))));
        out.fill_round_rect(r, CornerRadii::all(9), solid(gift_color(k.gift)));
        centred(gift_letter(k.gift), 12, hex(0x10131c), {r.x + r.width / 2, r.y + r.height / 2});
    });
    // The aim's forecast: where a launch now goes, to its second turn.
    s.world.each<Ball>([&](mgame::Entity, const Ball& b) {
        if (b.on != 1 || s.over) return;
        std::array<mgame::V2, 4> turns{};
        const std::size_t n = trace(s, b.at, aim_direction(s), b.radius, turns, 900);
        float travelled = 0;
        for (std::size_t k = 1; k < n; ++k) {
            const Point a = point(turns[k - 1]), z = point(turns[k]);
            const float len = std::sqrt((z.x - a.x) * (z.x - a.x) + (z.y - a.y) * (z.y - a.y));
            for (float d = 18 - std::fmod(travelled, 18.0f); d < len; d += 18) {
                const float f = 1 - (travelled + d) / 900;
                if (f <= 0) break;
                const Point at{a.x + (z.x - a.x) * d / len, a.y + (z.y - a.y) * d / len};
                out.fill_round_rect({at.x - 3, at.y - 3, 6, 6}, CornerRadii::all(3), solid(hex(ink::ball, 0.75f * f)));
            }
            travelled += len;
        }
    });
    // A rewound shot's ghost: the path it took before, so the next can differ.
    if (fx_.ghost_life > 0)
        for (std::size_t k = 0; k < fx_.ghost_count; ++k)
            out.fill_round_rect({fx_.ghost[k].x - 2.5f, fx_.ghost[k].y - 2.5f, 5, 5}, CornerRadii::all(2.5f), solid(hex(0x9fa8ff, 0.45f * fx_.ghost_life)));
    // Trails and balls: squashed along the bounce for a few frames; piercing, a diamond of its own colour. Never smaller than 6 px on screen.
    s.world.each<Ball>([&](mgame::Entity e, const Ball& b) {
        const Fx::Trail* t = nullptr;
        for (const Fx::Trail& c : fx_.trails)
            if (c.ball == e.index && c.generation == e.generation) t = &c;
        const float radius = std::max(static_cast<float>(b.radius), 6 * px);
        const Color colour = hex(b.pierce > 0 ? ink::pierce : ink::ball);
        if (t)
            for (std::uint32_t k = 1; k < t->count; ++k) {
                const float f = 1 - static_cast<float>(k) / static_cast<float>(t->at.size());
                const float r = radius * (0.35f + 0.55f * f);
                out.fill_round_rect({t->at[k].x - r, t->at[k].y - r, 2 * r, 2 * r}, CornerRadii::all(r), solid(colour.with_alpha(0.42f * f)));
            }
        const float q = t ? t->squash : 0;
        const Point dir = t ? t->dir : Point{0, -1};
        out.push_transform(Affine::translate(static_cast<float>(b.at.x), static_cast<float>(b.at.y)) * Affine::rotation(dir.x, dir.y) *
                           Affine::scale(1 - 0.3f * q, 1 + 0.3f * q));
        if (b.pierce > 0) {
            path_.clear();
            path_.move_to({radius * 1.5f, 0}).line_to({0, radius}).line_to({-radius * 1.1f, 0}).line_to({0, -radius}).close();
            out.stroke_path(path_, {.width = 3 * px, .join = render::LineJoin::round}, solid(hex(rim, 0.7f)));
            out.fill_path(path_, render::FillRule::nonzero, solid(colour));
        } else {
            out.fill_round_rect({-radius - 1.5f * px, -radius - 1.5f * px, 2 * radius + 3 * px, 2 * radius + 3 * px}, CornerRadii::all(radius + 1.5f * px), solid(hex(rim, 0.7f)));
            out.fill_round_rect({-radius, -radius, 2 * radius, 2 * radius}, CornerRadii::all(radius), solid(colour));
        }
        out.pop_transform();
    });
    // Paddles: squashed by a return; wide, blue; holding a magnet, violet-rimmed.
    for (std::size_t i = 0; i < s.paddles.size(); ++i) {
        const Paddle& p = s.paddles[i];
        if (!p.active) continue;
        const float q = fx_.paddle_squash[i];
        const float w = static_cast<float>(2 * p.half) + 10 * q, h = 16 * (1 - 0.35f * q);
        const Rect r{static_cast<float>(p.x) - w / 2, static_cast<float>(p.y) - h / 2 + (i == 0 ? 3 * q : -3 * q), w, h};
        if (p.magnet > 0) out.stroke_round_rect(grow(r, 3), CornerRadii::all(11), 2.5f, solid(hex(ink::magnet, 0.6f + 0.4f * pulse(s.frame, 40))));
        out.fill_round_rect(grow(r, 1.5f * px), CornerRadii::all(h / 2 + 1.5f * px), solid(hex(rim, 0.7f)));
        out.fill_round_rect(r, CornerRadii::all(h / 2), solid(hex(p.wide > 0 ? ink::wide : i == 1 ? ink::live : ink::paddle)));
        if (i == 1) centred("COPILOT", 10, cue(ink::live, 0.9f), {static_cast<float>(p.x), static_cast<float>(p.y) + 18});
    }
    // The copilot: where it means to send the ball, and its word.
    bool coming = false;  // a ball rising near the top paddle: it is about to act
    s.world.each<Ball>([&](mgame::Entity, const Ball& b) { coming = coming || (b.on == 0 && b.velocity.y < 0 && b.at.y < 260); });
    if (s.paddles[1].active && !says_.empty() && (s.frame < says_until_ || coming) && !s.over) {
        const Paddle& top = s.paddles[1];
        const float fade = coming ? 1.0f : std::min(1.0f, static_cast<float>(says_until_ - s.frame) / 30);
        if (intent_) {
            const Point a{static_cast<float>(top.x), static_cast<float>(top.y) + 14}, z{static_cast<float>(*intent_), 330};
            const float len = std::sqrt((z.x - a.x) * (z.x - a.x) + (z.y - a.y) * (z.y - a.y));
            for (float d = 10; d < len; d += 16)
                out.fill_round_rect({a.x + (z.x - a.x) * d / len - 2.5f, a.y + (z.y - a.y) * d / len - 2.5f, 5, 5}, CornerRadii::all(2.5f),
                                    solid(hex(ink::live, 0.6f * fade * (1 - d / len))));
        }
        const auto& said = label(says_, 15, hex(0x0b0e17));
        const float bw = said.size().width + 24, bh = said.size().height + 12;
        const float bx = std::clamp(static_cast<float>(top.x) - bw / 2, 8.0f, static_cast<float>(field_w) - bw - 8), by = static_cast<float>(top.y) + 22;
        out.fill_round_rect({bx, by, bw, bh}, CornerRadii::all(bh / 2), solid(hex(ink::live, 0.92f * fade)));
        said.paint(out, {bx + 12, by + 6});
    }
    // Rings: the shutdown's wave, a banked chain, a capsule caught.
    for (const Fx::Ring& r : fx_.rings) {
        if (r.life <= 0) continue;
        path_.clear();
        path_.add_circle(r.at, r.radius);
        out.stroke_path(path_, {.width = 3 + 6 * r.life}, solid(r.color.with_alpha(0.7f * r.life)));
    }
    // Numbers rising from what happened.
    for (const Fx::Floater& f : fx_.floaters) {
        if (f.life <= 0) continue;
        const Color c = light ? Color{f.color.r * 0.62f, f.color.g * 0.62f, f.color.b * 0.62f, f.color.a} : f.color;
        const auto& p = label(f.view(), f.size, c.with_alpha(std::min(1.0f, f.life * 2)));
        const float x = std::clamp(f.at.x - p.size().width / 2, 8.0f, static_cast<float>(field_w) - p.size().width - 8);
        p.paint(out, {x, f.at.y - p.size().height / 2});
    }
    out.pop_transform();

    // ── the HUD, on the screen ──
    const float W = ctx.size.width, H = ctx.size.height;
    label(std::format("{}", s.score), 26, text_ink).paint(out, {24, 14});
    label(std::format("{} / {}   {} · {}", def.number, room_count, def.name, def.title), 13, text_ink.with_alpha(0.65f), 0, 500).paint(out, {24, 50});
    for (std::int32_t i = 0; i < s.lives; ++i) out.fill_round_rect({W - 30 - 22.0f * static_cast<float>(i), 22, 13, 13}, CornerRadii::all(6.5f), solid(text_ink));
    // Sparks: three diamonds, each a rewind's worth, filling; glowing as a chain is banked.
    if (fx_.banked > 0) out.fill_round_rect({W - 110, 40, 92, 36}, CornerRadii::all(18), solid(cue(ink::spark, 0.35f * fx_.banked)));
    for (int k = 0; k < 3; ++k) {
        const float cx = W - 36 - 30.0f * static_cast<float>(2 - k), cy = 58;
        const float fill = std::clamp(static_cast<float>(s.sparks - k * rewind_cost) / rewind_cost, 0.0f, 1.0f);
        path_.clear();
        path_.move_to({cx, cy - 10}).line_to({cx + 9, cy}).line_to({cx, cy + 10}).line_to({cx - 9, cy}).close();
        out.fill_path(path_, render::FillRule::nonzero, solid(cue(ink::spark, 0.18f)));
        if (fill > 0) {
            out.push_clip(Rect{cx - 10, cy + 10 - 20 * fill, 20, 20 * fill});
            out.fill_path(path_, render::FillRule::nonzero, solid(cue(ink::spark, fill >= 1 ? 1.0f : 0.7f)));
            out.pop_clip();
        }
    }
    {
        const auto& n = label(std::format("{} ◆", std::max(0, s.sparks)), 12, cue(ink::spark, 0.85f));
        n.paint(out, {W - 128 - n.size().width, 51});
    }
    // The chain, as it grows.
    if (s.combo >= 2 && !s.over) {
        const float size = 30 + 12 * fx_.chain_pop;
        centred(std::format("×{}", s.combo), size, cue(ink::spark), {W - 76, 104});
        centred("CHAIN", 10, cue(ink::spark, 0.7f), {W - 76, 132});
    }
    // The duet: alternate returns toward the finisher.
    if (s.paddles[1].active && !s.over) {
        for (std::int32_t k = 0; k < duet_full; ++k)
            out.fill_round_rect({24 + 22.0f * static_cast<float>(k), 86, 14, 6}, CornerRadii::all(3),
                                solid(cue(ink::live, s.finisher ? 0.6f + 0.4f * pulse(s.frame, 12) : k < s.duet ? 0.95f : 0.25f)));
        label(s.finisher ? std::string("DUET — return it now") : std::format("duet · copilot: {}", order_), 11, cue(ink::live, 0.85f)).paint(out, {24, 98});
    }
    // The X-ray: on, or how soon again.
    if (def.dark || scan || (timeline_ && timeline_->frame() < scan_ready_)) {
        const std::int64_t now = timeline_->frame();
        const std::string what = scan ? std::string("X-RAY") : now < scan_ready_ ? std::format("X-ray in {:.1f} s", static_cast<double>(scan_ready_ - now) / steps_per_second)
                                                                                : std::string("X  X-ray ready");
        label(what, 12, hex(ink::cache, scan ? 1.0f : 0.7f)).paint(out, {24, H - 28});
    }
    // A hint, once, when it is needed; fading out.
    if (!hint_.empty() && s.frame < hint_until_ && !s.over) {
        const float a = std::min(1.0f, static_cast<float>(hint_until_ - s.frame) / 60);
        const auto& p = label(hint_, 16, on_dark.with_alpha(0.9f * a));
        out.fill_round_rect({W / 2 - p.size().width / 2 - 16, H - 92, p.size().width + 32, p.size().height + 14}, CornerRadii::all(12), solid(hex(0x0b0e17, 0.7f * a)));
        p.paint(out, {W / 2 - p.size().width / 2, H - 85});
    }
    // Rewinding: the picture tinted, how far back.
    if (rewinding_) {
        out.fill_rect({0, 0, W, H}, hex(0x2a4a8c, 0.16f));
        for (float y = 0; y < H; y += 6) out.fill_rect({0, y, W, 1}, hex(0x9fb8ff, 0.05f));
        const float back = static_cast<float>(rewind_from_ - s.frame) / static_cast<float>(rewind_reach);
        centred(std::format("◀◀  {:.1f} s   −{} ◆", static_cast<double>(rewind_from_ - s.frame) / steps_per_second, rewind_cost), 22, hex(0xc8d6ff), {W / 2, 92});
        out.fill_round_rect({W / 2 - 120, 112, 240, 5}, CornerRadii::all(2.5f), solid(hex(0xc8d6ff, 0.25f)));
        out.fill_round_rect({W / 2 - 120, 112, 240 * std::clamp(back, 0.0f, 1.0f), 5}, CornerRadii::all(2.5f), solid(hex(0xc8d6ff)));
    }
    // A room over: what happened, then what next.
    if (s.over && !choosing_) {
        const float in = std::min(1.0f, static_cast<float>(over_steps_) / 30);
        const bool cleared = s.over == 1, done = cleared && props.room >= room_count;
        const std::string title = !cleared ? std::string("Out of balls") : done ? std::string("The panel is off") : std::string("SHUT DOWN");
        const std::string line = !cleared ? std::format("Room {} of {} · score {} · longest chain {}", props.room, room_count, s.score, std::max(run_.best, s.best))
                                 : done   ? std::format("Run complete · score {} · longest chain {}", s.score, std::max(run_.best, s.best))
                                          : std::format("longest chain {}   ·   {} ◆   ·   score {}", s.best, std::max(0, s.sparks), s.score);
        out.fill_rect({0, 0, W, H}, hex(0x05070d, 0.45f * in));
        centred(title, 54, hex(cleared ? 0xffffff : ink::lost, in), {W / 2, H / 2 - 30});
        centred(line, 16, text_ink.with_alpha(0.85f * in), {W / 2, H / 2 + 22});
        if (!cleared || done || over_steps_ > 60) centred(!cleared || done ? "Enter — again" : "Enter — on", 14, hex(0xffd54f, in), {W / 2, H / 2 + 56});
    }
    // The inspector: each brick's role and id, the hidden ones too, the semantic tree's latest changes, the state, how the room is made.
    if (props.inspector) {
        const auto nodes = bricks();
        const Rect me = services().bounds(node());
        for (std::size_t i = 0; i < nodes.size() && i < def.bricks.size(); ++i) {
            if (!services().get(nodes[i], "visible").as_bool(true)) continue;
            const Rect r = services().bounds(nodes[i]);
            const Rect at{r.x - me.x, r.y - me.y, r.width, r.height};
            const bool hidden = def.bricks[i].kind == Kind::hidden;
            const Color c = hidden ? hex(0xff7043) : hex(0x64ffda);
            out.stroke_round_rect(at, CornerRadii::all(6), hidden ? 2.5f : 1.5f, solid(c));
            const auto& p = label(std::format("{} · {}", role_of(def.bricks[i].kind), brick_id(i)), 10, c);
            p.paint(out, {at.x + 3, at.y - p.size().height - 1});
        }
        const float x = W - 352, w = 330;
        const auto& made = label(def.made, 12, on_dark.with_alpha(0.9f), w, 400);
        const float line = 17, top = 84;
        const float h = made.size().height + line * static_cast<float>(log_size + 4) + 40;
        out.fill_round_rect({x - 12, top - 10, w + 24, h}, CornerRadii::all(10), solid(hex(0x0b0e17, 0.9f)));
        label("INSPECTOR · HOW THIS ROOM IS MADE", 11, hex(0x64ffda, 0.8f)).paint(out, {x, top});
        made.paint(out, {x, top + 20});
        float y = top + 32 + made.size().height;
        label("SEMANTIC TREE · LATEST CHANGES", 11, hex(0x64ffda, 0.8f)).paint(out, {x, y});
        for (std::size_t k = 0; k < log_.size(); ++k) {
            const float alpha = 0.45f + 0.55f * static_cast<float>(k + 1) / static_cast<float>(log_.size());
            label(log_[k], 11, on_dark.with_alpha(alpha), 0, 500).paint(out, {x, y + line * static_cast<float>(k + 1)});
        }
        y += line * static_cast<float>(log_size + 1) + 6;
        label(std::format("frame {} · state {:016x}", s.frame, checksum(s)), 11, on_dark.with_alpha(0.6f), 0, 500).paint(out, {x, y});
        label("Tab — back to the game (paused while you look)", 11, hex(0x64ffda, 0.7f), 0, 500).paint(out, {x, y + line * 2});
        if (!agent_log_.empty()) label(std::format("copilot: {}", agent_log_.back()), 11, hex(ink::live, 0.8f), w, 500).paint(out, {x, y + line});
    }
}

}  // namespace omui::breakout
