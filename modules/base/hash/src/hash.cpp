module openmui.hash;

import std;

namespace omui::hash {

std::string Fnv1a::hex() const { return std::format("{:016x}", h_); }

}  // namespace omui::hash
