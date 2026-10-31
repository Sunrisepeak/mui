// openmui.site:atlas — the site's places as windows on one infinite canvas
// (D-84 §6.6: places, not pages; S1's way). Each window is the size of the
// view, so a place is read at zoom 1, exactly as a page would be; the
// overview fits them all, and moving between two places is the camera
// flying out and in again.
export module openmui.site:atlas;

import std;
import openmui;
import openmui.ui.canvas;
import :app;

export namespace omui::site {

struct AtlasLayout {
    Size                                                   view{};    // the canvas's size, logical px
    float                                                  gap = 0;   // between windows, world units
    std::vector<std::pair<std::string, world::WorldRect>>  windows;   // by place id, in the places' order
    world::WorldRect                                       bounds;    // of every window and its label
};

// The windows of `places` for a view of `view`: rows of two (three from five
// places on), a label's strip above each.
[[nodiscard]] AtlasLayout atlas_layout(std::span<const Place> places, Size view);

// The height of a window's label strip, world units (above its window).
inline constexpr double label_strip = 64;

// The scene as the layout says: each window (kind "place", key the place's
// id) and its label (kind "label", key "<place>-label", its text column
// "title"), moved and resized where they exist, added where not; a camera
// preset per place (its window filling the view at zoom 1) and "overview".
void lay_out(world::Scene& scene, const AtlasLayout& layout, std::span<const Place> places, std::string_view lang);

// The size on screen (its larger side, logical px) from which a place's
// window is live rather than a texture: a little short of filling the view.
[[nodiscard]] float live_from(const AtlasLayout& layout) noexcept;

}  // namespace omui::site
