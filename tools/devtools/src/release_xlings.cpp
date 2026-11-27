// A release's programs through xlings (red 142): packed vendored, their runtime dependencies read from their ELF
// files, the descriptor that installs them on xlings' C library and graphics stack, and a home to try it in.
module omui.devtools.release;

import std;
import mcpplibs.os;
import omui.devtools.run;
import mlibs.version;

namespace omui::devtools::release {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;

// The libraries a program may name that xlings provides, and the package each one comes in. The X11 set and the
// Vulkan loader are what SDL's video and GPU drivers dlopen (compat.sdl3 builds them dynamic); the floors are the
// versions xim-pkgindex has carried since its graphics stack (xim:graphics 0.1.5).
struct Provider {
    std::string_view soname, package;
    bool graphics;
};
constexpr std::array<Provider, 9> providers{{
    {"libgcc_s.so.1", "xim:gcc-runtime", false},  // its floor: the GCC whose libstdc++ has the newest GLIBCXX_ named
    {"libstdc++.so.6", "xim:gcc-runtime", false},
    {"libX11.so.6", "xim:libX11@>=1.8", true},
    {"libXcursor.so.1", "xim:libXcursor@>=1.2", true},
    {"libXext.so.6", "xim:libXext@>=1.3", true},
    {"libXfixes.so.3", "xim:libXfixes@>=6.0", true},
    {"libXi.so.6", "xim:libXi@>=1.8", true},
    {"libXrandr.so.2", "xim:libXrandr@>=1.5", true},
    {"libvulkan.so.1", "xim:vulkan-loader@>=1.4", true},
}};

// "GLIBC_2.43" → {2, 43, 0}; "GLIBC_2.2.5" → {2, 2, 5}.
using GlibcVersion = std::array<int, 3>;
std::optional<GlibcVersion> glibc_at(std::string_view text, std::size_t at) {
    GlibcVersion version{};
    std::size_t part = 0;
    at += std::string_view("GLIBC_").size();
    while (part < version.size()) {
        const std::size_t from = at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') version[part] = version[part] * 10 + (text[at++] - '0');
        if (at == from) return std::nullopt;
        ++part;
        if (at + 1 >= text.size() || text[at] != '.' || text[at + 1] < '0' || text[at + 1] > '9') break;
        ++at;
    }
    if (part < 2) return std::nullopt;
    return version;
}

// The GCC release whose libstdc++ first has GLIBCXX_3.4.<minor> (libstdc++'s ABI history), for xim:gcc-runtime's
// floor; xim-pkgindex carries gcc-runtime from 15.1.
std::string_view gcc_of(int minor) {
    if (minor >= 35) return "16.1";
    return "15.1";
}

std::string lua_list(std::span<const std::string> items) {
    std::string out;
    for (const auto& item : items) out += std::format("{}\"{}\"", out.empty() ? "" : ", ", item);
    return out;
}

}  // namespace

std::expected<fs::path, std::string> pack_program(const fs::path& root, std::string_view program, std::string_view member, std::string_view features) {
    const fs::path packed = root / member / "target/dist";
    std::error_code error;
    fs::remove_all(packed, error);
    std::vector<std::string> args{"pack", std::string(program), "-p", std::string(member), "--profile", "dist", "--pkg-format", "dir"};
    if (!features.empty()) args.insert(args.end(), {"--features", std::string(features)});
    const auto ran = run::program(root, "mcpp", std::move(args), {.cwd = root, .merged = true});
    if (!ran || ran->status != 0) return std::unexpected(std::format("{} did not pack:\n{}", program, ran ? ran->out : ran.error()));
    for (const auto& entry : fs::directory_iterator(packed, error))
        if (entry.is_directory(error) && fs::exists(entry.path() / "bin" / std::string(program), error)) return entry.path();
    return std::unexpected(std::format("no bin/{} under {}", program, os::fs::utf8(packed)));
}

