// mpal:preferences — how the person prefers things to look and move, as the
// system says it (a dark or a light appearance, less motion): Linux's desktop
// portal (org.freedesktop.portal.Settings) over the session bus; elsewhere
// unsupported until its backend comes (README.md) — SDL3 tells a window's
// host the appearance on Windows and macOS.
//
//     auto now = mcpplibs::mpal::preferences();
//     if (now && now->dark.value_or(false)) … a dark theme …
//
// Their changes come to a PreferenceWatch, which keeps a conversation of its
// own open; a program with an interface waits on it in a thread of its own
// (no wakeups while nothing changes) and posts what changed to its interface:
//
//     auto watch = mcpplibs::mpal::PreferenceWatch::open();   // read once, then listened to
//     while (auto changed = watch->next(std::chrono::hours(24))) if (*changed) … (*changed)->dark …
export module mcpplibs.mpal:preferences;

import std;
import :error;
import :dbus;

namespace mcpplibs::mpal {
// What each setting read says, kept apart: a standard key wins over the desktop's own one (preferences.cpp).
struct SettingKeys {
    std::optional<bool> dark;               // color-scheme: 1 dark, 2 light, 0 no preference
    std::optional<bool> reduced_motion;     // reduced-motion: 1 reduced, 0 no preference
    std::optional<bool> enable_animations;  // GNOME's: false is less motion
};
}  // namespace mcpplibs::mpal

export namespace mcpplibs::mpal {

// What the person prefers; none where the system says nothing (no preference given, or no way to ask).
struct Preferences {
    std::optional<bool> dark;           // a dark appearance (false: a light one)
    std::optional<bool> reduce_motion;  // less motion: animations shortened or left out
    friend bool operator==(const Preferences&, const Preferences&) = default;
};

// The person's preferences now (L0: asked once).
[[nodiscard]] std::expected<Preferences, Error> preferences();
// The desktop portal's settings on a given bus (Linux's session bus, a test's own).
[[nodiscard]] std::expected<Preferences, Error> preferences_on(dbus::Bus& bus);

// The person's preferences and their changes: one conversation with the system's settings, which only listens
// once it has read them.
class PreferenceWatch {
public:
    // Linux's session bus; unsupported where the backend is not written.
    [[nodiscard]] static std::expected<PreferenceWatch, Error> open();
    // The desktop portal's settings on a given bus: its changes asked for first, then read — none is missed between.
    [[nodiscard]] static std::expected<PreferenceWatch, Error> on(dbus::Bus bus);

    // The preferences as last read or heard.
    [[nodiscard]] const Preferences& now() const noexcept { return now_; }
    // The next change, waiting at most `wait`: the preferences after it; none when none came. A setting changed to
    // what it was, or one this does not read, is not a change.
    [[nodiscard]] std::expected<std::optional<Preferences>, Error> next(std::chrono::milliseconds wait);
    // Stops listening, from another thread: a `next` waiting now answers none at once, and so does every later one.
    void stop();

private:
    std::optional<dbus::Bus>           bus_;
    SettingKeys                        keys_;
    Preferences                        now_;
    std::shared_ptr<std::atomic<bool>> stopped_ = std::make_shared<std::atomic<bool>>(false);
};

}  // namespace mcpplibs::mpal
