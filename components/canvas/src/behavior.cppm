// openmui.ui.canvas:behavior — what makes a canvas do more than pan and
// zoom (canvas-markdown §1.2–1.3): behaviours, by name.
//
// A behaviour is a C++ object a canvas holds (its `behaviors` property
// names them; canvas-kit registers select, drag, resize, snap, connect,
// grid, minimap and delete; a program registers its own, and canvas-kit
// makes declarative ones from *.behavior.omui.toml). Each frame's input
// goes to them in a gesture arena (Flutter's, simplified): a press is
// offered to every behaviour, the highest priority first, until one takes
// it; that one then has the pointer until it is released. A press no one
// takes pans the camera. Behaviours draw under the nodes (a grid) and over
// them (selection, handles, guides, a minimap), in screen coordinates; and
// they add to the semantic tree what they make possible — a node's `move`
// exists because drag is on (§1.2's "行为声明它带来的动作").
export module openmui.ui.canvas:behavior;

import std;
import openmui.core;
import openmui.view;
import openmui.render;
import :scene;
import :camera;

export namespace omui::world {

// A pointer, as a behaviour sees it: where on screen (the canvas's logical
// pixels) and in the world, and what it is over.
struct PointerInput {
    PointerKind            kind = PointerKind::move;
    Point                  screen;
    std::array<double, 2>  world{};
    MouseButton            button = MouseButton::none;
    Modifiers              mods;
    std::uint8_t           clicks = 1;
    std::optional<Hit>     hit;
};

enum class Claim : std::uint8_t {
    pass,  // not this behaviour's: offer it to the next
    take,  // this behaviour's until the pointer is released
};

// What the canvas offers its behaviours.
class Host {
public:
    virtual ~Host() = default;
    [[nodiscard]] virtual Scene& scene() = 0;
    [[nodiscard]] virtual Camera camera() const = 0;
    [[nodiscard]] virtual Size viewport() const = 0;
    virtual void set_camera(const Camera& c) = 0;
    virtual void animate_to(const Camera& c) = 0;
    [[nodiscard]] virtual Services& services() = 0;
    // Behaviours' drawing changed (a marquee grew): the canvas draws again.
    virtual void redraw() = 0;
    // The selection (by key, in the order chosen): what select keeps and
    // others act on.
    [[nodiscard]] virtual const std::vector<std::string>& selection() const = 0;
    virtual void select(std::vector<std::string> keys) = 0;
    // A finished change, as the canvas's events: `moved`, `resized`,
    // `connected`, `deleted` with their keys (scripts save on these).
    virtual void committed(std::string_view what, const Value& args) = 0;
    // Another behaviour of this canvas, by name (snap, for drag).
    [[nodiscard]] virtual class Behavior* behavior(std::string_view name) = 0;
};

class Behavior {
public:
    virtual ~Behavior() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    // Offered presses go from the highest first.
    [[nodiscard]] virtual int priority() const { return 0; }
    // A press (PointerKind::down) is offered; once taken, the moves and the
    // release (or cancel) come here too.
    virtual Claim pointer(Host&, const PointerInput&) { return Claim::pass; }
    // A press no behaviour took, released where it was pressed (a click):
    // offered in the same order; true: handled.
    virtual bool tap(Host&, const PointerInput&) { return false; }
    // Keys while the canvas has focus; true: handled.
    virtual bool key(Host&, const KeyEvent&) { return false; }
    // Drawing in the canvas's coordinates: under the nodes, and over them.
    virtual void paint_under(Host&, render::DisplayList&) {}
    virtual void paint_over(Host&, render::DisplayList&) {}
    // What a node is and can do because of this behaviour (states: selected;
    // actions: move, resize, connect, delete …) and doing it.
    virtual void describe(Host&, NodeRef, spec::SemanticNode&) {}
    [[nodiscard]] virtual std::optional<ActResult> act(Host&, NodeRef, std::string_view action, const Value& args) {
        (void)action, (void)args;
        return std::nullopt;
    }
    // The canvas lets it go (it goes, or its `behaviors` change): what it put
    // in the runtime — an image, a timer — goes too.
    virtual void unmounted(Host&) {}
};

// Makes a behaviour from what follows its name in `behaviors`
// ("grid:40" → "40"; "select" → "").
using BehaviorFactory = std::function<std::unique_ptr<Behavior>(std::string_view args)>;
// Registers a behaviour by name (again: replaces it); canvases made after
// take it by naming it in `behaviors`.
void register_behavior(std::string name, BehaviorFactory make);
// "name" or "name:args"; null for a name no one registered.
[[nodiscard]] std::unique_ptr<Behavior> make_behavior(std::string_view spec);
[[nodiscard]] std::vector<std::string> behaviors();

}  // namespace omui::world
