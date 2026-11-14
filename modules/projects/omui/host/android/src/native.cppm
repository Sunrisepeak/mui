// openmui.host.android:native — what the platform's NativeActivity gives the
// host, without one of its types: the window appearing, changing and going,
// the activity pausing, resuming and ending, the person's touches and keys,
// and the window's pixels. Defined in native/activity.cpp, built for Android
// alone, where the NDK's headers are.
module omui.host.android:native;

import std;

namespace omui::android::native {

struct Event {
    enum class Kind : std::uint8_t {
        window,       // a window to draw in (its size in display())
        no_window,    // gone: nothing is drawn until the next
        resized,
        paused,
        resumed,
        destroyed,    // the activity ends: the program returns
        touch_down,
        touch_up,
        touch_move,
        touch_cancel,
        key_down,
        key_up,
        focus,        // the window has the person's focus (pointer = 1) or not (0)
    };
    Kind          kind = Kind::window;
    std::int32_t  pointer = 0;  // a touch's id; focus: whether it is gained
    float         x = 0, y = 0; // physical pixels from the window's top left
    std::int32_t  key = 0;      // the platform's key code
    std::uint32_t meta = 0;     // its meta state (shift, control, alt …)
};

struct Display {
    int   width = 0, height = 0;  // the window, in physical pixels
    float density = 1;            // physical pixels per logical pixel (dpi / 160)
};

// Whether the platform's activity started this program (else it runs on its own: a test).
[[nodiscard]] bool started_by_activity();
// What happened since the last call, waiting up to `wait` for the first (negative: until something does).
[[nodiscard]] std::vector<Event> next(std::chrono::milliseconds wait);
// The window, as it is now.
[[nodiscard]] Display display();
// The frame (RGBA, premultiplied, rows `stride` bytes apart) onto the window; false while there is none.
bool present(const std::uint8_t* pixels, int width, int height, std::size_t stride);
// Wakes next() from another thread (an agent's request, a task's result).
void wake();
// The application's own directory (the activity's internal data path).
[[nodiscard]] std::filesystem::path files_directory();
// The files of the APK's `assets/<directory>` in the application's own directory (copied
// where missing or of another size): what the program reads by path (the default fonts).
[[nodiscard]] std::filesystem::path unpack_assets(std::string_view directory);
// Ends the activity (the person left it: Back with nothing to close).
void finish();
// A line in the system's log (logcat), under the application's tag.
void log(std::string_view text);

}  // namespace omui::android::native
