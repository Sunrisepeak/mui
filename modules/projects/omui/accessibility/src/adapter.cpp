// The adapter over the installed service (a bridge's), or none.
module omui.accessibility;

import std;
import mlibs.error;

namespace omui::accessibility {

namespace {

std::atomic<const Service*> installed{nullptr};

}  // namespace

void install(const Service& service) noexcept { installed.store(&service); }

bool available() noexcept { return installed.load() != nullptr; }

std::string_view service() noexcept {
    const Service* s = installed.load();
    return s ? s->name : std::string_view("none");
}

std::expected<std::unique_ptr<Adapter>, Failure> Adapter::attach(Mirror& mirror, void* window, AdapterOptions options) {
    const Service* s = installed.load();
    if (!s || !s->attach)
        return std::unexpected(Failure{mlibs::error::Kind::refused,
                                       "no assistive-technology service: the application leaves the bridge out, or AccessKit has no library here"});
    auto session = s->attach(mirror, window, options);
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
