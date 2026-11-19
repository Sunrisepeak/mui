// `omui-devtools release runtime` (E3b, D-250): the shared runtime packed as a release ships it, its xlings
// descriptor, and the check that two applications start from two runtime versions installed side by side.
module omui.devtools.release;

import std;
import mcpplibs.os;
import mlibs.hash;
import mlibs.image;
import nlohmann.json;
import tomlplusplus;
import omui.devtools.run;
import omui.entry.launcher;

namespace omui::devtools::release {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;
using json = nlohmann::json;
using clock = std::chrono::steady_clock;

constexpr std::string_view player_member = "modules/projects/omui/entry/player";
constexpr std::string_view launcher_member = "modules/projects/omui/entry/launcher";
constexpr std::string_view package_name = "omui-runtime";

std::string utf8(const fs::path& path) { return os::fs::utf8(path); }

// The archive of a version, and the directory in it: omui-runtime-<version>-<platform>.
std::string stem_of(std::string_view version, std::string_view platform) { return std::format("{}-{}-{}", package_name, version, platform); }

std::string platform_here() { return std::format("{}-{}", os::name(os::host()), os::machine::arch()); }

// `mcpp pack PROGRAM -p MEMBER --profile dist`, self-contained (the C library with it: red 142): the directory made.
std::expected<fs::path, std::string> pack(const fs::path& root, std::string_view program, std::string_view member) {
    const fs::path packed = root / member / "target/dist";
    std::error_code error;
    fs::remove_all(packed, error);
    const auto ran = run::program(root, "mcpp",
                                  {"pack", std::string(program), "-p", std::string(member), "--profile", "dist", "--pkg-format", "dir", "--mode", "self-contained"},
                                  {.cwd = root, .merged = true});
    if (!ran || ran->status != 0) return std::unexpected(std::format("{} did not pack:\n{}", program, ran ? ran->out : ran.error()));
    for (const auto& entry : fs::directory_iterator(packed, error))
        if (entry.is_directory(error) && fs::exists(entry.path() / "bin" / std::string(program), error)) return entry.path();
    return std::unexpected(std::format("no bin/{} under {}", program, utf8(packed)));
}

std::uintmax_t bytes_in(const fs::path& directory) {
    std::uintmax_t total = 0;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error))
        if (it->is_regular_file(error) && !it->is_symlink(error)) total += it->file_size(error);
    return total;
}

// Every version packed in `out`: its archive and its sha256 (the sidecar written when it was packed).
struct Packed {
    std::string version;
    fs::path archive;
    std::string sha256;
};
std::vector<Packed> packed_in(const fs::path& out, std::string_view platform) {
    std::vector<Packed> found;
    std::error_code error;
    const std::string prefix = std::format("{}-", package_name), suffix = std::format("-{}.tar.gz", platform);
    for (const auto& entry : fs::directory_iterator(out, error)) {
        const std::string name = utf8(entry.path().filename());
        if (!entry.is_regular_file(error) || !name.starts_with(prefix) || !name.ends_with(suffix)) continue;
        Packed one{.version = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size()), .archive = entry.path()};
        one.sha256 = os::fs::read(fs::path(entry.path()).concat(".sha256")).value_or("");
        if (one.sha256.size() >= 64) one.sha256.resize(64);
        else one.sha256 = mlibs::hash::sha256_hex(os::fs::read(entry.path()).value_or(""));
        found.push_back(std::move(one));
    }
    std::ranges::sort(found, [](const Packed& left, const Packed& right) { return omui::launcher::compare_versions(left.version, right.version) < 0; });
    return found;
}