Wants wants_of(const fs::path& directory) {
    Wants wants;
    std::optional<GlibcVersion> newest;
    int cxx_minor = 0;  // the newest GLIBCXX_3.4.<minor> named
    std::vector<bool> named(providers.size(), false);
    std::set<std::string> carried;  // the libraries the package has itself
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(directory, error); !error && it != fs::recursive_directory_iterator(); it.increment(error))
        if (it->is_regular_file(error) && !it->is_symlink(error)) carried.insert(os::fs::utf8(it->path().filename()));
    for (auto it = fs::recursive_directory_iterator(directory, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error) || it->is_symlink(error)) continue;
        const auto bytes = os::fs::read(it->path());
        if (!bytes || !bytes->starts_with("\x7f" "ELF")) continue;
        const std::string_view text(*bytes);
        for (auto at = text.find("GLIBC_2."); at != std::string_view::npos; at = text.find("GLIBC_2.", at + 1))
            if (const auto version = glibc_at(text, at); version && (!newest || *version > *newest)) newest = version;
        constexpr std::string_view cxx = "GLIBCXX_3.4.";
        for (auto at = text.find(cxx); at != std::string_view::npos; at = text.find(cxx, at + 1)) {
            int minor = 0;
            std::size_t digit = at + cxx.size();
            while (digit < text.size() && text[digit] >= '0' && text[digit] <= '9') minor = minor * 10 + (text[digit++] - '0');
            if (digit > at + cxx.size()) cxx_minor = std::max(cxx_minor, minor);
        }
        for (std::size_t index = 0; index < providers.size(); ++index)
            if (!named[index] && !carried.contains(std::string(providers[index].soname)) && text.find(providers[index].soname) != std::string_view::npos)
                named[index] = true;
    }
    if (newest) wants.glibc = (*newest)[2] != 0 ? std::format("{}.{}.{}", (*newest)[0], (*newest)[1], (*newest)[2]) : std::format("{}.{}", (*newest)[0], (*newest)[1]);
    for (std::size_t index = 0; index < providers.size(); ++index) {
        if (!named[index]) continue;
        std::string package(providers[index].package);
        if (package == "xim:gcc-runtime") package = std::format("xim:gcc-runtime@>={}", gcc_of(cxx_minor));
        if (std::ranges::find(wants.packages, package) == wants.packages.end()) wants.packages.push_back(package);
        wants.graphics = wants.graphics || providers[index].graphics;
    }
    return wants;
}

std::string release_url(std::string_view version, std::string_view archive) {
    return std::format("https://github.com/Sunrisepeak/mui/releases/download/v{}/{}", version, archive);
}

std::string xlings_descriptor(const XlingsRelease& release) {
    std::string about;
    for (const auto line : std::views::split(std::string_view(release.about), '\n')) about += std::format("-- {}\n", std::string_view(line.begin(), line.end()));
    std::vector<std::string> runtime;
    if (!release.wants.glibc.empty()) runtime.push_back(std::format("xim:glibc@>={}", release.wants.glibc));
    runtime.insert(runtime.end(), release.wants.packages.begin(), release.wants.packages.end());
    if (release.wants.graphics) runtime.emplace_back("xim:graphics@>=0.1.5");
    runtime.insert(runtime.end(), release.starts.begin(), release.starts.end());
    std::string versions;
    for (const XlingsVersion& one : release.versions)
        versions += std::format("            [\"{}\"] = {{\n                url = \"{}\",\n                sha256 = \"{}\",\n            }},\n", one.version, one.url, one.sha256);
    std::string added, removed;
    for (const std::string& program : release.programs) {
        added += std::format("    xvm.add(\"{}\", {{ bindir = bindir{} }})\n", program, release.wants.graphics ? ", envs = envs" : "");
        removed += std::format("    xvm.remove(\"{}\", pkginfo.version())\n", program);
    }
    return std::format(R"({}package = {{
    spec = "2",
    name = "{}",
    description = "{}",
    type = "package",
    licenses = {{"Apache-2.0"}},
    repo = "https://github.com/Sunrisepeak/mui",
    archs = {{"{}"}},
    status = "dev",
    categories = {{{}}},
    keywords = {{{}}},
    programs = {{{}}},
    xvm_enable = true,

    xpm = {{
        {} = {{
            -- What its ELF files name (omui-devtools release reads them): the newest GLIBC_ version and the
            -- libraries xlings provides; then the packages of programs it starts. xim:glibc is the one loader among
            -- them, so xlings points every ELF file's interpreter and search path at these when it installs it.
            deps = {{
                runtime = {{ {} }},
                build = {{ "xim:patchelf@0.18.0" }},
            }},
            ["latest"] = {{ ref = "{}" }},
{}        }},
    }},
}}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")
{}
-- Vendored: no C library in it; its programs under bin/, the libraries it carries under lib/.
function install()
    os.tryrm(pkginfo.install_dir())
    os.mv(pkginfo.install_file():replace(".tar.gz", ""), pkginfo.install_dir())
    return true
end

function config()
    local bindir = path.join(pkginfo.install_dir(), "bin")
{}{}    return true
end

function uninstall()
{}    return true
end
)",
                       about, release.name, release.description, os::machine::arch(), lua_list(release.categories), lua_list(release.keywords),
                       lua_list(release.programs), os::name(os::host()), lua_list(runtime),
                       release.versions.empty() ? std::string() : release.versions.back().version, versions,
                       release.wants.graphics ? "import(\"xim.pkgindex.graphics\")\n" : "",
                       release.wants.graphics ? "    -- Where the drivers are: xlings' Mesa, or the host's NVIDIA userspace (xim:graphics).\n    local envs = graphics.consumer_envs()\n" : "",
                       added, removed);
}

