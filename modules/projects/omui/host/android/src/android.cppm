// openmui.host.android — omui on Android (W10, G1): the framework draws every
// pixel (D-140) into the window the platform's NativeActivity gives it; the
// person's touches are pointers (touch: Android's conventions — a stretch at
// a scroll's edge is not Android's, a touch held asks for the context menu),
// Back is Escape (what is open closes; when nothing changes, the activity ends);
// the activity pausing stops the frames, resuming starts them.
//
//     int main() {                        // the application's library (kind = "app")
//         omui::AndroidHost phone({.app = "org.example.notes"});
//         phone.mount("main", notes());
//         return phone.run();             // until the activity ends
//     }
export module omui.host.android;

import std;
import omui.core;
import omui.view;
import mlibs.text;

export namespace omui {

struct AndroidOptions {
    std::string                 app = "app";         // the application's name (logs)
    Theme                       theme = Theme::light();
    mlibs::text::FontCollection* fonts = nullptr;    // none: the defaults
    Color                       background = Color::white();
    bool                        reduce_motion = false;
};

class AndroidHost {
public:
    explicit AndroidHost(AndroidOptions options = {});
    ~AndroidHost();
    AndroidHost(const AndroidHost&) = delete;
    AndroidHost& operator=(const AndroidHost&) = delete;

    [[nodiscard]] Runtime& runtime() noexcept;
    NodeId mount(std::string_view slot, View view);
    // Whether the platform's activity started this program (else there is no window to draw in).
    [[nodiscard]] static bool in_activity();
    // Frames while the activity lives: the window's, the person's input; returns when it ends (0).
    int run();
    // Frames shown so far (a test's, a log's).
    [[nodiscard]] std::size_t frames_shown() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui
