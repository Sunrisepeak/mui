// `bench apps` (C1, N2; lead design §2a.1): whole applications side by side, measured from outside
// their processes. The sides and the pins are tests/bench/competitors/apps/apps.bench.xdef.json;
// their sources are beside it (omui's in src/, Slint's in slint/, Flutter's in flutter/); NSH's are
// its own (`nsh init`'s templates, its examples) at the pinned commit. Their side is built with the
// system's compilers and libraries (what each documents: Rust, Flutter's SDK, GTK), ours with mcpp.
//
// The window is on an X server the harness starts (Xvfb, no window manager, no compositor):
// whatever is drawn on the screen is the program's, and the server tells when (DAMAGE). A frame is
// presented when the server has it. GL reaches Mesa there (llvmpipe: NVIDIA's EGL makes no context
// on a server without DRI3), Vulkan this machine's GPU; each run says which its process mapped.
module omui.devtools.bench;

import std;
import mcpplibs.os;
import mcpplibs.cmdline;
import nlohmann.json;
import omui.devtools.gpu;
import omui.devtools.lead;
import omui.devtools.run;
import omui.devtools.x11;
import mlibs.image;

namespace omui::devtools::bench {

namespace {

namespace fs = std::filesystem;
namespace process = mcpplibs::os::process;
using json = nlohmann::ordered_json;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr std::string_view catalogue_path = "tests/bench/competitors/apps/apps.bench.xdef.json";

std::string utf8(const fs::path& path) {
    return mcpplibs::os::fs::utf8(path);
}

double ms_between(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// One application as it is built and run.
struct Side {
    std::string series, app, build, world, program;
    std::vector<std::string> args;
    std::optional<std::array<int, 2>> click;   // the button, in the window
    std::optional<x11::Rect> label;            // what the click changes, in the window
    std::optional<std::array<int, 2>> window;  // its size, where not the comparison's
    std::vector<std::pair<std::string, std::optional<std::string>>> env;  // set for it
    std::string
        motion;  // "live" (it animates by itself) or "scroll" (the wheel turns every 1/120 s)
    // What the build made: the program run, where it runs (its files dropped before a cold start),
    // the package.
    fs::path executable, cwd, run_tree, tree;
    std::uintmax_t executable_bytes = 0, package_bytes = 0;
    std::vector<fs::path> mapped;  // the files its last run mapped: evicted before a cold start
    std::string driver;            // the graphics its process mapped
};

struct Settings {
    int width = 800, height = 600;  // every window, after its first frame
    int screen_width = 1920, screen_height = 1080;
    // A desktop's display (RM1, `--screen`): only the program's own window is watched and read,
    // found by its process; the windows open on `monitor` (the window manager places a new window
    // on the pointer's monitor), and input goes to the window only while it is under the pointer.
    bool desktop = false;
    x11::Rect monitor{0, 0, 1920, 1080};
    std::array<int, 2> park{1916, 1076};  // the pointer between cases: on the monitor, off the window
    // Draws further apart than this are two presents (half a refresh of the display: 60 Hz here,
    // the monitor's rate on a desktop).
    Clock::duration frame_gap = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(0.5 / 60.0));
    milliseconds rest{5000}, idle{5000}, quiet{600}, timeout{30000};
    int clicks = 5;
    // Where an omui side's measured launch writes its trace (`--profile`): where the time goes in the
    // process, beside what is measured from outside. Empty: none.
    fs::path trace;
};

// What a launch saw.
struct Launch {
    std::optional<double> start_ms, paint_ms, mapped_ms;
    std::optional<Memory> memory;
    std::optional<double> idle_cpu_percent, idle_wakeups, idle_frames;
    std::map<std::string, double> idle_waking;  // at rest: each thread's name, its switches a second (those that woke)
    std::vector<double> clicks;
    bool click_changed = true;
    std::vector<double> presents;  // in motion: the intervals between presented frames (ms)
    bool moved = false;  // on a desktop: opened on another monitor, moved onto the one asked for
    std::string failure;
};

// The environment both sides' programs run in, and the PATH their builds see.
struct Environment {
    std::vector<std::pair<std::string, std::optional<std::string>>> run;  // the programs under test
    std::vector<std::pair<std::string, std::optional<std::string>>> omui;  // and omui's besides
    std::string competitor_path;  // cargo's, Flutter's, the system's
    fs::path work, sdk;
};

// `program args…` with the variables set; its output (stdout and stderr together), or why it
// failed.
std::expected<std::string, std::string>
step(const fs::path& root, std::string program, std::vector<std::string> args, const fs::path& cwd,
     std::vector<std::pair<std::string, std::string>> variables = {}) {
    std::string shown = program;
    for (const auto& arg : args)
        shown += " " + arg;
    std::cout << std::format("  $ {}\n", shown) << std::flush;
    auto ran = run::program(root, std::move(program), std::move(args),
                            {.cwd = cwd, .merged = true, .variables = std::move(variables)});
    if (!ran)
        return std::unexpected(ran.error());
    if (ran->status != 0)
        return std::unexpected(
            std::format("`{}` exited {}: {}", shown, ran->status,
                        ran->out.substr(ran->out.size() > 3000 ? ran->out.size() - 3000 : 0)));
    return ran->out;
}

bool is_elf(const fs::path& file) {
    const auto head = mcpplibs::os::fs::read(file);
    return head && head->size() > 4 &&
           head->starts_with("\x7f"
                             "ELF");
}

// The bytes of the tree's programs and libraries, each stripped (a copy: `strip --strip-all`); the
// archive's
// (`tar -czf`, gzip's default level), the same for every side.
std::expected<std::pair<std::uintmax_t, std::uintmax_t>, std::string>
sizes(const fs::path& root, const fs::path& tree, const fs::path& scratch) {
    std::error_code error;
    std::uintmax_t executable = 0;
    fs::create_directories(scratch, error);
    for (auto at = fs::recursive_directory_iterator(tree, error);
         at != fs::recursive_directory_iterator(); at.increment(error)) {
        if (error)
            break;
        if (!at->is_regular_file(error) || at->is_symlink(error) || !is_elf(at->path()))
            continue;
        const fs::path copy = scratch / "stripped";
        fs::copy_file(at->path(), copy, fs::copy_options::overwrite_existing, error);
        if (auto done = step(root, "strip", {"--strip-all", utf8(copy)}, scratch); !done)
            return std::unexpected(done.error());
        executable += fs::file_size(copy, error);
    }
    const fs::path archive = scratch / "package.tar.gz";
    if (auto done =
            step(root, "tar",
                 {"-C", utf8(tree.parent_path()), "-czf", utf8(archive), utf8(tree.filename())},
                 scratch);
        !done)
        return std::unexpected(done.error());
    return std::pair{executable, fs::file_size(archive, error)};
}

// --- building each side
// ------------------------------------------------------------------------------------------

std::expected<void, std::string> build_omui(const fs::path& root, Side& side,
                                            const Environment& environment) {
    const fs::path out = environment.work / "omui" / side.world / side.program;
    // The program packed as mcpp packs an application (its fonts beside it), in the side's world;
    // mcpp writes the directory into the package's own target/dist, copied out before the next
    // world's.
    const fs::path packed_at = root / "tests/bench/competitors/apps/target/dist";
    std::error_code error;
    fs::remove_all(packed_at, error);
    fs::remove_all(out, error);
    fs::create_directories(out, error);
    const auto packed = step(root, "mcpp",
                             {"pack", side.program, "-p", "omui.bench.competitors.apps",
                              "--profile", "dist", "--features", side.world, "--pkg-format", "dir"},
                             root);
    if (!packed)
        return std::unexpected(packed.error());
    fs::copy(packed_at, out / "package", fs::copy_options::recursive, error);
    if (error)
        return std::unexpected(
            std::format("mcpp pack's directory cannot be copied: {}", error.message()));
    fs::path packed_program;
    for (const auto& entry : fs::recursive_directory_iterator(out / "package", error))
        if (entry.is_regular_file(error) && entry.path().filename() == side.program &&
            entry.path().parent_path().filename() == "bin")
            packed_program = entry.path();
    if (packed_program.empty())
        return std::unexpected(
            std::format("mcpp pack wrote no bin/{} under {}", side.program, utf8(out)));
    side.tree = packed_program.parent_path().parent_path();
    // What runs: the same program as mcpp built it, beside the package's fonts. The package's own
    // asks for the C library mcpp's toolchain links (glibc 2.43 or later), which a system older
    // than that (Ubuntu 24.04: 2.39) does not have; the build's names the toolchain's own.
    const auto built = run::build(root, {"-p", "omui.bench.competitors.apps", "--profile", "dist",
                                         "--features", side.world});
    if (!built)
        return std::unexpected(built.error());
    const auto artifact = std::ranges::find_if(*built, [&](const run::Artifact& made) {
        return made.kind == "bin" && made.name == side.program;
    });
    if (artifact == built->end())
        return std::unexpected(std::format("mcpp build reported no {}", side.program));
    side.run_tree = out / "run";
    fs::create_directories(side.run_tree, error);
    side.executable = side.run_tree / side.program;
    fs::copy_file(artifact->path, side.executable, fs::copy_options::overwrite_existing, error);
    fs::copy(packed_program.parent_path() / "fonts", side.run_tree / "fonts",
             fs::copy_options::recursive, error);
    if (error)
        return std::unexpected(
            std::format("the program cannot be laid out to run: {}", error.message()));
    side.cwd = side.run_tree;
    return {};
}

std::expected<void, std::string> build_cargo(const fs::path& root, Side& side,
                                             const Environment& environment) {
    const fs::path target = environment.sdk / "slint-target";
    const auto built =
        step(root, "cargo",
             {"build", "--release", "--locked", "--bin", side.program, "--manifest-path",
              utf8(root / "tests/bench/competitors/apps/slint/Cargo.toml")},
             root, {{"PATH", environment.competitor_path}, {"CARGO_TARGET_DIR", utf8(target)}});
    if (!built)
        return std::unexpected(built.error());
    side.tree = environment.work / "slint" / side.program;
    std::error_code error;
    fs::remove_all(side.tree, error);
    fs::create_directories(side.tree, error);
    side.executable = side.tree / side.program;
    fs::copy_file(target / "release" / side.program, side.executable,
                  fs::copy_options::overwrite_existing, error);
    if (error)
        return std::unexpected(std::format("cargo built no {}: {}", side.program, error.message()));
    side.cwd = side.tree;
    return {};
}

// Flutter's SDK at the pin, fetched and checked once.
std::expected<fs::path, std::string> flutter_sdk(const fs::path& root, const json& pin,
                                                 const Environment& environment) {
    const fs::path sdk = environment.sdk / "flutter";
    if (fs::exists(sdk / "bin" / "flutter"))
        return sdk;
    const std::string archive = utf8(environment.sdk / "flutter.tar.xz");
    if (auto done =
            step(root, "curl", {"-fsSL", "-o", archive, pin.value("archive", "")}, environment.sdk);
        !done)
        return std::unexpected(done.error());
    const auto sum = step(root, "sha256sum", {archive}, environment.sdk);
    if (!sum)
        return std::unexpected(sum.error());
    if (!sum->starts_with(pin.value("sha256", "?")))
        return std::unexpected(std::format("Flutter's archive is not the pinned one: {}", *sum));
    if (auto done = step(root, "tar", {"-xJf", archive}, environment.sdk); !done)
        return std::unexpected(done.error());
    std::error_code error;
    fs::remove(archive, error);
    return sdk;
}

std::expected<void, std::string> build_flutter(const fs::path& root, Side& side,
                                               const Environment& environment, const json& pin,
                                               const Settings& settings) {
    const auto sdk = flutter_sdk(root, pin, environment);
    if (!sdk)
        return std::unexpected(sdk.error());
    const std::string flutter = utf8(*sdk / "bin" / "flutter");
    const std::vector<std::pair<std::string, std::string>>
        variables{{"PATH", utf8(*sdk / "bin") + ":" + environment.competitor_path},
                  {"PUB_CACHE", utf8(environment.sdk / "pub-cache")},
                  {"FLUTTER_SUPPRESS_ANALYTICS", "true"}};
    // The project: our sources, and the Linux runner the pinned SDK writes, its window 800 × 600 as
    // everyone's.
    const fs::path project = environment.work / "flutter" / side.app;
    std::error_code error;
    fs::remove_all(project, error);
    fs::create_directories(project, error);
    const fs::path sources = root / "tests/bench/competitors/apps/flutter";
    fs::copy(sources, project, fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             error);
    if (error)
        return std::unexpected(
            std::format("Flutter's sources cannot be copied: {}", error.message()));
    if (auto done = step(root, flutter, {"--disable-analytics"}, project, variables); !done)
        return std::unexpected(done.error());
    if (auto done = step(root, flutter,
                         {"create", "--platforms", "linux", "--project-name", "bench_apps", "--org",
                          "dev.omui.bench", "."},
                         project, variables);
        !done)
        return std::unexpected(done.error());
    const fs::path runner = project / "linux" / "runner" / "my_application.cc";
    auto text = mcpplibs::os::fs::read(runner);
    if (!text)
        return std::unexpected("flutter create wrote no linux/runner/my_application.cc");
    const std::string from = "gtk_window_set_default_size(window, 1280, 720);";
    const auto at = text->find(from);
    if (at == std::string::npos)
        return std::unexpected(
            "the Linux runner sets no 1280 × 720 window: the pinned SDK's template changed");
    text->replace(at, from.size(),
                  std::format("gtk_window_set_default_size(window, {}, {});",
                              side.window ? (*side.window)[0] : settings.width,
                              side.window ? (*side.window)[1] : settings.height));
    if (!(std::ofstream(runner, std::ios::binary) << *text))
        return std::unexpected("the Linux runner cannot be written");
    if (auto done = step(root, flutter, {"build", "linux", "--release", "-t", side.program},
                         project, variables);
        !done)
        return std::unexpected(done.error());
    side.tree = project / "build" / "linux" / "x64" / "release" / "bundle";
    side.executable = side.tree / "bench_apps";
    side.cwd = side.tree;
    if (!fs::exists(side.executable))
        return std::unexpected("flutter build linux wrote no bundle/bench_apps");
    return {};
}

// NSH's checkout at the pin, its command line and runner built (link-time optimised, stripped: as
// its report's).
std::expected<fs::path, std::string>
nsh_checkout(const fs::path& root, const json& pin, const Environment& environment,
             const std::vector<std::pair<std::string, std::string>>& variables) {
    const fs::path source = environment.sdk / "nsh-src";
    const std::string commit = pin.value("commit", "");
    if (!fs::exists(source / ".git")) {
        if (auto done = step(root, "git", {"clone", pin.value("repository", ""), utf8(source)},
                             environment.sdk);
            !done)
            return std::unexpected(done.error());
    }
    const auto head = step(root, "git", {"rev-parse", "HEAD"}, source);
    if (!head)
        return std::unexpected(head.error());
    if (!head->starts_with(commit)) {
        if (auto done = step(root, "git", {"fetch", "origin", commit}, source); !done)
            return std::unexpected(done.error());
        if (auto done = step(root, "git", {"checkout", "--detach", commit}, source); !done)
            return std::unexpected(done.error());
    }
    if (auto done = step(root, "cargo", {"build", "--release", "-p", "nsh-cli"}, source, variables);
        !done)
        return std::unexpected(done.error());
    return source;
}

std::expected<void, std::string> build_nsh(const fs::path& root, Side& side,
                                           const Environment& environment, const json& pin,
                                           const Settings& settings) {
    const std::vector<std::pair<std::string, std::string>> variables{
        {"PATH", environment.competitor_path},
        {"CARGO_TARGET_DIR", utf8(environment.sdk / "nsh-target")},
        {"CARGO_PROFILE_RELEASE_LTO", "fat"},
        {"CARGO_PROFILE_RELEASE_CODEGEN_UNITS", "1"},
        {"CARGO_PROFILE_RELEASE_STRIP", "true"},
        {"RUSTUP_TOOLCHAIN",
         "stable"},  // the checkout's toolchain file asks for every mobile target too
    };
    const auto source = nsh_checkout(root, pin, environment, variables);
    if (!source)
        return std::unexpected(source.error());
    const std::string nsh = utf8(environment.sdk / "nsh-target" / "release" / "nsh");
    fs::path project = environment.work / "nsh" / (side.series + "-" + side.app);
    std::error_code error;
    fs::remove_all(project, error);
    fs::create_directories(project.parent_path(), error);
    if (side.program.starts_with("template:")) {
        const std::string template_name = side.program.substr(9);
        std::vector<std::string> args{"init",        utf8(project), "--template",
                                      template_name, "--name",      "Hello"};
        if (template_name != "ts")
            args.insert(args.end(), {"--nsh-source", utf8(*source)});
        if (auto done = step(root, nsh, std::move(args), project.parent_path(), variables); !done)
            return std::unexpected(done.error());
    } else if (side.program.starts_with("example:")) {
        // Beside the example in the checkout: what it names by a relative path (a sibling's theme)
        // still resolves.
        project = *source / "examples" / (side.program.substr(8) + "-omui-bench");
        fs::remove_all(project, error);
        fs::copy(*source / "examples" / side.program.substr(8), project,
                 fs::copy_options::recursive, error);
        if (error)
            return std::unexpected(
                std::format("NSH's {} cannot be copied: {}", side.program, error.message()));
    } else if (side.program.starts_with("source:")) {
        // An app of ours written in NSH's TSX (tests/bench/competitors/apps/nsh/<name>): the same
        // workload as omui's.
        fs::copy(root / "tests/bench/competitors/apps/nsh" / side.program.substr(7), project,
                 fs::copy_options::recursive, error);
        if (error)
            return std::unexpected(
                std::format("{} cannot be copied: {}", side.program, error.message()));
    } else {
        return std::unexpected(
            std::format("{}: an NSH side is template:<name>, example:<name> or source:<name>",
                        side.program));
    }
    // Its window 800 × 600 from the first frame, as everyone's (nsh.json's `window.defaultSize`).
    auto manifest =
        json::parse(mcpplibs::os::fs::read(project / "nsh.json").value_or("{}"), nullptr, false);
    if (manifest.is_discarded())
        return std::unexpected("NSH's nsh.json is not JSON");
    manifest["window"]["defaultSize"] = {{"width",
                                          side.window ? (*side.window)[0] : settings.width},
                                         {"height",
                                          side.window ? (*side.window)[1] : settings.height}};
    if (!(std::ofstream(project / "nsh.json", std::ios::binary) << manifest.dump(2) << '\n'))
        return std::unexpected("nsh.json cannot be written");
    if (auto done = step(root, nsh, {"package", "--target", "linux"}, project, variables); !done)
        return std::unexpected(done.error());
    const fs::path out = project / ".nsh" / "out";
    for (const auto& entry : fs::directory_iterator(out, error))
        if (entry.is_directory(error))
            side.tree = entry.path();
    if (side.tree.empty())
        return std::unexpected(std::format("nsh package wrote nothing under {}", utf8(out)));
    side.executable = side.tree / side.tree.filename();
    side.cwd = side.tree;
    if (!fs::exists(side.executable))
        return std::unexpected(std::format("nsh package wrote no {}", utf8(side.executable)));
    return {};
}

// --- the display, and what is read of a process
// ------------------------------------------------------------------

std::vector<std::string> words(std::string_view line) {
    std::vector<std::string> out;
    for (auto part : std::views::split(line, ' '))
        if (!part.empty())
            out.emplace_back(part.begin(), part.end());
    return out;
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    for (auto part : std::views::split(text, '\n'))
        out.emplace_back(part.begin(), part.end());
    return out;
}

// A process's memory, its children's added (`ps --ppid`, `pmap -XX`).
std::optional<Memory> memory_now(const fs::path& root, std::uint64_t pid) {
    std::vector<std::uint64_t> all{pid};
    if (const auto children =
            run::output(root, "ps", {"-o", "pid=", "--ppid", std::to_string(pid)}))
        for (const auto line : lines(*children))
            for (const auto& word : words(line)) {
                std::uint64_t child = 0;
                if (std::from_chars(word.data(), word.data() + word.size(), child).ec ==
                    std::errc{})
                    all.push_back(child);
            }
    Memory total;
    for (const std::uint64_t one : all) {
        const auto out = run::output(root, "pmap", {"-XX", std::to_string(one)});
        if (!out)
            return std::nullopt;
        const auto read = memory_of(*out);
        if (!read)
            return std::nullopt;
        total.rss += read->rss, total.pss += read->pss, total.uss += read->uss,
            total.private_dirty += read->private_dirty, total.swap += read->swap;
    }
    return total;
}

// The files a process maps (`pmap -p`), and the graphics it reached.
std::pair<std::vector<fs::path>, std::string> mapped_files(const fs::path& root,
                                                           std::uint64_t pid) {
    std::set<std::string> paths;
    if (const auto out = run::output(root, "pmap", {"-p", std::to_string(pid)}))
        for (const auto line : lines(*out)) {
            // A mapping's line starts with its address; the first line is "<pid>:   <command line>".
            const auto slash = line.find(" /");
            if (slash == std::string_view::npos || line.contains("(deleted)") ||
                line.find(':') < slash)
                continue;
            paths.emplace(line.substr(slash + 1));
        }
    std::string driver;
    const auto has = [&](std::string_view part) {
        return std::ranges::any_of(paths,
                                   [&](const std::string& path) { return path.contains(part); });
    };
    if (has("libnvidia-glcore") || has("libnvidia-eglcore") || has("libnvidia-gpucomp") ||
        has("libGLX_nvidia") || has("libEGL_nvidia"))
        driver += "nvidia ";
    if (has("libvulkan_lvp"))
        driver += "lavapipe ";
    if (has("libgallium") || has("swrast_dri") || has("libLLVM"))
        driver += "mesa-llvmpipe ";
    if (driver.empty())
        driver = "none ";
    driver.pop_back();
    std::vector<fs::path> files;
    for (const auto& path : paths)
        files.emplace_back(path);
    return {std::move(files), driver};
}

// The page cache dropped for each file (GNU dd's nocache: posix_fadvise DONTNEED), where no other
// process maps it.
void evict(const fs::path& root, const std::vector<fs::path>& files) {
    std::error_code error;
    for (const auto& file : files)
        if (fs::is_regular_file(file, error))  // a device a driver maps (/dev/nvidia0) has no cache
            (void)run::program(root, "dd",
                               {"if=" + utf8(file), "iflag=nocache", "count=0", "status=none"});
}

std::vector<fs::path> tree_files(const fs::path& tree) {
    std::vector<fs::path> out;
    std::error_code error;
    for (auto at = fs::recursive_directory_iterator(tree, error);
         at != fs::recursive_directory_iterator(); at.increment(error))
        if (!error && at->is_regular_file(error))
            out.push_back(at->path());
    return out;
}

// A launch watched on the display: until its window's picture stops changing.
class Watch {
public:
    // `live`: it draws by itself, so its picture never settles — the first picture with something
    // drawn is its start.
    Watch(x11::Display& display, const Settings& settings, bool live = false)
        : display_(display), settings_(settings), live_(live) {}

