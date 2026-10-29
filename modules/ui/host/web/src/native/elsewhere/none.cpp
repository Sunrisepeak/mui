// No page (any target but the Web): nothing to listen to, nothing to show.
// The host's loop still runs, stepped by hand — what its tests drive.
module openmui.host.web;

import std;
import openmui.core;
import openmui.raster;
import :platform;

namespace omui::page {

bool exists() { return false; }
bool open(std::string_view, std::string_view) { return false; }
Canvas canvas(std::string_view) { return {}; }
void take(std::vector<InputEvent>&) {}
void present(std::string_view, const raster::Surface&) {}
bool hidden() { return false; }
bool loop(void (*)(void*), void*) { return false; }
std::vector<std::string> locales() { return {}; }

}  // namespace omui::page
