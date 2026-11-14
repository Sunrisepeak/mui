// openmui.site:wall — the studio: the site's core features on one designer's
// canvas (the site plan §6–7, D4, E2): regions side by side — the components
// (a band a category), the signature works, the compositions, the recipes —
// each card live (a component, a work, a scene, a recipe on its card), all of
// it in reach. The places' buttons arrive at their region; the region chips
// fly between them (MOTION-OK: camera navigation); a double-click flies to a
// card, close up and usable; a double-click on nothing, back to all of it. A
// few cards play at random, the one under the pointer plays (a pause stops
// them). The camera keeps to the canvas (bounded) and never zooms past 2.5×;
// a recipe's Tune opens its editor beside the canvas, an inspector.
export module omui.site:wall;

import std;
import omui;
import omui.ui.canvas;
export import :gallery;
import :live_wall;
import :works;

export namespace omui::site {

struct Wall {
    Signal<double>      x = signal(0.0), y = signal(0.0), zoom = signal(0.3);
    Signal<std::string> query = signal(std::string());
    Signal<std::int64_t> found = signal(std::int64_t{-1});  // how many the search found (-1: no search)
    bool                fitted = false;  // the camera has been somewhere: kept, not fitted again
    // Alive: a few cards changing state at random, the one under the pointer too.
    LiveWall            live;
    MotionWall          compositions;  // the compositions' scenes on the canvas
    Signal<std::string> region = signal(std::string("components"));  // the region chosen in its chips
    Signal<bool>        tuning = signal(false);                      // the recipe editor open beside it
    std::string         arrived;  // the region the place last opened it at (another place: arrives at its own)
};

// What the studio shows besides the components: the works, the recipes, the compositions.
struct StudioSite {
    Gallery*       gallery = nullptr;
    GallerySite    gallery_site;
    Works*         works = nullptr;
    WorksSite      works_site;
    MotionWallSite compositions_site;
    std::string    root;  // the studio's node path (a place's `…/page/wall`)
};
// Its regions, by name (their nodes' keys are `region-<name>`).
inline constexpr std::array<std::string_view, 4> studio_regions{"components", "works", "compositions", "recipes"};

// The cards a search matches (by id, name or purpose, either language), in
// the wall's order; every card for an empty one.
[[nodiscard]] std::vector<std::string> wall_matches(std::string_view query);

// The studio's scene: its regions, the bands and the cards, where they stand (world units).
inline constexpr std::string_view wall_scene = "component_wall";

// The studio, opened at `region`, `width` × `height`.
[[nodiscard]] Element studio(Wall& wall, const StudioSite& site, std::string_view region, float width, float height);
void studio_tick(Wall& wall, const StudioSite& site);
// The same wall on a phone: a band a category, one above another, each a row
// of cards swiped sideways (the page keeps its own vertical scroll); a card
// tapped opens its detail.
[[nodiscard]] Element phone_wall(Gallery& g, const GallerySite& site, float width);

}  // namespace omui::site
