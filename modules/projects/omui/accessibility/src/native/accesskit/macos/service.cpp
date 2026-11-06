// NSAccessibility, through AccessKit's macOS subclassing adapter on the
// window's content view; SDL keeps the keyboard focus on the window, so the
// window's class forwards its focused element to the view.
module openmui.accessibility;

import std;
import mcpplibs.mwrap.accesskit;
import :native;

namespace omui::accessibility::native {

namespace {

void raise(accesskit_macos_queued_events* events) {
    if (events) accesskit_macos_queued_events_raise(events);
}

}  // namespace

struct Service {
    accesskit_macos_subclassing_adapter* adapter = nullptr;
};

std::expected<Service*, std::string> open(void* window, const Handlers& h) {
    if (!window) return std::unexpected("NSAccessibility needs the window's NSWindow");
    static const bool forwarded = [] {
        accesskit_macos_add_focus_forwarder_to_window_class("SDL3Window");
        return true;
    }();
    (void)forwarded;
    accesskit_macos_subclassing_adapter* a =
        accesskit_macos_subclassing_adapter_for_window(window, h.activation, h.userdata, h.action, h.userdata);
    if (!a) return std::unexpected("AccessKit's macOS adapter could not be made");
    return new Service{a};
}

void close(Service* s) noexcept {
    if (!s) return;
    accesskit_macos_subclassing_adapter_free(s->adapter);
    delete s;
}

void update(Service* s, accesskit_tree_update_factory factory, void* userdata) {
    raise(accesskit_macos_subclassing_adapter_update_if_active(s->adapter, factory, userdata));
}

void focus(Service* s, bool focused) { raise(accesskit_macos_subclassing_adapter_update_view_focus_state(s->adapter, focused)); }
void bounds(Service*, accesskit_rect, accesskit_rect) {}

std::string_view name() noexcept { return "nsaccessibility"; }

}  // namespace omui::accessibility::native
