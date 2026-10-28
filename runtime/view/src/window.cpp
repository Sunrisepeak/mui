module openmui.view;

import std;
import openmui.core;

namespace omui {

namespace {

constexpr std::array<std::string_view, 4> state_names{"normal", "maximized", "minimized", "fullscreen"};

// What can be operated keeps the pointer: its actions other than reading.
bool operable(const spec::SemanticNode& n) {
    return std::ranges::any_of(n.actions, [](const spec::ActionDesc& a) { return a.effect != Effect::read; });
}

}  // namespace

std::string_view name(WindowState s) noexcept { return state_names[static_cast<std::size_t>(s)]; }

Window::Window()
    : state_name(signal(std::string("normal"))), is_active(signal(true)), is_closing(signal(false)), is_framework(signal(false)) {}

void Window::attach(WindowHost* host, Verbs& verbs) {
    host_ = host;
    is_framework.set(host && host->framework_drawn());
    auto add = [&](std::string name, std::string summary, std::function<void(WindowHost&, const Value&)> fn) {
        if (verbs.find(name)) return;
        (void)verbs.add({name, std::move(summary), Effect::reversible, "", {}}, [this, fn = std::move(fn)](const Value& args) -> VerbResult {
            if (!host_) return std::unexpected(std::string("no window: the interface is not shown in one"));
            fn(*host_, args);
            return Value{};
        });
    };
    add("window.minimize", "Minimize the window", [](WindowHost& h, const Value&) { h.minimize(); });
    add("window.toggle_maximize", "Maximize the window, or restore it", [](WindowHost& h, const Value&) { h.toggle_maximize(); });
    add("window.fullscreen", "Show the window fullscreen ({\"on\": false} leaves it)", [](WindowHost& h, const Value& a) {
        h.set_fullscreen(a.get("on") ? a.get("on")->as_bool(true) : true);
    });
    add("window.close", "Close the window (the application may keep it to save first)", [](WindowHost& h, const Value&) { h.request_close(); });
}

void Window::set_state(WindowState s) { state_name.set(std::string(name(s))); }
void Window::set_active(bool active) { is_active.set(active); }
void Window::set_closing(bool closing) { is_closing.set(closing); }

WindowState Window::state() const {
    const std::string s = state_name.peek();
    for (std::size_t i = 0; i < state_names.size(); ++i)
        if (state_names[i] == s) return static_cast<WindowState>(i);
    return WindowState::normal;
}
bool Window::active() const { return is_active.peek(); }
bool Window::closing() const { return is_closing.peek(); }
bool Window::framework_drawn() const { return is_framework.peek(); }

Window& window() {
    thread_local Window w;
    return w;
}

WindowArea window_area_at(const Runtime& rt, Point p, Size size, float margin) {
    if (window().state() == WindowState::normal && margin > 0) {
        const bool left = p.x < margin, right = p.x >= size.width - margin, top = p.y < margin, bottom = p.y >= size.height - margin;
        if (top && left) return WindowArea::resize_top_left;
        if (top && right) return WindowArea::resize_top_right;
        if (bottom && left) return WindowArea::resize_bottom_left;
        if (bottom && right) return WindowArea::resize_bottom_right;
        if (top) return WindowArea::resize_top;
        if (bottom) return WindowArea::resize_bottom;
        if (left) return WindowArea::resize_left;
        if (right) return WindowArea::resize_right;
    }
    for (NodeId n = rt.hit_test(p); rt.valid(n); n = rt.parent(n)) {
        if (operable(rt.semantic_node(n))) return WindowArea::client;
        if (rt.get(n, "window_area").as_string() == "drag") return WindowArea::drag;
    }
    return WindowArea::client;
}

}  // namespace omui
