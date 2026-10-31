module openmui.view;

import std;
import openmui.math;
import openmui.hash;
import openmui.core;
import hanim;
import openmui.render.vector;
import :element;
import :glyphs;

namespace omui {

namespace {

// splitmix64: a seeded, platform-independent stream.
std::uint64_t mix(std::uint64_t x) noexcept {
    return math::mix64(x);
}
// A number in [-1, 1] for (seed, element, component), the same everywhere.
float signed_unit(std::uint64_t seed, std::uint64_t key, std::uint64_t component) noexcept {
    const std::uint64_t h = mix(seed ^ mix(key * hash::Fnv1a::prime + component));
    return static_cast<float>(static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0)) * 2 - 1;
}

std::uint32_t key_of(GlyphDomain d, const render::Glyph& g, std::uint32_t sequence) noexcept {
    switch (d) {
        case GlyphDomain::glyph: return sequence;
        case GlyphDomain::grapheme: return g.grapheme;
        case GlyphDomain::word: return g.word;
        case GlyphDomain::line: return g.line;
    }
    return sequence;
}

GlyphPose lerp(const GlyphPose& a, const GlyphPose& b, float t) noexcept {
    auto l = [t](float x, float y) { return x + (y - x) * t; };
    return {l(a.x, b.x), l(a.y, b.y), l(a.scale, b.scale), l(a.rotate, b.rotate), l(a.opacity, b.opacity),
            Color{l(a.tint.r, b.tint.r), l(a.tint.g, b.tint.g), l(a.tint.b, b.tint.b), l(a.tint.a, b.tint.a)}};
}

render::GlyphStyle style_of(const GlyphPose& p) noexcept {
    render::GlyphStyle s;
    const double c = hanim::dm::cos(p.rotate), sn = hanim::dm::sin(p.rotate);
    s.transform = Affine::translate(p.x, p.y) * Affine::rotation(static_cast<float>(c), static_cast<float>(sn)) * Affine::scale(p.scale, p.scale);
    if (p.rotate == 0) s.transform = Affine{p.scale, 0, 0, p.scale, p.x, p.y};  // exact: no rounding of cos(0)
    s.opacity = std::clamp(p.opacity, 0.0f, 1.0f);
    s.tint = p.tint;
    return s;
}

Value pose_value(const GlyphPose& p) {
    Value v;
    v.set("x", static_cast<double>(p.x)).set("y", static_cast<double>(p.y)).set("scale", static_cast<double>(p.scale));
    v.set("rotate", static_cast<double>(p.rotate)).set("opacity", static_cast<double>(p.opacity));
    v.set("tint", ValueOf<Color>::to(p.tint));
    return v;
}
GlyphPose pose_from(const Value* v, GlyphPose fallback) {
    if (!v || !v->map()) return fallback;
    auto f = [&](std::string_view k, float d) {
        const Value* x = v->get(k);
        return x && x->is_number() ? static_cast<float>(x->as_double()) : d;
    };
    GlyphPose p{f("x", fallback.x), f("y", fallback.y), f("scale", fallback.scale), f("rotate", fallback.rotate), f("opacity", fallback.opacity),
                fallback.tint};
    if (const Value* t = v->get("tint")) p.tint = ValueOf<Color>::from(*t);
    return p;
}

constexpr std::array<std::string_view, 4> domain_names{"glyph", "grapheme", "word", "line"};
constexpr std::array<std::string_view, 5> order_names{"forward", "reverse", "center", "edges", "random"};

}  // namespace

GlyphMotion GlyphMotion::typewriter(hanim::seconds per_letter) {
    GlyphMotion m;
    m.domain = GlyphDomain::grapheme;
    m.from = {.opacity = 0};
    m.to = {};
    m.transition = Transition::tween(hanim::seconds(0.001), hanim::ease::linear);
    m.stagger = per_letter;
    return m;
}

