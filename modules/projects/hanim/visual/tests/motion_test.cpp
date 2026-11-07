// hanim.visual: matter taken from display lists, and morphs between two
// drawings — matched by node, by meaning and glyph by glyph; pure functions
// of time; settled, the second drawing's pixels exactly.
import std;
import hanim.core;
import mlibs.geom;
import xrender.raster;
import xrender.vector;
import hanim.visual;
import mlibs.testing;

using namespace hanim;
using namespace mlibs::testing;
namespace mo = hanim::visual;
using mlibs::geom::Affine;
using mlibs::geom::Color;
using mlibs::geom::Rect;

namespace {

// Node markers named by the test: their keys are "n<marker>" unless renamed.
struct Names {
    std::map<std::uint32_t, std::string> key, meaning;
    std::set<std::uint32_t>              nested;
    mo::Resolver resolver() const {
        return {.keys = [this](std::uint32_t m, std::string& k, std::string& s) {
                    const auto it = key.find(m);
                    k = it != key.end() ? it->second : std::format("n{}", m);
                    if (const auto j = meaning.find(m); j != meaning.end()) s = j->second;
                },
                .nested = [this](std::uint32_t m) { return nested.contains(m); }};
    }
};

// A node 1 holding a rounded rectangle at `x`, coloured `c`, and (node 2) a label.
xrender::DisplayList card(float x, Color c, std::span<const xrender::Glyph> glyphs = {}) {
    xrender::DisplayList l;
    l.begin_node(1);
    l.push_transform(Affine::translate(10, 10));
    l.fill_round_rect({x, 0, 40, 20}, mlibs::geom::CornerRadii::all(6), xrender::Paint::solid(c));
    if (!glyphs.empty()) {
        l.begin_node(2);
        l.draw_glyphs(xrender::FontRef{1}, 16, {x, 40}, glyphs, xrender::Paint::solid(Color::hex(0x111827)));
        l.end_node();
    }
    l.pop_transform();
    l.end_node();
    return l;
}

const xrender::FillRoundRect* first_round(const xrender::DisplayList& l) {
    for (const auto& c : l.commands())
        if (const auto* f = std::get_if<xrender::FillRoundRect>(&c)) return f;
    return nullptr;
}

// Where the list draws its first rounded rectangle, through the transforms around it.
Rect drawn_rect(const xrender::DisplayList& l) {
    std::vector<Affine> t{Affine{}};
    for (const auto& c : l.commands()) {
        if (const auto* p = std::get_if<xrender::PushTransform>(&c)) t.push_back(t.back() * p->transform);
        else if (std::holds_alternative<xrender::PopTransform>(c)) t.pop_back();
        else if (const auto* f = std::get_if<xrender::FillRoundRect>(&c)) return t.back().map(f->rect);
    }
    return {};
}

xrender::raster::Surface pixels(const xrender::DisplayList& l) {
    xrender::raster::Surface s(120, 80);
    xrender::CpuRenderer cpu;
    xrender::ImageStore images;
    (void)cpu.render(l, s, {&images, nullptr}, {.scale = 1, .background = Color::hex(0xffffff)});
    return s;
}

bool same(const xrender::raster::Surface& a, const xrender::raster::Surface& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (!(a.at(x, y) == b.at(x, y))) return false;
    return true;
}

std::vector<xrender::Glyph> run(std::initializer_list<std::uint32_t> ids) {
    std::vector<xrender::Glyph> g;
    float x = 0;
    for (const std::uint32_t id : ids) g.push_back({.id = id, .position = {x, 0}}), x += 9;
    return g;
}

mo::Options linear(double seconds) {
    mo::Options o;
    o.curve.kind = mo::Curve::Kind::tween;
    o.curve.duration = seconds;
    o.curve.ease = hanim::ease::linear;
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"ui.motion"};

