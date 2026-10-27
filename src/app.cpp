module openmui;

import std;
import openmui.core;
import openmui.view;
import openmui.agent;
import openmui.ui.base;
import openmui.host.headless;
import openmui.host.sdl3;
import openmui.trace;
import mcpplibs.os;

namespace omui {

std::expected<RunOptions, std::string> RunOptions::parse(std::span<const std::string_view> args) {
    RunOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        auto value = [&]() -> std::optional<std::string_view> {
            if (i + 1 >= args.size()) return std::nullopt;
            return args[++i];
        };
        if (a == "--headless") o.headless = true;
        else if (a == "--serve") o.serve = o.headless = true;
        else if (a == "--no-agent") o.agent = false;
        else if (a == "--dark") o.dark = true;
        else if (a == "--frames") {
            auto v = value();
            int n = 0;
            if (!v || std::from_chars(v->data(), v->data() + v->size(), n).ec != std::errc{} || n < 1)
                return std::unexpected("--frames takes a positive number");
            o.frames = n;
        } else if (a == "--png") {
            auto v = value();
            if (!v) return std::unexpected("--png takes a file");
            o.png = std::filesystem::path(*v);
        } else if (a == "--level") {
            auto v = value();
            if (!v || v->size() != 2 || (*v)[0] != 'h' || (*v)[1] < '0' || (*v)[1] > '3') return std::unexpected("--level is h0, h1, h2 or h3");
            o.level = (*v)[1] - '0';
        } else if (a == "--agent-dir") {
            auto v = value();
            if (!v) return std::unexpected("--agent-dir takes a directory");
            o.agent_dir = std::filesystem::path(*v);
        } else if (a == "--size") {
            auto v = value();
            float w = 0, h = 0;
            const auto x = v ? v->find('x') : std::string_view::npos;
            if (!v || x == std::string_view::npos ||
                std::from_chars(v->data(), v->data() + x, w).ec != std::errc{} ||
                std::from_chars(v->data() + x + 1, v->data() + v->size(), h).ec != std::errc{} || w <= 0 || h <= 0)
                return std::unexpected("--size is WxH, e.g. 800x600");
            o.size = Size{w, h};
        } else if (a == "--debug") {
            auto v = value();
            if (!v) return std::unexpected("--debug takes a list: bounds,repaints,inspector,hud");
            std::string_view list = *v;
            while (!list.empty()) {
                const auto comma = list.find(',');
                const std::string_view item = list.substr(0, comma);
                list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
                if (item == "bounds") o.debug.bounds = true;
                else if (item == "repaints") o.debug.repaints = true;
                else if (item == "inspector") o.debug.inspector = true;
                else if (item == "hud") o.debug.hud = true;
                else return std::unexpected(std::format("--debug: no overlay `{}` (bounds, repaints, inspector, hud)", item));
            }
            o.debug.slow_frame_ms = 1000.0 / 60.0;
        } else if (a == "--log") {
            auto v = value();
            if (!v) return std::unexpected("--log takes a filter: layout=debug,*=info");
            o.log = std::string(*v);
        } else if (a == "--profile") {
            auto v = value();
            if (!v) return std::unexpected("--profile takes a file");
            o.profile = std::filesystem::path(*v);
        } else {
            return std::unexpected(std::format("unknown option `{}` (--headless, --frames N, --png FILE, --level hN, --serve, "
                                               "--agent-dir DIR, --no-agent, --dark, --size WxH, --debug LIST, --log FILTER, --profile FILE)",
                                               a));
        }
    }
    if (o.serve) o.level = 3;
    return o;
}

namespace {

void prepare(Runtime& rt, AppSpec& app) {
    for (VerbSpec& v : app.verbs)
        if (auto r = rt.verbs().add(std::move(v.desc), std::move(v.fn)); !r) warn("verb not added: {}", r.error());
    if (app.setup) app.setup(rt);
    if (app.view) rt.mount("main", app.view());
}

int headless(AppSpec app, const RunOptions& o) {
    HeadlessOptions h;
    h.level = static_cast<HeadlessLevel>(std::clamp(o.level, 0, 3));
    if (!o.agent && h.level == HeadlessLevel::session) h.level = HeadlessLevel::pixels;
    h.size = o.size.value_or(app.size);
    h.theme = o.dark ? Theme::dark_theme() : app.theme;
    h.app = app.id;
    h.endpoint_directory = o.agent_dir;
    Headless host(h);
    host.runtime().set_debug(o.debug);
    prepare(host.runtime(), app);
    if (o.frames) {
        for (int i = 0; i < *o.frames; ++i) host.step();
    } else {
        host.settle();
        // What runs beside the runtime (scripts) finishes, then the frames settle again.
        for (int i = 0; app.ready && !app.ready() && i < 10000; ++i) {
            host.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        host.settle();
    }
    if (o.serve) {
        if (!host.endpoint()) {
            error("the agent endpoint did not start");
            return 1;
        }
        std::println(std::cout, "{} serves openmui.tree@0.1 as `{}` in {} (Ctrl-C ends it)", app.id, host.endpoint()->name(),
                     utf8(host.endpoint()->directory()));
        std::cout.flush();
        // Frames at 60 Hz of virtual time, a frame per few milliseconds of
        // wall time: agents' waits see animations finish quickly and exactly.
        while (!mcpplibs::os::interrupt::requested()) {
            host.step();
            (void)mcpplibs::os::interrupt::wait(std::chrono::milliseconds(4));
        }
    }
    if (!o.png.empty()) {
        if (auto r = host.save_png(o.png); !r) {
            error("{}: {}", utf8(o.png), r.error());
            return 1;
        }
    }
    return 0;
}

}  // namespace

int run(AppSpec app, const RunOptions& o) {
    register_base_components();
    // Log levels: OMUI_LOG, then --log.
    for (const std::string& filter : {mcpplibs::os::env::get("OMUI_LOG").value_or(std::string{}), o.log})
        if (!filter.empty())
            if (auto r = set_log_filter(filter); !r) warn("log filter `{}`: {}", filter, r.error());
    if (!o.profile.empty()) trace::start();
    struct Profile {
        const std::filesystem::path& file;
        ~Profile() {
            if (file.empty()) return;
            const trace::Trace t = trace::stop();
            if (auto w = mcpplibs::os::fs::write_atomic(file, t.chrome_json()); !w) error("{}: {}", utf8(file), w.error().message());
            else std::print(std::cerr, "{}", t.table());
        }
    } profile{o.profile};
    if (o.headless) return headless(std::move(app), o);
    DesktopOptions d;
    d.title = app.title;
    d.size = o.size.value_or(app.size);
    d.app = app.id;
    d.agent = o.agent;
    d.agent_directory = o.agent_dir;
    d.theme = o.dark ? Theme::dark_theme() : app.theme;
    Desktop desktop(d);
    if (auto r = desktop.open(); !r) {
        error("no window ({}); `--headless` runs without a display", r.error());
        return 1;
    }
    desktop.runtime().set_debug(o.debug);
    prepare(desktop.runtime(), app);
    return desktop.run();
}

int run(AppSpec app, int argc, char** argv) {
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    auto o = RunOptions::parse(args);
    if (!o) {
        std::println(std::cerr, "{}", o.error());
        return 2;
    }
    return run(std::move(app), *o);
}

}  // namespace omui
