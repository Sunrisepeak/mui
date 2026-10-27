// openmui.render:shared — what the CPU and GPU renderers read a display list
// with, written once: its paths as the rasteriser's, and where a glyph is
// drawn from (one placement for the CPU renderer, its glyph pass and the GPU
// renderer, so all ask the glyph source the same questions and put the
// answers in the same place).
module openmui.render:shared;

import std;
import openmui.geom;
import openmui.raster;
import :display;

namespace omui::render {

// A display list's path as the rasteriser's (the verbs are the same, in the
// same order; the conversion keeps the verb array).
struct PathConv {
    std::vector<raster::Verb> verbs;
    raster::PathData data(const PathView& v) {
        verbs.resize(v.verbs.size());
        for (std::size_t i = 0; i < v.verbs.size(); ++i) verbs[i] = static_cast<raster::Verb>(v.verbs[i]);
        return {verbs, v.points};
    }
};

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

}  // namespace omui::render
