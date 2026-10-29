module openmui.showcase.fighting;

import std;
import openmui.core;
import openmui.view;
import gongfu.present2d;
import gongfu.training;
import hanim;

namespace omui::fighting::hud {

namespace {

namespace p2 = gongfu::present2d;
namespace tr = gongfu::training;

Color tone(std::uint32_t hex, float alpha = 1) { return Color::hex(hex, alpha); }

constexpr Color ink = Color::hex(0xf4f1ea);

// The meter's colours, by phase: free, startup, active, recovery, hitstun, blockstun, down, thrown.
constexpr std::array<std::uint32_t, 8> phase_tone{0x3a3f52, 0x4ef27a, 0xf24e5a, 0x4ea1f2, 0xf2c14e, 0xc9a227, 0x8a6fd1, 0xd16fb4};
// The buttons' colours: punches cool, kicks warm, light to heavy.
constexpr std::array<std::uint32_t, 6> button_tone{0x9ad1ff, 0x4ea1f2, 0x2f6fd1, 0xffb08a, 0xf27a4e, 0xd14e2f};

void digit(render::DisplayList& out, float x, float y, float h, int d, Color c) {
    constexpr std::array<std::uint8_t, 10> segments{0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
                                                    0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011};
    const float w = h * 0.55f, t = std::max(1.5f, h * 0.1f);
    const std::array<Rect, 7> bars{Rect{x, y, w, t},          Rect{x + w - t, y, t, h / 2}, Rect{x + w - t, y + h / 2, t, h / 2},
                                   Rect{x, y + h - t, w, t},  Rect{x, y + h / 2, t, h / 2}, Rect{x, y, t, h / 2},
                                   Rect{x, y + h / 2 - t / 2, w, t}};
    for (int s = 0; s < 7; ++s)
        if (segments[static_cast<std::size_t>(d)] & (1 << (6 - s))) out.fill_rect(bars[static_cast<std::size_t>(s)], c);
}

// An arrow from the centre toward a numpad direction; 5 is a dot.
void arrow(render::DisplayList& out, Point c, float size, int numpad, Color colour) {
    if (numpad == 5) {
        out.fill_round_rect({c.x - 2, c.y - 2, 4, 4}, CornerRadii::all(2), render::Paint::solid(colour.with_alpha(0.5f)));
        return;
    }
    const int dx = (numpad - 1) % 3 - 1, dy = 1 - (numpad - 1) / 3;  // screen: y down
    const float len = std::sqrt(static_cast<float>(dx * dx + dy * dy));
    const float ux = static_cast<float>(dx) / len, uy = static_cast<float>(-dy) / len;  // y down
    const float r = size / 2;
    render::Path p;
    const Point tip{c.x + ux * r, c.y + uy * r}, tail{c.x - ux * r, c.y - uy * r};
    p.move_to(tail).line_to(tip);
    p.move_to({tip.x - ux * r * 0.7f - uy * r * 0.5f, tip.y - uy * r * 0.7f + ux * r * 0.5f}).line_to(tip);
    p.line_to({tip.x - ux * r * 0.7f + uy * r * 0.5f, tip.y - uy * r * 0.7f - ux * r * 0.5f});
    out.stroke_path(p, {.width = 2, .cap = render::LineCap::round, .join = render::LineJoin::round}, render::Paint::solid(colour));
}

}  // namespace

float number(render::DisplayList& out, float right, float top, float size, std::int32_t value, bool sign, Color color) {
    const float w = size * 0.55f, gap = size * 0.25f;
    std::int32_t v = value < 0 ? -value : value;
    float x = right;
    do {
        x -= w;
        digit(out, x, top, size, v % 10, color);
        x -= gap;
        v /= 10;
    } while (v > 0);
    if (sign) {
        const float t = std::max(1.5f, size * 0.1f), sw = w * 0.8f;
        x -= sw;
        out.fill_rect({x, top + size / 2 - t / 2, sw, t}, color);
        if (value >= 0) out.fill_rect({x + sw / 2 - t / 2, top + size / 2 - sw / 2, t, sw}, color);
        x -= gap;
    }
    return x + gap;
}

void boxes(render::DisplayList& out, const gongfu::Match& match, const p2::Scene& scene, const p2::Camera& cam) {
    const gongfu::State& st = match.state();
    auto outline = [&](const gongfu::Rect& b, Color c) {
        const Point a = p2::on_picture(scene, cam, b.left.to_double(), b.top.to_double());
        const Point z = p2::on_picture(scene, cam, b.right.to_double(), b.bottom.to_double());
        const Rect r{a.x, a.y, z.x - a.x, z.y - a.y};
        out.fill_rect(r, c.with_alpha(0.18f));
        out.stroke_round_rect(r, {}, 1.5f, render::Paint::solid(c));
    };
    for (std::int32_t i = 0; i < 2; ++i) {
        const auto& f = st.f[static_cast<std::size_t>(i)];
        const auto& def = match.fighter(i).def;
        const gongfu::MoveDef* move = f.mode == gongfu::Mode::move ? &def.moves[static_cast<std::size_t>(f.move)] : nullptr;
        outline(gongfu::place(def.push, f.pos, f.facing), tone(0x9aa0a6));
        // The hurtboxes the simulation tests strikes against: a limb baked out of the body shows.
        std::array<gongfu::Rect, 4> hurt{};
        const std::size_t n = match.hurtboxes(i, hurt);
        for (std::size_t k = 0; k < n; ++k) outline(hurt[k], tone(0x4ea1f2));
        if (!move) continue;
        for (const auto& h : move->hits)
            if (h.active.contains(f.move_frame))
                for (const auto& b : h.boxes) outline(gongfu::place(b, f.pos, f.facing), tone(0xf24e5a));
        if (move->throw_ && move->throw_->active.contains(f.move_frame)) outline(gongfu::place(move->throw_->range, f.pos, f.facing), tone(0xf2f2f2));
    }
    for (const auto& p : st.p) {
        if (!p.alive) continue;
        const auto& m = match.fighter(p.from).def.moves[static_cast<std::size_t>(p.move)];  // its thrower's, turned back or not
        for (const auto& b : m.projectile->hit.boxes) outline(gongfu::place(b, p.pos, p.facing), tone(0xf24e5a));
    }
}

void meter(render::DisplayList& out, const tr::Readout& readout) {
    const float cell = 6, left = width / 2 - cell * tr::Readout::meter_frames / 2;
    for (std::int32_t i = 0; i < 2; ++i) {
        const auto m = readout.meter(i);
        const float y = height - 40 + static_cast<float>(i) * 14;
        out.fill_rect({left - 2, y - 2, cell * tr::Readout::meter_frames + 3, 14}, tone(0x0b0d12, 0.7f));
        // Newest at the right edge, as it reads in time.
        const float start = left + cell * static_cast<float>(tr::Readout::meter_frames - m.size());
        for (std::size_t k = 0; k < m.size(); ++k)
            out.fill_rect({start + cell * static_cast<float>(k), y, cell - 1, 10}, tone(phase_tone[static_cast<std::size_t>(m[k])], 0.95f));
    }
}

void inputs(render::DisplayList& out, const tr::Readout& readout, std::int32_t player) {
    const auto lines = readout.inputs(player);
    const bool left = player == 0;
    const float line = 20, top = 236;
    std::size_t row = 0;
    for (auto it = lines.rbegin(); it != lines.rend() && row < 11; ++it, ++row) {  // newest on top
        const float y = top + line * static_cast<float>(row);
        const float alpha = 1 - 0.04f * static_cast<float>(row);
        const float x0 = left ? 16 : width - 16 - 150;
        out.fill_rect({x0, y, 150, line - 2}, tone(0x0b0d12, 0.55f * alpha));
        number(out, x0 + 34, y + 3, 12, std::min(it->frames, 999), false, ink.with_alpha(0.8f * alpha));
        arrow(out, {x0 + 50, y + line / 2 - 1}, 12, tr::direction(it->pad), ink.with_alpha(alpha));
        for (std::size_t b = 0; b < 6; ++b)
            if (it->pad.buttons & (1u << b))
                out.fill_round_rect({x0 + 64 + 13 * static_cast<float>(b), y + 4, 10, 10}, CornerRadii::all(5),
                                    render::Paint::solid(tone(button_tone[b], alpha)));
    }
}

const typeset::Paragraph& Words::get(typeset::FontCollection& fonts, std::string_view text, float size, Color color) {
    const std::string key = std::format("{}|{}|{},{},{},{}", text, size, color.r, color.g, color.b, color.a);
    auto it = kept_.find(key);
    if (it == kept_.end()) {
        typeset::ParagraphStyle ps;
        ps.style.size = size;
        ps.style.weight = 600;
        ps.style.letter_spacing = 1;
        ps.style.color = color;
        ps.wrap = false;
        typeset::Paragraph p(fonts, std::string(text), std::move(ps));
        p.layout(std::numeric_limits<float>::infinity());
        it = kept_.emplace(key, std::move(p)).first;
    }
    return it->second;
}

void readings(render::DisplayList& out, const tr::Readout& readout, Words& words, typeset::FontCollection& fonts, std::array<Dummy, 2> dummies,
              std::int32_t frame) {
    // Under the timer: player 1's startup, the advantage, the combo.
    const float top = 104, label = 10, size = 18;
    const Color muted = ink.with_alpha(0.6f);
    out.fill_round_rect({width / 2 - 170, top - 4, 340, 46}, CornerRadii::all(6), render::Paint::solid(tone(0x0b0d12, 0.55f)));
    auto caption = [&](std::string_view text, float centre) {
        const auto& p = words.get(fonts, text, label, muted);
        p.paint(out, {centre - p.size().width / 2, top});
    };
    caption("STARTUP", width / 2 - 110);
    caption("ADVANTAGE", width / 2);
    caption("COMBO", width / 2 + 110);
    const float y = top + 16;
    if (auto s = readout.startup(0)) number(out, width / 2 - 110 + 10, y, size, *s, false, ink);
    if (auto a = readout.advantage()) {
        const std::int32_t v = readout.advantage_attacker() == 0 ? *a : -*a;  // from player 1's side
        number(out, width / 2 + 12, y, size, v, true, tone(v > 0 ? 0x4ef27a : v < 0 ? 0xf24e5a : 0xf4f1ea));
    }
    const auto& c = readout.combo();
    if (c.attacker >= 0) {
        const float end = number(out, width / 2 + 92, y, size, c.hits, false, c.going ? tone(0xf2c14e) : ink);
        (void)end;
        number(out, width / 2 + 160, y + 4, size * 0.7f, c.damage, false, muted);
    }
    // The latest clash, deflect or guard break, for a second and a half: what, and whose.
    if (const auto& x = readout.exchange(); x.frame >= 0 && frame - x.frame < 90) {
        const char* what = x.kind == gongfu::Event::Kind::clash ? (x.by < 0 ? "PERFECT CLASH" : "CLASH")
                         : x.kind == gongfu::Event::Kind::deflect ? "DEFLECT"
                                                                 : "GUARD BREAK";
        const std::string text = x.by < 0 ? std::string(what) : std::format("{}  P{}", what, x.by + 1);
        const float fade_out = 1 - static_cast<float>(std::max(0, frame - x.frame - 60)) / 30;
        const std::uint32_t c = x.kind == gongfu::Event::Kind::guard_break ? 0xf24e5a : x.kind == gongfu::Event::Kind::deflect ? 0x7ad7ff : 0xf2c14e;
        const auto& p = words.get(fonts, text, 14, tone(c, fade_out));
        p.paint(out, {width / 2 - p.size().width / 2, height - 66});
    }
    // A dummy recording or playing: a mark under its side's bar.
    for (std::size_t i = 0; i < 2; ++i) {
        if (dummies[i] == Dummy::none) continue;
        const float x = i == 0 ? 40 : width - 40 - 70;
        const bool rec = dummies[i] == Dummy::recording;
        out.fill_round_rect({x, 128, 70, 18}, CornerRadii::all(9), render::Paint::solid(tone(0x0b0d12, 0.7f)));
        out.fill_round_rect({x + 6, 132, 10, 10}, CornerRadii::all(rec ? 5.0f : 1.0f), render::Paint::solid(tone(rec ? 0xf24e5a : 0x4ef27a)));
        const auto& p = words.get(fonts, rec ? "REC" : "PLAY", label, ink);
        p.paint(out, {x + 22, 131});
    }
}

// ── the versus HUD ──────────────────────────────────────────────────────────
//
// Black lacquer, gold leaf and a red seal (the HUD's art, hud/*.webp: generated
// sheets cut by gongfu-studio): each side's frame holds its portrait, its
// health over a lacquer backing (the recent damage in red, draining once the
// combo ends), its posture; the timer in a medallion; the super meter's three
// chambers and its level; a combo's count and damage; COUNTER and PUNISH
// COUNTER as they land; K.O. A pure function of the match, its kept events
// and the frame: a rollback draws it again the same. Player 2's side is
// player 1's, mirrored.

namespace {

// The art's regions, in its pictures' pixels.
namespace region {
constexpr Rect health{177, 4, 810, 151};  // frames.webp: the health frame (portrait window, bar, posture slot)
constexpr Rect timer{4, 4, 169, 172};
constexpr Rect meter{4, 180, 432, 81};
constexpr Rect counter{458, 177, 373, 116};  // type.webp
constexpr Rect punish{4, 177, 450, 123};
constexpr Rect ko{4, 4, 309, 169};
constexpr Rect fight{4, 304, 258, 92};
constexpr Rect hits{230, 400, 163, 81};
constexpr std::array<Rect, 10> digit{{{564, 304, 76, 84}, {355, 304, 54, 87}, {683, 400, 88, 76}, {600, 400, 79, 78}, {266, 304, 85, 88},
                                       {718, 304, 100, 83}, {397, 400, 72, 81}, {644, 304, 70, 84}, {413, 304, 74, 85}, {491, 304, 69, 85}}};
// names.webp: 墨 霓 坤 璃 岳 刻, each with its seal.
constexpr std::array<std::pair<std::string_view, Rect>, 6> name{{{"mo", {40, 18, 206, 198}},
                                                                  {"ni", {325, 17, 213, 197}},
                                                                  {"kun", {596, 15, 184, 222}},
                                                                  {"li", {38, 249, 223, 194}},
                                                                  {"yue", {311, 249, 219, 193}},
                                                                  {"ke", {597, 250, 215, 198}}}};
constexpr Rect ink_fill{32, 104, 928, 62};    // fill.webp: dry-brush streaks (health)
constexpr Rect flow_fill{32, 224, 928, 68};   // flowing wisps (the meter)
}  // namespace region

// The health frame's openings, in its region's pixels: each a slanted
// quadrilateral (top left, top right, bottom right, bottom left).
using Quad = std::array<Point, 4>;
constexpr Quad portrait_window{{{76.5f, 19.5f}, {170.5f, 19.5f}, {117.5f, 117.5f}, {43, 117.5f}}};
constexpr Quad bar_window{{{174, 51}, {777.5f, 51}, {751, 94}, {151, 94}}};
constexpr Quad posture_window{{{171, 110}, {716, 110}, {702, 128.5f}, {160, 128.5f}}};
// The meter frame's three chambers and its level plate's centre.
constexpr std::array<Quad, 3> chamber{{{{{55, 24.5f}, {146.5f, 24.5f}, {130.5f, 53.5f}, {35, 53.5f}}},
                                        {{{157, 24.5f}, {252, 24.5f}, {236, 53.5f}, {141, 53.5f}}},
                                        {{{262.5f, 24.5f}, {363.5f, 24.5f}, {348, 53.5f}, {246.5f, 53.5f}}}}};
constexpr Point plate{393.5f, 40.5f};

// Where things are on the 960 × 540 picture (player 1's side).
constexpr float health_x = 8, health_y = 6, health_scale = 452.0f / region::health.width;
constexpr float meter_x = 10, meter_scale = 224.0f / region::meter.width, meter_y = height - 10 - region::meter.height * 224.0f / region::meter.width;

Point lerp(Point a, Point b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }

// A quadrilateral's part from its left side to `t` of the way across (the cut parallel to its slanted ends).
Quad part_of(const Quad& q, float from, float to) {
    return {lerp(q[0], q[1], from), lerp(q[0], q[1], to), lerp(q[3], q[2], to), lerp(q[3], q[2], from)};
}

render::Path path_of(const Quad& q, float scale, Point at, float grow = 0) {
    // Grown a little outward (under the frame's edge), by the centre.
    const Point c{(q[0].x + q[1].x + q[2].x + q[3].x) / 4, (q[0].y + q[1].y + q[2].y + q[3].y) / 4};
    render::Path p;
    for (std::size_t i = 0; i < 4; ++i) {
        const float dx = q[i].x - c.x, dy = q[i].y - c.y, d = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
        const Point v{at.x + (q[i].x + dx / d * grow) * scale, at.y + (q[i].y + dy / d * grow) * scale};
        if (i == 0) p.move_to(v);
        else p.line_to(v);
    }
    p.close();
    return p;
}

// A region of a picture as a quad: drawn as it is, or as its shape in one colour.
void picture(render::DisplayList& out, render::ImageRef image, const Rect& src, const Rect& dest, float opacity = 1) {
    if (image.id == 0 || opacity <= 0) return;
    out.draw_image(image, src, dest, render::Sampling::linear, opacity);
}
void shape(render::DisplayList& out, render::ImageRef image, const Rect& src, const Rect& dest, Color colour) {
    if (image.id == 0 || colour.a <= 0) return;
    const std::array<Point, 4> at{Point{dest.x, dest.y}, {dest.x + dest.width, dest.y}, {dest.x + dest.width, dest.y + dest.height},
                                  {dest.x, dest.y + dest.height}};
    const std::array<Point, 4> uv{Point{src.x, src.y}, {src.x + src.width, src.y}, {src.x + src.width, src.y + src.height}, {src.x, src.y + src.height}};
    constexpr std::array<std::uint16_t, 6> tri{0, 1, 2, 0, 2, 3};
    out.draw_mesh_mask(image, at, uv, tri, colour);
}
// A region `h` high with its left edge at `x` (or centred on it), keeping its proportions; answers its width.
float placed(const Rect& src, float h) { return src.width * h / src.height; }

// A number in the HUD's brush digits, `h` high, centred on `centre`; in a colour of its own when given.
float digits_width(std::int32_t value, float h) {
    float w = 0;
    for (const char c : std::to_string(std::max(0, value))) w += placed(region::digit[static_cast<std::size_t>(c - '0')], h) * 0.86f;
    return w;
}
void digits(render::DisplayList& out, render::ImageRef type, std::int32_t value, Point centre, float h, std::optional<Color> colour = {},
            float opacity = 1) {
    float x = centre.x - digits_width(value, h) / 2;
    for (const char c : std::to_string(std::max(0, value))) {
        const Rect& src = region::digit[static_cast<std::size_t>(c - '0')];
        const float w = placed(src, h);
        const Rect dest{x - w * 0.07f, centre.y - h / 2, w, h};
        if (colour) {  // in a colour: an ink edge under it, so it reads on any ground
            const float e = std::max(1.0f, h * 0.05f);
            for (const Point d : {Point{-e, 0}, Point{e, 0}, Point{0, -e}, Point{0, e}})
                shape(out, type, src, {dest.x + d.x, dest.y + d.y, dest.width, dest.height}, tone(0x07080b, 0.9f * opacity));
            shape(out, type, src, dest, colour->with_alpha(colour->a * opacity));
        } else {
            picture(out, type, src, dest, opacity);
        }
        x += w * 0.86f;
    }
}

// The latest string of hits `attacker` landed (kept events no more than 45
// frames apart): how many, how much, and the frame of the last.
struct String {
    std::int32_t hits = 0, damage = 0, last = -1;
};
String latest_string(const p2::Effects& effects, std::int32_t attacker) {
    String s;
    std::int32_t earliest = -1;
    const auto kept = effects.kept();
    for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
        const gongfu::Event& e = it->event;
        const bool landed = e.attacker == attacker && (e.kind == gongfu::Event::Kind::hit || e.kind == gongfu::Event::Kind::counter ||
                                                       e.kind == gongfu::Event::Kind::throw_);
        if (!landed) continue;
        if (earliest >= 0 && earliest - it->frame > 45) break;  // a gap: an earlier string
        if (s.last < 0) s.last = it->frame;
        earliest = it->frame;
        ++s.hits;
        s.damage += e.damage;
    }
    return s;
}

}  // namespace

