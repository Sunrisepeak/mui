module omui;

import std;
import mcpplibs.cmdline;
import omui.core;
import omui.view;
import omui.agent;
import omui.generate;
import omui.ui.base;
import omui.ui.window;
import omui.host.headless;
import mlibs.trace;
import mlibs.i18n;
import mlibs.i18n.cldr;
import mcpplibs.os;
import :app;
import :build_profile;
import :agent_guide;
import :shown;

namespace omui {

namespace cmdline = mcpplibs::cmdline;  // names this file uses unqualified, from their owner
using mlibs::log::set_log_filter;
using mlibs::log::error;
using mlibs::log::warn;
}

namespace omui {

std::string_view application_build_profile() noexcept { return build::profile; }

std::expected<AgentPolicy, std::string> application_agent_policy(const AppSpec& app, std::optional<AgentMode> startup_mode) {
    auto cap = parse_cap(app.agent);
    if (!cap) return std::unexpected(std::format("AppSpec::agent `{}` is not a cap (none, read, reversible, irreversible, external)", app.agent));
    auto policy = agent_policy_for_profile(application_build_profile(), startup_mode.value_or(app.agent_mode));
    policy.cap = *cap;
    policy.input = app.agent_input;
    policy.developer = app.agent_developer;
    policy.user = app.agent_user;
    return policy;
}

void RunOptions::declare(cmdline::App& app) {
    using cmdline::Option;
    (void)app
        .option(Option("headless").help("no window: drawn offscreen, operated through the agent protocol"))
        .option(Option("serve").help("headless, and kept running for agents (level h3)"))
        .option(Option("frames").takes_value().value_name("N").help("run N frames, then end"))
        .option(Option("png").takes_value().value_name("FILE").help("the last frame as a PNG"))
        .option(Option("level").takes_value().value_name("hN").help("how much a headless run keeps: h0, h1, h2 or h3"))
        .option(Option("agent-dir").takes_value().value_name("DIR").help("where the agent endpoint says where it is"))
        .option(Option("agent-name").takes_value().value_name("NAME").help("the endpoint's name (default: the application's id)"))
        .option(Option("agent-mode").takes_value().value_name("MODE").help("how far agents may go: automatic, development or dual-user"))
        .option(Option("agent-guide").takes_value().value_name("DIR").help("write what an agent needs to operate it into DIR, then end"))
        .option(Option("no-agent").help("no agent endpoint"))
        .option(Option("agent-input").help("agents may send input events (default: the application's)"))
        .option(Option("no-agent-input").help("agents may not send input events"))
        .option(Option("no-agent-player").help("no agent joins as a player"))
        .option(Option("no-accessibility").help("no accessibility bridge"))
        .option(Option("dark").help("the dark theme"))
        .option(Option("gpu").help("draw on the GPU"))
        .option(Option("cpu").help("draw on the CPU"))
        .option(Option("reduce-transparency").help("solid surfaces where styles would be translucent"))
        .option(Option("size").takes_value().value_name("WxH").help("the window's size, e.g. 800x600"))
        .option(Option("debug").takes_value().value_name("LIST").help("overlays: bounds, repaints, inspector, hud"))
        .option(Option("log").takes_value().value_name("FILTER").help("log levels per category: layout=debug,*=info (also OMUI_LOG)"))
        .option(Option("profile").takes_value().value_name("FILE").help("record a trace of the run (Chrome / Perfetto JSON)"))
        .option(Option("lang").takes_value().value_name("TAGS").help("the person's languages, best first: zh-CN,en"))
        .option(Option("window").takes_value().value_name("MODE").help("who draws the window's frame: native, framework or switchable"))
        .option(Option("replay").takes_value().value_name("FILE").help("play a recording (ui.record) headless, then check it ends as it did"))
        .option(Option("present-to").takes_value().value_name("SOCKET").help("drawn in a host's window (omui dev): its frames sent to SOCKET, its input from there"));
}

std::expected<RunOptions, std::string> RunOptions::from(const cmdline::ParsedArgs& parsed) {
    RunOptions o;
    o.headless = parsed.is_flag_set("headless");
    if (parsed.is_flag_set("serve")) o.serve = o.headless = true;
    o.agent = !parsed.is_flag_set("no-agent");
    if (parsed.is_flag_set("agent-input")) o.agent_input = true;
    if (parsed.is_flag_set("no-agent-input")) o.agent_input = false;
    o.no_agent_player = parsed.is_flag_set("no-agent-player");
    o.accessibility = !parsed.is_flag_set("no-accessibility");
    o.dark = parsed.is_flag_set("dark");
    if (parsed.is_flag_set("gpu")) o.gpu = true;
    if (parsed.is_flag_set("cpu")) o.gpu = false;
    o.reduce_transparency = parsed.is_flag_set("reduce-transparency");
    if (auto v = parsed.value("agent-mode")) {
        const auto mode = parse_agent_mode(*v);
        if (!mode) return std::unexpected("--agent-mode is automatic, development or dual-user");
        o.agent_mode = *mode;
    }
    if (auto v = parsed.value("frames")) {
        int count = 0;
        if (std::from_chars(v->data(), v->data() + v->size(), count).ec != std::errc{} || count < 1) return std::unexpected("--frames takes a positive number");
        o.frames = count;
    }
    if (auto v = parsed.value("png")) o.png = std::filesystem::path(*v);
    if (auto v = parsed.value("level")) {
        if (v->size() != 2 || (*v)[0] != 'h' || (*v)[1] < '0' || (*v)[1] > '3') return std::unexpected("--level is h0, h1, h2 or h3");
        o.level = (*v)[1] - '0';
    }
    if (auto v = parsed.value("agent-dir")) o.agent_dir = std::filesystem::path(*v);
    if (auto v = parsed.value("agent-name")) {
        if (v->empty()) return std::unexpected("--agent-name takes a name");
        o.agent_name = *v;
    }
    if (auto v = parsed.value("window")) {
        if (*v != "native" && *v != "framework" && *v != "switchable") return std::unexpected("--window is native, framework or switchable");
        o.window = *v;
    }
    if (auto v = parsed.value("size")) {
        float width = 0, height = 0;
        const auto by = v->find('x');
        if (by == std::string::npos || std::from_chars(v->data(), v->data() + by, width).ec != std::errc{} ||
            std::from_chars(v->data() + by + 1, v->data() + v->size(), height).ec != std::errc{} || width <= 0 || height <= 0)
            return std::unexpected("--size is WxH, e.g. 800x600");
        o.size = Size{width, height};
    }
    if (auto v = parsed.value("debug")) {
        for (auto part : *v | std::views::split(',')) {
            const std::string_view item(part.begin(), part.end());
            if (item == "bounds") o.debug.bounds = true;
            else if (item == "repaints") o.debug.repaints = true;
            else if (item == "inspector") o.debug.inspector = true;
            else if (item == "hud") o.debug.hud = true;
            else return std::unexpected(std::format("--debug: no overlay `{}` (bounds, repaints, inspector, hud)", item));
        }
        o.debug.slow_frame_ms = 1000.0 / 60.0;
    }
    if (auto v = parsed.value("log")) o.log = *v;
    if (auto v = parsed.value("lang")) {
        for (auto part : *v | std::views::split(',')) {
            const std::string_view item(part.begin(), part.end());
            if (!mlibs::i18n::Locale::parse(item)) return std::unexpected(std::format("--lang: `{}` is not a language tag (zh-CN, en)", item));
            o.languages.emplace_back(item);
        }
    }
    if (auto v = parsed.value("agent-guide")) {
        o.agent_guide = std::filesystem::path(*v);
        o.headless = true;
    }
    if (auto v = parsed.value("profile")) o.profile = std::filesystem::path(*v);
    if (auto v = parsed.value("replay")) {
        o.replay = std::filesystem::path(*v);
        o.headless = true;
    }
    if (auto v = parsed.value("present-to")) {
        o.present_to = mcpplibs::os::fs::from_utf8(*v);
        o.headless = true;
    }
    if (o.serve) o.level = 3;
    return o;
}

std::expected<RunOptions, std::string> RunOptions::parse(std::span<const std::string_view> args) {
    cmdline::App app{"application"};
    declare(app);
    std::vector<std::string> argv{"application"};
    for (const std::string_view arg : args) argv.emplace_back(arg);
    auto parsed = app.parse_from(std::span<const std::string>(argv));
    if (!parsed) return std::unexpected(parsed.error().message);
    return from(*parsed);
}

namespace {

// CLDR's packs, the application's catalogs, and the person's languages:
// --lang, else what the host or the system says.
void languages_of(const AppSpec& app, const RunOptions& o, std::vector<std::string> host) {
    Languages& l = languages();
    l.edit([](mlibs::i18n::Library& lib) { mlibs::i18n::cldr::add_to(lib); });
    if (auto source = mlibs::i18n::Locale::parse(app.source_language)) l.set_fallback(*source);
    auto add = [&](std::string_view tag, std::string_view text, std::string_view file) {
        auto locale = mlibs::i18n::Locale::parse(tag);
        if (!locale) {
            warn("{}: `{}` is not a language tag", file, tag);
            return;
        }
        if (auto r = l.add_messages(*locale, text, file); !r)
            for (const auto& d : r.error()) warn("{}", d.text());
    };
    for (const auto& [tag, text] : app.catalogs) add(tag, text, std::format("{}.messages.omui.json", tag));
    if (!app.messages.empty()) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(app.messages, ec)) {
            const std::string name = utf8(e.path().filename());
            constexpr std::string_view suffix = ".messages.omui.json";
            if (!name.ends_with(suffix)) continue;
            auto text = mcpplibs::os::fs::read(e.path());
            if (!text) {
                warn("{}: {}", utf8(e.path()), text.error().message());
                continue;
            }
            add(std::string_view(name).substr(0, name.size() - suffix.size()), *text, utf8(e.path()));
        }
    }
    std::vector<mlibs::i18n::Locale> requested;
    for (const auto& t : o.languages.empty() ? host : o.languages)
        if (auto loc = mlibs::i18n::Locale::parse(t)) requested.push_back(*loc);
    if (o.languages.empty() && requested.empty()) requested = system_locales();
    l.request(std::move(requested));
}

