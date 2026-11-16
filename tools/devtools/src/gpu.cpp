module omui.devtools.gpu;

import std;
import mcpplibs.os;
import nlohmann.json;

namespace omui::devtools::gpu {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;

struct Ran {
    int status = -1;
    std::string out;
};

Ran run(std::string program, std::vector<std::string> args) {
    os::process::spawn_options options;
    options.program = std::move(program);
    options.args = std::move(args);
    auto done = os::process::run(std::move(options));
    if (!done) return {};
    return {done->status.shell_code(), std::move(done->out)};
}

// xlings's packages: $HOME/.xlings/data/xpkgs.
fs::path packages() {
    const auto home = os::env::get(os::is_windows ? "USERPROFILE" : "HOME").value_or("");
    return fs::path(home) / ".xlings" / "data" / "xpkgs";
}

// The last (`sort -V`) file under a package whose directory names `package`, whose name `matches`.
std::optional<fs::path> find_in(std::string_view package, const std::function<bool(const fs::path&)>& matches) {
    std::vector<fs::path> found;
    std::error_code error;
    for (const auto& top : fs::directory_iterator(packages(), error)) {
        if (!os::fs::utf8(top.path().filename()).contains(package)) continue;
        for (auto it = fs::recursive_directory_iterator(top.path(), fs::directory_options::skip_permission_denied, error);
             it != fs::recursive_directory_iterator(); it.increment(error)) {
            if (error) break;
            if (!it->is_directory(error) && matches(it->path())) found.push_back(it->path());
        }
    }
    if (found.empty()) return std::nullopt;
    return *std::ranges::max_element(found, [](const fs::path& a, const fs::path& b) { return version_less(os::fs::utf8(a), os::fs::utf8(b)); });
}

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::function<bool(const fs::path&)> named(std::string name) {
    return [name = lower(std::move(name))](const fs::path& path) { return lower(os::fs::utf8(path.filename())) == name; };
}

// xlings install …, three tries a while apart.
bool install(const std::vector<std::string>& names, int tries) {
    std::vector<std::string> args{"install"};
    args.insert(args.end(), names.begin(), names.end());
    args.insert(args.end(), {"-y", "-g"});
    for (int attempt = 1; attempt <= tries; ++attempt) {
        if (run("xlings", args).status == 0) return true;
        if (attempt < tries) std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    return false;
}

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::expected<Environment, std::string> software() {
    Environment out;
    const auto set = [&](std::string name, std::string value) { out.variables.emplace_back(std::move(name), std::move(value)); };
    if constexpr (os::is_macos) {
        set("OMUI_GPU_VIDEO_DRIVER", "cocoa");
        set("OMUI_REQUIRE_GPU", "1");
        return out;
    }
    if (!install({"mesa-lavapipe", "vulkan-loader"}, 3)) return std::unexpected("lavapipe could not be installed (xlings install mesa-lavapipe vulkan-loader)");
    auto icd = find_in("mesa-lavapipe", [](const fs::path& path) {
        const std::string name = os::fs::utf8(path.filename());
        return name.starts_with("lvp_icd") && name.ends_with(".json");
    });
    if (!icd) return std::unexpected("lavapipe's driver manifest (lvp_icd*.json) is not where xlings installs");
    std::optional<fs::path> loader;
    if constexpr (os::is_windows) {
        loader = find_in("vulkan-loader", named("vulkan-1.dll"));
        const auto driver = find_in("mesa-lavapipe", named("vulkan_lvp.dll"));
        if (!driver) return std::unexpected("lavapipe's vulkan_lvp.dll is not where xlings installs");
        const auto manifest = nlohmann::json::parse(read_text(*icd).value_or("{}"), nullptr, false);
        std::string api = "1.4.0";
        if (!manifest.is_discarded() && manifest.contains("ICD") && manifest["ICD"].contains("api_version")) api = manifest["ICD"]["api_version"].get<std::string>();
        const fs::path temporary = os::env::get("RUNNER_TEMP").transform([](const std::string& dir) { return fs::path(dir); }).value_or(fs::temp_directory_path());
        const fs::path written = temporary / "lvp_icd.x86_64.json";
        const nlohmann::json rewritten{{"file_format_version", "1.0.1"}, {"ICD", {{"library_path", os::fs::generic_utf8(*driver)}, {"api_version", api}}}};
        if (!(std::ofstream(written, std::ios::binary) << rewritten.dump() << '\n')) return std::unexpected("the driver manifest cannot be written");
        icd = written;
        // Elevated, the loader ignores VK_DRIVER_FILES / VK_ICD_FILENAMES ("Registry lookup failed to get ICD manifest
        // files" on 0dae888): the manifest is registered where it always looks.
        if (run("reg", {"add", "HKLM\\SOFTWARE\\Khronos\\Vulkan\\Drivers", "/v", os::fs::utf8(written), "/t", "REG_DWORD", "/d", "0", "/f"}).status != 0)
            return std::unexpected("the driver manifest could not be registered (reg add HKLM\\SOFTWARE\\Khronos\\Vulkan\\Drivers)");
    } else {
        loader = find_in("vulkan-loader", named("libvulkan.so.1"));
    }
    if (!loader) return std::unexpected("the Vulkan loader is not where xlings installs");
    if constexpr (os::is_linux) {
        // xim's vulkan-loader has one Linux payload, x86_64, installed on every machine: on aarch64 it is a library of the
        // wrong architecture. The ELF header says so; there, the distribution's loader is used (lavapipe itself is aarch64).
        const std::string here = os::machine::arch();
        const auto built = elf_machine(*loader);
        if (built && *built != here) {
            out.said.push_back(std::format("xim's Vulkan loader is {} on an {} machine; using the distribution's", *built, here));
            const fs::path system = fs::path("/usr/lib") / (here + "-linux-gnu") / "libvulkan.so.1";
            if (!fs::exists(system)) run("sudo", {"apt-get", "install", "-y", "--no-install-recommends", "libvulkan1"});
            if (!fs::exists(system)) return std::unexpected(std::format("no Vulkan loader for {}", here));
            loader = system;
        }
        // What the loader and the driver resolve to, so a device that does not come up says why.
        for (const fs::path& library : {*loader, icd->parent_path() / ".." / ".." / ".." / "lib" / "libvulkan_lvp.so"}) {
            const auto resolved = run("ldd", {os::fs::utf8(library)});
            out.said.push_back(os::fs::utf8(library));
            for (auto part : std::views::split(std::string_view(resolved.out), '\n'))
                if (!std::string_view(part.begin(), part.end()).empty()) out.said.push_back("  " + std::string(part.begin(), part.end()));
            if (resolved.status != 0 || resolved.out.contains("not found")) return std::unexpected(std::format("{} has unresolved libraries", os::fs::utf8(library)));
        }
    }
    // The loader reports its own errors on stderr; the manifest is shown as the loader will read it.
    out.said.push_back(os::fs::utf8(*icd));
    for (auto part : std::views::split(read_text(*icd).value_or("(unreadable)"), '\n'))
        if (!std::string_view(part.begin(), part.end()).empty()) out.said.push_back("  " + std::string(part.begin(), part.end()));
    set("VK_LOADER_DEBUG", "error,warn");
    set("VK_DRIVER_FILES", os::fs::utf8(*icd));
    set("VK_ICD_FILENAMES", os::fs::utf8(*icd));
    set("OMUI_VULKAN_LIBRARY", os::fs::utf8(*loader));
    if constexpr (!os::is_linux) {
        set("SDL_GPU_DRIVER", "vulkan");
        set("OMUI_GPU_VIDEO_DRIVER", "windows");
    }
    set("OMUI_REQUIRE_GPU", "1");
    return out;
}

std::expected<Environment, std::string> real() {
    if constexpr (!os::is_linux) return std::unexpected("the real device is linked by xlings on Linux (nvidia-gl-host-link)");
    const auto manifest_of = [] {
        return find_in("nvidia-gl-host-link", [](const fs::path& path) { return os::fs::generic_utf8(path).ends_with("share/vulkan/icd.d/10_nvidia.json"); });
    };
    auto manifest = manifest_of();
    auto loader = find_in("vulkan-loader", named("libvulkan.so.1"));
    if (!manifest || !loader) {
        install({"nvidia-gl-host-link", "vulkan-loader"}, 1);
        manifest = manifest_of();
        loader = find_in("vulkan-loader", named("libvulkan.so.1"));
    }
    if (!manifest || !loader) return std::unexpected("no NVIDIA driver linked by xlings on this machine (xlings-gl-doctor says why)");
    const fs::path package = manifest->parent_path().parent_path().parent_path().parent_path();
    Environment out;
    out.said.push_back(std::format("{} with {}", os::fs::utf8(*manifest), os::fs::utf8(*loader)));
    const auto searched = os::env::get("LD_LIBRARY_PATH");
    out.variables = {{"VK_DRIVER_FILES", os::fs::utf8(*manifest)},
                     {"VK_ICD_FILENAMES", os::fs::utf8(*manifest)},
                     {"OMUI_VULKAN_LIBRARY", os::fs::utf8(*loader)},
                     {"LD_LIBRARY_PATH", os::fs::utf8(package / "lib") + (searched && !searched->empty() ? ":" + *searched : "")},
                     {"SDL_VIDEO_DRIVER", "offscreen"},
                     {"OMUI_GPU_VIDEO_DRIVER", "offscreen"},
                     {"OMUI_REQUIRE_GPU", "1"}};
    return out;
}

}  // namespace

bool version_less(std::string_view a, std::string_view b) {
    std::size_t i = 0, j = 0;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            std::size_t end_a = i, end_b = j;
            while (end_a < a.size() && digit(a[end_a])) ++end_a;
            while (end_b < b.size() && digit(b[end_b])) ++end_b;
            std::string_view run_a = a.substr(i, end_a - i), run_b = b.substr(j, end_b - j);
            while (run_a.size() > 1 && run_a.front() == '0') run_a.remove_prefix(1);
            while (run_b.size() > 1 && run_b.front() == '0') run_b.remove_prefix(1);
            if (run_a.size() != run_b.size()) return run_a.size() < run_b.size();
            if (run_a != run_b) return run_a < run_b;
            i = end_a, j = end_b;
        } else {
            if (a[i] != b[j]) return a[i] < b[j];
            ++i, ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

std::optional<std::string> elf_machine(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::array<unsigned char, 20> header{};
    if (!in.read(reinterpret_cast<char*>(header.data()), header.size())) return std::nullopt;
    if (header[0] != 0x7F || header[1] != 'E' || header[2] != 'L' || header[3] != 'F') return std::nullopt;
    const unsigned number = header[5] == 2 ? (header[18] << 8u) | header[19] : header[18] | (header[19] << 8u);  // EI_DATA: 2 is big-endian
    if (number == 62) return "x86_64";
    if (number == 183) return "aarch64";
    if (number == 243) return "riscv64";
    return std::to_string(number);
}

std::expected<Environment, std::string> prepare(Device device) { return device == Device::real ? real() : software(); }

}  // namespace omui::devtools::gpu
