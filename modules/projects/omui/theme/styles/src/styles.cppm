// openmui.theme.styles — the style packs (S2), carried in the program.
//
//     omui::themes::add_styles();               // glass, neon, retro, ink, liquid; fluent, cupertino, material
//     omui::theme::apply_style(rt, "neon");    // a cross-fade from what was shown
export module omui.theme.styles;

export import :glass_style;
export import :neon_style;
export import :retro_style;
export import :ink_style;
export import :liquid_style;
export import :fluent_style;
export import :cupertino_style;
export import :material_style;

import std;
import omui.theme;

export namespace omui::themes {

// The packs' names, in the order a gallery shows them: the creative ones, then
// the platforms' (W9: Windows' Fluent, Apple's, Google's Material).
inline constexpr std::array<std::string_view, 8> style_names{"glass", "neon", "retro", "ink", "liquid", "fluent", "cupertino", "material"};

// Adds the default theme (what they start from) and the packs to the
// program's library (again: the same, a no-op).
std::expected<void, theme::Diagnostics> add_styles();

}  // namespace omui::themes