    // The process whose window it is (a desktop's display: the window that says it is its,
    // _NET_WM_PID, and no other).
    void expect(std::uint64_t process) {
        process_ = process;
    }

    // Events until the window has a picture and nothing was drawn for `quiet` since this call or
    // the last draw (a live window: until it has a picture), or the deadline.
    std::expected<void, std::string> settle(Clock::time_point deadline, milliseconds quiet,
                                            process::process* program) {
        const auto called = Clock::now();  // quiet from here at least: what was just asked of it
                                           // may not be drawn yet
        while (Clock::now() < deadline) {
            auto event = display_.next(dirty_ ? milliseconds{0} : milliseconds{10});
            if (!event)
                return std::unexpected(event.error());
            if (*event) {
                take(**event);
                continue;
            }
            if (dirty_ && main_) {
                auto image = grab();
                if (!image && settings_.desktop) {
                    // Mapped inside a window manager's frame not shown yet: not viewable, so not
                    // readable (BadMatch) — read again a moment later.
                    std::this_thread::sleep_for(milliseconds{1});
                    continue;
                }
                if (!image)
                    return std::unexpected(image.error());
                batches_.push_back(
                    {last_damage_, x11::hash(image->pixels), x11::uniform(image->pixels)});
                dirty_ = false;
            }
            const bool painted =
                std::ranges::any_of(batches_, [](const Batch& batch) { return !batch.uniform; });
            if (painted && (live_ || Clock::now() - std::max(last_damage_, called) >= quiet))
                return {};
            if (program) {
                if (auto ended = program->try_wait(); ended && *ended)
                    return std::unexpected(
                        std::format("the program ended before its window settled (status {})",
                                    (*ended)->code));
            }
        }
        return std::unexpected(main_ ? "its window did not settle in time"
                                     : "no window was mapped in time");
    }

