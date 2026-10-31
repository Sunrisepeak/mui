// window.toy through src/js/omui_toy.js: a call carries an id; the page
// answers through settled(), the callback it is given, on its own thread.
module openmui.host.web.toy;

import std;
import :sdk;

extern "C" {
int  omui_toy_available();
void omui_toy_call(int id, const char* method, const char* args, void (*done)(int id, int ok, char* json));
}

namespace omui::toy::sdk {

namespace {

std::map<int, std::function<void(bool, std::string_view)>> waiting;
int next_id = 1;

void settled(int id, int ok, char* json) {
    const auto it = waiting.find(id);
    if (it != waiting.end()) {
        auto answer = std::move(it->second);
        waiting.erase(it);
        if (answer) answer(ok != 0, json ? std::string_view(json) : std::string_view("null"));
    }
    std::free(json);
}

}  // namespace

bool available() noexcept { return omui_toy_available() != 0; }

void call(std::string_view method, std::string_view args_json, std::function<void(bool, std::string_view)> answer) {
    const int id = next_id++;
    waiting.emplace(id, std::move(answer));
    omui_toy_call(id, std::string(method).c_str(), std::string(args_json).c_str(), &settled);
}

}  // namespace omui::toy::sdk
