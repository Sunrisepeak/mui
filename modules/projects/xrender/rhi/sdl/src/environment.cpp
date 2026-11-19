// xrender.rhi.sdl's options from the environment (api.cppm): one place reads the variables `omui-devtools gpu` sets.
module xrender.rhi.sdl.api;

import std;
import mcpplibs.os;

namespace xrender::rhi::sdl {

Options environment_options(Options base) {
    if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) base.vulkan_library = *library;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) base.video_driver = *video;
    return base;
}

bool device_required() { return mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1"; }

VulkanDriverChoice vulkan_driver_choice(std::span<const std::uint32_t> pci_vendors) {
    // The drivers' file names by vendor (Mesa's and the vendors' own, as the distributions install them).
    static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 6> by_vendor{{
        {0x10de, "nvidia_icd*,nouveau_icd*"},
        {0x1002, "radeon_icd*,amd_icd*"},
        {0x8086, "intel_icd*,intel_hasvk_icd*"},
        {0x1af4, "virtio_icd*"},
        {0x5143, "freedreno_icd*"},
        {0x13b5, "panfrost_icd*"},
    }};
    std::string globs;
    bool all_known = !pci_vendors.empty();
    for (const std::uint32_t vendor : pci_vendors) {
        const auto known = std::ranges::find(by_vendor, vendor, &std::pair<std::uint32_t, std::string_view>::first);
        if (known == by_vendor.end()) {
            all_known = false;
            break;
        }
        if (!globs.contains(known->second)) globs += std::format("{}{}", globs.empty() ? "" : ",", known->second);
    }
    if (all_known) return {"VK_LOADER_DRIVERS_SELECT", std::move(globs)};
    return {"VK_LOADER_DRIVERS_DISABLE", "lvp_icd*,*swiftshader*"};
}

std::vector<std::uint32_t> display_vendors() {
    std::vector<std::uint32_t> out;
    if constexpr (mcpplibs::os::is_linux) {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm", error)) {
            // card0, card1 … (not their connectors, card0-HDMI-A-1).
            const std::string name = mcpplibs::os::fs::utf8(entry.path().filename());
            if (!name.starts_with("card") || name.find('-') != std::string::npos) continue;
            const auto text = mcpplibs::os::fs::read(entry.path() / "device" / "vendor");
            if (!text) continue;  // a platform device: no PCI vendor
            unsigned vendor = 0;
            const std::string_view hex = std::string_view(*text).substr(text->starts_with("0x") ? 2 : 0);
            if (std::from_chars(hex.data(), hex.data() + hex.size(), vendor, 16).ec != std::errc{}) continue;
            if (std::ranges::find(out, vendor) == out.end()) out.push_back(vendor);
        }
    }
    return out;
}

}  // namespace xrender::rhi::sdl
