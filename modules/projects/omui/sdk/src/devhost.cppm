// openmui:devhost — the development host (W16, D-154): it owns the window,
// the input, and (with omui dev) the designer and its history; the
// application under development is a render target in a process of its own
// (--present-to). When a new build is ready its process starts beside the
// old: the old one is asked what the person set, the new one is given it,
// its first frame is drawn unseen, then it fades in over the old one, which
// then goes. A build that fails leaves the one running as it was, its
// problems shown over it.
//
//     omui::DevHost dev(rt, {.directory = ".omui/dev", .start = [](const std::filesystem::path& socket) { … spawn the app … }});
//     dev.restart();                 // the first build, and every new one
//     dev.show_problems(problems);   // a build that failed
export module omui:devhost;

import std;
import omui.core;
import omui.view;

export namespace omui {

struct DevProblem {
    std::string file;  // as the compiler names it
    int         line = 0, column = 0;
    std::string text;
};

// The problems a compiler's output names (`file:line:column: error: text`).
[[nodiscard]] std::vector<DevProblem> problems_in(std::string_view compiler_output);

struct DevHostOptions {
    std::filesystem::path directory;  // where the applications connect to the host
    // Starts an application presenting to `socket` (its --present-to); false: it did not start.
    std::function<bool(const std::filesystem::path& socket)> start;
    seconds fade{0.3};  // a new build fading in over the old
};

class DevHost {
public:
    // Mounts the application's view (at "main") and steps on the runtime's timers.
    DevHost(Runtime& runtime, DevHostOptions options);
    ~DevHost();
    DevHost(const DevHost&) = delete;
    DevHost& operator=(const DevHost&) = delete;

    // Starts a build's application: the first, or one to take over from the one running.
    bool restart();
    // A build's problems shown over the application (none: cleared).
    void show_problems(std::vector<DevProblem> problems);
    // What it shows: the applications it has had, the one shown, whether one is fading in, its frames.
    [[nodiscard]] Value status() const;

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
};

}  // namespace omui
