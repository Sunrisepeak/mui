// openmui.site:wall — the components page's wall (the site plan §6, D4).
//
// The 36 components on one canvas (omui.ui.canvas), a band a category: far
// off each is a box, nearer a card with its name and purpose, close up the
// live component itself (the gallery's specimen), usable where it stands.
// A card clicked: the camera flies to it (MOTION-OK: camera navigation);
// its Details open the gallery's detail below; See all flies back out. A
// search dims the rest and flies to what it found; a style pack reskins the
// whole wall, drawn and live cards alike (the gallery's scope).
export module omui.site:wall;

import std;
import omui;
import omui.ui.canvas;
export import :gallery;

export namespace omui::site {

struct Wall {
    Signal<double>      x = signal(0.0), y = signal(0.0), zoom = signal(0.3);
    Signal<std::string> query = signal(std::string());
    Signal<std::int64_t> found = signal(std::int64_t{-1});  // how many the search found (-1: no search)
    bool                fitted = false;  // the camera has been somewhere: kept, not fitted again
};

// The cards a search matches (by id, name or purpose, either language), in
// the wall's order; every card for an empty one.
[[nodiscard]] std::vector<std::string> wall_matches(std::string_view query);

// The wall's scene: the bands and the cards, where they stand (world units).
inline constexpr std::string_view wall_scene = "component_wall";

[[nodiscard]] Element component_wall(Wall& wall, Gallery& g, const GallerySite& site, float width, float height);
// The same wall on a phone: a band a category, one above another, each a row
// of cards swiped sideways (the page keeps its own vertical scroll); a card
// tapped opens its detail.
[[nodiscard]] Element phone_wall(Gallery& g, const GallerySite& site, float width);

}  // namespace omui::site