// Each application startup owns its policy and grants. An endpoint may
// already exist; its Protocol binds this instance lazily at first request.
std::shared_ptr<AgentAccess> agent_access_of(const AppSpec& app, const RunOptions& options) {
    auto policy = application_agent_policy(app, options.agent_mode);
    if (!policy) { error("{}", policy.error()); return {}; }
    policy->enabled &= options.agent;
    if (!options.agent) policy->source += ";startup:no-agent";
    auto access = std::make_shared<AgentAccess>();
    access->set_policy(std::move(*policy));
    for (const std::string& text : app.grants) {
        auto grant = openas::parse_grant(text);
        if (!grant) {
            error("AppSpec::grants `{}` is not a grant (actor=effect[@scope,…], openas §5.5)", text);
            return {};
        }
        access->give(std::move(*grant));
    }
    return access;
}

// Who draws the window's frame: --window, else the application's choice.
std::string window_mode(const AppSpec& app, const RunOptions& o) { return o.window.empty() ? app.window : o.window; }

// The framework's frame around the application, when it may show (D-75).
std::unique_ptr<FramedWindow> frame_of(Runtime& rt, const AppSpec& app, std::string_view mode) {
    if (mode == "native") return nullptr;
    WindowFrameOptions f = app.frame;
    if (!f.title.is_set()) f.title = app.title;
    return std::make_unique<FramedWindow>(rt, std::move(f));
}