    // Since the launch: the first frame equal to the settled picture, the first non-uniform one,
    // the window's map.
    struct Frames {
        std::optional<Clock::time_point> complete, painted, mapped;
    };
    std::expected<Frames, std::string> frames() {
        auto image = grab();
        if (!image)
            return std::unexpected(image.error());
        final_ = x11::hash(image->pixels);
        Frames out;
        out.mapped = mapped_at_;
        for (const auto& batch : batches_) {
            if (!out.painted && !batch.uniform)
                out.painted = batch.at;
            if (!out.complete && batch.hash == final_)
                out.complete = batch.at;
        }
        if (live_)
            out.complete = out.painted;
        return out;
    }

    // Presents counted while waiting for `span` (the program's frames at rest). A present is a
    // run of draws on the window that do not overlap one another, each within half a refresh of
    // the display after the last — a frame's rectangles, or the strips one image is sent in where
    // it is not shared (an X server without MIT-SHM takes a big image as many requests) — and it
    // is presented when its last draw is: `times` gets that. With `wheel`, a notch down every
    // 1/120 s, turning back every 400 notches.
    std::expected<int, std::string>
    count(milliseconds span, std::vector<Clock::time_point>* times = nullptr, bool wheel = false) {
        const auto until = Clock::now() + span;
        int presents = 0, notches = 0;
        std::vector<x11::Rect> current;  // what the present being drawn has covered
        Clock::time_point last{};
        const auto close = [&] {
            if (current.empty())
                return;
            ++presents;
            if (times)
                times->push_back(last);
            current.clear();
        };
        auto next_notch = Clock::now();
        const auto step =
            std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 120.0));
        while (Clock::now() < until) {
            if (wheel && Clock::now() >= next_notch) {
                // A desktop's pointer may be moved away by its person: the wheel turns only over
                // the program's own window.
                if (auto under = pointer_on_window(); !under || !*under)
                    return std::unexpected(
                        under ? std::string("the window was no longer under the pointer: the "
                                            "wheel stopped")
                              : under.error());
                if (auto turned = display_.wheel(notches++ / 400 % 2 == 0); !turned)
                    return std::unexpected(turned.error());
                next_notch += step;
            }
            auto wait = std::chrono::duration_cast<milliseconds>(until - Clock::now());
            if (wheel)
                wait = std::min(wait,
                                std::max(milliseconds{0}, std::chrono::duration_cast<milliseconds>(
                                                              next_notch - Clock::now())));
            auto event = display_.next(wait);
            if (!event)
                return std::unexpected(event.error());
            if (*event) {
                if ((*event)->kind == x11::Event::Kind::damaged && overlaps(**event)) {
                    const x11::Rect& drawn = (*event)->area;
                    const bool again = std::ranges::any_of(current, [&](const x11::Rect& done) {
                        return drawn.x < done.x + done.width && done.x < drawn.x + drawn.width &&
                               drawn.y < done.y + done.height && done.y < drawn.y + drawn.height;
                    });
                    if (again || (*event)->at - last >= settings_.frame_gap)
                        close();
                    current.push_back(drawn);
                    last = (*event)->at;
                }
                take(**event);
            }
        }
        close();
        return presents;
    }

    // The next draw on the window after `since`, waiting at most `wait`.
    std::expected<std::optional<Clock::time_point>, std::string> next_draw(milliseconds wait) {
        const auto until = Clock::now() + wait;
        while (Clock::now() < until) {
            auto event =
                display_.next(std::chrono::duration_cast<milliseconds>(until - Clock::now()));
            if (!event)
                return std::unexpected(event.error());
            if (!*event)
                continue;
            take(**event);
            if ((*event)->kind == x11::Event::Kind::damaged && overlaps(**event))
                return std::optional{(*event)->at};
        }
        return std::optional<Clock::time_point>{};
    }

    // The window's picture, or a part of it (in the window), as a hash.
    std::expected<std::uint64_t, std::string>
    picture(std::optional<x11::Rect> part = std::nullopt) {
        auto image = grab(part);
        if (!image)
            return std::unexpected(image.error());
        return x11::hash(image->pixels);
    }

