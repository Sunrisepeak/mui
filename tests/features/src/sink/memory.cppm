// The `sink-memory` feature's partition: lines are kept.
export module openmui.features:sink;

import std;

export namespace omui::features {
inline constexpr std::string_view sink_name = "memory";
inline constexpr bool             buffered = true;
}  // namespace omui::features