GlyphMotion GlyphMotion::rise(hanim::seconds stagger) {
    GlyphMotion m;
    m.domain = GlyphDomain::word;
    m.from = {.y = 14, .opacity = 0};
    m.to = {};
    m.transition = Transition::tween(hanim::seconds(0.5), hanim::ease::out_cubic);
    m.stagger = stagger;
    return m;
}

GlyphMotion GlyphMotion::gather(std::uint64_t seed) {
    GlyphMotion m;
    m.domain = GlyphDomain::grapheme;
    m.from = {.scale = 1.6f, .opacity = 0};
    m.jitter = {.x = 160, .y = 90, .scale = 0.5f, .rotate = 2.5f, .opacity = 0};
    m.to = {};
    m.transition = Transition::spring_with(hanim::SpringParams::response(0.55, 0.15));
    m.stagger = hanim::seconds(0.012);
    m.order = GlyphOrder::random;
    m.seed = seed;
    return m;
}

GlyphMotion GlyphMotion::scatter(std::uint64_t seed) {
    GlyphMotion m = gather(seed);
    m.from = {};
    m.to = {.scale = 1.6f, .opacity = 0};
    m.jitter_to = true;  // from rest to the spread
    m.transition = Transition::tween(hanim::seconds(0.6), hanim::ease::in_cubic);
    return m;
}

GlyphMotion GlyphMotion::wave(float height) {
    GlyphMotion m;
    m.domain = GlyphDomain::grapheme;
    m.from = {.y = height / 2};
    m.to = {.y = -height / 2};
    m.transition = Transition::tween(hanim::seconds(0.45), hanim::ease::in_out_sine);
    m.stagger = hanim::seconds(0.06);
    m.repeat = 0;
    m.alternate = true;
    return m;
}

bool operator==(const GlyphMotion& a, const GlyphMotion& b) noexcept { return to_value(a) == to_value(b); }

Value to_value(const GlyphMotion& m) {
    Value v;
    v.set("domain", std::string(domain_names[static_cast<std::size_t>(m.domain)]));
    v.set("begin", static_cast<std::int64_t>(m.begin));
    if (m.end != std::numeric_limits<std::size_t>::max()) v.set("end", static_cast<std::int64_t>(m.end));
    v.set("from", pose_value(m.from)).set("to", pose_value(m.to)).set("jitter", pose_value(m.jitter)).set("jitter_to", m.jitter_to);
    Value t;
    if (m.transition.kind == Transition::Kind::tween) {
        t.set("kind", "tween").set("duration", m.transition.duration.count()).set("ease", hanim::to_string(m.transition.ease));
    } else {
        t.set("kind", "spring").set("stiffness", m.transition.spring.stiffness).set("damping", m.transition.spring.damping);
        t.set("mass", m.transition.spring.mass);
    }
    if (m.transition.delay.count() != 0) t.set("delay", m.transition.delay.count());
    v.set("transition", std::move(t));
    v.set("stagger", m.stagger.count()).set("order", std::string(order_names[static_cast<std::size_t>(m.order)]));
    v.set("seed", static_cast<std::int64_t>(m.seed)).set("repeat", static_cast<std::int64_t>(m.repeat)).set("alternate", m.alternate);
    return v;
}

