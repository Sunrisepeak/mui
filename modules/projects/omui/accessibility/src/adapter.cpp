// The adapter over the installed service (a bridge's), or none.
module omui.accessibility;

import std;
import mlibs.compose;
import mlibs.error;

namespace omui::accessibility {

// A second installation of the one service is refused by the composition and changes nothing.
void install(const Service& service) noexcept { (void)mlibs::compose::process().install(service_slot, service, service.name); }

bool available() noexcept { return mlibs::compose::process().get(service_slot) != nullptr; }

std::string_view service() noexcept {
    const Service* installed = mlibs::compose::process().get(service_slot);
    return installed ? installed->name : std::string_view("none");
}

std::expected<std::unique_ptr<Adapter>, Failure> Adapter::attach(Mirror& mirror, void* window, AdapterOptions options) {
    const Service* installed = mlibs::compose::process().get(service_slot);
    if (!installed || !installed->attach) {
        const mlibs::compose::Error absent = mlibs::compose::process().missing(service_slot.info);
        return std::unexpected(Failure{absent.kind(), absent.message});
    }
    auto session = installed->attach(mirror, window, options);
    if (!session) return std::unexpected(std::move(session.error()));
    return std::unique_ptr<Adapter>(new Adapter(std::move(*session)));
}

Adapter::Adapter(std::unique_ptr<Session> session) : session_(std::move(session)) {}
Adapter::~Adapter() = default;
void Adapter::update() { session_->update(); }
void Adapter::window_focus(bool focused) { session_->window_focus(focused); }
void Adapter::set_scale(float scale) { session_->set_scale(scale); }
void Adapter::window_bounds(Rect outer, Rect inner) { session_->window_bounds(outer, inner); }
bool Adapter::active() const noexcept { return session_->active(); }

}  // namespace omui::accessibility
