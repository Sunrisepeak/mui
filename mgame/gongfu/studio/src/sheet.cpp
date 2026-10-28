module gongfu.studio;

import std;
import gongfu;
import gongfu.present2d;
import openmui.geom;
import openmui.render.vector;
import openmui.raster;
import openmui.image;

namespace gongfu::studio {

namespace {

namespace p2 = gongfu::present2d;
namespace render = omui::render;
using omui::geom::Color;

}  // namespace

std::expected<Sheet, std::string> sheet(const Fighter& fighter, const p2::Look& look, int columns) {
    // A fighter whose moves include one per clip, so each clip plays as a move does.
    FighterDef def = fighter.def;
    for (const auto& [name, clip] : look.clips)
        if (std::ranges::find(def.moves, name, &MoveDef::name) == def.moves.end())
            def.moves.push_back({.name = name, .command = {.buttons = bit(Button::lp)}, .total = std::max(clip.frames, 1)});
    auto compiled = compile(def);
    if (!compiled) return std::unexpected(compiled.error().front().where + ": " + compiled.error().front().text);
    const Fighter& f = *compiled;
    const float cell = 240, ground = 30;
    const int rows = (static_cast<int>(look.clips.size()) + columns - 1) / columns;
    Sheet out;
    out.width = static_cast<int>(cell) * columns;
    out.height = static_cast<int>(cell) * std::max(rows, 1);
    render::DisplayList dl;
    dl.fill_rect({0, 0, static_cast<float>(out.width), static_cast<float>(out.height)}, Color::hex(0xf4f1ea));
    p2::Painter painter;
    const p2::Scene scene{.width = cell, .height = cell, .ground = cell - ground};
    const p2::Camera camera{0, 0, 0.9};
    int k = 0;
    for (const auto& [name, clip] : look.clips) {
        out.cells.push_back(name);
        const float ox = cell * static_cast<float>(k % columns), oy = cell * static_cast<float>(k / columns);
        dl.push_clip({ox, oy, cell, cell});
        dl.push_transform(omui::geom::Affine::translate(ox, oy));
        dl.fill_rect({4, cell - ground, cell - 8, 1}, Color::hex(0xb7aa90));
        const std::int32_t move = f.find(name);
        const MoveDef& m = f.def.moves[static_cast<std::size_t>(move)];
        const std::int32_t last = std::max(clip.frames, 1);
        // The frame the cell shows: the move's first active frame (its boxes are
        // drawn on it), else the clip's middle. The first and last frames are ghosts.
        const std::int32_t first_active = f.startup(move);
        const std::int32_t shown = first_active <= m.total ? first_active : std::max(1, (last + 1) / 2);
        for (const std::int32_t at : {1, last, shown}) {
            FighterState s;
            s.mode = Mode::move;
            s.move = move;
            s.move_frame = at;
            s.facing = 1;
            const bool solid = at == shown;
            if (!solid) dl.push_layer(0.25f);
            painter.body(look, f, s, 0, scene, camera, dl);
            if (!solid) dl.pop_layer();
        }
        // The move's boxes on its first active frame: hits red, hurt blue.
        const std::int32_t active = f.startup(move);
        auto outline = [&](const Box& b, Color c) {
            const p2::Point a = p2::on_picture(scene, camera, b.x.to_double(), (b.y + b.h).to_double());
            const p2::Point z = p2::on_picture(scene, camera, (b.x + b.w).to_double(), b.y.to_double());
            dl.stroke_round_rect({a.x, a.y, z.x - a.x, z.y - a.y}, {}, 1.5f, render::Paint::solid(c));
        };
        if (active <= m.total) {
            for (const HitDef& h : m.hits)
                if (h.active.contains(active))
                    for (const Box& b : h.boxes) outline(b, Color::hex(0xc0392b));
            for (const HurtDef& h : m.hurt)
                if (h.frames.contains(active))
                    for (const Box& b : h.boxes) outline(b, Color::hex(0x2f6fd1));
        }
        dl.pop_transform();
        dl.pop_clip();
        ++k;
    }
    omui::raster::Surface px(out.width, out.height);
    omui::render::CpuRenderer{}.render(dl, px, {}, {.scale = 1});
    const omui::imageio::View v{reinterpret_cast<const std::uint8_t*>(px.row(0)), px.width(), px.height(), px.width() * 4,
                                omui::imageio::Alpha::premultiplied};
    auto png = omui::imageio::encode_png(v);
    if (!png) return std::unexpected(std::string("the sheet did not encode as PNG"));
    out.png = std::move(*png);
    return out;
}

}  // namespace gongfu::studio
