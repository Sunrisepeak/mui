module openmui.showcase.fighting;

import std;
import openmui.core;
import openmui.view;
import gongfu.present2d;
import gongfu.training;

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

void bars(render::DisplayList& out, const gongfu::Match& match) {
    const gongfu::State& st = match.state();
    for (std::int32_t i = 0; i < 2; ++i) {
        const float w = 380, h = 16, y = 24, x = i == 0 ? 40 : width - 40 - w;
        const double share = std::clamp(st.f[static_cast<std::size_t>(i)].health / static_cast<double>(match.fighter(i).def.health), 0.0, 1.0);
        out.fill_round_rect({x, y, w, h}, CornerRadii::all(4), render::Paint::solid(tone(0x0b0d12, 0.8f)));
        const float filled = static_cast<float>(w * share);
        // Each bar drains from the middle toward its own side, as the genre's do.
        out.fill_round_rect({i == 0 ? x : x + w - filled, y, filled, h}, CornerRadii::all(4), render::Paint::solid(tone(i == 0 ? 0xf2c14e : 0x4ec9f2)));
        // Posture (system 0.3): a thin bar under the health, filling from the middle as it is taken; red past 70%, white when broken.
        const auto& f = st.f[static_cast<std::size_t>(i)];
        const double taken = std::clamp(f.posture / static_cast<double>(match.fighter(i).def.posture_max), 0.0, 1.0);
        const float pw = static_cast<float>(w * taken), py = y + h + 4;
        out.fill_rect({x, py, w, 5}, tone(0x0b0d12, 0.7f));
        const std::uint32_t c = f.mode == gongfu::Mode::broken ? 0xffffff : taken > 0.7 ? 0xf24e5a : 0xe8dcc4;
        out.fill_rect({i == 0 ? x + w - pw : x, py, pw, 5}, tone(c));
        // Meter: three bars in the bottom corner on its side.
        const float mw = 60, mh = 8, my = height - 22;
        for (int bar = 0; bar < 3; ++bar) {
            const float mx = i == 0 ? 40 + bar * (mw + 6) : width - 40 - mw - bar * (mw + 6);
            const double full = std::clamp((f.meter - bar * 1000) / 1000.0, 0.0, 1.0);
            out.fill_round_rect({mx, my, mw, mh}, CornerRadii::all(3), render::Paint::solid(tone(0x0b0d12, 0.7f)));
            const float fw = static_cast<float>(mw * full);
            out.fill_round_rect({i == 0 ? mx : mx + mw - fw, my, fw, mh}, CornerRadii::all(3), render::Paint::solid(tone(full >= 1 ? 0x7af0ff : 0x3a8fa8)));
        }
    }
}

void timer(render::DisplayList& out, std::int32_t seconds) {
    const int s = std::clamp(seconds, 0, 99);
    digit(out, width / 2 - 22, 16, 30, s / 10, ink);
    digit(out, width / 2 + 2, 16, 30, s % 10, ink);
}

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
        const auto& m = match.fighter(p.owner).def.moves[static_cast<std::size_t>(p.move)];
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
    const float line = 20, top = 64;
    std::size_t row = 0;
    for (auto it = lines.rbegin(); it != lines.rend() && row < 16; ++it, ++row) {  // newest on top
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

void readings(render::DisplayList& out, const tr::Readout& readout, Words& words, typeset::FontCollection& fonts, std::array<Dummy, 2> dummies) {
    // Under the timer: player 1's startup, the advantage, the combo.
    const float top = 60, label = 10, size = 18;
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
    // A dummy recording or playing: a mark under its side's bar.
    for (std::size_t i = 0; i < 2; ++i) {
        if (dummies[i] == Dummy::none) continue;
        const float x = i == 0 ? 40 : width - 40 - 70;
        const bool rec = dummies[i] == Dummy::recording;
        out.fill_round_rect({x, 46, 70, 18}, CornerRadii::all(9), render::Paint::solid(tone(0x0b0d12, 0.7f)));
        out.fill_round_rect({x + 6, 50, 10, 10}, CornerRadii::all(rec ? 5.0f : 1.0f), render::Paint::solid(tone(rec ? 0xf24e5a : 0x4ef27a)));
        const auto& p = words.get(fonts, rec ? "REC" : "PLAY", label, ink);
        p.paint(out, {x + 22, 49});
    }
}

}  // namespace omui::fighting::hud
