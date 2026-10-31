// openmui.accessibility:native — the one platform service of this build, as
// the adapter drives it: AccessKit's unix adapter (AT-SPI), its Windows
// subclassing adapter (UI Automation) or its macOS one (NSAccessibility).
// Each lives in its own unit (unix/, windows/, macos/), chosen by the
// manifest: AccessKit declares each only on its platform.
module openmui.accessibility:native;

import std;
import mcpplibs.mwrap.accesskit;

namespace omui::accessibility::native {

struct Service;

struct Handlers {
    accesskit_activation_handler_callback   activation = nullptr;
    accesskit_action_handler_callback       action = nullptr;
    accesskit_deactivation_handler_callback deactivation = nullptr;
    void*                                   userdata = nullptr;
};

[[nodiscard]] std::expected<Service*, std::string> open(void* window, const Handlers& handlers);
void close(Service* service) noexcept;
// Builds the update with `factory` when assistive technology listens, and
// raises the events it makes.
void update(Service* service, accesskit_tree_update_factory factory, void* userdata);
void focus(Service* service, bool focused);
void bounds(Service* service, accesskit_rect outer, accesskit_rect inner);
[[nodiscard]] std::string_view name() noexcept;

}  // namespace omui::accessibility::native
