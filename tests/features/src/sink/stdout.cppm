// The `sink-stdout` feature's partition: lines go straight out.
export module openmui.features:sink;

import std;

export namespace omui::features {
inline constexpr std::string_view sink_name = "stdout";
inline constexpr bool             buffered = false;
}  // namespace omui::features