void versus(render::DisplayList& out, const gongfu::Match& match, const p2::Effects& effects, const Art& art,
            const std::array<const p2::Look*, 2>& looks, const std::array<std::string_view, 2>& names, std::array<std::int32_t, 2> wins,
            std::int32_t to_win) {
    const gongfu::State& st = match.state();
    const std::int32_t frame = st.frame;
    for (std::int32_t i = 0; i < 2; ++i) {
        const auto& f = st.f[static_cast<std::size_t>(i)];
        const auto& def = match.fighter(i).def;
        // Player 2 is player 1 mirrored about the picture's middle.
        if (i == 1) out.push_transform(Affine::translate(width, 0) * Affine::scale(-1, 1));
        const Point at{health_x, health_y};
        const float k = health_scale;

        // The portrait: the look's head, in its window over a lacquer backing lit from the side it faces.
        {
            const render::Path window = path_of(portrait_window, k, at, 2);
            out.push_clip(window);
            const Color backing = i == 0 ? tone(0x3a1418) : tone(0x112634);
            std::array<render::GradientStop, 2> stops{{{0, backing}, {1, tone(0x07080b)}}};
            render::Paint g;
            g.kind = render::Paint::Kind::gradient;
            g.gradient = {.kind = render::GradientKind::linear, .start = {at.x + 150 * k, at.y}, .end = {at.x + 40 * k, at.y + 120 * k}, .stops = stops};
            out.fill_path(window, render::FillRule::nonzero, g);
            if (const p2::Look* look = looks[static_cast<std::size_t>(i)]) {
                const auto head = std::ranges::find(look->parts, std::string_view("head"), &p2::Part::name);
                if (head != look->parts.end() && head->picture >= 0 && head->shape == p2::Shape::image) {
                    const p2::Picture& pic = look->pictures[static_cast<std::size_t>(head->picture)];
                    const Rect& src = head->source;
                    // The face fills the window's height and a little more, looking inward.
                    const float h = 118 * k * 1.25f, w = src.width * h / src.height;
                    const Rect dest{at.x + 108 * k - w / 2, at.y + 14 * k, w, h};
                    picture(out, pic.ref, src, dest);
                }
            }
            out.pop_clip();
        }

        // Health: the backing, the recent damage, the health itself, its brush texture, a lit top edge.
        const double max = def.health;
        const float share = static_cast<float>(std::clamp(f.health / max, 0.0, 1.0));
        const String taken = latest_string(effects, 1 - i);
        float trail = share;
        if (taken.last >= 0) {
            const bool going = f.mode == gongfu::Mode::hitstun || f.mode == gongfu::Mode::knockdown || f.mode == gongfu::Mode::cinematic || f.stop > 0;
            const float after = going ? 0.0f : static_cast<float>(frame - taken.last - 30) / 24;
            const float drain = std::clamp(1 - after, 0.0f, 1.0f);
            trail = std::min(1.0f, share + static_cast<float>(taken.damage / max) * drain);
        }
        out.fill_path(path_of(bar_window, k, at, 2), render::FillRule::nonzero, render::Paint::solid(tone(0x07080b, 0.92f)));
        if (trail > share) out.fill_path(path_of(part_of(bar_window, share, trail), k, at, 2), render::FillRule::nonzero, render::Paint::solid(tone(0xd9372b)));
        if (share > 0) {
            const render::Path fill = path_of(part_of(bar_window, 0, share), k, at, 2);
            // Low: the colour pulses toward red.
            const float low = share < 0.25f ? 0.5f + 0.5f * static_cast<float>(hanim::dm::sin(frame * 0.25)) : 0.0f;
            const Color top = i == 0 ? tone(0xfff0b8) : tone(0xd6f6ff), bottom = i == 0 ? tone(0xe8a02e) : tone(0x2ea6d9);
            const Color red = tone(0xff5a3c);
            auto mix = [](Color a, Color b, float t) { return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1}; };
            std::array<render::GradientStop, 2> stops{{{0, mix(top, red, low * 0.6f)}, {1, mix(bottom, red, low)}}};
            render::Paint g;
            g.kind = render::Paint::Kind::gradient;
            g.gradient = {.kind = render::GradientKind::linear, .start = {0, at.y + 51 * k}, .end = {0, at.y + 94 * k}, .stops = stops};
            out.fill_path(fill, render::FillRule::nonzero, g);
            // The dry brush's streaks, multiplied in, drifting slowly along.
            out.push_layer(0.55f, render::BlendMode::multiply);
            render::Paint tex;
            tex.kind = render::Paint::Kind::image;
            tex.image = art.fill;
            tex.color = Color{1, 1, 1, 1};
            const float sx = (777 - 151) * k / region::ink_fill.width, sy = (94 - 51) * k / region::ink_fill.height;
            const float drift = std::fmod(static_cast<float>(frame) * 0.4f, region::ink_fill.width * 0.5f);
            tex.image_transform = Affine::translate(at.x + 151 * k - (region::ink_fill.x + drift) * sx, at.y + 51 * k - region::ink_fill.y * sy) * Affine::scale(sx, sy);
            out.fill_path(fill, render::FillRule::nonzero, tex);
            out.pop_layer();
            // The lacquer's light along the top.
            out.fill_path(path_of(part_of({{bar_window[0], bar_window[1], lerp(bar_window[1], bar_window[2], 0.18f), lerp(bar_window[0], bar_window[3], 0.18f)}}, 0, share), k, at),
                          render::FillRule::nonzero, render::Paint::solid(tone(0xffffff, 0.28f)));
        }
        // Posture: taken from the middle outward, cream, red past 70 %, white when broken.
        {
            const float taken_share = static_cast<float>(std::clamp(f.posture / static_cast<double>(def.posture_max), 0.0, 1.0));
            out.fill_path(path_of(posture_window, k, at, 1.5f), render::FillRule::nonzero, render::Paint::solid(tone(0x07080b, 0.9f)));
            if (taken_share > 0) {
                const std::uint32_t c = f.mode == gongfu::Mode::broken ? 0xffffff : taken_share > 0.7f ? 0xf24e5a : 0xe8dcc4;
                out.fill_path(path_of(part_of(posture_window, 1 - taken_share, 1), k, at, 1.5f), render::FillRule::nonzero, render::Paint::solid(tone(c)));
            }
        }
        picture(out, art.frames, region::health, {at.x, at.y, region::health.width * k, region::health.height * k});

        // The super meter: three chambers, each filling; full ones flowing and bright; the level on its plate.
        {
            const Point m{meter_x, meter_y};
            const float km = meter_scale;
            const std::int32_t level = std::clamp(f.meter / 1000, 0, 3);
            for (std::size_t c = 0; c < 3; ++c) {
                const float full = static_cast<float>(std::clamp((f.meter - static_cast<std::int32_t>(c) * 1000) / 1000.0, 0.0, 1.0));
                out.fill_path(path_of(chamber[c], km, m, 1.5f), render::FillRule::nonzero, render::Paint::solid(tone(0x07080b, 0.9f)));
                if (full <= 0) continue;
                const render::Path fill = path_of(part_of(chamber[c], 0, full), km, m, 1.5f);
                out.fill_path(fill, render::FillRule::nonzero, render::Paint::solid(full >= 1 ? tone(0x63e6ff) : tone(0x1f6f8c)));
                if (full >= 1) {
                    out.push_layer(0.8f, render::BlendMode::screen);
                    render::Paint tex;
                    tex.kind = render::Paint::Kind::image;
                    tex.image = art.fill;
                    tex.color = Color{1, 1, 1, 1};
                    const float s = (chamber[c][1].x - chamber[c][3].x) * km / (region::flow_fill.width * 0.3f);
                    const float drift = std::fmod(static_cast<float>(frame) * 3.0f + static_cast<float>(c) * 140, region::flow_fill.width * 0.6f);
                    tex.image_transform = Affine::translate(m.x + chamber[c][3].x * km - (region::flow_fill.x + drift) * s, m.y + 20 * km - region::flow_fill.y * s) * Affine::scale(s, s);
                    out.fill_path(fill, render::FillRule::nonzero, tex);
                    out.pop_layer();
                }
            }
            picture(out, art.frames, region::meter, {m.x, m.y, region::meter.width * km, region::meter.height * km});
            // The level, gold on the plate (unmirrored: drawn after the transform is gone, below).
            (void)level;
        }
        if (i == 1) out.pop_transform();

        // Unmirrored things: the meter's level, the name.
        const auto x_of = [&](float x) { return i == 0 ? x : width - x; };
        {
            // The level, bright gold over a cyan glow that breathes while a super is ready; a dim zero otherwise.
            const std::int32_t level = std::clamp(f.meter / 1000, 0, 3);
            const Point at{x_of(meter_x + plate.x * meter_scale), meter_y + plate.y * meter_scale};
            if (level > 0) {
                const float pulse = 0.5f + 0.5f * static_cast<float>(hanim::dm::sin(frame * 0.2));
                const float r = 13;
                out.box_shadow({at.x - r, at.y - r, 2 * r, 2 * r}, CornerRadii::all(r), {0, 0}, 8 + 6 * pulse, 1,
                               tone(0x7fe3ff, (level == 3 ? 0.55f : 0.35f) + 0.25f * pulse));
            }
            digits(out, art.type, level, at, 28, tone(level > 0 ? 0xffe6a0 : 0x8a7a55), level > 0 ? 1.0f : 0.6f);
        }
        const auto nm = std::ranges::find(region::name, names[static_cast<std::size_t>(i)], &std::pair<std::string_view, Rect>::first);
        if (nm != region::name.end()) {
            const float h = 40, w = placed(nm->second, h);
            const float x = i == 0 ? health_x + 150 * health_scale : width - health_x - 150 * health_scale - w;
            const Rect dest{x, health_y + 132 * health_scale, w, h};
            // On a slanted lacquer plate with a gold edge: cream calligraphy on black reads on any stage.
            const float px = i == 0 ? x - 10 : x - 14, pw = w + 24, py = dest.y + 2, ph = h - 2, lean = 8;
            render::Path plate;
            plate.move_to({px + lean, py}).line_to({px + pw + lean, py}).line_to({px + pw - lean, py + ph}).line_to({px - lean, py + ph}).close();
            out.fill_path(plate, render::FillRule::nonzero, render::Paint::solid(tone(0x07080b, 0.88f)));
            out.stroke_path(plate, {.width = 1.2f}, render::Paint::solid(tone(0xc9a24a, 0.9f)));
            shape(out, art.names, nm->second, dest, tone(0xf6ecd6));
            // The rounds won, once there are any: a red seal for each, an empty one for each to go, beside the name toward the middle.
            if (wins[0] > 0 || wins[1] > 0)
                for (std::int32_t n = 0; n < to_win; ++n) {
                    const float r = 7, gap = 20, c = i == 0 ? px + pw + 16 + static_cast<float>(n) * gap : px - 16 - static_cast<float>(n) * gap;
                    const Point m{c, py + ph / 2};
                    render::Path seal;  // a diamond: a seal stood on its corner
                    seal.move_to({m.x, m.y - r}).line_to({m.x + r, m.y}).line_to({m.x, m.y + r}).line_to({m.x - r, m.y}).close();
                    const bool won = n < wins[static_cast<std::size_t>(i)];
                    out.fill_path(seal, render::FillRule::nonzero, render::Paint::solid(won ? tone(0xc8282a) : tone(0x07080b, 0.85f)));
                    out.stroke_path(seal, {.width = 1.4f}, render::Paint::solid(tone(0xd9b25a, won ? 1.0f : 0.7f)));
                }
        }
    }

    // The timer in its medallion: red in the last ten seconds.
    {
        const float d = 90, x = width / 2 - d / 2, y = 2;
        picture(out, art.frames, region::timer, {x, y, d, d * region::timer.height / region::timer.width});
        const std::int32_t seconds = std::clamp((st.timer + 59) / 60, 0, 99);
        const bool last = seconds <= 10 && seconds > 0;
        digits(out, art.type, seconds, {width / 2, y + d * region::timer.height / region::timer.width / 2 + 1}, 36,
               last ? std::optional<Color>(tone(0xff5a3c)) : std::nullopt);
    }

    // A combo's count and damage on the attacker's side; COUNTER and PUNISH COUNTER as they land.
    for (std::int32_t a = 0; a < 2; ++a) {
        const String s = latest_string(effects, a);
        const auto& d = st.f[static_cast<std::size_t>(1 - a)];
        if (s.hits >= 2 && s.last >= 0) {
            const std::int32_t age = frame - s.last;
            const bool going = d.combo > 0;
            const float fade = going ? 1.0f : std::clamp(1 - static_cast<float>(age - 20) / 20, 0.0f, 1.0f);
            if (fade > 0) {
                const float pop = 1 + 0.4f * std::max(0.0f, 1 - static_cast<float>(age) / 6);
                const float h = 54 * pop, hits_h = 26;
                const float w = digits_width(s.hits, h) + placed(region::hits, hits_h) + 6;
                const float x = a == 0 ? 36 : width - 36 - w, y = 150;
                digits(out, art.type, s.hits, {x + digits_width(s.hits, h) / 2, y}, h, std::nullopt, fade);
                picture(out, art.type, region::hits, {x + digits_width(s.hits, h) + 6, y + h / 2 - hits_h - 2, placed(region::hits, hits_h), hits_h}, fade);
                digits(out, art.type, s.damage, {x + w - digits_width(s.damage, 20) / 2, y + h / 2 + 14}, 20, tone(0xffd36b), fade);
            }
        }
    }
    // The latest counter, its banner sliding in from its side.
    const auto kept = effects.kept();
    for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
        if (it->event.kind != gongfu::Event::Kind::counter) continue;
        const std::int32_t age = frame - it->frame;
        if (age > 60) break;
        const bool punish = it->event.punish != 0;
        const Rect& src = punish ? region::punish : region::counter;
        const float h = punish ? 40 : 36, w = placed(src, h);
        const float in = hanim::ease::out_back(std::min(1.0, (age + 1) / 7.0));
        const float fade = std::clamp(1 - static_cast<float>(age - 44) / 16, 0.0f, 1.0f);
        const bool left = it->event.attacker == 0;
        const float x = left ? 24 - (1 - in) * (w + 40) : width - 24 - w + (1 - in) * (w + 40);
        picture(out, art.type, src, {x, 186, w, h}, fade);
        break;
    }
    // K.O.: stamped down in the middle for the blow's moment, then up under the timer, smaller, so the
    // winner and the fallen are seen while the round plays out.
    for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
        if (it->event.kind != gongfu::Event::Kind::ko) continue;
        const std::int32_t age = frame - it->frame;
        const float t = static_cast<float>(hanim::ease::out_cubic(std::min(1.0, (age + 1) / 8.0)));
        const float up = static_cast<float>(hanim::ease::in_out_cubic(std::clamp((age - 45) / 20.0, 0.0, 1.0)));
        const float h = 150 * (1.7f - 0.7f * t) * (1 - 0.45f * up), w = placed(region::ko, h);
        const float y = 230 - 80 * up;
        picture(out, art.type, region::ko, {width / 2 - w / 2, y - h / 2, w, h}, std::min(1.0f, t * 1.5f));
        break;
    }
    // FIGHT! as the round begins.
    if (frame < 50) {
        const float t = static_cast<float>(hanim::ease::out_back(std::min(1.0, (frame + 1) / 10.0)));
        const float fade = std::clamp(1 - static_cast<float>(frame - 36) / 14, 0.0f, 1.0f);
        const float h = 90 * t, w = placed(region::fight, h);
        if (h > 1) picture(out, art.type, region::fight, {width / 2 - w / 2, 220 - h / 2, w, h}, fade);
    }
}

}  // namespace omui::fighting::hud
