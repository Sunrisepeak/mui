// UI Automation, through AccessKit's Windows subclassing adapter: it answers
// WM_GETOBJECT for the window itself (activation on the window's thread; an
// action perhaps on another).
module openmui.accessibility;

import std;
import mcpplibs.mwrap.accesskit;
import :native;

namespace omui::accessibility::native {

namespace {

// The window handle's type, as AccessKit's adapter takes it (HWND is the
// system header's, which only the wrapped module sees).
template <class F>
struct first_parameter;
template <class R, class A, class... Rest>
struct first_parameter<R (*)(A, Rest...)> {
    using type = A;
};
using Hwnd = first_parameter<decltype(&accesskit_windows_subclassing_adapter_new)>::type;

void raise(accesskit_windows_queued_events* events) {
    if (events) accesskit_windows_queued_events_raise(events);
}

}  // namespace

struct Service {
    accesskit_windows_subclassing_adapter* adapter = nullptr;
};

std::expected<Service*, std::string> open(void* window, const Handlers& h) {
    if (!window) return std::unexpected("UI Automation needs the window's HWND");
    accesskit_windows_subclassing_adapter* a =
        accesskit_windows_subclassing_adapter_new(static_cast<Hwnd>(window), h.activation, h.userdata, h.action, h.userdata);
    if (!a) return std::unexpected("AccessKit's Windows adapter could not be made");
    return new Service{a};
}

void close(Service* s) noexcept {
    if (!s) return;
    accesskit_windows_subclassing_adapter_free(s->adapter);
    delete s;
}

void update(Service* s, accesskit_tree_update_factory factory, void* userdata) {
    raise(accesskit_windows_subclassing_adapter_update_if_active(s->adapter, factory, userdata));
}

// The subclassing adapter follows the window's focus messages itself.
void focus(Service*, bool) {}
void bounds(Service*, accesskit_rect, accesskit_rect) {}

std::string_view name() noexcept { return "uia"; }

}  // namespace omui::accessibility::native