    s.test("matter: each drawable a piece with its node, its place among the node's pieces of its class, its transform, clip and opacity", [] {
        xrender::DisplayList l;
        l.begin_node(7);
        l.push_transform(Affine::translate(5, 6));
        l.fill_rect({0, 0, 10, 10}, Color::hex(0xff0000));
        l.push_clip(Rect{0, 0, 50, 50});
        l.push_layer(0.5f);
        l.fill_round_rect({1, 1, 4, 4}, mlibs::geom::CornerRadii::all(2), xrender::Paint::solid(Color::hex(0x00ff00)));
        l.begin_node(8);
        l.fill_rect({2, 2, 3, 3}, Color::hex(0x0000ff));
        l.end_node();
        l.pop_layer();
        l.pop_clip();
        l.pop_transform();
        l.end_node();
        Names names;
        const mo::Matter m = mo::Matter::extract(l, names.resolver());
        check_eq(m.pieces().size(), std::size_t{3});
        check_eq(m.nodes().size(), std::size_t{2});
        check_eq(m.nodes()[0].key, std::string("n7"));
        const auto& a = m.pieces()[0];
        const auto& b = m.pieces()[1];
        const auto& c = m.pieces()[2];
        check(a.node == 0 && a.ordinal == 0 && b.node == 0 && b.ordinal == 1, "two fills of node 7, in order");
        check(c.node == 1 && c.ordinal == 0, "node 8's own first fill");
        check(a.clip == -1 && b.clip == 0 && c.clip == 0, "the clip around the later two");
        check(a.opacity == 1 && b.opacity == 0.5f && c.opacity == 0.5f, "the plain layer's opacity carried");
        check(b.transform == Affine::translate(5, 6), "the transform from the range's start");
        check_eq(m.bounds(), Rect{5, 6, 10, 10});
    });

    s.test("matter: a style's effect layer around everything is the wrapper; one inside is a group; a nested root's range is drawn whole", [] {
        xrender::DisplayList l;
        const std::array<float, 2> params{0.3f, 0.7f};
        l.begin_node(1);
        l.push_layer(1, xrender::BlendMode::normal, xrender::EffectRef{3}, params, {0, 0, 50, 50});
        l.fill_rect({0, 0, 50, 50}, Color::hex(0x222222));
        l.push_layer(1, xrender::BlendMode::screen);
        l.fill_rect({1, 1, 2, 2}, Color::hex(0xffffff));
        l.pop_layer();
        l.begin_node(9);
        l.fill_rect({4, 4, 4, 4}, Color::hex(0x00ffff));
        l.end_node();
        l.pop_layer();
        l.end_node();
        Names names;
        names.nested = {9};
        const mo::Matter m = mo::Matter::extract(l, names.resolver());
        check(m.wrapper().present && m.wrapper().command == 1, "the outer effect layer is the wrapper");
        check_eq(m.pieces().size(), std::size_t{3});
        check(m.pieces()[1].cls == mo::PieceClass::group && m.pieces()[1].nested == 0, "a blended layer inside: a group drawn whole");
        check(m.pieces()[2].cls == mo::PieceClass::group && m.pieces()[2].nested == 9, "the nested root: a group");
        check_eq(m.pieces()[2].end - m.pieces()[2].command, std::size_t{3});
    });

    s.test("a morph moves a matched piece: its place, radii and colour between the two drawings (OKLab), a pure function of time", [] {
        Names names;
        const auto a = card(0, Color::hex(0xff0000));
        const auto b = card(60, Color::hex(0x0000ff));
        mo::Morph m(mo::Matter::extract(a, names.resolver()), mo::Matter::extract(b, names.resolver()), linear(1.0), 2.0);
        check_eq(m.stats().matched, std::size_t{1});
        check_eq(m.end(), 3.0);
        auto at = [&](double t) {
            xrender::DisplayList out;
            m.draw(t, out);
            check(out.balanced(), "pushes and pops balanced");
            return out;
        };
        check_eq(drawn_rect(at(1.0)), Rect{10, 10, 40, 20}, std::source_location::current());
        check_eq(drawn_rect(at(2.5)), Rect{40, 10, 40, 20}, std::source_location::current());
        check_eq(drawn_rect(at(9.0)), Rect{70, 10, 40, 20}, std::source_location::current());
        const auto mid = first_round(at(2.5))->paint.color;
        check(mid.r > 0.05f && mid.b > 0.05f && mid.g < mid.r + 0.2f, std::format("between red and blue: {} {} {}", mid.r, mid.g, mid.b));
        check_eq(at(2.5).hash(), at(2.5).hash());
        // Settled, the second drawing's pixels exactly.
        check(same(pixels(at(9.0)), pixels(b)), "at rest, the second drawing");
        check(same(pixels(at(0.0)), pixels(a)), "before its start, the first");
    });