    // The first draw after which `part` of the window no longer shows `was`, waiting at most
    // `wait`: the time of the last draw the server had carried out when it answered (the change
    // came with it or before).
    std::expected<std::optional<Clock::time_point>, std::string>
    changed(std::optional<x11::Rect> part, std::uint64_t was, milliseconds wait) {
        const auto until = Clock::now() + wait;
        while (Clock::now() < until) {
            auto drawn = next_draw(std::chrono::duration_cast<milliseconds>(until - Clock::now()));
            if (!drawn)
                return std::unexpected(drawn.error());
            if (!*drawn)
                return std::optional<Clock::time_point>{};
            Clock::time_point last = **drawn;
            while (true) {
                auto more = display_.next(milliseconds{0});
                if (!more)
                    return std::unexpected(more.error());
                if (!*more)
                    break;
                take(**more);
                if ((*more)->kind == x11::Event::Kind::damaged && overlaps(**more))
                    last = (*more)->at;
            }
            auto now = picture(part);
            if (!now)
                return std::unexpected(now.error());
            if (*now != was)
                return std::optional{last};
        }
        return std::optional<Clock::time_point>{};
    }

    // The window's picture as a PNG (what a person checks the click's place and the drawing by).
    std::expected<void, std::string> save(const fs::path& file) {
        const x11::Rect where = area();
        auto image = grab();
        if (!image)
            return std::unexpected(image.error());
        mlibs::image::Image picture{.width = where.width, .height = where.height};
        picture.rgba.resize(image->pixels.size() * 4);
        for (std::size_t at = 0; at < image->pixels.size(); ++at) {
            const std::uint32_t pixel = image->pixels[at];
            picture.rgba[at * 4 + 0] = static_cast<std::uint8_t>(pixel >> 16);
            picture.rgba[at * 4 + 1] = static_cast<std::uint8_t>(pixel >> 8);
            picture.rgba[at * 4 + 2] = static_cast<std::uint8_t>(pixel);
            picture.rgba[at * 4 + 3] = 255;
        }
        return mlibs::image::save_png(file, mlibs::image::view(picture));
    }

    [[nodiscard]] std::uint32_t window() const noexcept {
        return main_;
    }
    // What is read of the window: on the harness's own server its place on the screen (the
    // screen's pixels there), on a desktop the window itself from its corner.
    [[nodiscard]] x11::Rect area() const noexcept {
        if (settings_.desktop)
            return {0, 0, std::max(rect_.width, 1), std::max(rect_.height, 1)};
        x11::Rect out = rect_;
        const x11::Rect screen = display_.screen();
        out.x = std::clamp(out.x, 0, screen.width - 1);
        out.y = std::clamp(out.y, 0, screen.height - 1);
        out.width = std::clamp(out.width, 1, screen.width - out.x);
        out.height = std::clamp(out.height, 1, screen.height - out.y);
        return out;
    }
    // The window's place on the screen (where the pointer goes to press in it).
    [[nodiscard]] std::expected<x11::Rect, std::string> on_screen() {
        if (!settings_.desktop)
            return area();
        auto corner = display_.origin(main_);
        if (!corner)
            return std::unexpected(corner.error());
        return x11::Rect{(*corner)[0], (*corner)[1], rect_.width, rect_.height};
    }
    // Whether the window under the pointer is the program's (on a desktop, input pressed anywhere
    // else would reach another's window).
    [[nodiscard]] std::expected<bool, std::string> pointer_on_window() {
        if (!settings_.desktop)
            return main_ != 0;
        if (!main_)
            return false;
        auto top = display_.top_level(main_);
        if (!top)
            return std::unexpected(top.error());
        auto pointer = display_.where_pointer();
        if (!pointer)
            return std::unexpected(pointer.error());
        return pointer->over == *top;
    }
    void forget_frames() {
        batches_.clear();
    }
private:
    struct Batch {
        Clock::time_point at;
        std::uint64_t hash = 0;
        bool uniform = true;
    };

    // The window's pixels (`part` of it, in the window).
    std::expected<x11::Display::Image, std::string> grab(std::optional<x11::Rect> part = {}) {
        x11::Rect where = area();
        if (part)
            where = {where.x + part->x, where.y + part->y, part->width, part->height};
        return settings_.desktop ? display_.image(where, main_) : display_.image(where);
    }

    // A drawing on the window: on a desktop one its own damage reports, on the harness's server
    // one on the screen where the window is.
    bool overlaps(const x11::Event& drawn) const noexcept {
        if (settings_.desktop)
            return main_ && drawn.window == main_;
        const x11::Rect& area = drawn.area;
        return main_ && area.x < rect_.x + rect_.width && rect_.x < area.x + area.width &&
               area.y < rect_.y + rect_.height && rect_.y < area.y + area.height;
    }

    // On a desktop: whether `window` is the program's own (it says so), and if so made the one
    // watched — its own damage reported from now on.
    bool adopt(std::uint32_t window, Clock::time_point at) {
        if (!process_ || main_ == window)
            return false;
        const auto owner = display_.process_of(window);
        if (!owner || *owner != process_)
            return false;
        const auto exact = display_.geometry(window);
        if (!exact || exact->width < 64 || exact->height < 64 ||
            exact->width * exact->height <= rect_.width * rect_.height)
            return false;
        (void)display_.follow(window);
        if (!display_.damage(window))
            return false;
        main_ = window;
        rect_ = {0, 0, exact->width, exact->height};
        if (!mapped_at_)
            mapped_at_ = at;
        // What it drew before its damage was asked for is dated now, when it is known to be there
        // (never earlier than it was: a start is not flattered).
        last_damage_ = std::max(last_damage_, Clock::now());
        dirty_ = true;
        return true;
    }

    void take(const x11::Event& event) {
        using Kind = x11::Event::Kind;
        const std::uint32_t root = display_.root();
        switch (event.kind) {
            case Kind::created:
                if (event.parent == root) {
                    windows_[event.window] = event.area;
                    // Its own map is sent to it: a window manager reparents it into a frame first.
                    if (settings_.desktop)
                        (void)display_.follow(event.window);
                }
                break;
            case Kind::configured:
                if (windows_.contains(event.window))
                    windows_[event.window] = event.area;
                if (event.window == main_)
                    rect_ = settings_.desktop ? x11::Rect{0, 0, event.area.width, event.area.height}
                                              : event.area;
                break;
            case Kind::mapped:
                {
                    if (settings_.desktop) {
                        // The program's window, or a frame around it (its map came before ours was
                        // followed).
                        if (adopt(event.window, event.at) || event.parent != root)
                            break;
                        if (const auto inside = display_.children(event.window))
                            for (const std::uint32_t child : *inside)
                                if (adopt(child, event.at))
                                    break;
                        break;
                    }
                    if (!windows_.contains(event.window))
                        break;
                    x11::Rect place = windows_[event.window];
                    if (auto exact = display_.geometry(event.window))
                        place = *exact;
                    if (place.width >= 64 && place.height >= 64 &&
                        place.width * place.height > rect_.width * rect_.height) {
                        main_ = event.window;
                        rect_ = place;
                        windows_[event.window] = place;
                        if (!mapped_at_)
                            mapped_at_ = event.at;
                        dirty_ = true;
                    }
                    break;
                }
            case Kind::unmapped:
            case Kind::destroyed:
                if (event.window == main_)
                    main_ = 0, rect_ = {};
                break;
            case Kind::damaged:
                if (settings_.desktop && event.window != main_)
                    break;
                last_damage_ = event.at;
                dirty_ = true;
                break;
            default:
                break;
        }
    }

    x11::Display& display_;
    const Settings& settings_;
    bool live_ = false;
    std::uint64_t process_ = 0;
    std::map<std::uint32_t, x11::Rect> windows_;
    std::uint32_t main_ = 0;
    x11::Rect rect_;
    std::optional<Clock::time_point> mapped_at_;
    Clock::time_point last_damage_{};
    bool dirty_ = false;
    std::vector<Batch> batches_;
    std::uint64_t final_ = 0;
};

