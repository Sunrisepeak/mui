// omui.accessibility.bridge — the platform's assistive-technology service
// through AccessKit (UI Automation, NSAccessibility, AT-SPI over D-Bus), a
// package of its own (B6): an application that leaves it out carries the
// mapping alone (omui.accessibility) and not AccessKit (1.59 MB of example
// 01 on Linux). omui.sdk installs it unless told otherwise.
//
//     omui::accessibility::bridge::install();   // once, before a host attaches
export module omui.accessibility.bridge;

import std;
import omui.accessibility;

export namespace omui::accessibility::bridge {

// Installs this platform's service (omui::accessibility::install); where
// AccessKit has no library, nothing.
void install();

// The tree as the platform's service would first be told it: AccessKit's own
// debug text of the full update ("" where AccessKit has no library). For
// tests and tools; a program that does not call it does not carry it.
[[nodiscard]] std::string debug(const Mirror& mirror, const AdapterOptions& options = {});

}  // namespace omui::accessibility::bridge
