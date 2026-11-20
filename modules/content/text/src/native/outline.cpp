// A glyph as geometry: its outline through hb_font_draw_glyph, its colour
// layers through the OT COLR/CPAL API. Rasterising them is a renderer's.
module mlibs.text.api;

import std;
import mlibs.geom;
import mcpplibs.mwrap.harfbuzz;
using namespace mcpplibs::mwrap::harfbuzz::macros;

namespace mlibs::text {

namespace {

// Draw callbacks: font units (y up) into the outline (y down), unscaled.
struct Sink {
    GlyphOutline* out;

    void add(geom::PathVerb verb, std::initializer_list<geom::Point> points) {
        out->verbs.push_back(verb);
        out->points.insert(out->points.end(), points);
    }
};

void move_to(hb_draw_funcs_t*, void* d, hb_draw_state_t*, float x, float y, void*) {
    static_cast<Sink*>(d)->add(geom::PathVerb::move, {{x, -y}});
}
void line_to(hb_draw_funcs_t*, void* d, hb_draw_state_t*, float x, float y, void*) {
    static_cast<Sink*>(d)->add(geom::PathVerb::line, {{x, -y}});
}
void quad_to(hb_draw_funcs_t*, void* d, hb_draw_state_t*, float cx, float cy, float x, float y, void*) {
    static_cast<Sink*>(d)->add(geom::PathVerb::quad, {{cx, -cy}, {x, -y}});
}
void cubic_to(hb_draw_funcs_t*, void* d, hb_draw_state_t*, float c1x, float c1y, float c2x, float c2y, float x, float y, void*) {
    static_cast<Sink*>(d)->add(geom::PathVerb::cubic, {{c1x, -c1y}, {c2x, -c2y}, {x, -y}});
}
void close_path(hb_draw_funcs_t*, void* d, hb_draw_state_t*, void*) { static_cast<Sink*>(d)->add(geom::PathVerb::close, {}); }

hb_draw_funcs_t* draw_funcs() {
    static hb_draw_funcs_t* funcs = [] {
        hb_draw_funcs_t* f = hb_draw_funcs_create();
        hb_draw_funcs_set_move_to_func(f, move_to, nullptr, nullptr);
        hb_draw_funcs_set_line_to_func(f, line_to, nullptr, nullptr);
        hb_draw_funcs_set_quadratic_to_func(f, quad_to, nullptr, nullptr);
        hb_draw_funcs_set_cubic_to_func(f, cubic_to, nullptr, nullptr);
        hb_draw_funcs_set_close_path_func(f, close_path, nullptr, nullptr);
        hb_draw_funcs_make_immutable(f);
        return f;
    }();
    return funcs;
}

}  // namespace

GlyphOutline FontCollection::outline(FontRef font, std::uint32_t glyph) const {
    GlyphOutline out;
    Sink sink{&out};
    hb_font_draw_glyph(static_cast<hb_font_t*>(hb_font(font)), glyph, draw_funcs(), &sink);
    // Font units to ems.
    const float k = 1.0f / static_cast<float>(face(face_of(font)).units_per_em);
    for (geom::Point& p : out.points) p = {p.x * k, p.y * k};
    return out;
}

std::vector<ColorLayer> FontCollection::color_layers(FontRef font, std::uint32_t glyph) const {
    hb_face_t* face = hb_font_get_face(static_cast<hb_font_t*>(hb_font(font)));
    unsigned count = hb_ot_color_glyph_get_layers(face, glyph, 0, nullptr, nullptr);
    if (count == 0) return {};
    std::vector<hb_ot_color_layer_t> layers(count);
    hb_ot_color_glyph_get_layers(face, glyph, 0, &count, layers.data());
    unsigned colors = hb_ot_color_palette_get_colors(face, 0, 0, nullptr, nullptr);
    std::vector<hb_color_t> palette(colors);
    hb_ot_color_palette_get_colors(face, 0, 0, &colors, palette.data());
    std::vector<ColorLayer> out(count);
    for (unsigned i = 0; i < count; ++i) {
        out[i].glyph = layers[i].glyph;
        out[i].color = geom::Color::black();
        if (layers[i].color_index < palette.size()) {
            const hb_color_t c = palette[layers[i].color_index];
            out[i].color = geom::Color::rgba8(hb_color_get_red(c), hb_color_get_green(c), hb_color_get_blue(c), hb_color_get_alpha(c));
        }
    }
    return out;
}

}  // namespace mlibs::text