// The xlings descriptor of every version in `out`: where a release puts each archive (GitHub, the tag of its
// version), its sha256; installed, the launcher is on PATH (xvm).
std::string descriptor(std::span<const Packed> versions, std::string_view platform) {
    std::string entries;
    for (const Packed& one : versions)
        entries += std::format("            [\"{}\"] = {{\n                url = \"https://github.com/Sunrisepeak/mui/releases/download/v{}/{}\",\n"
                               "                sha256 = \"{}\",\n            }},\n",
                               one.version, one.version, utf8(one.archive.filename()), one.sha256);
    return std::format(R"(-- omui-runtime: the shared runtime of omui applications (E3b, D-250). Written by
-- `omui-devtools release runtime`: each version is a directory of its own, its runtime.json saying the openmui
-- specification it implements and the capabilities it carries; omui-launch starts an application with the newest
-- installed one that satisfies what its manifest declares ([package.metadata.openmui.runtime]).
package = {{
    spec = "1",
    name = "{}",
    description = "The omui runtime: starts TypeScript applications, shipped as data, that declare the openmui specification it implements",
    type = "package",
    licenses = {{"Apache-2.0"}},
    repo = "https://github.com/Sunrisepeak/mui",
    archs = {{"{}"}},
    status = "dev",
    categories = {{"gui", "runtime"}},
    keywords = {{"omui", "openmui", "ui", "runtime"}},
    programs = {{"omui-launch"}},
    xvm_enable = true,

    xpm = {{
        {} = {{
            deps = {{ build = {{ "xim:patchelf" }} }},
            ["latest"] = {{ ref = "{}" }},
{}        }},
    }},
}}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")
import("xim.libxpkg.elfpatch")

-- Self-contained: its programs run on the C library and loader it carries (lib/). Packed, their interpreter is
-- the build machine's; installed, each one's is its own lib/ld-linux and its search path its own lib/, so that
-- bin/omui-player starts as well as the launcher scripts beside it (which name the loader themselves).
function install()
    os.tryrm(pkginfo.install_dir())
    os.mv(pkginfo.install_file():replace(".tar.gz", ""), pkginfo.install_dir())
    local lib = path.join(pkginfo.install_dir(), "lib")
    elfpatch.set({{ interpreter = path.join(lib, "ld-linux-x86-64.so.2"), extra_rpath = {{ lib }} }})
    return true
end

function config()
    xvm.add("omui-launch", {{ bindir = pkginfo.install_dir() }})
    return true
end

function uninstall()
    xvm.remove("omui-launch", pkginfo.version())
    return true
end
)",
                       package_name, os::machine::arch(), os::name(os::host()), versions.empty() ? std::string() : versions.back().version, entries);
}

// Packs this tree's runtime as `version` into `out`; its directory.
int pack_runtime(const fs::path& root, const fs::path& out, const std::string& version, std::string_view platform) {
    const auto fail = [](std::string_view why) {
        std::println(std::cerr, "omui-devtools release runtime: {}", why);
        return 1;
    };
    std::error_code error;
    fs::create_directories(out, error);
    const std::string stem = stem_of(version, platform);
    const fs::path dir = out / stem;
    fs::remove_all(dir, error);
    // The player without the designer (a release leaves it out), then the launcher beside it (the libraries they
    // share already there).
    for (const auto& [program, member] : {std::pair{std::string_view("omui-player"), player_member}, std::pair{std::string_view("omui-launch"), launcher_member}}) {
        auto made = pack(root, program, member);
        if (!made) return fail(made.error());
        fs::copy(*made, dir, fs::copy_options::recursive | fs::copy_options::skip_existing, error);
        if (error) return fail(std::format("{}: {}", utf8(*made), error.message()));
    }
    fs::create_directories(dir / "licenses/omui", error);
    fs::copy_file(root / "LICENSE", dir / "licenses/omui/LICENSE", fs::copy_options::overwrite_existing, error);
    // What it is: the player's own word (--runtime), the version it ships as.
    const auto said = run::output(root, utf8(dir / "omui-player"), {"--runtime"}, {.cwd = root});
    if (!said) return fail(std::format("omui-player --runtime: {}", said.error()));
    json described = json::parse(*said, nullptr, false);
    if (described.is_discarded() || described.value("$format", "") != "runtime@1") return fail(std::format("omui-player --runtime said `{}`", *said));
    described["runtime"]["version"] = version;
    described["runtime"].erase("directory");  // where it ran from here: the installed copy is elsewhere
    if (auto written = os::fs::write(dir / "runtime.json", described.dump(2) + "\n"); !written) return fail(written.error().message());
    if (auto helped = run::output(root, utf8(dir / "omui-launch"), {"--help"}, {.cwd = root}); !helped) return fail(std::format("omui-launch --help: {}", helped.error()));
    // Its archive (no hard links: xlings extracts them with link(2), which some systems refuse) and its sha256.
    const fs::path archive = out / std::format("{}.tar.gz", stem);
    if (auto made = run::output(root, "tar", {"--hard-dereference", "-C", utf8(out), "-czf", utf8(archive), stem}); !made) return fail(std::format("tar: {}", made.error()));
    const std::string sha256 = mlibs::hash::sha256_hex(os::fs::read(archive).value_or(""));
    if (auto written = os::fs::write(fs::path(archive).concat(".sha256"), std::format("{}  {}\n", sha256, utf8(archive.filename()))); !written)
        return fail(written.error().message());
    for (const std::string_view program : {"omui-player", "omui-launch"})
        std::println("  bin/{:12} {:>10} B", program, fs::file_size(dir / "bin" / std::string(program), error));
    std::println("release runtime: {} ({} B, archive {} B): {}", utf8(dir), bytes_in(dir), fs::file_size(archive, error), described["runtime"].dump());
    return 0;
}

}  // namespace

