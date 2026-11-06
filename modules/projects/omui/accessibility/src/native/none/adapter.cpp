// No platform service here: an adapter says so, the mirror works as anywhere.
module openmui.accessibility;

import std;

namespace omui::accessibility {

struct Adapter::Impl {};

std::string_view service() noexcept { return "none"; }

std::expected<std::unique_ptr<Adapter>, std::string> Adapter::attach(Mirror&, void*, AdapterOptions) {
    return std::unexpected("no assistive-technology service on this platform (AccessKit has no library for it)");
}

Adapter::Adapter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Adapter::~Adapter() = default;
void Adapter::update() {}
void Adapter::window_focus(bool) {}
void Adapter::set_scale(float) {}
void Adapter::window_bounds(Rect, Rect) {}
bool Adapter::active() const noexcept { return false; }
std::string debug(const Mirror&, const AdapterOptions&) { return {}; }

}  // namespace omui::accessibility
