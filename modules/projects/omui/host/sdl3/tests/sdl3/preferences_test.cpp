// The person's appearance as the system says it, on Linux the desktop portal's settings (mpal): a copy of this program
// opens a window on SDL's offscreen driver with its session bus pointed at a portal of the test's own — no display, and
// nothing of the machine's own session read or changed. The window opens in the scheme the portal says, a theme that
// follows the preference with it; the portal's change wakes the loop asleep in its wait, and the theme turns.
import std;
import mlibs.text.harfbuzz;
import mcpplibs.os;
import mcpplibs.mpal;
import mlibs.trace;
import omui.host.sdl3;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace dbus = mcpplibs::mpal::dbus;

namespace {

using dbus::Value;

Value variant_of(std::string signature, Value inner) { return Value{dbus::Variant{std::move(signature), std::make_shared<Value>(std::move(inner))}}; }

Value field(std::uint8_t code, std::string signature, Value value) {
    return Value{std::vector<Value>{Value{code}, variant_of(std::move(signature), std::move(value))}};
}

// A message as a bus sends it: its kind and serial, header fields, a body by signature.
std::string message_bytes(dbus::Kind kind, std::uint32_t serial, std::vector<Value> fields, std::string_view signature, std::span<const Value> body_values) {
    const auto body = dbus::marshal(signature, body_values).value();
    if (!signature.empty()) fields.push_back(field(8, "g", Value::of(std::string(signature))));
    std::string header{'l', static_cast<char>(kind), '\0', '\1'};
    for (const std::uint32_t word : {static_cast<std::uint32_t>(body.size()), serial})
        for (int index = 0; index < 4; ++index) header.push_back(static_cast<char>((word >> (8 * index)) & 0xFF));
    header += dbus::marshal("a(yv)", std::array{Value{std::move(fields)}}, header.size()).value();
    while (header.size() % 8 != 0) header.push_back('\0');
    return header + body;
}

std::string reply_bytes(std::uint32_t serial, std::uint32_t reply_to, std::string_view signature, std::span<const Value> body_values) {
    return message_bytes(dbus::Kind::reply, serial, {field(5, "u", Value{reply_to})}, signature, body_values);
}

// The portal's SettingChanged(namespace, key, value).
std::string setting_changed(std::uint32_t serial, std::string space, std::string key, std::string signature, Value value) {
    return message_bytes(dbus::Kind::signal, serial,
                         {field(1, "o", Value::of("/org/freedesktop/portal/desktop")), field(2, "s", Value::of("org.freedesktop.portal.Settings")),
                          field(3, "s", Value::of("SettingChanged"))},
                         "ssv", std::array{Value::of(std::move(space)), Value::of(std::move(key)), variant_of(std::move(signature), std::move(value))});
}

// A session bus of the test's own with a desktop portal on it, for one connection: the authentication, Hello, the
// match rules and ReadAll answered (a dark scheme, GNOME's animations off), then a change of scheme when told.
struct Portal {
    std::filesystem::path                      dir = std::filesystem::temp_directory_path() / std::format("omui-portal-{}", std::random_device{}());
    std::optional<mcpplibs::os::ipc::listener> listener;
    std::vector<std::string>                   heard;  // the members asked, in order
    std::binary_semaphore                      change{0}, ended{0};
    std::jthread                               thread;

