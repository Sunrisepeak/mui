// hanim.studio — animation as a document an Agent (or a person) writes, and
// the tool that checks, samples and renders it without a GUI (convergence
// plan M4: the animation tool's CLI).
//
// A document (`<name>.animation.omui.json`) names its objects — shapes with a
// fill, a stroke, where they stand — and its program: steps (move, rotate,
// scale, fill, fade, morph, drop, pulse, shake, wobble) composed in
// sequences, parallels, staggers, waits and loops. `read` checks all of it
// and reports every problem where it is (a JSON pointer, or the line and
// column of a syntax error); `build` makes the objects in a host's Engine and
// the program hanim plays. `Studio` operates it through hanim.control (each
// call admitted by mlibs.control, recorded) and draws any moment with the
// shared renderer (hanim.render.vector into xrender's CPU raster); `state`
// says where every object is and what it covers, so a check needs no pixels.
//
//     auto document = hanim::studio::read(text);          // or every problem
//     auto studio = hanim::studio::Studio::open(*document);
//     studio->render(seconds(0.5), surface);              // pixels at 0.5 s
//     studio->state(seconds(0.5));                        // positions, bounds
export module hanim.studio;

import std;
export import hanim;
export import hanim.control;
export import hanim.render.vector;
export import xrender.raster;
export import mlibs.control;

export namespace hanim::studio {

// What is wrong, and where: a JSON pointer into the document ("/program/items/2/ease"),
// or "line L, column C" for a syntax error.
struct Problem {
    std::string where, text;
};
using Problems = std::vector<Problem>;
[[nodiscard]] std::string to_text(const Problems& problems);

enum class ShapeKind : std::uint8_t { rect, circle, ellipse, polygon, star, path };
// A shape about its object's origin, in pixels: a rectangle (size, corner
// radius), a circle (radius), an ellipse (radii), a polygon (points), a star
// (points, outer and inner radius), a path (SVG path data, its own coordinates).
struct ShapeSpec {
    ShapeKind kind = ShapeKind::rect;
    Vec2 size{};                // rect: width, height; ellipse: the two radii
    double radius = 0;          // rect: corner; circle and star: outer
    double inner = 0;           // star: inner radius
    int count = 5;              // star: its points
    std::vector<Vec2> points;   // polygon
    std::string data;           // path: SVG path data (one contour or several)
};
struct ObjectSpec {
    std::string id;
    ShapeSpec shape;
    std::optional<Rgba> fill;
    std::optional<PartStroke> stroke;
    Vec2 at{};                  // where its origin stands
    double rotation = 0;        // radians
    Vec2 scale{1, 1};
    bool hidden = false;        // drawn only while animated (a morph's target)
};

enum class StepKind : std::uint8_t {
    sequence, parallel, stagger, wait, loop, marker,
    move_to, move_by, rotate_to, rotate_by, scale_to, scale_by, fill_to, fade_to,
    morph, drop, pulse, shake, wobble,
    // hanim's recipe library (anim::): entrances, exits, cues, idle loops, moves
    fade_in, rise_in, sink_in, slide_in, pop_in, zoom_in, spin_in, unfold_in, write_in, drop_in,
    fade_out, rise_out, sink_out, slide_out, pop_out, zoom_out, spin_out, fold_out, unwrite,
    bounce, flash, heartbeat, rubber_band, swing, tada, nudge,
    bob, breathe, spin, sway, orbit, blink,
    arc_to, swap, reorder
};
[[nodiscard]] std::string_view step_name(StepKind kind) noexcept;
struct StepSpec {
    StepKind kind = StepKind::sequence;
    std::string where;                 // its JSON pointer, for what is said about it
    std::string object, into, name;    // the object; morph's target; a marker's name
    Vec2 vector{};                     // to / by (a point, a scale, an amplitude)
    double number = 0;                 // radians, opacity, a pulse's factor
    std::optional<Rgba> color;
    std::optional<seconds> duration;   // its own, when it says
    std::optional<Ease> ease;
    std::optional<SpringParams> spring;
    std::vector<StepSpec> items;       // sequence, parallel, stagger; loop's one
    double count = std::numeric_limits<double>::infinity();  // loop
    bool alternate = false;            // loop
    seconds step{0.1};                 // stagger, reorder
    anim::DropOptions drop;
    seconds period{1};                 // an idle loop's course
    std::string with;                  // swap's other object
    std::vector<std::string> objects;  // reorder's
    std::vector<Vec2> offsets;         // reorder's, one an object
};
struct Document {
    int version = 1;
    double width = 480, height = 320;  // the stage, in pixels
    Rgba background{0.06f, 0.07f, 0.1f, 1};
    std::vector<ObjectSpec> objects;
    StepSpec program;
};

// The document in `text`, checked whole: every problem, each where it is.
[[nodiscard]] std::expected<Document, Problems> read(std::string_view text);

// A variant of a document (Part 2 H: alternatives a person picks from by
// looking): the document with one value changed, read again whole.
struct Variant {
    std::string value;  // the value set, as JSON (empty: the document as written)
    std::string text;   // the variant's document
    std::expected<Document, Problems> document;
};
// `text` as written, then once with each of `values` (JSON text) at `where` —
// a JSON pointer to a value, or to a key its object does not have yet. A
// variant the reader refuses carries its problems; a pointer to nowhere or a
// value that is not JSON refuses them all.
[[nodiscard]] std::expected<std::vector<Variant>, Problems> vary(std::string_view text, std::string_view where,
                                                                 std::span<const std::string> values);

// One moment of an object, as drawn: where its origin is, its scale,
// rotation and fill, and the box its geometry covers on the stage.
struct ObjectState {
    std::string id;
    bool drawn = false;
    Vec2 position{}, scale{1, 1};
    double rotation = 0;
    double opacity = 1;  // its group opacity (a recipe's fade), apart from its fill's alpha
    std::optional<Rgba> fill;
    Vec2 low{}, high{};  // the bounds (empty when not drawn)
};
// A step of the document that governs a moment (Studio::at): its JSON
// pointer, when it runs, and the objects it moves — what to change to change
// what is seen then.
struct Governing {
    std::string where;                 // the step's JSON pointer
    double start = 0, end = 0;         // seconds
    double progress = 0;               // how far through it the moment is (0…1)
    std::vector<std::string> objects;  // the objects its own leaves move
};
// An operation the studio performed through hanim.control, and the decision.
struct Operation {
    std::string name;
    openas::Decision decision = openas::Decision::refuse;
};

// A document opened: its objects in an Engine of its own, its program
// prepared and played (paused) through hanim.control, ready to be sampled at
// any moment, in any order, the same each time.
class Studio {
public:
    [[nodiscard]] static std::expected<Studio, Problems> open(const Document& document);
    Studio(Studio&&) noexcept;
    Studio& operator=(Studio&&) noexcept;
    ~Studio();

