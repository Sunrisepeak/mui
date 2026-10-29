// The panel the film's pixel belongs to: the style workshop in the
// blueprint style on a headless host of its own, its pixel, and what drew it.
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;
import openmui.showcase.style_gallery;

namespace omui::promo {

const PanelPicture& panel_picture() {
    static const PanelPicture picture = [] {
        add_styles();
        typeset::FontCollection fonts;
        if (auto r = fonts.add_defaults(); !r) warn("promo: the default fonts did not load: {}", r.error());
        PanelPicture out;
        out.size = {1440, 900};
        for (const float scale : {1.0f, 2.0f}) {
            styles::Studio studio;
            Headless h(HeadlessOptions{.size = out.size, .scale = scale, .theme = blueprint(), .fonts = &fonts, .app = "openmui-promo-panel",
                                       .complete_frames = true});
            AppSpec spec = styles::app(studio, look_style);
            spec.setup(h.runtime());
            h.mount("main", spec.view());
            h.settle();
            auto surface = std::make_shared<raster::Surface>(h.pixels());
            if (scale == 1) {
                Runtime& rt = h.runtime();
                NodeId save{};
                rt.walk([&](NodeId n, int) {
                    if (!save && rt.path(n).ends_with("/save")) save = n;
                });
                if (!save) fatal("promo: the panel has no Save button");
                out.button = rt.bounds(save);
                // A pixel of the button's own fill by its rounded corner: the accent itself, with the
                // corner's softened pixels and the panel just beyond it.
                const raster::Rgba8 accent = raster::premultiply(rt.theme().color("accent"));
                int x = static_cast<int>(out.button.x) + 2, y = static_cast<int>(out.button.y) + 2;
                for (int k = 0; k < 24; ++k)
                    if (surface->at(static_cast<int>(out.button.x) + k, static_cast<int>(out.button.y) + k) == accent) {
                        x = static_cast<int>(out.button.x) + k + 1;
                        y = static_cast<int>(out.button.y) + k + 1;
                        break;
                    }
                out.pixel = {static_cast<float>(x), static_cast<float>(y)};
                const NodeId drew = h.node_at_pixel({out.pixel.x + 0.5f, out.pixel.y + 0.5f});
                const NodeId node = drew ? drew : save;
                out.path = rt.path(node);
                const spec::SemanticNode sem = rt.semantic_node(node);
                std::string effect = "none";
                for (const spec::ActionDesc& a : rt.actions(node))
                    if (a.name == "activate") effect = std::string(spec::name(a.effect));
                const std::string id = out.path.substr(out.path.rfind('/') + 1);
                out.provenance = std::format("{}#{} · role={} · effect={}", rt.type(node).name, id, spec::name(sem.role), effect);
                semantic::Tree tree(rt);
                out.tree = tree.snapshot({}).nodes;
                out.x1 = std::move(surface);
            } else {
                out.x2 = std::move(surface);
            }
        }
        return out;
    }();
    return picture;
}

Point panel_to_screen(Point p, Point at, Point look, double zoom) noexcept {
    return {static_cast<float>(at.x + (p.x - look.x) * zoom), static_cast<float>(at.y + (p.y - look.y) * zoom)};
}

void paint_panel(render::DisplayList& list, render::ImageStore& images, std::pair<render::ImageRef, render::ImageRef> refs, const PanelPicture& panel,
                 Size screen, Point at, Point look, double zoom, float opacity) {
    if (opacity <= 0.001f || !(zoom > 0)) return;
    if (opacity < 1) list.push_layer(opacity);
    // What of the panel the screen shows, in its 1× pixels.
    const double sx0 = std::max(0.0, look.x + (0 - at.x) / zoom), sy0 = std::max(0.0, look.y + (0 - at.y) / zoom);
    const double sx1 = std::min<double>(panel.size.width, look.x + (screen.width - at.x) / zoom);
    const double sy1 = std::min<double>(panel.size.height, look.y + (screen.height - at.y) / zoom);
    if (sx1 > sx0 && sy1 > sy0) {
        const double ix0 = std::floor(sx0), iy0 = std::floor(sy0), ix1 = std::ceil(sx1), iy1 = std::ceil(sy1);
        const Point d0 = panel_to_screen({static_cast<float>(ix0), static_cast<float>(iy0)}, at, look, zoom);
        const Point d1 = panel_to_screen({static_cast<float>(ix1), static_cast<float>(iy1)}, at, look, zoom);
        const Rect dest{d0.x, d0.y, d1.x - d0.x, d1.y - d0.y};
        // Larger than 1:1 its real pixels, square; smaller, the 2× picture; between, one over the other.
        const double big = smooth(hanim::dm::log(zoom) / hanim::dm::ln2, 0.15, 0.7);
        if (big < 1 && images.get(refs.second))
            list.draw_image(refs.second, {static_cast<float>(ix0 * 2), static_cast<float>(iy0 * 2), static_cast<float>((ix1 - ix0) * 2), static_cast<float>((iy1 - iy0) * 2)},
                            dest, render::Sampling::linear);
        if (big > 0 && images.get(refs.first))
            list.draw_image(refs.first, {static_cast<float>(ix0), static_cast<float>(iy0), static_cast<float>(ix1 - ix0), static_cast<float>(iy1 - iy0)}, dest,
                            zoom >= 2 ? render::Sampling::nearest : render::Sampling::linear, static_cast<float>(big));
        // Between the pixels, once each is large: the hairlines of a sheet of graph paper.
        const double lines = smooth(hanim::dm::log(zoom) / hanim::dm::ln2, 3.2, 5.5);
        if (lines > 0) {
            const Color c = hex(ink::paper_deep, static_cast<float>(0.55 * lines));
            const float w = std::max(1.0f, static_cast<float>(zoom * 0.035));
            for (double x = ix0; x <= ix1; ++x) {
                const float px = panel_to_screen({static_cast<float>(x), 0}, at, look, zoom).x;
                list.fill_rect({px - w / 2, std::max(0.0f, dest.y), w, std::min(screen.height, dest.height)}, c);
            }
            for (double y = iy0; y <= iy1; ++y) {
                const float py = panel_to_screen({0, static_cast<float>(y)}, at, look, zoom).y;
                list.fill_rect({std::max(0.0f, dest.x), py - w / 2, std::min(screen.width, dest.width), w}, c);
            }
        }
    }
    if (opacity < 1) list.pop_layer();
}

}  // namespace omui::promo
