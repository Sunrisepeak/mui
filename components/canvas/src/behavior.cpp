module openmui.ui.canvas;

import std;

namespace omui::world {

namespace {
std::map<std::string, BehaviorFactory, std::less<>>& registry() {
    static std::map<std::string, BehaviorFactory, std::less<>> r;
    return r;
}
}  // namespace

void register_behavior(std::string name, BehaviorFactory make) { registry()[std::move(name)] = std::move(make); }

std::unique_ptr<Behavior> make_behavior(std::string_view spec) {
    const auto colon = spec.find(':');
    const std::string_view name = spec.substr(0, colon), args = colon == std::string_view::npos ? std::string_view{} : spec.substr(colon + 1);
    const auto it = registry().find(name);
    return it == registry().end() ? nullptr : it->second(args);
}

std::vector<std::string> behaviors() {
    std::vector<std::string> out;
    for (const auto& [name, make] : registry()) out.push_back(name);
    return out;
}

}  // namespace omui::world
