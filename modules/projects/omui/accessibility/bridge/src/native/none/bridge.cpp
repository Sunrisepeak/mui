// Where AccessKit has no library (Linux arm64, x86_64 macOS, the Web, Android):
// nothing to install, the mirror works as anywhere and an adapter says so.
module omui.accessibility.bridge;

namespace omui::accessibility::bridge {

void install() {}

std::string debug(const Mirror&, const AdapterOptions&) { return {}; }

}  // namespace omui::accessibility::bridge
