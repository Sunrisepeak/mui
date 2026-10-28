module openmui.ui.canvas.kit;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.canvas;
import :parts;

namespace omui {

namespace kit {

void line(render::DisplayList& list, Point a, Point b, float w, Color c) {
    if (a.x == b.x || a.y == b.y) {
        // Along an axis: a rectangle (no path to flatten).
        const float h = w / 2;
        list.fill_rect(Rect::ltrb(std::min(a.x, b.x) - h, std::min(a.y, b.y) - h, std::max(a.x, b.x) + h, std::max(a.y, b.y) + h), c);
        return;
    }
    render::Path p;
    p.move_to(a);
    p.line_to(b);
    list.stroke_path(p, {.width = w, .cap = render::LineCap::round}, render::Paint::solid(c));
}

Value keys_value(std::span<const std::string> keys) {
    Value::List l;
    l.reserve(keys.size());
    for (const std::string& k : keys) l.emplace_back(k);
    return Value(std::move(l));
}

std::optional<NodeRef> node_of(Scene& s, const Value& v) {
    if (v.type() != Value::Type::text) return std::nullopt;
    return s.find(v.as_string());
}

double grid_step(double base, double zoom) noexcept {
    if (!(base > 0) || !(zoom > 0)) return 0;
    double s = base;
    // Powers of two of the base: the lines stay where they were as it changes.
    for (int i = 0; i < 64 && s * zoom < 16; ++i) s *= 2;
    for (int i = 0; i < 64 && s * zoom >= 32; ++i) s /= 2;
    return s;
}

}  // namespace kit

void register_canvas_kit() {
    world::register_behavior("select", kit::make_select);
    world::register_behavior("drag", kit::make_drag);
    world::register_behavior("resize", kit::make_resize);
    world::register_behavior("snap", kit::make_snap);
    world::register_behavior("connect", kit::make_connect);
    world::register_behavior("grid", kit::make_grid);
    world::register_behavior("minimap", kit::make_minimap);
    world::register_behavior("delete", kit::make_delete);
}

}  // namespace omui