std::optional<GlyphMotion> glyph_motion_from(const Value& v) {
    if (!v.map()) return std::nullopt;
    GlyphMotion m;
    if (const Value* d = v.get("domain")) {
        auto it = std::ranges::find(domain_names, d->as_string());
        if (it == domain_names.end()) return std::nullopt;
        m.domain = static_cast<GlyphDomain>(it - domain_names.begin());
    }
    if (const Value* b = v.get("begin")) m.begin = static_cast<std::size_t>(std::max<std::int64_t>(0, b->as_int()));
    if (const Value* e = v.get("end")) m.end = static_cast<std::size_t>(std::max<std::int64_t>(0, e->as_int()));
    m.from = pose_from(v.get("from"), m.from);
    m.to = pose_from(v.get("to"), m.to);
    m.jitter = pose_from(v.get("jitter"), m.jitter);
    if (const Value* j = v.get("jitter_to")) m.jitter_to = j->as_bool();
    if (const Value* t = v.get("transition"); t && t->map()) {
        if (t->get("kind") && t->get("kind")->as_string() == "spring") {
            hanim::SpringParams p;
            if (const Value* x = t->get("stiffness")) p.stiffness = x->as_double(p.stiffness);
            if (const Value* x = t->get("damping")) p.damping = x->as_double(p.damping);
            if (const Value* x = t->get("mass")) p.mass = x->as_double(p.mass);
            m.transition = Transition::spring_with(p);
        } else {
            hanim::Ease e = hanim::ease::out_cubic;
            if (const Value* x = t->get("ease"))
                if (auto pe = hanim::parse_ease(x->as_string())) e = *pe;
            m.transition = Transition::tween(hanim::seconds(t->get("duration") ? t->get("duration")->as_double(0.45) : 0.45), e);
        }
        if (const Value* d = t->get("delay")) m.transition.delay = hanim::seconds(d->as_double(0));
    } else if (const Value* t2 = v.get("transition"); t2 && t2->type() == Value::Type::text) {
        if (auto tok = Transition::token(t2->as_string())) m.transition = *tok;
    }
    if (const Value* s = v.get("stagger")) m.stagger = hanim::seconds(s->as_double(m.stagger.count()));
    if (const Value* o = v.get("order")) {
        auto it = std::ranges::find(order_names, o->as_string());
        if (it != order_names.end()) m.order = static_cast<GlyphOrder>(it - order_names.begin());
    }
    if (const Value* s = v.get("seed")) m.seed = static_cast<std::uint64_t>(s->as_int());
    if (const Value* r = v.get("repeat")) m.repeat = static_cast<int>(std::max<std::int64_t>(0, r->as_int(1)));
    if (const Value* a = v.get("alternate")) m.alternate = a->as_bool();
    return m;
}

Element animate_glyphs(Element e, const GlyphMotion& m) {
    e.prop("glyph_motion", Source{to_value(m), {}, {}});
    return e;
}