std::expected<std::vector<std::string>, std::string> xlings_loader(const fs::path& package) {
    const auto own = os::env::get("XLINGS_HOME");
    const fs::path packages = (own && !own->empty() ? os::fs::from_utf8(*own) : os::fs::from_utf8(os::env::get("HOME").value_or("")) / ".xlings") / "data/xpkgs";
    // The newest version of an installed package, and its library directory.
    const auto newest = [&](std::string_view name) -> std::optional<fs::path> {
        std::optional<fs::path> found;
        std::string version;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(packages / std::format("xim-x-{}", name), error)) {
            const std::string here = os::fs::utf8(entry.path().filename());
            for (const std::string_view lib : {"lib64", "lib"})
                if (fs::is_directory(entry.path() / lib, error) && (version.empty() || mlibs::version::compare(here, version) > 0)) {
                    version = here;
                    found = entry.path() / lib;
                    break;
                }
        }
        return found;
    };
    const auto glibc = newest("glibc");
    if (!glibc || !fs::exists(*glibc / "ld-linux-x86-64.so.2")) return std::unexpected(std::format("no xim:glibc under {} (xlings install glibc)", os::fs::utf8(packages)));
    std::string searched = os::fs::utf8(*glibc);
    if (const auto runtime = newest("gcc-runtime")) searched += ":" + os::fs::utf8(*runtime);
    if (std::error_code error; fs::is_directory(package / "lib", error)) searched = os::fs::utf8(package / "lib") + ":" + searched;
    return std::vector<std::string>{os::fs::utf8(*glibc / "ld-linux-x86-64.so.2"), "--library-path", searched};
}

std::expected<XlingsHome, std::string> xlings_home(const fs::path& root, const fs::path& given, const fs::path& descriptor) {
    const fs::path home = fs::absolute(given).lexically_normal();  // XLINGS_HOME is read from wherever xlings runs
    const auto found = os::env::find_executable("xlings");
    if (!found) return std::unexpected("xlings is not on PATH");
    std::error_code error;
    // xlings itself in the home: a program's shim links to it, and runs it from there.
    const fs::path xlings = home / "bin/xlings";
    fs::create_directories(home / "bin", error);
    if (!fs::exists(xlings, error)) {
        fs::copy_file(fs::canonical(*found, error), xlings, error);
        if (error) return std::unexpected(std::format("xlings into {}: {}", os::fs::utf8(xlings), error.message()));
    }
    XlingsHome made{.variables = {{"XLINGS_HOME", os::fs::utf8(home)}}, .shims = home / "subos/default/bin"};
    // What says which home and which of its subos this process runs in.
    for (const std::string_view variable : {"XLINGS_ACTIVE_SUBOS", "XLINGS_SUBOS_MODE", "XLINGS_SUBOS_LIB", "XLINGS_PROJECT_DIR", "XLINGS_BIN", "XLINGS_SHIM_DEPTH"})
        made.unset.emplace_back(variable);
    const auto with_xlings = [&](std::vector<std::string> args) {
        return run::program(root, os::fs::utf8(xlings), std::move(args),
                            {.cwd = home, .merged = true, .variables = made.variables, .unset = made.unset, .timeout = std::chrono::seconds(600)});
    };
    if (!fs::exists(home / "data/xim-pkgindex", error))
        if (auto ran = with_xlings({"self", "init"}); !ran || ran->status != 0) return std::unexpected(std::format("xlings self init: {}", ran ? ran->out : ran.error()));
    // This machine's own downloads, linked into the new home's store.
    const auto own = os::env::get("XLINGS_HOME");
    const fs::path store = (own && !own->empty() ? os::fs::from_utf8(*own) : os::fs::from_utf8(os::env::get("HOME").value_or("")) / ".xlings") / "data/runtimedir";
    fs::create_directories(home / "data/runtimedir", error);
    if (!fs::equivalent(store, home / "data/runtimedir", error))
        for (const auto& entry : fs::directory_iterator(store, error)) {
            const std::string name = os::fs::utf8(entry.path().filename());
            if (!entry.is_regular_file(error) || name.ends_with(".lock") || fs::exists(fs::symlink_status(home / "data/runtimedir" / entry.path().filename(), error))) continue;
            fs::create_symlink(entry.path(), home / "data/runtimedir" / entry.path().filename(), error);
        }
    if (auto added = with_xlings({"config", "--add-xpkg", os::fs::utf8(fs::absolute(descriptor))}); !added || added->status != 0)
        return std::unexpected(std::format("xlings config --add-xpkg: {}", added ? added->out : added.error()));
    return made;
}

}  // namespace omui::devtools::release