    Portal() {
        std::filesystem::create_directories(dir);
        if (auto listening = mcpplibs::os::ipc::listener::listen(dir, "bus")) listener.emplace(std::move(*listening));
        if (!listener) return;
        thread = std::jthread([this] {
            auto peer = listener->accept_for(std::chrono::milliseconds(20000));
            if (!peer || !*peer) return;
            auto& connection = **peer;
            std::string pending;
            const auto line = [&] {
                while (!pending.contains("\r\n")) {
                    auto chunk = connection.read_for(std::chrono::milliseconds(5000));
                    if (!chunk || !*chunk || (*chunk)->empty()) return false;
                    pending += **chunk;
                }
                pending.erase(0, pending.find("\r\n") + 2);
                return true;
            };
            if (!line() || !connection.write("DATA\r\n") || !line() || !connection.write("OK 0123456789abcdef\r\n") || !line()) return;
            std::uint32_t serial = 100;
            for (int answered = 0; answered < 4;) {
                auto message = dbus::read_message(pending);
                if (!message) return;
                if (!*message) {
                    auto chunk = connection.read_for(std::chrono::milliseconds(5000));
                    if (!chunk || !*chunk || (*chunk)->empty()) return;
                    pending += **chunk;
                    continue;
                }
                auto [read, used] = std::move(**message);
                pending.erase(0, used);
                heard.push_back(read.member);
                ++answered;
                if (read.member == "Hello") (void)connection.write(reply_bytes(++serial, read.serial, "s", std::array{Value::of(":1.9")}));
                else if (read.member == "AddMatch") (void)connection.write(reply_bytes(++serial, read.serial, "", {}));
                else {
                    const auto setting = [](std::string key, std::string signature, Value value) {
                        return Value{std::vector<Value>{Value::of(std::move(key)), variant_of(std::move(signature), std::move(value))}};
                    };
                    const std::vector<Value> spaces{
                        Value{std::vector<Value>{Value::of("org.freedesktop.appearance"), Value{std::vector<Value>{setting("color-scheme", "u", Value{std::uint32_t{1}})}}}},
                        Value{std::vector<Value>{Value::of("org.gnome.desktop.interface"), Value{std::vector<Value>{setting("enable-animations", "b", Value{false})}}}}};
                    (void)connection.write(reply_bytes(++serial, read.serial, "a{sa{sv}}", std::array{Value{spaces}}));
                }
            }
            if (!change.try_acquire_for(std::chrono::seconds(30))) return;
            (void)connection.write(setting_changed(++serial, "org.freedesktop.appearance", "color-scheme", "u", Value{std::uint32_t{2}}));
            (void)ended.try_acquire_for(std::chrono::seconds(30));
        });
    }
    ~Portal() {
        change.release();
        ended.release();
        if (thread.joinable()) thread.join();
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
};

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made{mlibs::text::harfbuzz::shaper()};
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

// The copy: a window following the system, the application's theme following the preference; what the runtime has
// once it is open, and once it has been woken by a change (or by the watchdog, 10 s on).
child preferring_window{"preferring-window", [](std::vector<std::string>) {
    DesktopOptions o;
    o.title = "openmui preferences";
    o.size = {120, 80};
    o.video_driver = "offscreen";
    o.vsync = false;
    o.fonts = &fonts();
    o.app = "sdl3-preferences";
    o.agent = false;
    o.accessibility = false;
    o.renderer = DesktopOptions::Renderer::cpu;
    Desktop desktop(o);
    if (auto opened = desktop.open(); !opened) {
        std::println("no window: {}", opened.error().text());
        return 1;
    }
    Runtime& runtime = desktop.runtime();
    runtime.set_theme_by_preference([](bool dark) { return dark ? Theme::dark_theme() : Theme::light(); });
    desktop.mount("main", column({}, {}));
    for (int step = 0; step < 4; ++step) desktop.step();
    std::println("open dark={} theme={} reduce={}", runtime.dark_preferred(), runtime.theme().dark, runtime.reduce_motion());
    std::cout.flush();
    std::atomic<bool> timed_out{false};
    std::jthread watchdog([&](std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any never;
        std::unique_lock lock(mutex);
        if (never.wait_for(lock, stop, std::chrono::seconds(10), [] { return false; })) return;
        if (stop.stop_requested()) return;
        timed_out = true;
        desktop.wake();
    });
    int turns = 0;
    while (runtime.dark_preferred() && !timed_out) {
        desktop.step(true);  // asleep: nothing animates, no timers — until something wakes it
        ++turns;
    }
    watchdog.request_stop();
    std::println("changed dark={} theme={} reduce={} timed_out={} turns={}", runtime.dark_preferred(), runtime.theme().dark,
                 runtime.reduce_motion(), timed_out.load(), turns);
    std::cout.flush();
    return 0;
}};

// The lines the copy wrote, read until one starts with `prefix` (none after `wait`).
std::optional<std::string> line_starting(mcpplibs::os::process::process& copy, std::string& pending, std::string_view prefix,
                                         std::chrono::milliseconds wait) {
    const auto until = std::chrono::steady_clock::now() + wait;
    while (true) {
        for (std::size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
            std::string line = pending.substr(0, end);
            pending.erase(0, end + 1);
            if (line.starts_with(prefix)) return line;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        if (left.count() <= 0) return std::nullopt;
        auto chunk = copy.read_output_for(left);
        if (!chunk || (*chunk && chunk->value().empty())) return std::nullopt;
        if (*chunk) pending += **chunk;
    }
}

}  // namespace

static UTest cases{"host.sdl3.preferences", [] {
    "the system's appearance (the portal, through mpal): read as the window opens, a change heard while it sleeps, the theme turning"_test = [] {
        namespace os = mcpplibs::os;
        if constexpr (!os::is_linux) skip("the portal is Linux's; SDL says the appearance elsewhere");
        Portal portal;
        expect(fatal(portal.listener.has_value())) << "the test's own bus";
        const auto command = child_command("preferring-window");
        os::process::spawn_options options;
        options.program = os::fs::from_utf8(command.front());
        options.args.assign(command.begin() + 1, command.end());
        options.env = {{"DBUS_SESSION_BUS_ADDRESS", std::format("unix:path={}", os::fs::utf8(portal.dir / "bus"))}};
        options.out = os::process::stdio::pipe;
        options.err = os::process::stdio::null;
        options.bound_lifetime = true;
        auto copy = os::process::process::spawn(options);
        expect(fatal(copy.has_value()));
        std::string pending;
        const auto opened = line_starting(*copy, pending, "open ", std::chrono::seconds(30));
        expect(fatal(opened.has_value())) << "the window opened: " << pending;
        expect(*opened == "open dark=true theme=true reduce=true") << *opened << " — the portal's dark scheme and GNOME's animations off, read as it opened";
        const auto told = std::chrono::steady_clock::now();
        portal.change.release();  // the scheme turns light
        const auto changed = line_starting(*copy, pending, "changed ", std::chrono::seconds(30));
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - told);
        expect(fatal(changed.has_value())) << "the copy said what it has after the change";
        expect(changed->starts_with("changed dark=false theme=false reduce=true timed_out=false")) << *changed;
        expect(took < std::chrono::seconds(5)) << std::format("woken by the change, not by the watchdog: {} ms", took.count());
        std::println("  {}; {} ({} ms after the portal said it)", *opened, *changed, took.count());
        (void)copy->wait();
        portal.ended.release();
        if (portal.thread.joinable()) portal.thread.join();
        expect(portal.heard == std::vector<std::string>{"Hello", "AddMatch", "AddMatch", "ReadAll"}) << "one conversation: its changes asked for, then read";
    };

    // On a person's desktop, when asked (OMUI_REAL_WINDOW_AT=x,y: where on the screen, in X11's pixels): a real window
    // opened there — placed by the program, the pointer left where it is — reads the session's own settings, and
    // nothing of them is changed. What the window has is what the portal says, read here as well.
    "on a real display (OMUI_REAL_WINDOW_AT): the session's own appearance read as the window opens, nothing changed"_test = [] {
        namespace os = mcpplibs::os;
        const auto at = os::env::get("OMUI_REAL_WINDOW_AT");
        if (!at || at->empty()) skip("no real display asked for (OMUI_REAL_WINDOW_AT=x,y)");
        std::array<int, 2> place{};
        const auto comma = at->find(',');
        if (comma == std::string::npos || std::from_chars(at->data(), at->data() + comma, place[0]).ec != std::errc{} ||
            std::from_chars(at->data() + comma + 1, at->data() + at->size(), place[1]).ec != std::errc{})
            skip(std::format("OMUI_REAL_WINDOW_AT is x,y, not {}", *at));
        const auto said = mcpplibs::mpal::preferences();
        DesktopOptions o;
        o.title = "openmui preferences (real display)";
        o.size = {240, 160};
        o.position = place;
        o.fonts = &fonts();
        o.app = "sdl3-preferences-real";
        o.agent = false;
        o.accessibility = false;
        o.renderer = DesktopOptions::Renderer::cpu;
        mlibs::trace::start();
        const auto began = std::chrono::steady_clock::now();
        Desktop desktop(o);
        auto opened = desktop.open();
        expect(fatal(opened.has_value())) << (opened ? std::string() : opened.error().text());
        const auto open_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        double waited_ms = 0;  // how long the window's opening waited for the reading (begun with the host)
        for (const mlibs::trace::Stat& stat : mlibs::trace::stop().summary())
            if (stat.name == "start.preferences") waited_ms = stat.max_ms;
        desktop.mount("main", column({}, {}));
        for (int step = 0; step < 100; ++step) {  // a second and a half on the screen: long enough to be looked at
            desktop.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        const Runtime& runtime = desktop.runtime();
        std::println("  the portal says: {}; the window has dark={} reduce={}, open in {:.1f} ms, {:.2f} of them waiting for the reading",
                     said ? std::format("dark={} reduce={}", said->dark ? (*said->dark ? "yes" : "no") : "unsaid",
                                        said->reduce_motion ? (*said->reduce_motion ? "yes" : "no") : "unsaid")
                          : std::format("unread ({})", said.error().text),
                     runtime.dark_preferred(), runtime.reduce_motion(), open_ms, waited_ms);
        const bool dark = said && said->dark.value_or(false);
        const bool reduce = said && said->reduce_motion.value_or(false);
        expect(runtime.dark_preferred() == dark && runtime.reduce_motion() == reduce) << "the window has what the session's portal says";
    };
}};
