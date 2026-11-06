// No Toy page (any target but the Web): no SDK; every call is refused, at once.
module openmui.host.web.toy;

import std;
import :sdk;

namespace omui::toy::sdk {

bool available() noexcept { return false; }

void call(std::string_view, std::string_view, std::function<void(bool, std::string_view)> answer) {
    if (answer) answer(false, R"({"type":"unsupported","code":0,"message":"no Toy SDK off the Web"})");
}

}  // namespace omui::toy::sdk
