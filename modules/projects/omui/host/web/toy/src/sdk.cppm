// openmui.host.web.toy:sdk — window.toy as the rest of the layer sees it:
// a method by name, JSON in, JSON out, answered later on the page's thread
// (native/browser/sdk.cpp; off the Web, native/elsewhere/none.cpp refuses).
module openmui.host.web.toy:sdk;

import std;

namespace omui::toy::sdk {

[[nodiscard]] bool available() noexcept;

// `answer(ok, json)`: the result, or Toy's error as {type, code, message}.
void call(std::string_view method, std::string_view args_json, std::function<void(bool ok, std::string_view json)> answer);

}  // namespace omui::toy::sdk
