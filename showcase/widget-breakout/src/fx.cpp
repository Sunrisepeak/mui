module openmui.showcase.widget_breakout;

import std;
import openmui;
import openmui.showcase.widget_breakout.sim;

namespace omui::breakout {

namespace {

std::uint64_t mix(std::uint64_t z) noexcept {  // splitmix64's finaliser: a seed's stream
    z += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
float unit(std::uint64_t& s) noexcept {  // [0, 1)
    s = mix(s);
    return static_cast<float>(s >> 40) / static_cast<float>(1ull << 24);
}
Point point(mgame::V2 v) noexcept { return {static_cast<float>(v.x), static_cast<float>(v.y)}; }

// Unit directions round a circle, sixteen of them (no libm: the same everywhere).
constexpr std::array<Point, 16> compass{{{1, 0}, {0.92388f, 0.38268f}, {0.70711f, 0.70711f}, {0.38268f, 0.92388f},
                                         {0, 1}, {-0.38268f, 0.92388f}, {-0.70711f, 0.70711f}, {-0.92388f, 0.38268f},
                                         {-1, 0}, {-0.92388f, -0.38268f}, {-0.70711f, -0.70711f}, {-0.38268f, -0.92388f},
                                         {0, -1}, {0.38268f, -0.92388f}, {0.70711f, -0.70711f}, {0.92388f, -0.38268f}}};

}  // namespace

Color kind_color(Kind k) {
    switch (k) {
        case Kind::toggle: return Color::hex(ink::live);
        case Kind::slider: return Color::hex(ink::charge);
        case Kind::core: return Color::hex(ink::core);
        case Kind::hidden: return Color::hex(ink::cache);
        case Kind::card: return Color::hex(0xb8c4dc);
        case Kind::kicker: return Color::hex(ink::kicker);
        case Kind::button:
        case Kind::wall:
        case Kind::gate:
        case Kind::shutter: break;
    }
    return Color::hex(0x90caf9);
}

void Fx::burst(Point at, Color c, int n, float speed, std::uint64_t seed, float size) {
    for (int i = 0; i < n; ++i) {
        Particle& p = particles[next_particle_];
        next_particle_ = (next_particle_ + 1) % particles.size();
        const Point d = compass[static_cast<std::size_t>(seed + static_cast<std::uint64_t>(i) * 5) % compass.size()];
        const float v = speed * (0.35f + 0.65f * unit(seed));
        p = {at.x, at.y, d.x * v + (unit(seed) - 0.5f), d.y * v + (unit(seed) - 0.5f), 1, 24 + 30 * unit(seed), size * (0.6f + 0.8f * unit(seed)), c};
    }
}

void Fx::floater(Point at, Color c, float size, std::string_view text) {
    Floater& f = floaters[next_floater_];
    next_floater_ = (next_floater_ + 1) % floaters.size();
    f.text.fill(0);
    std::ranges::copy(text.substr(0, f.text.size() - 1), f.text.begin());
    f.at = at;
    f.life = 1;
    f.size = size;
    f.color = c;
}

void Fx::ring(Point at, Color c, float radius, float grow, float life) {
    Ring& r = rings[next_ring_];
    next_ring_ = (next_ring_ + 1) % rings.size();
    r = {at, life, radius, grow, c};
}

void Fx::clear() {
    for (Particle& p : particles) p.life = 0;
    for (Trail& t : trails) t = {};
    for (Beam& b : beams) b.life = 0;
    for (Pulse& p : pulses) p.life = 0;
    for (Floater& f : floaters) f.life = 0;
    for (Ring& r : rings) r.life = 0;
    flash.fill(0);
    paddle_squash.fill(0);
    shake = 0;
    chain_pop = 0;
    banked = 0;
    last_combo_ = 0;
}

void Fx::ghost_from(std::int64_t to, std::int64_t from) {
    ghost_count = 0;
    for (std::int64_t f = to; f <= from && ghost_count < ghost.size(); f += 2) {
        const std::size_t i = static_cast<std::size_t>(f % static_cast<std::int64_t>(path_.size()));
        if (path_frame_[i] == f) ghost[ghost_count++] = path_[i];
    }
    ghost_life = ghost_count > 1 ? 1 : 0;
}

void Fx::age() {
    for (Particle& p : particles) {
        if (p.life <= 0) continue;
        p.x += p.vx;
        p.y += p.vy;
        p.vy += 0.06f;
        p.vx *= 0.97f;
        p.life -= 1 / p.span;
    }
    for (Beam& b : beams) b.life = std::max(0.0f, b.life - 1 / 26.0f);
    for (Pulse& p : pulses) p.life = std::max(0.0f, p.life - 1 / 28.0f);
    for (Floater& f : floaters)
        if (f.life > 0) f.life = std::max(0.0f, f.life - 1 / 100.0f), f.at.y -= 0.5f;
    for (Ring& r : rings)
        if (r.life > 0) r.radius += r.grow, r.life = std::max(0.0f, r.life - 1 / 40.0f);
    for (float& f : flash) f = std::max(0.0f, f - 1 / 14.0f);
    for (float& q : paddle_squash) q = std::max(0.0f, q - 1 / 10.0f);
    shake *= 0.86f;
    if (shake < 0.2f) shake = 0;
    chain_pop = std::max(0.0f, chain_pop - 1 / 16.0f);
    banked = std::max(0.0f, banked - 1 / 50.0f);
    ghost_life = std::max(0.0f, ghost_life - 1 / 360.0f);
}

void Fx::react(const State& s, const RoomDef& room) {
    frame = s.frame;
    age();
    if (s.combo > last_combo_) chain_pop = 1;
    last_combo_ = s.combo;

    // The balls: their trails (a bounce squashes one), the first flying one's path kept.
    bool first = true;
    s.world.each<Ball>([&](mgame::Entity e, const Ball& b) {
        Trail* t = nullptr;
        for (Trail& c : trails)
            if (c.ball == e.index && c.generation == e.generation) t = &c;
        if (!t)
            for (Trail& c : trails)
                if (c.seen < s.frame - 1) {
                    c = {};
                    c.ball = e.index;
                    c.generation = e.generation;
                    t = &c;
                    break;
                }
        if (!t) return;
        const Point at = point(b.at);
        if (b.on == 0) {
            const float len = static_cast<float>(mgame::length(b.velocity));
            const Point dir = len > 0 ? Point{static_cast<float>(b.velocity.x) / len, static_cast<float>(b.velocity.y) / len} : t->dir;
            if (t->count > 0 && dir.x * t->dir.x + dir.y * t->dir.y < 0.995f) t->squash = 1;
            t->dir = dir;
            if (first) {
                const std::size_t i = static_cast<std::size_t>(s.frame % static_cast<std::int64_t>(path_.size()));
                path_[i] = at;
                path_frame_[i] = s.frame;
                first = false;
            }
        }
        t->squash = std::max(0.0f, t->squash - 1 / 7.0f);
        for (std::size_t k = t->at.size() - 1; k > 0; --k) t->at[k] = t->at[k - 1];
        t->at[0] = at;
        t->count = std::min<std::uint32_t>(t->count + 1, static_cast<std::uint32_t>(t->at.size()));
        if (b.on != 0) t->count = 1;  // resting: no trail
        t->seen = s.frame;
    });

    // The step's events.
    for (std::uint32_t k = 0; k < s.events; ++k) {
        const Event& e = s.event[k];
        const Point at = point(e.at);
        const std::uint64_t seed = static_cast<std::uint64_t>(s.frame) * 131 + k;
        const Kind kind = e.id < room.bricks.size() ? room.bricks[e.id].kind : Kind::button;
        auto lit = [&](std::uint32_t id) {
            if (id < flash.size()) flash[id] = 1;
        };
        switch (e.what) {
            case Happened::hit: burst(at, kind_color(kind), 4, 2.2f, seed); lit(e.id); break;
            case Happened::broke: burst(at, kind_color(kind), e.n < 0 ? 6 : 12, e.n < 0 ? 2.5f : 3.4f, seed, 3); break;
            case Happened::paddle: paddle_squash[std::min<std::size_t>(e.id, 1)] = 1; burst(at, Color::hex(ink::paddle), 3, 1.6f, seed, 2); break;
            case Happened::flipped: burst(at, Color::hex(ink::live), 8, 2.6f, seed); lit(e.id); break;
            case Happened::wired: {
                Pulse& p = pulses[next_pulse_];
                next_pulse_ = (next_pulse_ + 1) % pulses.size();
                p = {at, point(e.to), 1};
                lit(e.id);
                if (kind == Kind::shutter && e.n) ring(point(e.to), Color::hex(0xffffff), 6, 2.5f, 0.7f);  // a shutter opening
                break;
            }
            case Happened::charged: burst(at, Color::hex(ink::charge), 5, 2.0f, seed); lit(e.id); break;
            case Happened::armed: ring(at, Color::hex(ink::charge), 20, 2.5f, 0.6f); break;
            case Happened::beam: {
                Beam& b = beams[next_beam_];
                next_beam_ = (next_beam_ + 1) % beams.size();
                b = {at, point(e.to), 1, e.n};
                const Point to = point(e.to);
                for (int i = 0; i < 8; ++i) {
                    const float f = static_cast<float>(i) / 7;
                    burst({at.x + (to.x - at.x) * f, at.y + (to.y - at.y) * f}, Color::hex(ink::beam), 3, 2.4f, seed + static_cast<std::uint64_t>(i) * 17);
                }
                shake = std::max(shake, 5.0f);
                break;
            }
            case Happened::kicked: burst(at, Color::hex(ink::kicker), 8, 2.8f, seed); lit(e.id); break;
            case Happened::core_hit: burst(at, Color::hex(ink::core), 16, 3.8f, seed, 3); lit(e.id); shake = std::max(shake, 4.0f); break;
            case Happened::shutdown:
                burst(at, Color::hex(ink::core), 40, 5.5f, seed, 3.5f);
                burst(at, Color::hex(0xffffff), 20, 3.0f, seed + 7, 2.5f);
                ring(at, Color::hex(0xffffff), 10, 22, 1);
                shake = std::max(shake, 10.0f);
                break;
            case Happened::breach: {
                burst(at, Color::hex(0xffffff), e.n ? 30 : 10, e.n ? 4.5f : 3.0f, seed);
                if (e.n) {
                    shake = std::max(shake, 6.0f);
                    floater({at.x, at.y + 40}, Color::hex(ink::spark), 22, "BREAKTHROUGH  +50 ◆");
                }
                break;
            }
            case Happened::banked: {
                std::array<char, 40> t{};
                const auto r = std::format_to_n(t.data(), t.size() - 1, "BANKED  {}-chain  +{} ◆", e.n, e.id);
                floater({at.x, at.y - 30}, Color::hex(ink::spark), e.n >= 10 ? 26 : 20, {t.data(), r.out});
                ring(at, Color::hex(ink::spark), 12, 3, 1);
                banked = 1;
                break;
            }
            case Happened::chain_lost: {
                std::array<char, 40> t{};
                const auto r = std::format_to_n(t.data(), t.size() - 1, "{}-chain lost", e.n);
                floater({at.x, at.y - 40}, Color::hex(ink::lost), 20, {t.data(), r.out});
                break;
            }
            case Happened::cache: floater({at.x, at.y - 10}, Color::hex(ink::cache), 18, "+30 ◆"); break;
            case Happened::lost: burst(at, Color::hex(ink::lost), 14, 3.0f, seed); break;
            case Happened::caught: ring(at, Color::hex(0xffffff), 8, 3, 0.8f); break;
            case Happened::magnet: ring(at, Color::hex(ink::magnet), 10, 2, 0.8f); break;
            case Happened::finisher: floater({at.x, at.y + 60}, Color::hex(ink::live), 22, "DUET READY — return it"); break;
            case Happened::duet:
                if (e.n >= 2 && e.n < duet_full) {
                    std::array<char, 40> t{};
                    const auto r = std::format_to_n(t.data(), t.size() - 1, "duet {}/{}", e.n, duet_full);
                    floater({at.x, at.y + (e.id == 0 ? -40.0f : 40.0f)}, Color::hex(ink::live), 15, {t.data(), r.out});
                }
                break;
            case Happened::launched:
            case Happened::wall:
            case Happened::gift:
            case Happened::cleared:
            case Happened::life:
            case Happened::over: break;
        }
    }
}

}  // namespace omui::breakout