// One launch of a side: to its first settled frame; with `whole`, then rest, memory, the processor
// and the frames at rest, and the clicks.
Launch launch(const fs::path& root, x11::Display& display, Side& side, const Settings& settings,
              const Environment& environment, bool cold, bool whole, const fs::path& shot = {}) {
    Launch out;
    // The pointer out of every window (on a desktop: on the monitor the window is to open on);
    // nothing left over from the last program.
    (void)display.pointer(settings.park[0], settings.park[1]);
    while (true) {
        auto pending = display.next(milliseconds{0});
        if (!pending || !*pending)
            break;
    }
    if (cold) {
        std::vector<fs::path> files = tree_files(side.run_tree.empty() ? side.tree : side.run_tree);
        files.insert(files.end(), side.mapped.begin(), side.mapped.end());
        evict(root, files);
    }
    auto variables = environment.run;
    if (side.build == "omui")
        variables.insert(variables.end(), environment.omui.begin(), environment.omui.end());
    variables.insert(variables.end(), side.env.begin(), side.env.end());
    std::vector<std::string> args = side.args;
    if (whole && side.build == "omui" && !settings.trace.empty())
        args.insert(args.end(), {"--profile", utf8(settings.trace / std::format("{}-{}.trace.json",
                                                                               side.series, side.app))});
    process::spawn_options options{.program = side.executable,
                                   .args = std::move(args),
                                   .cwd = side.cwd,
                                   .env = std::move(variables)};
    options.out = process::stdio::null;
    // omui's own complaints into the run's log (a launch that ends early says why); the others' are
    // theirs.
    options.err = side.build == "omui" ? process::stdio::inherit : process::stdio::null;
    options.in = process::stdio::null;
    options.own_unit = true;
    Watch watch(display, settings, side.motion == "live");
    const auto started = Clock::now();
    auto program = process::process::spawn(options);
    if (!program) {
        out.failure =
            std::format("{} did not start: {}", utf8(side.executable), program.error().message());
        return out;
    }
    watch.expect(program->id());
    const auto end = [&] {
        program->terminate();
        if (auto ended = program->wait_for(std::chrono::seconds{3}); !ended || !*ended)
            program->kill();
        (void)program->wait_for(std::chrono::seconds{2});
    };
    if (auto settled = watch.settle(started + settings.timeout, settings.quiet, &*program);
        !settled) {
        out.failure = settled.error();
        end();
        return out;
    }
    const auto frames = watch.frames();
    if (!frames) {
        out.failure = frames.error();
        end();
        return out;
    }
    if (frames->complete)
        out.start_ms = ms_between(started, *frames->complete);
    if (frames->painted)
        out.paint_ms = ms_between(started, *frames->painted);
    if (frames->mapped)
        out.mapped_ms = ms_between(started, *frames->mapped);
    if (!whole) {
        end();
        return out;
    }
    const std::uint64_t pid = program->id();
    if (!shot.empty())
        (void)watch.save(shot);
    // On a desktop, on the monitor asked for (a window manager may have opened it on another) and
    // above its person's windows for the rest of the case (where they overlap the monitor, the
    // clicks and the wheel would not reach it); gone with it at the end.
    if (settings.desktop) {
        if (const auto where = watch.on_screen();
            where && (where->x < settings.monitor.x || where->y < settings.monitor.y ||
                      where->x >= settings.monitor.x + settings.monitor.width ||
                      where->y >= settings.monitor.y + settings.monitor.height)) {
            (void)display.move(watch.window(), settings.monitor.x + 60, settings.monitor.y + 60);
            (void)watch.settle(Clock::now() + std::chrono::seconds{5}, settings.quiet, &*program);
            out.moved = true;
        }
        (void)display.keep_above(watch.window());
    }
    // The same size for everyone (most open at it already), then rest.
    const x11::Rect placed = watch.area();
    const int width = side.window ? (*side.window)[0] : settings.width,
              height = side.window ? (*side.window)[1] : settings.height;
    if (placed.width != width || placed.height != height) {
        (void)display.resize(watch.window(), width, height);
        (void)watch.settle(Clock::now() + settings.timeout, settings.quiet, &*program);
    }
    const auto rested = (frames->complete ? *frames->complete : Clock::now()) + settings.rest;
    if (auto waited = watch.count(std::chrono::duration_cast<milliseconds>(
            std::max(rested - Clock::now(), Clock::duration::zero())));
        !waited) {
        out.failure = waited.error();
        end();
        return out;
    }
    out.memory = memory_now(root, pid);
    // At rest: the processor (mcpplibs.os), the switches a second (pidstat), the frames drawn.
    const auto before = process::describe(pid);
    std::optional<process::process> switches;
    if (const auto pidstat = mcpplibs::os::env::find_executable("pidstat")) {
        process::spawn_options watching{.program = *pidstat,
                                        .args = {"-w", "-t", "-p", std::to_string(pid),
                                                 std::to_string(settings.idle.count() / 1000), "1"},
                                        .env = {{"LC_ALL", "C"}}};
        watching.err = process::stdio::null;
        if (auto started_watch = process::process::spawn(watching))
            switches = std::move(*started_watch);
    }
    if (side.motion == "scroll") {
        if (const auto where = watch.on_screen())
            (void)display.pointer(where->x + where->width / 2, where->y + where->height / 2);
    }
    std::vector<Clock::time_point> presented;
    const auto idle_from = Clock::now();
    const auto drawn = watch.count(settings.idle, &presented, side.motion == "scroll");
    const double idle_seconds = ms_between(idle_from, Clock::now()) / 1000.0;
    for (std::size_t at = 1; at < presented.size(); ++at)
        out.presents.push_back(ms_between(presented[at - 1], presented[at]));
    if (side.motion == "scroll")
        (void)display.pointer(settings.park[0], settings.park[1]);
    if (!drawn) {  // a part of the span is no measurement of it
        out.failure = drawn.error();
        out.presents.clear();
        presented.clear();
    }
    if (!side.motion.empty() && drawn)
        out.idle_frames = static_cast<double>(presented.size()) / idle_seconds;
    const auto after = process::describe(pid);
    if (before && after)
        out.idle_cpu_percent =
            static_cast<double>(after->cpu_ns - before->cpu_ns) / 1e9 / idle_seconds * 100.0;
    if (drawn && side.motion.empty())
        out.idle_frames = *drawn / idle_seconds;
    if (switches) {
        std::string said;
        while (auto chunk = switches->read_output()) {
            if (chunk->empty())
                break;
            said += *chunk;
        }
        (void)switches->wait();
        out.idle_wakeups = switches_of(said);
        out.idle_waking = waking_of(said);
    }
    // The clicks: pointer onto the button, pressed, let go; from letting go to the next frame on
    // the window.
    if (side.click) {
        const auto where = watch.on_screen();
        for (int click = 0; where && click < settings.clicks; ++click) {
            (void)display.pointer(where->x + (*side.click)[0], where->y + (*side.click)[1]);
            (void)watch.settle(Clock::now() + std::chrono::seconds{5}, milliseconds{300},
                               &*program);
            // On a desktop, pressed only where the program's window is under the pointer.
            if (const auto under = watch.pointer_on_window(); !under || !*under) {
                out.click_changed = false;
                out.failure = "the window was not under the pointer: no click pressed";
                break;
            }
            (void)display.button(true);
            (void)watch.settle(Clock::now() + std::chrono::seconds{5}, milliseconds{300},
                               &*program);
            // What the click changes (the count's label), or the whole window where none is named.
            const auto held = watch.picture(side.label);
            if (!held)
                break;
            const auto let_go = Clock::now();
            (void)display.button(false);
            const auto shown = watch.changed(side.label, *held, std::chrono::seconds{2});
            if (shown && *shown)
                out.clicks.push_back(ms_between(let_go, **shown));
            else
                out.click_changed = false;
            (void)watch.settle(Clock::now() + std::chrono::seconds{5}, milliseconds{300},
                               &*program);
        }
        (void)display.pointer(settings.park[0], settings.park[1]);
    }
    auto [files, driver] = mapped_files(root, pid);
    side.mapped = std::move(files);
    side.driver = std::move(driver);
    end();
    return out;
}

double median_of(std::vector<double> values) {
    return lead::median(std::move(values));
}

double percentile(std::vector<double> values, double share) {
    if (values.empty())
        return 0;
    std::ranges::sort(values);
    const double at = share * static_cast<double>(values.size() - 1);
    const auto low = static_cast<std::size_t>(at);
    const std::size_t high = std::min(low + 1, values.size() - 1);
    return values[low] + (values[high] - values[low]) * (at - static_cast<double>(low));
}

std::optional<int> number(const std::optional<std::string>& text) {
    if (!text)
        return std::nullopt;
    int value = 0;
    if (std::from_chars(text->data(), text->data() + text->size(), value).ec != std::errc{})
        return std::nullopt;
    return value;
}

// What draws on this machine, for the record: the NVIDIA GPU and its driver as nvidia-smi says
// them ("" where it says nothing).
std::string graphics_here(const fs::path& root) {
    const auto said = run::output(root, "nvidia-smi",
                                  {"--query-gpu=name,driver_version", "--format=csv,noheader"});
    if (!said || said->empty())
        return {};
    const std::string first(lines(*said).front());
    const auto comma = first.find(',');
    if (comma == std::string::npos)
        return {};
    std::string release = first.substr(comma + 1);
    release.erase(0, release.find_first_not_of(' '));
    return std::format("; GL and Vulkan to {} (NVIDIA's driver {})", first.substr(0, comma), release);
}

// The processor's power preference (cpufreq's energy_performance_preference), for the record.
std::string power_preference() {
    std::string said = mcpplibs::os::fs::read(
                           "/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference")
                           .value_or("unknown");
    while (!said.empty() && std::isspace(static_cast<unsigned char>(said.back())))
        said.pop_back();
    return said;
}

// The machine's one-minute load average (other work beside the run, for the record), as uptime
// says it ("load average: 1.33, 2.29, 1.76").
double load_average(const fs::path& root) {
    const std::string said = run::output(root, "uptime", {}).value_or("");
    const auto at = said.find("average:");
    double value = 0;
    if (at != std::string::npos) {
        const std::string_view rest = std::string_view(said).substr(at + 8);
        const auto start = rest.find_first_not_of(' ');
        if (start != std::string_view::npos)
            (void)std::from_chars(rest.data() + start, rest.data() + rest.size(), value);
    }
    return value;
}

// A fonts.conf where the machine has none (a container's): the system's fonts and fontconfig's own
// rules, a cache of the harness's, built once before any program reads it.
std::optional<fs::path> fontconfig(const fs::path& root, const fs::path& work) {
    if (fs::exists("/etc/fonts/fonts.conf"))
        return std::nullopt;
    const fs::path dir = work / "fontconfig";
    std::error_code error;
    fs::create_directories(dir / "cache", error);
    std::string text = "<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM "
                       "\"urn:fontconfig:fonts.dtd\">\n<fontconfig>\n"
                       "  <dir>/usr/share/fonts</dir>\n  <dir>/usr/local/share/fonts</dir>\n";
    text += std::format("  <cachedir>{}</cachedir>\n", utf8(dir / "cache"));
    for (const std::string_view rule :
         {"10-hinting-slight", "10-scale-bitmap-fonts", "20-unhint-small-vera", "30-metric-aliases",
          "40-nonlatin", "45-generic", "45-latin", "49-sansserif", "60-generic", "60-latin",
          "65-fonts-persian", "65-nonlatin", "69-unifont", "80-delicious", "90-synthetic"})
        text += std::
            format("  <include "
                   "ignore_missing=\"yes\">/usr/share/fontconfig/conf.avail/{}.conf</include>\n",
                   rule);
    text += "</fontconfig>\n";
    const fs::path file = dir / "fonts.conf";
    if (!(std::ofstream(file, std::ios::binary) << text))
        return std::nullopt;
    (void)run::program(root, "fc-cache", {}, {.variables = {{"FONTCONFIG_FILE", utf8(file)}}});
    return file;
}

}  // namespace

