// openmui.theme.styles — the style packs (S2), carried in the program.
//
//     omui::themes::add_styles();               // glass, neon, retro, ink, liquid
//     omui::theme::apply_style(rt, "neon");    // a cross-fade from what was shown
export module openmui.theme.styles;

export import :glass_style;
export import :neon_style;
export import :retro_style;
export import :ink_style;
export import :liquid_style;

import std;
import openmui.theme;

export namespace omui::themes {

// The packs' names, in the order a gallery shows them.
inline constexpr std::array<std::string_view, 5> style_names{"glass", "neon", "retro", "ink", "liquid"};

// Adds the default theme (what they start from) and the five packs to the
// program's library (again: the same, a no-op).
std::expected<void, theme::Diagnostics> add_styles();

}  // namespace omui::themes
