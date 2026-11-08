// openmui.site:wall — the components page's wall (the site plan §6, D4).
//
// The 36 components on one canvas (omui.ui.canvas), a band a category, all
// of it shown at first: the live components themselves (the gallery's
// specimens) as soon as a card is a hundred pixels across, so the whole wall
// shows its details; far smaller, a card with its name, then a box. A few
// change state at random, and the one under the pointer. The camera keeps to
// the wall (bounded: never out past it, never panned onto nothing; past it,
// the wheel scrolls the page). A card double-clicked: the camera flies to it
// (MOTION-OK: camera navigation), close up and usable;
// its Details open the gallery's detail below; See all flies back out. A
// search dims the rest and flies to what it found; a style pack reskins the
// whole wall, drawn and live cards alike (the gallery's scope).
export module omui.site:wall;

import std;
import omui;
import omui.ui.canvas;
export import :gallery;
import :live_wall;

export namespace omui::site {

struct Wall {
    Signal<double>      x = signal(0.0), y = signal(0.0), zoom = signal(0.3);
    Signal<std::string> query = signal(std::string());
    Signal<std::int64_t> found = signal(std::int64_t{-1});  // how many the search found (-1: no search)
    bool                fitted = false;  // the camera has been somewhere: kept, not fitted again
    // Alive: a few cards changing state at random, the one under the pointer too.
    LiveWall            live;
};

// The cards a search matches (by id, name or purpose, either language), in
// the wall's order; every card for an empty one.
[[nodiscard]] std::vector<std::string> wall_matches(std::string_view query);

// The wall's scene: the bands and the cards, where they stand (world units).
inline constexpr std::string_view wall_scene = "component_wall";

[[nodiscard]] Element component_wall(Wall& wall, Gallery& g, const GallerySite& site, float width, float height);
void component_wall_tick(Wall& wall, Gallery& g, const GallerySite& site);
// The same wall on a phone: a band a category, one above another, each a row
// of cards swiped sideways (the page keeps its own vertical scroll); a card
// tapped opens its detail.
[[nodiscard]] Element phone_wall(Gallery& g, const GallerySite& site, float width);

}  // namespace omui::site