    s.test("text changes glyph by glyph: the glyphs both have travel, the new ones appear, the gone ones fade", [] {
        Names names;
        const auto save = run({11, 12, 13, 14});          // "Save"
        const auto saved = run({11, 12, 13, 14, 15, 16});  // "Saved ✓"
        mo::Morph m(mo::Matter::extract(card(0, Color::hex(0x10b981), save), names.resolver()),
                    mo::Matter::extract(card(0, Color::hex(0x10b981), saved), names.resolver()), linear(1), 0);
        check_eq(m.stats().glyphs_kept, std::size_t{4});
        check_eq(m.stats().glyphs_new, std::size_t{2});
        check_eq(m.stats().glyphs_gone, std::size_t{0});
        xrender::DisplayList out;
        m.draw(0.2, out);
        const xrender::DrawGlyphs* text = nullptr;
        for (const auto& c : out.commands())
            if (const auto* g = std::get_if<xrender::DrawGlyphs>(&c)) text = g;
        check(text && text->styles.size() == 6, "the second run drawn with a style a glyph");
        if (text && text->styles.size() == 6) {
            check(text->styles[0].opacity == 1 && text->styles[3].opacity == 1, "kept glyphs whole");
            check(text->styles[4].opacity < 0.1f && text->styles[5].opacity < 0.1f, "new glyphs still faint at 0.2");
        }
        // A word replaced in the middle: its common letters kept.
        mo::Morph w(mo::Matter::extract(card(0, Color::hex(0), run({1, 2, 3, 4, 5})), names.resolver()),
                    mo::Matter::extract(card(0, Color::hex(0), run({1, 9, 3, 5})), names.resolver()), linear(1), 0);
        check_eq(w.stats().glyphs_kept, std::size_t{3});
        check_eq(w.stats().glyphs_gone, std::size_t{2});
    });

    s.test("by meaning: a node whose key changed but whose role and name did not is matched; ambiguous meanings are not", [] {
        Names before, after;
        before.key[1] = "main/list/a";
        before.meaning[1] = "button|Delete";
        after.key[1] = "main/dialog/confirm";
        after.meaning[1] = "button|Delete";
        mo::Morph m(mo::Matter::extract(card(0, Color::hex(0xef4444)), before.resolver()),
                    mo::Matter::extract(card(50, Color::hex(0xef4444)), after.resolver()), linear(1), 0);
        check_eq(m.stats().by_meaning, std::size_t{1});
        check_eq(m.stats().matched, std::size_t{1});
        Names other;
        other.key[1] = "main/elsewhere";
        mo::Morph none(mo::Matter::extract(card(0, Color::hex(0xef4444)), before.resolver()),
                       mo::Matter::extract(card(50, Color::hex(0xef4444)), other.resolver()), linear(1), 0);
        check_eq(none.stats().matched, std::size_t{0});
        check(none.stats().from_only == 1 && none.stats().to_only == 1, "unmatched: one fades out, one in");
    });

    s.test("what only one drawing has fades: out by fade_out, in from fade_in; staggered by distance, the far ones later", [] {
        Names names;
        xrender::DisplayList a, b;
        a.begin_node(1);
        a.fill_rect({0, 0, 10, 10}, Color::hex(0xff0000));
        a.end_node();
        b.begin_node(1);
        b.fill_rect({0, 0, 10, 10}, Color::hex(0xff0000));
        b.fill_rect({100, 0, 10, 10}, Color::hex(0x00ff00));
        b.fill_rect({50, 0, 10, 10}, Color::hex(0x0000ff));
        b.end_node();
        mo::Options o = linear(1);
        o.order = mo::Order::distance;
        o.stagger = 0.1;
        mo::Morph m(mo::Matter::extract(a, names.resolver()), mo::Matter::extract(b, names.resolver()), o, 0);
        check_eq(m.stats().to_only, std::size_t{2});
        check_eq(m.end(), 1.2);
        xrender::DisplayList out;
        m.draw(0.2, out);
        float faint = 1;
        for (const auto& c : out.commands())
            if (const auto* f = std::get_if<xrender::FillRect>(&c); f && f->paint.color.g > 0.9f) faint = f->paint.color.a;
        check(faint == 1, "the new piece is not drawn yet (its alpha would be > 0)");
    });

