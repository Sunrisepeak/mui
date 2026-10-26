// openmui.view:primitives — the runtime's own components and their builders.
//
// Boxes (with column / row / stack / grid presets), text, images, scroll
// views and custom painters: what every component package composes. Builders
// take option structs with designated initializers, so a call reads like the
// view document it is equivalent to:
//
//     omui::column({.padding = 16, .gap = 8}, {
//         omui::text({.text = omui::fmt("clicked {} times", count), .style = "title"}),
//         omui::spacer(),
//     });
//
// Every property accepts a value, a signal or a computed value (Prop<T>).
export module openmui.view:primitives;

import std;
import openmui.core;
import openmui.layout;
import openmui.render;
import openmui.text;
import :element;
import :theme;
import :component;

export namespace omui {

// A length: points (a number), a percentage, or automatic.
struct Dim {
    layout::Length length;
    Dim() = default;
    Dim(float points) : length(layout::Length::points(points)) {}            // NOLINT
    Dim(int points) : length(layout::Length::points(static_cast<float>(points))) {}  // NOLINT
    Dim(double points) : length(layout::Length::points(static_cast<float>(points))) {}  // NOLINT
    Dim(layout::Length l) : length(l) {}                                     // NOLINT
};
template <>
struct ValueOf<Dim> {
    static Value to(const Dim& d);
    static Dim from(const Value& v);
};

// A colour: a theme token ("accent") or a literal colour.
struct Fill {
    std::string text;
    Fill() = default;
    Fill(const char* token) : text(token) {}             // NOLINT
    Fill(std::string token) : text(std::move(token)) {}  // NOLINT
    Fill(Color c);                                       // NOLINT
};
template <>
struct ValueOf<Fill> {
    static Value to(const Fill& f) { return Value(f.text); }
    static Fill from(const Value& v) { return Fill(std::string(v.as_string())); }
};

// Insets: one value for all sides, (vertical, horizontal), or four.
struct Edges {
    Insets insets;
    Edges() = default;
    Edges(float all) : insets(Insets::all(all)) {}                          // NOLINT
    Edges(int all) : insets(Insets::all(static_cast<float>(all))) {}        // NOLINT
    Edges(float vertical, float horizontal) : insets(Insets::symmetric(vertical, horizontal)) {}
    Edges(Insets i) : insets(i) {}                                          // NOLINT
};
template <>
struct ValueOf<Edges> {
    static Value to(const Edges& e);
    static Edges from(const Value& v);
};

// Properties every box shares: layout, look, accessibility, handlers.
struct BoxOptions {
    std::string   id;
    Prop<Dim>     width, height, min_width, min_height, max_width, max_height;
    Prop<Edges>   padding, margin;
    Prop<float>   gap;
    std::string   justify;      // start end center space_between space_around space_evenly
    std::string   align;        // start end center stretch baseline
    std::string   align_self;
    std::string   wrap;         // wrap, no_wrap
    Prop<float>   grow, shrink;
    Prop<Fill>    background, border_color;
    Prop<float>   border_width, radius;
    std::string   shadow;       // a theme shadow token: sm md lg
    Prop<float>   opacity, translate_x, translate_y, scale, rotation;
    bool          clip = false;
    Prop<bool>    visible, disabled;
    std::string   label;        // the accessible name (spec S-6)
    std::string   description;
    std::vector<std::pair<std::string, Handler>>    on;       // {"press", fn}, {"pointer_enter", fn} …
    std::vector<std::pair<std::string, Transition>> animate;  // {"opacity", Transition::tween(…)}
};

[[nodiscard]] Element box(BoxOptions options = {}, std::vector<Element> children = {},
                          std::source_location where = std::source_location::current());
[[nodiscard]] Element column(BoxOptions options = {}, std::vector<Element> children = {},
                             std::source_location where = std::source_location::current());
[[nodiscard]] Element row(BoxOptions options = {}, std::vector<Element> children = {},
                          std::source_location where = std::source_location::current());
[[nodiscard]] Element stack(BoxOptions options = {}, std::vector<Element> children = {},
                            std::source_location where = std::source_location::current());
// columns: track list, e.g. "1fr 2fr 120" or "repeat(3, 1fr)"
[[nodiscard]] Element grid(BoxOptions options, std::string columns, std::vector<Element> children = {},
                           std::source_location where = std::source_location::current());
[[nodiscard]] Element spacer(float grow = 1, std::source_location where = std::source_location::current());

struct TextOptions {
    std::string       id;
    Prop<std::string> text;
    std::string       style = "body";  // display title heading body label caption code
    Prop<Fill>        color;
    std::string       align;           // start center end justify
    bool              wrap = true;
    int               max_lines = 0;
    bool              selectable = false;
    std::string       origin;          // app user_content remote (S-3)
    Prop<float>       size, weight;
    Prop<Dim>         width, max_width;
    Prop<float>       grow, opacity;
    std::vector<std::pair<std::string, Transition>> animate;
};
[[nodiscard]] Element text(TextOptions options, std::source_location where = std::source_location::current());

struct ImageOptions {
    std::string  id;
    std::string  source;  // a PNG path, or "image:<n>" for a stored image
    std::string  alt;     // the text alternative; empty marks it decorative
    std::string  fit = "contain";  // contain cover fill none scale_down
    Prop<Dim>    width, height;
    Prop<float>  radius, opacity;
};
[[nodiscard]] Element image(ImageOptions options, std::source_location where = std::source_location::current());

struct ScrollOptions {
    std::string id;
    bool        horizontal = false;
    bool        vertical = true;
    Prop<float> x, y;  // offsets (bindable)
    Prop<Dim>   width, height, max_height;
    Prop<float> grow, gap;
    Prop<Edges> padding;
    Prop<Fill>  background;
    std::string label;
};
[[nodiscard]] Element scroll(ScrollOptions options, std::vector<Element> children = {},
                             std::source_location where = std::source_location::current());

// A node that paints itself with a function (charts, games, custom widgets).
struct PaintOptions {
    std::string id;
    Prop<Dim>   width, height;
    Prop<float> grow;
    Role        role = Role::canvas;
    std::string label;
    bool        focusable = false;
    std::function<void(PaintContext&)>                          paint;
    std::function<bool(EventContext&, const InputEvent&)>       on_event;
    std::vector<std::pair<std::string, Handler>>                on;
};
[[nodiscard]] Element painter(PaintOptions options, std::source_location where = std::source_location::current());

// The component types of the primitives (view documents name them).
[[nodiscard]] const ComponentType& root_type();
[[nodiscard]] const ComponentType* find_type(std::string_view name);
void register_type(const ComponentType& type);

// Track lists: "1fr 2fr 120 auto minmax(80, 1fr) repeat(3, 1fr)".
[[nodiscard]] std::vector<layout::Track> parse_tracks(std::string_view text);

}  // namespace omui
