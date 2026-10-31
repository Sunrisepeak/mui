// AT-SPI, through AccessKit's unix adapter: it speaks D-Bus on threads of its
// own, so every handler is called from another thread.
module openmui.accessibility;

import std;
import mcpplibs.mwrap.accesskit;
import :native;

namespace omui::accessibility::native {

struct Service {
    accesskit_unix_adapter* adapter = nullptr;
};

std::expected<Service*, std::string> open(void*, const Handlers& h) {
    accesskit_unix_adapter* a = accesskit_unix_adapter_new(h.activation, h.userdata, h.action, h.userdata, h.deactivation, h.userdata);
    if (!a) return std::unexpected("AccessKit's unix adapter could not be made");
    return new Service{a};
}

void close(Service* s) noexcept {
    if (!s) return;
    accesskit_unix_adapter_free(s->adapter);
    delete s;
}

void update(Service* s, accesskit_tree_update_factory factory, void* userdata) {
    accesskit_unix_adapter_update_if_active(s->adapter, factory, userdata);
}

void focus(Service* s, bool focused) { accesskit_unix_adapter_update_window_focus_state(s->adapter, focused); }

void bounds(Service* s, accesskit_rect outer, accesskit_rect inner) {
    accesskit_unix_adapter_set_root_window_bounds(s->adapter, outer, inner);
}

std::string_view name() noexcept { return "atspi"; }

}  // namespace omui::accessibility::native