std::optional<Memory> memory_of(std::string_view pmap_output) {
    std::vector<std::string> header, totals;
    for (const auto line : lines(pmap_output)) {
        const auto split = words(line);
        if (split.size() > 2 && split[0] == "Address" && split[1] == "Perm")
            header = split;
        if (!split.empty() && split.back() == "KB")
            totals = split;
    }
    const auto first = std::ranges::find(header, "Size");
    if (first == header.end() || totals.empty())
        return std::nullopt;
    std::map<std::string, std::uint64_t> value;
    std::size_t at = 0;
    for (auto name = first; name != header.end() && at + 1 < totals.size(); ++name) {
        if (*name == "VmFlags" || *name == "Mapping")
            continue;
        std::uint64_t kib = 0;
        if (std::from_chars(totals[at].data(), totals[at].data() + totals[at].size(), kib).ec !=
            std::errc{})
            return std::nullopt;
        value[*name] = kib * 1024;
        ++at;
    }
    if (!value.contains("Rss") || !value.contains("Pss") || !value.contains("Private_Clean") ||
        !value.contains("Private_Dirty"))
        return std::nullopt;
    return Memory{.rss = value["Rss"],
                  .pss = value["Pss"],
                  .uss = value["Private_Clean"] + value["Private_Dirty"],
                  .private_dirty = value["Private_Dirty"],
                  .swap = value.contains("Swap") ? value["Swap"] : 0};
}

// Which threads woke at rest, by name (pidstat's Command, "|__" dropped): what an idle process's switches are.
std::map<std::string, double> waking_of(std::string_view pidstat_output) {
    std::map<std::string, double> waking;
    for (const auto line : lines(pidstat_output)) {
        const auto split = words(line);
        if (split.size() < 7 || split[0] != "Average:" || split[3] == "-" || split[3] == "TID") continue;
        double voluntary = 0, involuntary = 0;
        if (std::from_chars(split[4].data(), split[4].data() + split[4].size(), voluntary).ec != std::errc{} ||
            std::from_chars(split[5].data(), split[5].data() + split[5].size(), involuntary).ec != std::errc{})
            continue;
        std::string name(split[6]);
        if (name.starts_with("|__")) name.erase(0, 3);
        if (voluntary + involuntary > 0) waking[name] += voluntary + involuntary;
    }
    return waking;
}

std::optional<double> switches_of(std::string_view pidstat_output) {
    double total = 0;
    bool any = false;
    for (const auto line : lines(pidstat_output)) {
        const auto split = words(line);
        // Average:  UID  TGID  TID  cswch/s  nvcswch/s  Command: a thread's line has its TID (the
        // process's has "-").
        if (split.size() < 7 || split[0] != "Average:" || split[3] == "-" || split[3] == "TID")
            continue;
        double voluntary = 0, involuntary = 0;
        if (std::from_chars(split[4].data(), split[4].data() + split[4].size(), voluntary).ec !=
            std::errc{})
            continue;
        if (std::from_chars(split[5].data(), split[5].data() + split[5].size(), involuntary).ec !=
            std::errc{})
            continue;
        total += voluntary + involuntary;
        any = true;
    }
    if (!any)
        return std::nullopt;
    return total;
}

mcpplibs::cmdline::App apps_command_line() {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Option;
    return App("apps")
        .description("Whole applications side by side (C1, N2): omui's in each world, Slint's, "
                     "Flutter's and NSH's built at their pins, each in a window "
                     "of an X server of its own, measured from outside the process; rounds "
                     "alternating, judged into the lead ledger")
        .option(Option("rounds").takes_value().value_name("N").help("how many (default: 10)"))
        .option(Option("only").takes_value().value_name("SIDES").help(
            "series or series:app, comma-separated (default: every side)"))
        .option(Option("sdk").takes_value().value_name("DIR").help(
            "where their SDKs and builds go (default: target/bench-apps/sdk)"))
        .option(Option("xvfb").takes_value().value_name("PATH").help(
            "the X server (default: Xvfb on PATH)"))
        .option(Option("display").takes_value().value_name("N").help(
            "its display number (default: 97; with --screen, $DISPLAY's)"))
        .option(Option("screen").takes_value().value_name("MONITOR").help(
            "on this machine's own display instead (RM1: its X server, window manager, "
            "compositor and GPU), every window opened on the monitor named by RANDR (e.g. "
            "HDMI-0); only each program's own window is watched and read"))
        .option(Option("note").takes_value().value_name("TEXT").help(
            "said on every record of the run beside the display: what the run cannot see of the "
            "machine (e.g. a sandbox's X server without MIT-SHM)"))
        .option(Option("build-only").help("build and size every side; launch none"))
        .option(Option("no-build")
                    .help("measure what the last build made (target/bench-apps/built.json)"))
        .option(Option("dry-run").help("print the verdicts; write the samples, not the ledger"))
        .option(Option("trace").takes_value().value_name("DIR").help(
            "each omui side's measured run records its trace into DIR (Chrome / Perfetto JSON)"));
}

