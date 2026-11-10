module omui.theme.styles;

import std;
import omui.theme;
import omui.theme.default_theme;

namespace omui::themes {

std::expected<void, theme::Diagnostics> add_styles() {
    if (auto d = add_default(); !d) return d;
    for (auto [text, file] : {std::pair{glass_style, "glass.style.omui.json"}, std::pair{neon_style, "neon.style.omui.json"},
                              std::pair{retro_style, "retro.style.omui.json"}, std::pair{ink_style, "ink.style.omui.json"},
                              std::pair{liquid_style, "liquid.style.omui.json"}, std::pair{fluent_style, "fluent.style.omui.json"},
                              std::pair{cupertino_style, "cupertino.style.omui.json"}, std::pair{material_style, "material.style.omui.json"}})
        if (auto r = theme::library().add_styles(text, file); !r) return r;
    return {};
}

}  // namespace omui::themes
