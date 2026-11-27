// Red 158: the pointer moving elsewhere on the screen does not wake a window's loop. SDL's X11 driver asks for
// XInput2's raw motion on the root window at its start; the host clears that selection (raw motion only while the
// pointer is locked). A copy of this program opens a window on an X server of the test's own (OMUI_TEST_X_DISPLAY,
// an Xvfb: its pointer is the test's to move, never a person's) and sleeps in its loop; the test moves that server's
// pointer outside the window 120 times a second through XTEST and counts the copy's main thread's switches (pidstat).
import std;
import mlibs.text.harfbuzz;
import mcpplibs.os;
import omui.host.sdl3;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made{mlibs::text::harfbuzz::shaper()};
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

// The copy: a window at the screen's corner, asleep in its loop until a watchdog ends it four seconds on.
child resting_window{"resting-window", [](std::vector<std::string>) {
    DesktopOptions o;
    o.title = "openmui at rest";
    o.size = {160, 100};
    o.position = std::array{0, 0};
    o.video_driver = "x11";
    o.vsync = false;
    o.fonts = &fonts();
    o.app = "sdl3-root-motion";
    o.agent = false;
    o.accessibility = false;
    o.follow_system = false;
    o.renderer = DesktopOptions::Renderer::cpu;
    Desktop desktop(o);
    if (auto opened = desktop.open(); !opened) {
        std::println("no window: {}", opened.error().text());
        return 1;
    }
    desktop.mount("main", column({}, {}));
    for (int step = 0; step < 6; ++step) desktop.step();
    std::println("ready");
    std::cout.flush();
    std::atomic<bool> over{false};
    std::jthread watchdog([&](std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any never;
        std::unique_lock lock(mutex);
        (void)never.wait_for(lock, stop, std::chrono::seconds(4), [] { return false; });
        over = true;
        desktop.wake();
    });
    while (!over) desktop.step(true);
    return 0;
}};

// The main thread's switches a second (voluntary and not) in `pidstat -w -t` output, the thread whose id is the
// process's.
std::optional<double> main_switches(std::string_view said, std::uint64_t pid) {
    const std::string id = std::to_string(pid);
    for (const auto line : std::views::split(said, '\n')) {
        std::vector<std::string_view> words;
        for (const auto word : std::views::split(std::string_view(line.begin(), line.end()), ' '))
            if (!word.empty()) words.emplace_back(word.begin(), word.end());
        // Average:  UID  TGID  TID  cswch/s  nvcswch/s  Command
        if (words.size() < 7 || words[0] != "Average:" || words[3] != id) continue;
        double voluntary = 0, involuntary = 0;
        if (std::from_chars(words[4].data(), words[4].data() + words[4].size(), voluntary).ec != std::errc{} ||
            std::from_chars(words[5].data(), words[5].data() + words[5].size(), involuntary).ec != std::errc{})
            continue;
        return voluntary + involuntary;
    }
    return std::nullopt;
}

}  // namespace

static UTest cases{"host.sdl3.root_motion", [] {
    "red 158: the pointer moving elsewhere on an X server does not wake a window's loop (OMUI_TEST_X_DISPLAY)"_test = [] {
        namespace os = mcpplibs::os;
        namespace library = mcpplibs::os::library;
        const auto server = os::env::get("OMUI_TEST_X_DISPLAY");
        if (!server || server->empty()) skip("no X server of the test's own (OMUI_TEST_X_DISPLAY=:N, an Xvfb whose pointer it may move)");
        // The X libraries the program's own loader finds, else the system's (XTEST is no library SDL loads).
        const auto opened = [](std::string_view name) {
            auto found = library::open(std::filesystem::path(name));
            if (!found) found = library::open(std::filesystem::path("/usr/lib/x86_64-linux-gnu") / name);
            return found;
        };
        const auto x11 = opened("libX11.so.6");
        const auto xtest = opened("libXtst.so.6");
        if (!x11 || !xtest) skip("no libX11 or libXtst here");
        const auto pidstat = os::env::find_executable("pidstat");  // the switches counted from outside, as bench apps counts them
        if (!pidstat) skip("no pidstat here (sysstat)");
        const auto open_display = x11->function<void*(const char*)>("XOpenDisplay");
        const auto close_display = x11->function<int(void*)>("XCloseDisplay");
        const auto flush = x11->function<int(void*)>("XFlush");
        const auto fake_motion = xtest->function<int(void*, int, int, int, unsigned long)>("XTestFakeMotionEvent");
        expect(fatal(open_display && close_display && flush && fake_motion));
        void* display = open_display(server->c_str());
        expect(fatal(display != nullptr)) << "the test's X server " << *server;
        const auto command = child_command("resting-window");
        os::process::spawn_options options;
        options.program = os::fs::from_utf8(command.front());
        options.args.assign(command.begin() + 1, command.end());
        options.env = {{"DISPLAY", *server}, {"SDL_VIDEO_DRIVER", "x11"}};
        options.out = os::process::stdio::pipe;
        options.err = os::process::stdio::null;
        options.bound_lifetime = true;
        auto copy = os::process::process::spawn(options);
        expect(fatal(copy.has_value()));
        std::string said;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!said.contains("ready\n") && !said.contains("no window") && std::chrono::steady_clock::now() < until)
            if (auto chunk = copy->read_output_for(std::chrono::milliseconds(500)); chunk && *chunk) said += **chunk;
        expect(fatal(said.contains("ready\n"))) << said;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));  // at rest
        os::process::spawn_options watching{.program = *pidstat, .args = {"-w", "-t", "-p", std::to_string(copy->id()), "2", "1"},
                                            .env = {{"LC_ALL", "C"}}};
        watching.err = os::process::stdio::null;
        auto counting = os::process::process::spawn(watching);
        expect(fatal(counting.has_value()));
        for (int step = 0; step < 250; ++step) {  // two seconds of a hand moving the pointer, outside the window
            (void)fake_motion(display, -1, 400 + step % 100, 300 + (step / 100) * 4, 0);
            (void)flush(display);
            std::this_thread::sleep_for(std::chrono::microseconds(8333));
        }
        std::string counted;
        while (auto chunk = counting->read_output()) {
            if (chunk->empty()) break;
            counted += *chunk;
        }
        (void)counting->wait();
        (void)close_display(display);
        (void)copy->wait();
        const auto woke = main_switches(counted, copy->id());
        expect(fatal(woke.has_value())) << "pidstat counted the main thread: " << counted;
        std::println("  the window's main thread woke {:.1f} times a second while the pointer moved 120 times a second elsewhere",
                     woke.value_or(0));
        expect(woke.value_or(0) <= 3.0) << std::format("{:.1f} wakeups a second: motion that is not the window's reached its loop", woke.value_or(0));
    };
}};
