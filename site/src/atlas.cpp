module openmui.site;

import std;
import openmui;
import openmui.ui.canvas;

namespace omui::site {

AtlasLayout atlas_layout(std::span<const Place> places, Size view) {
    AtlasLayout l;
    // Whole units, rounded up: a window at zoom 1 covers the view to its last fractional pixel.
    l.view = {std::ceil(std::max(view.width, 320.0f)), std::ceil(std::max(view.height, 240.0f))};
    l.gap = std::round(std::max(l.view.width, l.view.height) * 0.08f);
    const std::size_t columns = places.size() <= 4 ? 2 : 3;
    const double w = l.view.width, h = l.view.height, step_x = w + l.gap, step_y = h + label_strip + l.gap;
    for (std::size_t i = 0; i < places.size(); ++i) {
        const world::WorldRect r{static_cast<double>(i % columns) * step_x, label_strip + static_cast<double>(i / columns) * step_y, w, h};
        l.windows.emplace_back(std::string(places[i].id), r);
        const world::WorldRect with_label{r.x, r.y - label_strip, r.width, r.height + label_strip};
        l.bounds = l.bounds.empty() ? with_label : l.bounds.united(with_label);
    }
    return l;
}

float live_from(const AtlasLayout& layout) noexcept { return 0.8f * std::max(layout.view.width, layout.view.height); }

void lay_out(world::Scene& scene, const AtlasLayout& layout, std::span<const Place> places, std::string_view lang) {
    for (std::size_t i = 0; i < layout.windows.size() && i < places.size(); ++i) {
        const auto& [id, r] = layout.windows[i];
        auto put = [&](std::string_view kind, std::string_view key, const world::WorldRect& at) {
            const auto n = scene.find(key);
            const world::NodeRef node = n ? *n : scene.add({.kind = kind, .x = at.x, .y = at.y, .width = 1, .height = 1, .key = key});
            scene.move(node, at.x, at.y);
            scene.resize(node, static_cast<float>(at.width), static_cast<float>(at.height));
            return node;
        };
        const world::NodeRef window = put("place", id, r);
        scene.set(window, "title", std::string(lang == "zh" ? places[i].zh : places[i].en));
        const world::NodeRef label = put("label", id + "-label", {r.x, r.y - label_strip, r.width, label_strip});
        scene.set(label, "title", std::string(lang == "zh" ? places[i].zh : places[i].en));
        scene.preset(id, {r.x + r.width / 2, r.y + r.height / 2, 1});
    }
    // The overview: every window whole, with a margin.
    const world::WorldRect& b = layout.bounds;
    const double zoom = 0.9 * std::min(layout.view.width / b.width, layout.view.height / b.height);
    scene.preset("overview", {b.x + b.width / 2, b.y + b.height / 2, zoom});
}

}  // namespace omui::site