GlyphFrame glyph_frame(const GlyphMotion& m, hanim::seconds elapsed, std::span<const std::span<const render::Glyph>> commands,
                       bool reduce_motion, std::span<const std::uint32_t> paragraphs) {
    GlyphFrame f;
    f.styles.resize(commands.size());
    // An element's key: its paragraph above, its place in the paragraph below.
    auto keyed = [&](std::size_t command, const render::Glyph& g, std::uint32_t sequence) {
        const std::uint64_t paragraph = command < paragraphs.size() ? paragraphs[command] : 0;
        return (paragraph << 32) | key_of(m.domain, g, sequence);
    };
    // The elements in range, by key: their rank is their place in reading order.
    std::vector<std::uint64_t> keys;
    std::uint32_t sequence = 0;
    for (std::size_t ci = 0; ci < commands.size(); ++ci)
        for (const render::Glyph& g : commands[ci]) {
            if (g.cluster >= m.begin && g.cluster < m.end) keys.push_back(keyed(ci, g, sequence));
            ++sequence;
        }
    std::ranges::sort(keys);
    keys.erase(std::ranges::unique(keys).begin(), keys.end());
    const std::size_t n = keys.size();
    auto rank = [&](std::uint64_t k) { return static_cast<std::size_t>(std::ranges::lower_bound(keys, k) - keys.begin()); };
    // Each element's place in time.
    std::vector<std::size_t> slot(n);
    for (std::size_t r = 0; r < n; ++r) {
        switch (m.order) {
            case GlyphOrder::forward: slot[r] = r; break;
            case GlyphOrder::reverse: slot[r] = n - 1 - r; break;
            case GlyphOrder::center: slot[r] = static_cast<std::size_t>(std::abs(2 * static_cast<long long>(r) - static_cast<long long>(n - 1)) / 2); break;
            case GlyphOrder::edges:
                slot[r] = (n - 1) / 2 - static_cast<std::size_t>(std::abs(2 * static_cast<long long>(r) - static_cast<long long>(n - 1)) / 2);
                break;
            case GlyphOrder::random: break;
        }
    }
    if (m.order == GlyphOrder::random) {
        std::vector<std::pair<std::uint64_t, std::size_t>> shuffled(n);
        for (std::size_t r = 0; r < n; ++r) shuffled[r] = {mix(m.seed ^ mix(keys[r])), r};
        std::ranges::sort(shuffled);
        for (std::size_t i = 0; i < n; ++i) slot[shuffled[i].second] = i;
    }
    // One cycle's length, and the progress of an element `u` seconds after its start.
    const bool spring = m.transition.kind == Transition::Kind::spring;
    const hanim::Oscillator osc(0, 0, 1, m.transition.spring);
    const double cycle = spring ? osc.settle_time() : std::max(1e-6, m.transition.duration.count());
    const std::size_t last_slot = n ? *std::ranges::max_element(slot) : 0;
    const double spread = static_cast<double>(last_slot) * m.stagger.count();
    const double begins = m.transition.delay.count();  // every element held at `from` until then
    f.ends = m.repeat == 0 ? hanim::seconds(std::numeric_limits<double>::infinity())
                           : hanim::seconds(begins + spread + cycle * m.repeat);
    f.moving = !reduce_motion && elapsed < f.ends;
    auto progress = [&](double u) -> float {
        if (reduce_motion) return m.alternate && m.repeat % 2 == 0 && m.repeat > 0 ? 0.0f : 1.0f;
        if (u <= 0) return 0;
        double c = std::floor(u / cycle);
        if (m.repeat > 0 && c >= m.repeat) return m.alternate && m.repeat % 2 == 0 ? 0.0f : 1.0f;
        double local = u - c * cycle;
        double p = spring ? osc.position(local) : m.transition.ease(std::clamp(local / cycle, 0.0, 1.0));
        if (m.alternate && static_cast<long long>(c) % 2 == 1) p = 1 - p;
        return static_cast<float>(p);
    };
    sequence = 0;
    for (std::size_t ci = 0; ci < commands.size(); ++ci) {
        auto& out = f.styles[ci];
        out.resize(commands[ci].size());
        bool any = false;
        for (std::size_t gi = 0; gi < commands[ci].size(); ++gi, ++sequence) {
            const render::Glyph& g = commands[ci][gi];
            if (g.cluster < m.begin || g.cluster >= m.end) continue;
            const std::uint64_t key = keyed(ci, g, sequence);
            const std::size_t r = rank(key);
            const double delay = static_cast<double>(slot[r]) * m.stagger.count();
            GlyphPose from = m.from, to = m.to;
            if (!(m.jitter == GlyphPose{.scale = 0, .opacity = 0})) {
                GlyphPose& j = m.jitter_to ? to : from;
                j.x += m.jitter.x * signed_unit(m.seed, key, 0);
                j.y += m.jitter.y * signed_unit(m.seed, key, 1);
                j.scale += m.jitter.scale * signed_unit(m.seed, key, 2);
                j.rotate += m.jitter.rotate * signed_unit(m.seed, key, 3);
                j.opacity += m.jitter.opacity * signed_unit(m.seed, key, 4);
            }
            const render::GlyphStyle s = style_of(lerp(from, to, progress(elapsed.count() - begins - delay)));
            out[gi] = s;
            any = any || !s.plain();
        }
        if (!any) out.clear();  // nothing changes this command's glyphs
    }
    return f;
}

}  // namespace omui
