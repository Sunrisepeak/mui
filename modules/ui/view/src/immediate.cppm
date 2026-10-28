// openmui.view:immediate — immediate mode (openmui.im, D-05).
//
// A function declares the interface every build; the declarations are
// reconciled by id with the nodes, so immediate and retained mode produce one
// tree and nest in each other:
//
//     omui::immediate([&](omui::Ui& ui) {
//         ui.column({.gap = 8}, [&] {
//             ui.text(std::format("clicked {} times", clicks));
//             if (ui.button("Add", {.id = "add"})) ++clicks;
//         });
//     });
//
// The build runs again only when something happened — an action on one of its
// nodes, a value a control wrote back, a timer it asked for, a signal it
// read — never every frame for nothing. An id is the explicit `.id`, else the
// id stack plus the call site (spec S-8); never a label (S-9: labels are
// translated text).
export module openmui.view:immediate;

import std;
import openmui.core;
import :element;
import :model;
import :component;
import :primitives;

export namespace omui {

// The options of immediate-mode controls (the base components' properties).
namespace im {

struct ButtonOptions {
    std::string id;
    std::string variant;   // primary secondary ghost danger
    std::string icon;
    std::string verb;      // the verb activation invokes (S-1)
    bool        disabled = false;
};

struct ToggleOptions {
    std::string id;
    bool        disabled = false;
    bool        indeterminate = false;  // checkbox only
};

struct SliderOptions {
    std::string id;
    double      min = 0, max = 100, step = 1;
    bool        disabled = false;
};

struct TextBoxOptions {
    std::string id;
    std::string placeholder;
    bool        multiline = false;
    bool        password = false;
    int         max_length = 0;
    bool        disabled = false;
};

}  // namespace im

struct ImState;  // shared by an immediate root, its builder and its handlers

class Ui {
public:
    // ── containers: the body declares the children ──
    void column(BoxOptions o, FunctionRef<void()> body, std::source_location at = std::source_location::current());
    void row(BoxOptions o, FunctionRef<void()> body, std::source_location at = std::source_location::current());
    void stack(BoxOptions o, FunctionRef<void()> body, std::source_location at = std::source_location::current());
    void box(BoxOptions o, FunctionRef<void()> body, std::source_location at = std::source_location::current());
    void grid(BoxOptions o, std::string columns, FunctionRef<void()> body,
              std::source_location at = std::source_location::current());
    void scroll(ScrollOptions o, FunctionRef<void()> body, std::source_location at = std::source_location::current());

    // ── content ──
    void text(std::string_view text, TextOptions o = {}, std::source_location at = std::source_location::current());
    void image(ImageOptions o, std::source_location at = std::source_location::current());
    void spacer(float grow = 1, std::source_location at = std::source_location::current());

    // ── controls (the base components when they are linked) ──
    // True when it was activated since the last build.
    bool button(std::string_view label, im::ButtonOptions o = {}, std::source_location at = std::source_location::current());
    // True when the user changed the value (it is written into `value`).
    bool checkbox(std::string_view label, bool& value, im::ToggleOptions o = {},
                  std::source_location at = std::source_location::current());
    bool toggle(std::string_view label, bool& value, im::ToggleOptions o = {},
                std::source_location at = std::source_location::current());
    bool slider(std::string_view label, double& value, im::SliderOptions o = {},
                std::source_location at = std::source_location::current());
    bool text_box(std::string_view label, std::string& value, im::TextBoxOptions o = {},
                  std::source_location at = std::source_location::current());

    // Any element: retained views nest inside immediate ones.
    void add(Element e, std::source_location at = std::source_location::current());

    // ── ids ──
    void push_id(std::string_view part);
    void push_id(std::int64_t part);
    void pop_id();
    // A scope of the id stack: `for (auto& item : items) { auto s = ui.scope(item.key); ... }`
    struct Scope {
        explicit Scope(Ui* u) noexcept : ui(u) {}
        ~Scope() { ui->pop_id(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        Ui* ui;
    };
    [[nodiscard]] Scope scope(std::string_view part);
    [[nodiscard]] Scope scope(std::int64_t part);

    // ── time ──
    [[nodiscard]] seconds now() const;
    // Builds again after `delay` (a blinking caret, a countdown).
    void rebuild_after(seconds delay);
    // Builds again next frame (an animation driven from the build).
    void rebuild_next_frame();

    // The events delivered to a node this build: `ui.event("canvas", "pointer_down")`.
    [[nodiscard]] std::optional<Value> event(std::string_view id, std::string_view name) const;

    explicit Ui(ImState& state);

private:
    friend struct ImBuild;
    std::string next_id(std::string_view type, std::string_view explicit_id, const std::source_location& at);
    Element&    put(Element e);
    void        container(Element e, FunctionRef<void()> body);
    void        listen(Element& e, std::span<const std::string_view> events);

    ImState&                               state_;
    std::vector<std::vector<Element>*>     open_;
    std::vector<std::string>               ids_;
    std::unordered_map<std::string, int>   seen_;
    std::vector<Element>                   top_;
};

// An immediate-mode view: `build` declares it, as often as something happens.
[[nodiscard]] Element immediate(std::function<void(Ui&)> build, BoxOptions options = {},
                                std::source_location where = std::source_location::current());

}  // namespace omui