// A headless run's window (a frame drawn as it would be on a desktop, for
// pictures and agents): its operations change its state, nothing else.
struct HeadlessWindow final : WindowHost {
    bool framework = true;
    bool framework_drawn() const noexcept override { return framework; }
    void minimize() override { window().set_state(WindowState::minimized); }
    void toggle_maximize() override {
        window().set_state(window().state() == WindowState::maximized ? WindowState::normal : WindowState::maximized);
    }
    void set_fullscreen(bool on) override { window().set_state(on ? WindowState::fullscreen : WindowState::normal); }
    void request_close() override { window().set_closing(true); }
    bool pass_through(bool) override { return false; }
    bool set_framework_drawn(bool on) override {
        framework = on;
        return true;
    }
};

// AppSpec::teardown, when it goes out of scope: declared after the host, so
// before the runtime goes.
struct TearDown {
    AppSpec* app = nullptr;
    Runtime* rt = nullptr;
    TearDown(AppSpec& a, Runtime& r) : app(&a), rt(&r) {}
    TearDown(const TearDown&) = delete;
    TearDown& operator=(const TearDown&) = delete;
    ~TearDown() {
        if (app && app->teardown) app->teardown(*rt);
    }
};

// What is kept while the application runs: its background tasks (task.*, W8) and its
// generation (generate.*), when it has one — the generation goes first.
struct Prepared {
    std::unique_ptr<agent::TaskBroker> tasks;
    std::unique_ptr<generate::Generation> generation;
};

