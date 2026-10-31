// openmui.accessibility:platform — where AccessKit has no library (Linux
// arm64, x86_64 macOS, the Web): the mapping without a platform service.
export module openmui.accessibility:platform;

export namespace omui::accessibility::platform {

inline constexpr bool available = false;

}  // namespace omui::accessibility::platform