    [[nodiscard]] const Document& document() const noexcept;
    // The program's length (infinite for a loop without a count).
    [[nodiscard]] seconds duration() const noexcept;
    [[nodiscard]] const PreparedProgram& plan() const noexcept;
    [[nodiscard]] std::span<const ProgramMarker> markers() const noexcept;
    // The operations performed so far (prepare, play, pause, seek …), in order.
    [[nodiscard]] std::span<const Operation> operations() const noexcept;

    // The steps that govern `time`, innermost first — those that move `object`
    // at that moment when one is named (empty: any): from a moment someone
    // points at to the step, and the pointer to its numbers, to change.
    [[nodiscard]] std::vector<Governing> at(seconds time, std::string_view object = {}) const;
    // Every object at `time` (clamped to the program), in the document's order.
    [[nodiscard]] std::expected<std::vector<ObjectState>, Problems> state(seconds time);
    // The stage at `time` as the display list `render` draws (the studio's own,
    // valid until the next call): for other renderers, or SVG.
    [[nodiscard]] std::expected<const xrender::DisplayList*, Problems> display(seconds time);
    // The stage at `time` drawn into `surface` (of the document's size).
    [[nodiscard]] std::expected<void, Problems> render(seconds time, xrender::raster::Surface& surface);

private:
    struct Data;
    explicit Studio(std::unique_ptr<Data> data) noexcept;
    std::unique_ptr<Data> data_;
};

}  // namespace hanim::studio