int apps(const fs::path& where, const mcpplibs::cmdline::ParsedArgs& given) {
    std::error_code error;
    const fs::path root =
        fs::absolute(where, error).lexically_normal();  // the steps run in other directories
    const auto failed = [](std::string_view why) {
        std::cerr << "omui-devtools bench apps: " << why << '\n';
        return 2;
    };
    const auto catalogue =
        json::parse(mcpplibs::os::fs::read(root / catalogue_path).value_or(""), nullptr, false);
    if (catalogue.is_discarded() || !catalogue.contains("sides"))
        return failed(std::format("{} is not the list of sides", catalogue_path));
    Settings settings;
    settings.width = catalogue["window"].value("width", 800);
    settings.height = catalogue["window"].value("height", 600);
    settings.rest = milliseconds{catalogue.value("rest_ms", 5000)};
    settings.idle = milliseconds{catalogue.value("idle_ms", 5000)};
    settings.quiet = milliseconds{catalogue.value("quiet_ms", 600)};
    if (const auto trace = given.value("trace")) {
        settings.trace = fs::absolute(mcpplibs::os::fs::from_utf8(*trace), error);
        fs::create_directories(settings.trace, error);
    }
    settings.timeout = milliseconds{catalogue.value("timeout_ms", 30000)};
    settings.clicks = catalogue.value("clicks", 5);
    const int rounds = number(given.value("rounds")).value_or(10);
    const std::optional<std::string> screen_name = given.value("screen");
    settings.desktop = screen_name.has_value();
    // A desktop's display is $DISPLAY's (":1", ":1.0"); the harness's own is 97 unless named.
    std::optional<int> desktop_number;
    if (const auto named = mcpplibs::os::env::get("DISPLAY"); named && named->starts_with(':'))
        desktop_number = number(named->substr(1, named->find('.') - 1));
    const int display_number = number(given.value("display"))
                                   .value_or(settings.desktop ? desktop_number.value_or(0) : 97);

    Environment environment;
    environment.work = root / "target" / "bench-apps";
    environment.sdk = given.value("sdk")
                          ? fs::absolute(mcpplibs::os::fs::from_utf8(*given.value("sdk")), error)
                          : environment.work / "sdk";
    fs::create_directories(environment.sdk, error);
    // Their builds: cargo's and the system's compilers (Rust's linker is the system's cc, not
    // mcpp's toolchain's).
    std::string cargo_bin;
    if (const auto cargo = mcpplibs::os::env::find_executable("cargo"))
        cargo_bin = utf8(cargo->parent_path()) + ":";
    environment.competitor_path = cargo_bin + "/usr/local/bin:/usr/bin:/bin";

    std::set<std::string> only;
    if (const auto named = given.value("only"))
        for (auto part : std::views::split(std::string_view(*named), ','))
            only.emplace(part.begin(), part.end());
    std::vector<Side> sides;
    for (const auto& entry : catalogue["sides"]) {
        Side side{.series = entry.value("series", ""),
                  .app = entry.value("app", ""),
                  .build = entry.value("build", ""),
                  .world = entry.value("world", ""),
                  .program = entry.value("program", "")};
        for (const auto& arg : entry.value("args", json::array()))
            side.args.push_back(arg.get<std::string>());
        if (entry.contains("click"))
            side.click = std::array{entry["click"][0].get<int>(), entry["click"][1].get<int>()};
        if (entry.contains("window"))
            side.window = std::array{entry["window"][0].get<int>(), entry["window"][1].get<int>()};
        for (const auto& item : entry.value("env", json::object()).items())
            side.env.emplace_back(item.key(), item.value().get<std::string>());
        side.motion = entry.value("motion", "");
        if (entry.contains("label"))
            side.label = x11::Rect{entry["label"][0].get<int>(), entry["label"][1].get<int>(),
                                   entry["label"][2].get<int>(), entry["label"][3].get<int>()};
        // Where a side lays out otherwise on a desktop (its fonts are the desktop's): its own button
        // and label there.
        if (const json& there = entry.value("desktop", json::object()); settings.desktop) {
            if (there.contains("click"))
                side.click = std::array{there["click"][0].get<int>(), there["click"][1].get<int>()};
            if (there.contains("label"))
                side.label = x11::Rect{there["label"][0].get<int>(), there["label"][1].get<int>(),
                                       there["label"][2].get<int>(), there["label"][3].get<int>()};
        }
        if (!only.empty() && !only.contains(side.series) &&
            !only.contains(side.series + ":" + side.app))
            continue;
        sides.push_back(std::move(side));
    }

    // Built and sized, each side once; what was built is kept (built.json), so a later run can
    // measure without building again (`--no-build`) after their build trees were removed.
    const json& pins = catalogue["pins"];
    const fs::path built_path = environment.work / "built.json";
    auto built_before =
        json::parse(mcpplibs::os::fs::read(built_path).value_or("{}"), nullptr, false);
    if (built_before.is_discarded())
        built_before = json::object();
    if (given.is_flag_set("no-build")) {
        for (auto& side : sides) {
            const std::string key = side.series + ":" + side.app;
            if (!built_before.contains(key))
                return failed(std::format("{} was not built: run without --no-build first", key));
            const json& made = built_before[key];
            side.executable = mcpplibs::os::fs::from_utf8(made.value("executable", ""));
            side.cwd = mcpplibs::os::fs::from_utf8(made.value("cwd", ""));
            side.tree = mcpplibs::os::fs::from_utf8(made.value("tree", ""));
            side.run_tree = mcpplibs::os::fs::from_utf8(made.value("run_tree", ""));
            side.executable_bytes = made.value("executable_bytes", std::uintmax_t{0});
            side.package_bytes = made.value("package_bytes", std::uintmax_t{0});
        }
    }
    if (!given.is_flag_set("no-build"))
        std::cout << std::format("building {} sides\n", sides.size()) << std::flush;
    for (auto& side : sides) {
        if (given.is_flag_set("no-build"))
            break;
        std::cout << std::format("{} {} ({})\n", side.series, side.app, side.build) << std::flush;
        std::expected<void, std::string> built =
            std::unexpected("unknown build: omui, cargo, flutter or nsh");
        if (side.build == "omui")
            built = build_omui(root, side, environment);
        else if (side.build == "cargo")
            built = build_cargo(root, side, environment);
        else if (side.build == "flutter")
            built = build_flutter(root, side, environment, pins["flutter"], settings);
        else if (side.build == "nsh")
            built = build_nsh(root, side, environment, pins["nsh"], settings);
        if (!built)
            return failed(std::format("{} {}: {}", side.series, side.app, built.error()));
        const auto measured = sizes(root, side.tree, environment.work / "sizes");
        if (!measured)
            return failed(measured.error());
        side.executable_bytes = measured->first;
        side.package_bytes = measured->second;
        std::cout << std::format("  {}: programs {} B stripped, package {} B (tar.gz)\n",
                                 utf8(side.tree), side.executable_bytes, side.package_bytes)
                  << std::flush;
        built_before[side.series + ":" + side.app] = {{"executable", utf8(side.executable)},
                                                      {"cwd", utf8(side.cwd)},
                                                      {"tree", utf8(side.tree)},
                                                      {"run_tree", utf8(side.run_tree)},
                                                      {"executable_bytes", side.executable_bytes},
                                                      {"package_bytes", side.package_bytes}};
        if (!(std::ofstream(built_path, std::ios::binary) << built_before.dump(2) << '\n'))
            return failed("built.json cannot be written");
    }
    if (given.is_flag_set("build-only"))
        return 0;

    // The display: an X server of the harness's own, no window manager (or, with --screen, this
    // machine's own); a session bus of the harness's, as a desktop has.
    std::optional<process::process> server;
    if (!settings.desktop) {
        const std::string xvfb = given.value("xvfb").value_or("Xvfb");
        process::spawn_options server_options{
            .program = mcpplibs::os::fs::from_utf8(xvfb),
            .args = {std::format(":{}", display_number), "-screen", "0",
                     std::format("{}x{}x24", settings.screen_width, settings.screen_height),
                     "-nolisten", "tcp", "-noreset"}};
        server_options.out = server_options.err = process::stdio::null;
        server_options.bound_lifetime = true;
        auto started = process::process::spawn(server_options);
        if (!started)
            return failed(std::format("{} did not start: {}", xvfb, started.error().message()));
        server = std::move(*started);
    }
    std::optional<x11::Display> display;
    for (int attempt = 0; attempt < 100 && !display; ++attempt) {
        if (auto opened = x11::Display::open(display_number))
            display.emplace(std::move(*opened));
        else if (settings.desktop)
            return failed(opened.error());
        else
            std::this_thread::sleep_for(milliseconds{50});
    }
    if (!display)
        return failed(std::format("display :{} did not come up", display_number));
    if (auto watching = display->watch(!settings.desktop); !watching)
        return failed(watching.error());
    std::string shown = std::format("Xvfb :{} {}x{} (the harness's own: no window manager, no "
                                    "compositor)",
                                    display_number, settings.screen_width, settings.screen_height);
    // On a desktop: the monitor named, the pointer parked on it (a window manager opens a new
    // window on the pointer's monitor), and put back where its person left it when the run ends.
    std::optional<x11::Display::Pointer> left;
    if (settings.desktop) {
        const auto monitors = display->monitors();
        if (!monitors)
            return failed(monitors.error());
        const auto chosen = std::ranges::find(*monitors, *screen_name, &x11::Monitor::name);
        if (chosen == monitors->end()) {
            std::string names;
            for (const auto& monitor : *monitors)
                names += (names.empty() ? "" : ", ") + monitor.name;
            return failed(std::format("display :{} has no monitor {} (it has {})", display_number,
                                      *screen_name, names.empty() ? "none RANDR names" : names));
        }
        settings.monitor = chosen->area;
        settings.screen_width = chosen->area.width;
        settings.screen_height = chosen->area.height;
        settings.park = {chosen->area.x + 4, chosen->area.y + chosen->area.height - 4};
        if (chosen->hertz > 0)
            settings.frame_gap = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(0.5 / chosen->hertz));
        if (auto pointer = display->where_pointer())
            left = *pointer;
        shown = std::format("{} {}x{} at {:.2f} Hz on this machine's X server :{} (its window "
                            "manager and compositor){}",
                            chosen->name, chosen->area.width, chosen->area.height, chosen->hertz,
                            display_number, graphics_here(root));
    }
    struct PutBack {
        x11::Display& display;
        std::optional<x11::Display::Pointer> left;
        ~PutBack() {
            if (left)
                (void)display.pointer(left->x, left->y);
        }
    } put_back{*display, left};
    const std::string power = power_preference();
    std::vector<double> loads{load_average(root)};
    const fs::path bus = environment.work / "session-bus";
    fs::remove(bus, error);
    process::spawn_options bus_options{.program = "dbus-daemon",
                                       .args = {"--session", "--nofork",
                                                "--address=unix:path=" + utf8(bus)}};
    bus_options.out = bus_options.err = process::stdio::null;
    bus_options.bound_lifetime = true;
    auto session = process::process::spawn(bus_options);
    for (int attempt = 0; attempt < 100 && session && !fs::exists(bus); ++attempt)
        std::this_thread::sleep_for(milliseconds{20});
    environment.run = {{"DISPLAY", std::format(":{}", display_number)},
                       {"WAYLAND_DISPLAY", std::nullopt}};
    if (session && fs::exists(bus))
        environment.run.emplace_back("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + utf8(bus));
    if (const auto fonts = fontconfig(root, environment.work))
        environment.run.emplace_back("FONTCONFIG_FILE", utf8(*fonts));
    // GL through Mesa on the harness's server: NVIDIA's EGL claims an X display it cannot make a
    // context on without DRI3 (Flutter fails). A desktop's server has DRI3: each side's GL and
    // Vulkan reach the machine's own driver.
    if (!settings.desktop && fs::exists("/usr/share/glvnd/egl_vendor.d/50_mesa.json"))
        environment.run.emplace_back("__EGL_VENDOR_LIBRARY_FILENAMES",
                                     "/usr/share/glvnd/egl_vendor.d/50_mesa.json");
    // A desktop's text scale (Xft.dpi above 96) is taken by some sides and not by others: every
    // side drawn at 1 as on the harness's server, the sizes and the places clicked the same (GTK's
    // DPI scale, winit's factor; omui draws at 1 here).
    if (settings.desktop) {
        double dpi = 96;
        if (const auto said = display->resources())
            for (const auto line : lines(*said))
                if (line.starts_with("Xft.dpi:")) {
                    const auto value = line.substr(line.find_first_not_of(" \t", 8));
                    (void)std::from_chars(value.data(), value.data() + value.size(), dpi);
                }
        if (dpi > 0 && dpi != 96) {
            environment.run.emplace_back("GDK_DPI_SCALE", std::format("{:.4f}", 96.0 / dpi));
            shown += std::format("; Xft.dpi {:g}, every side drawn at scale 1 (GDK_DPI_SCALE, "
                                 "WINIT_X11_SCALE_FACTOR)",
                                 dpi);
        }
        environment.run.emplace_back("WINIT_X11_SCALE_FACTOR", "1");
        environment.run.emplace_back("GDK_SCALE", "1");
        // omui's programs link the toolchain's C library, whose loader does not search the
        // system's libraries: they reach the machine's Vulkan loader and NVIDIA's driver the way
        // the device tests do (`gpu --real`: xlings's nvidia-gl-host-link and Vulkan loader), in a
        // real window here (not the offscreen driver), SDL told the loader (SDL_VULKAN_LIBRARY).
        // Without it their GPU path finds no driver and the CPU draws, as on a machine without one.
        if (const auto real = gpu::prepare(gpu::Device::real)) {
            for (const auto& [name, value] : real->variables) {
                if (name == "SDL_VIDEO_DRIVER" || name == "OMUI_GPU_VIDEO_DRIVER" ||
                    name == "OMUI_REQUIRE_GPU")
                    continue;
                environment.omui.emplace_back(name == "OMUI_VULKAN_LIBRARY" ? "SDL_VULKAN_LIBRARY"
                                                                            : name,
                                              value);
            }
            shown += "; omui's sides given xlings's nvidia-gl-host-link and Vulkan loader (the "
                     "toolchain's C library does not search the system's)";
        } else {
            shown += std::format("; omui's sides without the GPU ({})", real.error());
        }
    }
    std::cout << std::format("display: {}\n", shown) << std::flush;

    // A first launch of each, warming nothing in particular but learning which files it maps
    // (evicted from then on).
    for (auto& side : sides) {
        const fs::path shot =
            environment.work / "shots" / std::format("{}-{}.png", side.series, side.app);
        fs::create_directories(shot.parent_path(), error);
        const Launch first = launch(root, *display, side, settings, environment, false, true, shot);
        std::cout << std::format("{} {}: {}\n", side.series, side.app,
                                 first.failure.empty() ? "runs (" + side.driver + ")"
                                                       : first.failure)
                  << std::flush;
    }

    const fs::path samples_path = environment.work / "samples.jsonl";
    fs::remove(samples_path, error);
    struct Seen {
        std::vector<double> cold, warm, paint, private_dirty, uss, pss, rss, cpu, wakeups, frames,
            clicks, presents;
        bool changed = true;
        std::vector<std::string> failures;
    };
    std::map<std::pair<std::string, std::string>, Seen> seen;
    for (int round = 1; round <= rounds; ++round) {
        std::cout << std::format("round {} of {}\n", round, rounds) << std::flush;
        for (auto& side : sides) {
            const Launch cold = launch(root, *display, side, settings, environment, true, true);
            const Launch warm = launch(root, *display, side, settings, environment, false, false);
            Seen& mine = seen[{side.series, side.app}];
            loads.push_back(load_average(root));
            json sample{{"series", side.series},
                        {"round", round},
                        {"key", side.app},
                        {"executable_bytes", side.executable_bytes},
                        {"package_bytes", side.package_bytes},
                        {"driver", side.driver},
                        {"load_1m", loads.back()}};
            if (cold.moved)
                sample["moved_to_monitor"] = true;
            if (!cold.failure.empty())
                mine.failures.push_back("cold: " + cold.failure);
            if (!warm.failure.empty())
                mine.failures.push_back("warm: " + warm.failure);
            if (cold.start_ms)
                sample["start_cold_ms"] = *cold.start_ms, mine.cold.push_back(*cold.start_ms);
            if (warm.start_ms)
                sample["start_warm_ms"] = *warm.start_ms, mine.warm.push_back(*warm.start_ms);
            if (cold.paint_ms)
                sample["paint_cold_ms"] = *cold.paint_ms, mine.paint.push_back(*cold.paint_ms);
            if (cold.memory) {
                sample["private_dirty_bytes"] = cold.memory->private_dirty,
                mine.private_dirty.push_back(static_cast<double>(cold.memory->private_dirty));
                sample["uss_bytes"] = cold.memory->uss,
                mine.uss.push_back(static_cast<double>(cold.memory->uss));
                sample["pss_bytes"] = cold.memory->pss,
                mine.pss.push_back(static_cast<double>(cold.memory->pss));
                sample["rss_bytes"] = cold.memory->rss,
                mine.rss.push_back(static_cast<double>(cold.memory->rss));
            }
            if (cold.idle_cpu_percent)
                sample["idle_cpu_percent"] = *cold.idle_cpu_percent,
                mine.cpu.push_back(*cold.idle_cpu_percent);
            if (cold.idle_wakeups)
                sample["idle_wakeups_per_s"] = *cold.idle_wakeups,
                mine.wakeups.push_back(*cold.idle_wakeups);
            if (!cold.idle_waking.empty()) sample["idle_waking"] = cold.idle_waking;
            if (cold.idle_frames)
                sample["idle_frames_per_s"] = *cold.idle_frames,
                mine.frames.push_back(*cold.idle_frames);
            if (!cold.presents.empty()) {
                const double fps = cold.idle_frames.value_or(0);
                sample["present_p50_ms"] = percentile(cold.presents, 0.5),
                sample["present_p95_ms"] = percentile(cold.presents, 0.95);
                sample["present_p99_ms"] = percentile(cold.presents, 0.99);
                if (fps > 0 && cold.idle_cpu_percent)
                    sample["cpu_ms_per_frame"] = *cold.idle_cpu_percent / 100.0 * 1000.0 / fps;
                mine.presents.insert(mine.presents.end(), cold.presents.begin(),
                                     cold.presents.end());
            }
            if (!cold.clicks.empty()) {
                sample["click_ms"] = median_of(cold.clicks);
                mine.clicks.insert(mine.clicks.end(), cold.clicks.begin(), cold.clicks.end());
            }
            mine.changed = mine.changed && cold.click_changed;
            if (!lead::append(samples_path, std::vector{sample.dump()}))
                return failed("the samples cannot be written");
            std::cout << std::format("  {:<14} {:<8} cold {:>8.1f} ms  warm {:>8.1f} ms  private "
                                     "dirty {:>6.1f} MiB  cpu {:>5.2f} %  click {:>6.1f} ms{}\n",
                                     side.series, side.app, cold.start_ms.value_or(-1),
                                     warm.start_ms.value_or(-1),
                                     cold.memory ? static_cast<double>(cold.memory->private_dirty) /
                                                       1048576.0
                                                 : -1.0,
                                     cold.idle_cpu_percent.value_or(-1),
                                     cold.clicks.empty() ? -1.0 : median_of(cold.clicks),
                                     cold.failure.empty() ? "" : "  (" + cold.failure + ")")
                      << std::flush;
        }
    }

    // A table a person reads: medians and the 90th percentile of the rounds.
    const auto mib = [](const std::vector<double>& values) {
        return values.empty() ? std::string("—")
                              : std::format("{:.1f}", median_of(values) / 1048576.0);
    };
    const auto ms = [](const std::vector<double>& values) {
        return values.empty()
                   ? std::string("—")
                   : std::format("{:.1f} (p90 {:.1f})", median_of(values), percentile(values, 0.9));
    };
    std::cout << "\n| side | app | driver | programs (B, stripped) | package (B, tar.gz) | start "
                 "cold ms | start warm ms | private dirty MiB | USS | PSS | RSS | "
                 "cpu at rest % | switches/s | frames/s at rest | click → frame ms "
                 "|\n|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& side : sides) {
        const Seen& mine = seen[{side.series, side.app}];
        std::cout << std::format("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} "
                                 "| {} | {}{} |\n",
                                 side.series, side.app, side.driver, side.executable_bytes,
                                 side.package_bytes, ms(mine.cold), ms(mine.warm),
                                 mib(mine.private_dirty), mib(mine.uss), mib(mine.pss),
                                 mib(mine.rss),
                                 mine.cpu.empty() ? std::string("—")
                                                  : std::format("{:.2f}", median_of(mine.cpu)),
                                 mine.wakeups.empty()
                                     ? std::string("—")
                                     : std::format("{:.1f}", median_of(mine.wakeups)),
                                 mine.frames.empty()
                                     ? std::string("—")
                                     : std::format("{:.1f}", median_of(mine.frames)),
                                 ms(mine.clicks), mine.changed ? "" : " (a click changed nothing)");
        if (!mine.presents.empty())
            std::cout << std::format("  {} {} in motion: {:.1f} frames/s, present interval p50 "
                                     "{:.2f} ms, p95 {:.2f}, p99 {:.2f}; cpu {:.1f} %\n",
                                     side.series, side.app, median_of(mine.frames),
                                     percentile(mine.presents, 0.5),
                                     percentile(mine.presents, 0.95),
                                     percentile(mine.presents, 0.99), median_of(mine.cpu));
        for (const auto& failure : mine.failures)
            std::cout << std::format("  {} {}: {}\n", side.series, side.app, failure);
    }
    // Where and under what it ran, on every record: the display, what each side drew with, the
    // machine's state.
    loads.push_back(load_average(root));
    lead::Context context = lead::context_here(root);
    std::map<std::string, std::set<std::string>> drew;  // series → the graphics their processes mapped
    for (const auto& side : sides)
        if (!side.driver.empty())
            drew[side.series].insert(side.driver == "none" ? "no GPU driver (the CPU)" : side.driver);
    std::string drivers;
    for (const auto& [series, used] : drew) {
        std::string list;
        for (const auto& one : used)
            list += (list.empty() ? "" : ", ") + one;
        drivers += std::format("{}{} {}", drivers.empty() ? "" : "; ", series, list);
    }
    context.display = shown + (drivers.empty() ? "" : "; drew with: " + drivers) +
                      (given.value("note") ? "; " + *given.value("note") : std::string{});
    context.conditions = std::format(
        "energy_performance_preference {}; 1-minute load {:.1f} at the start, {:.1f} at the end, "
        "{:.1f}–{:.1f} over the rounds",
        power, loads.front(), loads.back(), std::ranges::min(loads), std::ranges::max(loads));
    std::cout << std::format("conditions: {}\n", context.conditions) << std::flush;

    std::vector<std::string> skipped;
    const auto judged =
        lead::verdicts(mcpplibs::os::fs::read(root / lead::metrics_path).value_or(""),
                       mcpplibs::os::fs::read(samples_path).value_or(""),
                       mcpplibs::os::fs::read(root / lead::ledger_path).value_or(""), context,
                       skipped);
    if (!judged)
        return failed(judged.error());
    std::vector<std::string> records;
    for (const auto& verdict : *judged) {
        const auto record = json::parse(verdict.record, nullptr, false);
        if (record.is_discarded() ||
            (!record.contains("ours") && record.value("verdict", "") != "unverified"))
            continue;
        // Only this run's metrics: those whose series these sides are.
        const std::string ours = record.contains("ours") ? record["ours"].value("name", "") : "";
        if (!ours.empty() &&
            std::ranges::none_of(sides, [&](const Side& side) { return side.series == ours; }))
            continue;
        if (ours.empty())
            continue;
        std::cout << verdict.said << '\n';
        records.push_back(verdict.record);
    }
    if (given.is_flag_set("dry-run"))
        return 0;
    if (!lead::append(root / lead::ledger_path, records))
        return failed("the ledger cannot be written");
    const auto written = lead::write_pages(root);
    if (!written)
        return failed(written.error());
    for (const auto& path : *written)
        std::cout << std::format("wrote {}\n", path);
    return 0;
}

}  // namespace omui::devtools::bench
