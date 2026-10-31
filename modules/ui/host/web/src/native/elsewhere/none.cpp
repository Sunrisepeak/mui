// No page (any target but the Web): nothing to listen to, nothing to show,
// no clipboard, no localStorage. The host's loop still runs, stepped by
// hand — what its tests drive.
module openmui.host.web;

import std;
import openmui.core;
import openmui.raster;
import :platform;

namespace omui::page {

bool exists() { return false; }
bool open(std::string_view, std::string_view) { return false; }
Canvas canvas(std::string_view) { return {}; }
void take(std::vector<InputEvent>&, std::vector<Request>&) {}
void text_input(std::optional<Rect>, std::string_view) {}
void mirror(std::string_view) {}
void agent_reply(std::string_view) {}
std::string clipboard() { return {}; }
void set_clipboard(std::string_view) {}
bool open_url(std::string_view) { return false; }
bool storage() { return false; }
std::optional<std::string> storage_get(std::string_view) { return std::nullopt; }
bool storage_set(std::string_view, std::string_view) { return false; }
bool storage_remove(std::string_view) { return false; }
std::vector<std::string> storage_keys(std::string_view) { return {}; }
void present(std::string_view, const raster::Surface&) {}
void backing(std::string_view, int, int) {}
bool hidden() { return false; }
bool loop(void (*)(void*), void*) { return false; }
std::vector<std::string> locales() { return {}; }
std::string query(std::string_view) { return {}; }
bool matches(std::string_view) { return false; }
bool reload_with(std::string_view, std::string_view) { return false; }

}  // namespace omui::page
