// openmui.features — what the chosen sink is, and code that does not care
// which: the partition is chosen by the manifest's features, its facts are
// constants, and the branches are `if constexpr`.
export module openmui.features;

import std;
export import :sink;

export namespace omui::features {

// The lines a program wrote, as the chosen sink keeps them.
inline std::string report(std::span<const std::string_view> lines) {
    std::string out = std::format("sink: {}", sink_name);
    if constexpr (buffered) {
        out += std::format(" (kept {} lines)", lines.size());
    } else {
        for (std::string_view l : lines) out += std::format("\n  {}", l);
    }
    return out;
}

}  // namespace omui::features