Prepared prepare(Runtime& rt, AppSpec& app, const std::shared_ptr<AgentAccess>& access) {
    rt.set_agent_access(access);
    rt.set_agent_players(app.agent_players);
    for (VerbSpec& v : app.verbs)
        if (auto r = rt.verbs().add(std::move(v.desc), std::move(v.fn)); !r) warn("verb not added: {}", r.error());
    if (app.setup) app.setup(rt);
    if (app.view) rt.mount("main", app.view());
    Prepared kept;
    kept.tasks = std::make_unique<agent::TaskBroker>(rt);
    if (app.generation) {
        if (!app.generation->tasks) app.generation->tasks = kept.tasks.get();
        kept.generation = std::make_unique<generate::Generation>(rt, std::move(*app.generation));
    }
    return kept;
}

// A recording (the protocol's `record`, W12) played again: each frame at its
// time with its input, then — what runs beside the runtime done, at the last
// frame's time — the tree and the pixels hashed as the recording's were.
int replay(Headless& host, const AppSpec& app, const std::filesystem::path& file) {
    std::optional<Value> recording;
    if (const auto text = mcpplibs::os::fs::read(file))
        if (auto parsed = parse_json(*text)) recording = std::move(*parsed);
    // A recording is an answer's data, or the scenario itself.
    if (recording && recording->get("data")) recording = Value(*recording->get("data"));
    if (!recording || !recording->get("frames") || !recording->get("frames")->list()) {
        error("{}: not a recording (omui agent call … '{{\"op\":\"record\"}}')", utf8(file));
        return 2;
    }
    const Value& scenario = *recording;
    double last = 0;
    std::size_t frames = 0;
    for (const Value& frame : *scenario.get("frames")->list()) {
        std::vector<InputEvent> events;
        if (const Value* list = frame.get("events"); list && list->list())
            for (const Value& event : *list->list())
                if (auto parsed = event_from_value(event)) events.push_back(std::move(*parsed));
        last = frame.get("t") ? frame.get("t")->as_double() : last;
        (void)host.step(std::move(events), seconds(last));
        ++frames;
    }
    for (int i = 0; app.ready && !app.ready() && i < 10000; ++i) {
        (void)host.step({}, seconds(last));  // what runs beside it finishes; the time stays the last frame's
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Value report;
    report.set("command", "replay").set("frames", static_cast<double>(frames));
    const Value* expect = scenario.get("expect");
    bool same = true;
    const std::string tree = agent::tree_hash(host.tree());
    if (const Value* expected = expect ? expect->get("tree") : nullptr) {
        report.set("tree", Value{}.set("expected", *expected).set("got", tree).set("same", expected->as_string() == tree));
        same = same && expected->as_string() == tree;
    }
    if (const Value* expected = expect ? expect->get("pixels") : nullptr) {
        std::string got;
        if (auto png = host.png()) {
            auto pixels = agent::pixels_hash(*png);
            got = pixels ? *pixels : pixels.error();
        } else {
            got = png.error();
        }
        const bool equal = expected->as_string() == got;
        report.set("pixels", Value{}.set("expected", *expected).set("got", got).set("same", equal));
        same = same && equal;
    }
    report.set("ok", same);
    std::println(std::cout, "{}", report.json());
    return same ? 0 : 1;
}

// Drawn in a host's window (W16, D-154): each frame's display list sent to the host, its person's
// input and its window's size taken from it, on the wall clock, until the host goes.
int present(Headless& host, const std::filesystem::path& socket) {
    auto connected = mcpplibs::os::ipc::connection::connect(socket.parent_path(), utf8(socket.filename()));
    if (!connected) {
        error("no host at {}: {}", utf8(socket), connected.error().message());
        return 1;
    }
    Presenter presenter(host.runtime(), std::move(*connected));
    const auto started = std::chrono::steady_clock::now();
    while (!mcpplibs::os::interrupt::requested()) {
        if (const auto size = presenter.resized()) host.runtime().resize(size->first, size->second);
        const seconds now{std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()};
        const FrameOutput frame = host.step(presenter.input(), now);
        if (!presenter.send(frame)) break;  // the host is gone
        (void)mcpplibs::os::interrupt::wait(std::chrono::milliseconds(frame.animating ? 8 : 16));
    }
    return 0;
}

int headless(AppSpec app, const RunOptions& o, const std::shared_ptr<AgentAccess>& access) {
    HeadlessOptions h;
    h.level = static_cast<HeadlessLevel>(std::clamp(o.level, 0, 3));
    if (!o.agent && h.level == HeadlessLevel::session) h.level = HeadlessLevel::pixels;
    h.size = o.size.value_or(app.size);
    h.theme = o.dark ? Theme::dark_theme() : app.theme;
    h.app = app.id;
    h.endpoint_directory = o.agent_dir;
    h.agent_input = o.agent_input.value_or(app.agent_input);
    h.endpoint_name = o.agent_name;
    Headless host(h);
    // Its endpoint is found from here on; what an agent asks before the application is ready waits for it.
    host.hold_requests(static_cast<bool>(app.ready));
    host.runtime().set_debug(o.debug);
    host.runtime().set_reduce_transparency(o.reduce_transparency);
    const std::string mode = window_mode(app, o);
    HeadlessWindow shown;
    shown.framework = mode == "framework";
    if (mode != "native") window().attach(&shown, host.runtime().verbs());
    struct Detach {
        bool attached;
        ~Detach() {
            if (attached) window().detach();
        }
    } detach{mode != "native"};
    const auto prepared = prepare(host.runtime(), app, access);
    const TearDown teardown(app, host.runtime());
    const auto framed = frame_of(host.runtime(), app, mode);
    if (!o.replay.empty() || o.frames) host.hold_requests(false);  // a run of given frames is as it was told
    if (!o.replay.empty()) return replay(host, app, o.replay);
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
        host.hold_requests(false);
    }
    if (!o.present_to.empty()) return present(host, o.present_to);
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
    if (!o.agent_guide.empty()) {
        // What an agent needs, from the application as it is mounted.
        const Value snapshot = host.tree().snapshot().to_value();
        if (auto r = write_agent_guide(agent_guide(app, host.runtime(), snapshot), o.agent_guide); !r) {
            error("{}", r.error());
            return 1;
        }
        std::println(std::cout, "{{\"command\": \"agent-guide\", \"ok\": true, \"dir\": \"{}\"}}", utf8(o.agent_guide));
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
    if (o.no_agent_player) app.agent_players = false;  // the person's word for this run
    register_base_components();
    register_window_components();
    if (const std::string mode = window_mode(app, o); mode != "native" && mode != "framework" && mode != "switchable") {
        error("AppSpec::window `{}` is native, framework or switchable", mode);
        return 2;
    }
    // Log levels: OMUI_LOG, then --log.
    for (const std::string& filter : {mcpplibs::os::env::get("OMUI_LOG").value_or(std::string{}), o.log})
        if (!filter.empty())
            if (auto r = set_log_filter(filter); !r) warn("log filter `{}`: {}", filter, r.error());
    if (!o.profile.empty()) mlibs::trace::start();
    struct Profile {
        const std::filesystem::path& file;
        ~Profile() {
            if (file.empty()) return;
            const mlibs::trace::Trace t = mlibs::trace::stop();
            if (auto w = mcpplibs::os::fs::write_atomic(file, t.chrome_json()); !w) error("{}: {}", utf8(file), w.error().message());
            else std::print(std::cerr, "{}", t.table());
        }
    } profile{o.profile};
    languages_of(app, o, o.headless ? std::vector<std::string>{} : hosting::host_locales());
    auto access = agent_access_of(app, o);
    if (!access) return 2;
    if (o.headless) return headless(std::move(app), o, access);
    // A window on the desktop, the page's canvas on the Web (src/host/<where>/shown.cpp).
    const std::string mode = window_mode(app, o);
    return hosting::shown(app, o, [&](Runtime& rt) -> std::shared_ptr<void> {
        auto prepared = prepare(rt, app, access);
        // Kept as long as the window: the frame, the tasks and generation, and the teardown before them.
        struct Kept {
            std::unique_ptr<FramedWindow> frame;
            Prepared                      prepared;
            TearDown                      teardown;  // declared last: it goes first
            Kept(AppSpec& a, Runtime& r, std::unique_ptr<FramedWindow> f, Prepared p) : frame(std::move(f)), prepared(std::move(p)), teardown(a, r) {}
        };
        return std::make_shared<Kept>(app, rt, frame_of(rt, app, mode), std::move(prepared));
    });
}

std::expected<CommandLine, int> command_line(cmdline::App line, int argc, char** argv) {
    RunOptions::declare(line);
    auto parsed = line.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return std::unexpected(0);  // --help, --version: said
        std::println(std::cerr, "{}", parsed.error().message);
        return std::unexpected(2);
    }
    auto options = RunOptions::from(*parsed);
    if (!options) {
        std::println(std::cerr, "{}", options.error());
        return std::unexpected(2);
    }
    return CommandLine{std::move(*parsed), std::move(*options)};
}

int run(AppSpec app, int argc, char** argv) {
    auto command = command_line(cmdline::App(app.id.empty() ? std::string("application") : app.id).description(app.title), argc, argv);
    if (!command) return command.error();
    return run(std::move(app), command->options);
}

}  // namespace omui
