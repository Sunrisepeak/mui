module openmui.theme.styles;

import std;
import openmui.theme;
import openmui.theme.default_theme;

namespace omui::themes {

std::expected<void, theme::Diagnostics> add_styles() {
    if (auto d = add_default(); !d) return d;
    for (auto [text, file] : {std::pair{glass_style, "glass.style.omui.toml"}, std::pair{neon_style, "neon.style.omui.toml"},
                              std::pair{retro_style, "retro.style.omui.toml"}, std::pair{ink_style, "ink.style.omui.toml"},
                              std::pair{liquid_style, "liquid.style.omui.toml"}})
        if (auto r = theme::library().add_styles(text, file); !r) return r;
    return {};
}

}  // namespace omui::themes
