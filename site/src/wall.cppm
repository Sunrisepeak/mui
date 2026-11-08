// openmui.site:wall — the components page's wall (the site plan §6, D4).
//
// The 36 components on one canvas (omui.ui.canvas), a band a category: far
// off each is a box, nearer a card with its name and purpose, close up the
// live component itself (the gallery's specimen), usable where it stands.
// A card clicked: the camera flies to it (MOTION-OK: camera navigation);
// its Details open the gallery's detail below; See all flies back out.
export module omui.site:wall;

import std;
import omui;
import omui.ui.canvas;
export import :gallery;

export namespace omui::site {

struct Wall {
    Signal<double> x = signal(0.0), y = signal(0.0), zoom = signal(0.3);
    bool           fitted = false;  // the camera has been somewhere: kept, not fitted again
};

// The wall's scene: the bands and the cards, where they stand (world units).
inline constexpr std::string_view wall_scene = "component_wall";

[[nodiscard]] Element component_wall(Wall& wall, Gallery& g, const GallerySite& site, float width, float height);

}  // namespace omui::site
