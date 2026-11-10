// openmui.core:platform — whose conventions the interface follows (D6). The
// interface is drawn the same everywhere (D-140); how it answers a person is
// each platform's: which key is `Mod`, whether a scroll pulled past its end
// stretches and springs back, when a right button asks for a context menu,
// how long a touch is held to ask for one. The host sets the platform it runs
// on; a test or an agent chooses any, and the same input then does the same
// thing on every machine.
export module omui.core:platform;

import std;
import :keys;

export namespace omui {

// freedesktop: Linux and the BSDs (and what is not one of the others).
enum class Platform : std::uint8_t { freedesktop, windows, macos, ios, android };

struct PlatformConventions {
    Modifier primary = Modifier::ctrl;    // `Mod`: Command on Apple's, else Control
    bool     overscroll_bounce = false;   // a scroll pulled past its end stretches and springs back (Apple's)
    bool     context_on_release = false;  // a right button asks for a context menu when released (Windows), else pressed
    bool     control_click_context = false;  // Control and a click is a right click (macOS)
    double   long_press = 0.5;            // seconds a touch is held, not moving, to ask for a context menu
    float    touch_slop = 8;              // how far (logical pixels) a touch moves and is still a press, not a drag
};

[[nodiscard]] constexpr PlatformConventions conventions(Platform platform) noexcept {
    switch (platform) {
        case Platform::windows: return {.context_on_release = true, .long_press = 0.8};
        case Platform::macos:
            return {.primary = Modifier::meta, .overscroll_bounce = true, .control_click_context = true};
        case Platform::ios: return {.primary = Modifier::meta, .overscroll_bounce = true, .touch_slop = 10};
        case Platform::android: return {.long_press = 0.4};
        case Platform::freedesktop: break;
    }
    return {};
}

// The platform the interface follows now (freedesktop until the host says).
void set_platform(Platform platform) noexcept;
[[nodiscard]] Platform current_platform() noexcept;
[[nodiscard]] inline PlatformConventions platform_conventions() noexcept { return conventions(current_platform()); }

[[nodiscard]] constexpr std::string_view name(Platform platform) noexcept {
    switch (platform) {
        case Platform::windows: return "windows";
        case Platform::macos: return "macos";
        case Platform::ios: return "ios";
        case Platform::android: return "android";
        case Platform::freedesktop: break;
    }
    return "freedesktop";
}
[[nodiscard]] std::optional<Platform> parse_platform(std::string_view text) noexcept;

}  // namespace omui
