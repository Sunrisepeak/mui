// openmui.render:shared — what the CPU and GPU renderers read a display list
// with, written once: its paths as the rasteriser's, and where a glyph is
// drawn from (one placement for the CPU renderer, its glyph pass and the GPU
// renderer, so all ask the glyph source the same questions and put the
// answers in the same place).
module xengine.render.vector.api:shared;

import std;
import mlibs.geom;
import mlibs.raster;
import :display;

namespace xengine::render {

// Where a glyph's mask is asked for and placed: one function for the bands
// and for the pass that resolves their glyphs first, so both ask the same.
inline bool simple_glyphs(const Affine& m) noexcept { return m.b == 0 && m.c == 0 && m.a == m.d && m.a > 0; }

struct MaskCall {
    float ix, iy;
    float size;
    Point fraction;
};

inline MaskCall mask_call(Point pen, float size, const Affine& m) noexcept {
    const Point d = m.apply(pen);
    const float ix = std::floor(d.x), iy = std::floor(d.y);
    return {ix, iy, size * m.a, {d.x - ix, d.y - iy}};
}

// How glyph i of a command is drawn under the list's transform m (D-68):
// through the glyph cache's masks at an exact pixel placement when nothing
// but a translation applies (the glyph's own style may move it), else as an
// outline (or, for a colour glyph, its image) under `outline`.
struct Placed {
    bool              mask = true;
    Point             pen;          // the glyph's origin, list coordinates
    Affine            outline;      // glyph space (ems) to device, when not a mask
    const GlyphStyle* style = nullptr;
};

inline Placed place(const DrawGlyphs& c, std::size_t i, const Affine& m) noexcept {
    const Glyph& g = c.glyphs[i];
    Placed p;
    p.pen = {c.origin.x + g.position.x, c.origin.y + g.position.y};
    if (!c.styles.empty() && !c.styles[i].plain()) p.style = &c.styles[i];
    if (!p.style || p.style->translation_only()) {
        if (p.style) p.pen = {p.pen.x + p.style->transform.e, p.pen.y + p.style->transform.f};
        p.mask = simple_glyphs(m);
        p.outline = m * Affine::translate(p.pen.x, p.pen.y) * Affine::scale(c.size, c.size);
        return p;
    }
    p.mask = false;
    p.outline = m * Affine::translate(p.pen.x, p.pen.y) * p.style->transform * Affine::scale(c.size, c.size);
    return p;
}

// A rounded rectangle's stroke (centred on its edge) as the area between two
// rounded rectangles: the outer edge — each radius grown by half the width, a
// sharp corner kept sharp as a miter joins it — and the inner, each radius
// shrunk by as much and none below zero. Both renderers draw it so: the CPU
// fills the two even-odd, the GPU subtracts their exact areas (ui.slang's
// rring), and neither strokes a curve (whose inner offset over-covers).
struct Ring {
    Rect        outer;
    CornerRadii outer_radii;
    Rect        inner;
    CornerRadii inner_radii;
    bool        hollow = false;  // the inner is not empty
};

inline Ring stroke_ring(const Rect& r, const CornerRadii& radii, float width) noexcept {
    const float h = width / 2;
    const CornerRadii f = radii.zero() ? CornerRadii{} : radii.fitted(r.size());
    auto out = [h](float x) { return x > 0 ? x + h : 0.0f; };
    auto in = [h](float x) { return std::max(x - h, 0.0f); };
    Ring ring;
    ring.outer = {r.x - h, r.y - h, r.width + width, r.height + width};
    ring.outer_radii = {out(f.top_left), out(f.top_right), out(f.bottom_right), out(f.bottom_left)};
    ring.inner = {r.x + h, r.y + h, r.width - width, r.height - width};
    ring.inner_radii = {in(f.top_left), in(f.top_right), in(f.bottom_right), in(f.bottom_left)};
    ring.hollow = ring.inner.width > 0 && ring.inner.height > 0;
    return ring;
}

}  // namespace xengine::render