std::expected<Ink, std::string> ink(const fs::path& png) {
    auto image = mlibs::image::load_png(png);
    if (!image) return std::unexpected(std::format("{}: {}", utf8(png), image.error()));
    Ink out{.width = image->width, .height = image->height};
    for (int row = 0; row < image->height; ++row) {
        const auto line = image->row(row);
        for (std::size_t at = 0; at + 4 <= line.size(); at += 4)
            if (line[at] < 70 && line[at + 1] < 70 && line[at + 2] < 70 && line[at + 3] > 200) ++out.dark;
    }
    return out;
}

int runtime(const fs::path& root, const RuntimeRelease& options) {
    const std::string platform = platform_here();
    if (platform != "linux-x86_64") {
        std::println(std::cerr, "omui-devtools release runtime: packs on linux-x86_64 for now (E3b's first platform), not {}", platform);
        return 1;
    }
    const fs::path out = options.out.empty() ? fs::absolute(root).lexically_normal() / "target/release/runtime" : fs::absolute(options.out);
    std::string version = options.version;
    if (version.empty()) {
        try {
            version = toml::parse_file(utf8(root / player_member / "mcpp.toml"))["package"]["version"].value_or(std::string());
        } catch (const toml::parse_error& error) {
            std::println(std::cerr, "omui-devtools release runtime: {}: {}", utf8(root / player_member / "mcpp.toml"), error.description());
            return 1;
        }
    }
    if (options.pack)
        if (const int packed = pack_runtime(root, out, version, platform); packed != 0) return packed;
    const std::vector<Packed> versions = packed_in(out, platform);
    if (versions.empty()) {
        std::println(std::cerr, "omui-devtools release runtime: no {}-*-{}.tar.gz in {} (pack one first)", package_name, platform, utf8(out));
        return 1;
    }
    if (auto written = os::fs::write(out / "omui-runtime.lua", descriptor(versions, platform)); !written) {
        std::println(std::cerr, "omui-devtools release runtime: omui-runtime.lua: {}", written.error().message());
        return 1;
    }
    std::string listed;
    for (const Packed& one : versions) listed += std::format("{}{}", listed.empty() ? "" : ", ", one.version);
    std::println("release runtime: {} lists {}", utf8(out / "omui-runtime.lua"), listed);
    if (!options.check) return 0;

    // --- the check (E3b's acceptance) ---------------------------------------------------------------------------
    json report{{"versions", json::array()}, {"steps", json::array()}, {"ok", false}};
    for (const Packed& one : versions) report["versions"].push_back(one.version);
    const auto written = [&] { (void)os::fs::write(out / "check.json", report.dump(2) + "\n"); };
    auto since = clock::now();
    const auto passed = [&](std::string_view step, json detail = json::object()) {
        detail["step"] = std::string(step);
        detail["seconds"] = std::chrono::duration<double>(clock::now() - since).count();
        report["steps"].push_back(detail);
        std::println("  ok   {} ({:.1f} s)", step, detail["seconds"].get<double>());
        since = clock::now();
    };
    const auto failed = [&](std::string_view step, std::string_view why) {
        report["failed"] = {{"step", std::string(step)}, {"why", std::string(why)}};
        written();
        std::println(std::cerr, "  FAIL {}: {}", step, why);
        return 1;
    };
    if (versions.size() < 2) return failed("versions", std::format("two versions side by side are checked; {} has {} (pack another: --version)", utf8(out), versions.size()));
    const auto xlings = os::env::find_executable("xlings");
    if (!xlings) return failed("xlings", "xlings is not on PATH");
    const fs::path home = options.home.empty() ? out / "xlings" : fs::absolute(options.home);
    std::error_code error;
    const std::vector<std::pair<std::string, std::string>> in_home{{"XLINGS_HOME", utf8(home)}};
    const auto with_xlings = [&](std::vector<std::string> args, std::chrono::seconds timeout = std::chrono::seconds(600)) {
        return run::program(root, utf8(*xlings), std::move(args), {.cwd = out, .merged = true, .variables = in_home, .timeout = timeout});
    };
    const auto said = [](const std::expected<run::Ran, std::string>& ran) { return ran ? ran->out : ran.error(); };
    const auto ok = [](const std::expected<run::Ran, std::string>& ran) { return ran && ran->status == 0; };

    // An xlings home of its own (its index fetched once), the descriptor registered beside the index.
    if (!fs::exists(home / "data/xim-pkgindex", error))
        if (auto made = with_xlings({"self", "init"}); !ok(made)) return failed("home", said(made));
    if (auto added = with_xlings({"config", "--add-xpkg", utf8(out / "omui-runtime.lua")}); !ok(added)) return failed("home", said(added));
    passed("home", {{"home", utf8(home)}});

    // Every version installed side by side: its archive seeded where xlings downloads to (nothing is published).
    const auto install = [&](const Packed& one) -> std::expected<void, std::string> {
        fs::create_directories(home / "data/runtimedir", error);
        fs::copy_file(one.archive, home / "data/runtimedir" / one.archive.filename(), fs::copy_options::overwrite_existing, error);
        if (error) return std::unexpected(error.message());
        if (auto installed = with_xlings({"install", std::format("{}@{}", package_name, one.version), "-y"}); !ok(installed)) return std::unexpected(said(installed));
        return {};
    };
    for (const Packed& one : versions) {
        (void)with_xlings({"remove", std::format("{}@{}", package_name, one.version), "-y"});
        if (auto installed = install(one); !installed) return failed("install", std::format("{}: {}", one.version, installed.error()));
    }
    // Where each version went (`<home>/data/xpkgs/<namespace>-x-omui-runtime/<version>/`).
    std::map<std::string, fs::path> places;
    for (const auto& package : fs::directory_iterator(home / "data/xpkgs", error))
        if (utf8(package.path().filename()).ends_with(std::format("-x-{}", package_name)))
            for (const Packed& one : versions)
                if (fs::exists(package.path() / one.version / "runtime.json", error)) places[one.version] = package.path() / one.version;
    if (places.size() != versions.size()) return failed("install", std::format("{} of {} versions have their runtime.json under {}", places.size(), versions.size(), utf8(home / "data/xpkgs")));
    passed("install", {{"installed", places.size()}});

    // Two applications from the app-ts template (as `omui new` makes them): `first` pinned to the oldest version,
    // `second` taking the newest; each one's package is its files (≤ 100 KB).
    const fs::path apps = out / "apps";
    fs::remove_all(apps, error);
    const fs::path from = fs::absolute(root).lexically_normal() / templates_directory / "app-ts";
    const std::string oldest = versions.front().version, newest = versions.back().version;
    json packages = json::object();
    for (const std::string name : {"first", "second"}) {
        for (auto it = fs::recursive_directory_iterator(from, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
            if (!it->is_regular_file(error)) continue;
            std::string relative = os::fs::generic_utf8(it->path().lexically_relative(from));
            if (relative == "template.toml" || relative.starts_with("tests/")) continue;
            if (relative.ends_with(".in")) relative.resize(relative.size() - 3);
            std::string text = os::fs::read(it->path()).value_or("");
            for (const auto& [placeholder, value] : {std::pair{std::string_view("{{project.name}}"), name}, std::pair{std::string_view("{{self.version}}"), version}})
                for (auto at = text.find(placeholder); at != std::string::npos; at = text.find(placeholder, at + value.size())) text.replace(at, placeholder.size(), value);
            if (relative == "mcpp.toml" && name == "first") {
                const std::string section = "[package.metadata.openmui.runtime]\nopenmui = \"0.4\"\n";
                if (const auto at = text.find(section); at != std::string::npos) text.insert(at + section.size(), std::format("runtime = \"={}\"\n", oldest));
            }
            fs::create_directories((apps / name / relative).parent_path(), error);
            if (auto made = os::fs::write(apps / name / relative, text); !made) return failed("apps", made.error().message());
        }
        const fs::path archive = apps / std::format("{}.tar.gz", name);
        if (auto made = run::output(root, "tar", {"-C", utf8(apps), "-czf", utf8(archive), name}); !made) return failed("apps", made.error());
        const auto bytes = fs::file_size(archive, error);
        if (bytes > 100 * 1024) return failed("apps", std::format("{}'s package is {} B: over the 100 KB E3b allows", name, bytes));
        packages[name] = {{"files_bytes", bytes_in(apps / name)}, {"archive_bytes", bytes}};
    }
    passed("apps", {{"packages", packages}});

    // Which runtime each takes (omui-launch --which, the launcher of the newest installed version).
    const fs::path launcher = places.at(newest) / "omui-launch";
    const std::vector<std::pair<std::string, std::string>> session{{"XLINGS_HOME", utf8(home)}, {"SDL_VIDEO_DRIVER", "offscreen"}};
    const auto launch = [&](std::vector<std::string> args) {
        return run::program(root, utf8(launcher), std::move(args), {.cwd = apps, .merged = true, .variables = session, .timeout = std::chrono::seconds(120)});
    };
    const auto which = [&](std::string_view name) -> std::expected<std::string, std::string> {
        const auto asked = launch({"--which", utf8(apps / name)});
        if (!ok(asked)) return std::unexpected(said(asked));
        const json answer = json::parse(asked->out, nullptr, false);
        if (answer.is_discarded() || !answer.contains("runtime")) return std::unexpected(asked->out);
        return answer["runtime"].value("version", "");
    };
    for (const auto& [name, expected] : {std::pair{std::string("first"), oldest}, std::pair{std::string("second"), newest}}) {
        const auto chosen = which(name);
        if (!chosen) return failed("which", std::format("{}: {}", name, chosen.error()));
        if (*chosen != expected) return failed("which", std::format("{} takes {}, not {}", name, *chosen, expected));
    }
    passed("which", {{"first", oldest}, {"second", newest}});

    // Each one's launcher becomes its runtime's player: asked through it (`-- --runtime`), the player says where it
    // runs from — the version's own directory.
    for (const auto& [name, expected] : {std::pair{std::string("first"), oldest}, std::pair{std::string("second"), newest}}) {
        const auto asked = launch({utf8(apps / name), "--", "--runtime"});
        if (!ok(asked)) return failed("became", std::format("{}: {}", name, said(asked)));
        const json answer = json::parse(asked->out, nullptr, false);
        const fs::path directory = answer.is_discarded() ? fs::path() : os::fs::from_utf8(answer["runtime"].value("directory", ""));
        if (directory.empty() || !fs::equivalent(directory, places.at(expected), error))
            return failed("became", std::format("{} ran the player of `{}`, not {}'s ({})", name, asked->out, expected, utf8(places.at(expected))));
    }
    passed("became", {{"first", utf8(places.at(oldest))}, {"second", utf8(places.at(newest))}});

    // Both running at once: started through the launcher (no --frames: they stay open), both still running a second
    // later, then ended.
    {
        std::vector<os::process::process> running;
        for (const std::string_view name : {"first", "second"}) {
            os::process::spawn_options how{.program = launcher, .args = {utf8(apps / name)}, .cwd = apps};
            for (const auto& [variable, value] : session) how.env.emplace_back(variable, value);
            how.in = os::process::stdio::null;
            how.out = how.err = os::process::stdio::null;
            auto started = os::process::process::spawn(how);
            if (!started) return failed("together", std::format("{}: {}", name, started.error().message()));
            running.push_back(std::move(*started));
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::string ended;
        for (std::size_t index = 0; index < running.size(); ++index)
            if (auto status = running[index].try_wait(); !status || *status)
                ended += std::format("{}{} ended (status {})", ended.empty() ? "" : "; ", index == 0 ? "first" : "second", status && *status ? (*status)->code : -1);
        for (auto& one : running) one.terminate();
        for (auto& one : running)
            if (!one.wait_for(std::chrono::seconds(5)).value_or(std::nullopt)) one.kill();
        if (!ended.empty()) return failed("together", ended);
    }
    passed("together", {{"running_seconds", 2}});

    // Each draws its first frame, its text in the runtime's own fonts.
    json drawn = json::object();
    for (const std::string_view name : {"first", "second"}) {
        const fs::path picture = apps / std::format("{}.png", name);
        if (auto shown = launch({utf8(apps / name), "--", "--frames", "3", "--png", utf8(picture)}); !ok(shown)) return failed("drawn", std::format("{}: {}", name, said(shown)));
        const auto read = ink(picture);
        if (!read) return failed("drawn", read.error());
        if (read->dark < 200) return failed("drawn", std::format("{}: {} dark pixels: its text was not drawn", name, read->dark));
        drawn[std::string(name)] = read->dark;
    }
    passed("drawn", {{"ink_pixels", drawn}});

    // `omui run` (E3b): an application of this release that declares its runtime starts as on a user's machine — the
    // omui command finds the launcher on PATH and the launcher the newest runtime that satisfies it.
    if (!options.omui.empty()) {
        auto with_path = session;
        with_path.emplace_back("PATH", std::format("{}:{}", utf8(launcher.parent_path()), os::env::get("PATH").value_or("")));
        const auto ran = run::program(root, utf8(fs::absolute(options.omui)), {"run", "--", "--runtime"},
                                      {.cwd = apps / "second", .merged = true, .variables = with_path, .timeout = std::chrono::seconds(120)});
        if (!ok(ran)) return failed("omui run", said(ran));
        // The player's word (its runtime@1 line) among omui's own records.
        fs::path directory;
        for (const auto line : std::views::split(ran->out, '\n')) {
            const json answer = json::parse(std::string_view(line.begin(), line.end()), nullptr, false);
            if (!answer.is_discarded() && answer.is_object() && answer.value("$format", "") == "runtime@1")
                directory = os::fs::from_utf8(answer["runtime"].value("directory", ""));
        }
        if (directory.empty() || !fs::equivalent(directory, places.at(newest), error))
            return failed("omui run", std::format("second ran `{}`, not on {}'s runtime ({})", ran->out, newest, utf8(places.at(newest))));
        passed("omui run", {{"second", utf8(places.at(newest))}});
    }

    // The way out: the version `first` is pinned to removed, it is refused with a command; that command, run as it is
    // written, installs it again, and `first` starts.
    if (auto removed = with_xlings({"remove", std::format("{}@{}", package_name, oldest), "-y"}); !ok(removed)) return failed("way out", said(removed));
    const auto refused = launch({"--which", utf8(apps / "first")});
    if (!refused || refused->status != 2) return failed("way out", std::format("without {}, first was not refused (exit 2): {}", oldest, said(refused)));
    const std::string marker = "install one: ";
    const auto at = refused->out.find(marker);
    if (at == std::string::npos) return failed("way out", std::format("no command in: {}", refused->out));
    std::string command = refused->out.substr(at + marker.size());
    command = command.substr(0, command.find('\n'));
    std::vector<std::string> words;
    for (const auto word : std::views::split(command, ' '))
        if (!std::string_view(word.begin(), word.end()).empty()) words.emplace_back(word.begin(), word.end());
    if (words.size() < 3 || words[0] != "xlings") return failed("way out", std::format("`{}` is not an xlings command", command));
    words.erase(words.begin());
    words.push_back("-y");  // as a person answers its question
    fs::copy_file(versions.front().archive, home / "data/runtimedir" / versions.front().archive.filename(), fs::copy_options::overwrite_existing, error);
    if (auto installed = with_xlings(words); !ok(installed)) return failed("way out", std::format("`{}`: {}", command, said(installed)));
    const auto again = which("first");
    if (!again || *again != oldest) return failed("way out", std::format("after `{}`: {}", command, again ? *again : again.error()));
    passed("way out", {{"command", command}});

    report["ok"] = true;
    written();
    std::println("release runtime (check): ok — {}", utf8(out / "check.json"));
    return 0;
}

}  // namespace omui::devtools::release
