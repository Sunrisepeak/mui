// The browser runs the site; it does not ship native build-time exporters.
export module openmui.site:offline;
export namespace omui::site { inline constexpr bool native_build_tools = false; }