    s.test("interrupted: the drawing as it is becomes matter again, its pieces keeping their nodes, and the next morph starts from there", [] {
        Names names;
        mo::Morph m(mo::Matter::extract(card(0, Color::hex(0xff0000), run({1, 2})), names.resolver()),
                    mo::Matter::extract(card(60, Color::hex(0xff0000), run({1, 2})), names.resolver()), linear(1), 0);
        const mo::Matter now = m.snapshot(0.5);
        check_eq(now.pieces().size(), std::size_t{2});
        check(now.nodes().size() == 2 && now.nodes()[0].key == "n1" && now.nodes()[1].key == "n2", "the nodes' keys kept");
        mo::Morph next(m.snapshot(0.5), mo::Matter::extract(card(-20, Color::hex(0xff0000), run({1, 2})), names.resolver()), linear(1), 0.5);
        check_eq(next.stats().matched, std::size_t{2});
        xrender::DisplayList out;
        next.draw(0.5, out);
        check_eq(drawn_rect(out), Rect{40, 10, 40, 20}, std::source_location::current());
    });

    s.test("a spring's curve: 0 at its start, at rest 1 exactly; an interrupted morph's velocity carries on", [] {
        mo::Curve c;
        check_eq(c.at(0), 0.0);
        check_eq(c.at(c.length() + 0.01), 1.0);
        check(c.at(0.1) > 0 && c.at(0.1) < 1, "under way");
        mo::Curve fast = c;
        fast.velocity = 8;
        check(fast.at(0.05) > c.at(0.05), "carried velocity: ahead at once");
    });

    s.test("a style's effect on the node stays around the moving pieces, its numbers between the two", [] {
        Names names;
        auto glow = [](float x, float strength) {
            xrender::DisplayList l;
            const std::array<float, 1> p{strength};
            l.begin_node(1);
            l.push_layer(1, xrender::BlendMode::normal, xrender::EffectRef{5}, p, {0, 0, 100, 40});
            l.fill_rect({x, 0, 10, 10}, Color::hex(0x22d3ee));
            l.pop_layer();
            l.end_node();
            return l;
        };
        mo::Morph m(mo::Matter::extract(glow(0, 0.2f), names.resolver()), mo::Matter::extract(glow(40, 1.0f), names.resolver()), linear(1), 0);
        xrender::DisplayList out;
        m.draw(0.5, out);
        const xrender::PushLayer* layer = nullptr;
        for (const auto& c : out.commands())
            if (const auto* p = std::get_if<xrender::PushLayer>(&c); p && p->effect.id == 5) layer = p;
        check(layer && layer->params.size() == 1 && std::abs(layer->params[0] - 0.6f) < 1e-5f, "the effect's strength half way");
    });

    s.test("content sized effect groups retain a measured envelope through morph and snapshots", [] {
        Names names;const auto blur=*xrender::effects().find("blur");
        auto scene=[&](float x){xrender::DisplayList list;list.begin_node(1);
            list.fill_rect({0,0,3,3},Color::white());
            list.push_transform(Affine::translate(x,10));
            const auto values=xrender::effects().params(blur,std::array{std::pair<std::string_view,float>{"radius",2}});
            list.push_layer(1,xrender::BlendMode::normal,blur,values,{}, {},xrender::LayerBoundsMode::content);
            list.fill_rect({10,10,30,20},Color::white());list.pop_layer();list.pop_transform();list.end_node();return list;};
        auto a=scene(10),b=scene(40);auto source=mo::Matter::extract(a,names.resolver());
        const auto group=std::ranges::find(source.pieces(),mo::PieceClass::group,&mo::Piece::cls);
        check(group!=source.pieces().end() && group->bounds.width>=30 && group->bounds.height>=20);
        mo::Morph morph(std::move(source),mo::Matter::extract(b,names.resolver()),linear(1),0);
        auto pixels=[](const xrender::DisplayList& list){xrender::raster::Surface surface(120,80);xrender::CpuRenderer cpu;
            check(cpu.render(list,surface,{}).effects_skipped==0);return surface.hash();};
        for(double at:{0.,.25,.5,.75,1.}) {
            xrender::DisplayList frame;morph.draw(at,frame);
            check(std::ranges::any_of(frame.commands(),[](const auto& command){const auto* layer=std::get_if<xrender::PushLayer>(&command);
                return layer && layer->bounds_mode==xrender::LayerBoundsMode::content;}));
            if(at==0)check(pixels(frame)==pixels(a));if(at==1)check(pixels(frame)==pixels(b));
            auto snapshot=mo::Matter::extract(frame,names.resolver());check(!snapshot.pieces().empty());
        }
    });
    return s.run(argc, argv);
}
