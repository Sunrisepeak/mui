// openmui.ui.canvas.kit:parts — what the behaviours share (not exported):
// geometry between the scene and the screen, and snapping, which dragging
// and resizing ask.
module openmui.ui.canvas.kit:parts;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.ui.canvas;

namespace omui::kit {

using world::Behavior;
using world::Claim;
using world::Host;
using world::NodeRef;
using world::PointerInput;
using world::Scene;
using world::WorldRect;

inline Color accent(Host& h) { return h.services().theme().color("accent", Color::hex(0x4f46e5)); }

inline Rect screen_of(Host& h, const WorldRect& r) { return h.camera().to_screen(r, h.viewport()); }

inline bool selected(Host& h, std::string_view key) { return std::ranges::find(h.selection(), key) != h.selection().end(); }

inline Rect grow(Rect r, float by) { return {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by}; }

// A line of `w` pixels between two points on screen.
void line(render::DisplayList& list, Point a, Point b, float w, Color c);

// The keys as a value (the events' `keys`).
Value keys_value(std::span<const std::string> keys);

// A node's key from an action's or an expression's argument.
std::optional<NodeRef> node_of(Scene& s, const Value& v);

// The grid's spacing in world units at this zoom: `base` doubled or halved
// until a cell is 16–32 screen pixels (the grid draws every second line
// stronger, so the step that comes next is already there when it changes).
double grid_step(double base, double zoom) noexcept;

// Snapping (the `snap` behaviour): a moving rectangle's edges and centre meet
// the other nodes' on screen within a few pixels, or the grid's lines; the
// lines they meet on are drawn as guides while it moves.
class Snap final : public Behavior {
public:
    explicit Snap(std::string_view args);
    ~Snap() override;
    Snap(const Snap&) = delete;
    Snap& operator=(const Snap&) = delete;

    std::string_view name() const override { return "snap"; }
    void paint_over(Host& h, render::DisplayList& list) override;

    // The canvas's snap behaviour, if it holds canvas-kit's.
    [[nodiscard]] static Snap* of(Host& h);

    // The correction that makes `proposed` meet what is near. A resize moves
    // only some lines: `xs` / `ys` say which of left, centre, right and top,
    // middle, bottom may snap.
    struct Result {
        double dx = 0, dy = 0;
    };
    Result snap(Host& h, std::span<const std::string> moving, const WorldRect& proposed, std::array<bool, 3> xs = {true, true, true},
                std::array<bool, 3> ys = {true, true, true});
    // The move ended: no guides.
    void done(Host& h);

private:
    struct Guide {
        bool   vertical = true;
        double at = 0, from = 0, to = 0;  // world: the line, and its extent along it
    };
    float              threshold_ = 6;  // screen pixels
    double             grid_ = 0;       // world units; < 0: the grid behaviour's step
    std::vector<Guide> guides_;
    std::vector<NodeRef> near_;
};

// The kit's behaviours.
std::unique_ptr<Behavior> make_select(std::string_view args);
std::unique_ptr<Behavior> make_drag(std::string_view args);
std::unique_ptr<Behavior> make_resize(std::string_view args);
std::unique_ptr<Behavior> make_snap(std::string_view args);
std::unique_ptr<Behavior> make_connect(std::string_view args);
std::unique_ptr<Behavior> make_grid(std::string_view args);
std::unique_ptr<Behavior> make_minimap(std::string_view args);
std::unique_ptr<Behavior> make_delete(std::string_view args);

// The grid behaviour's base spacing, when the canvas holds it.
[[nodiscard]] std::optional<double> grid_base(Host& h);

}  // namespace omui::kit
