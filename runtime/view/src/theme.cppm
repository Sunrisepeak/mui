// openmui.view:theme — design tokens.
//
// Components never hard-code a colour, a size or a curve: they read tokens
// (colours, text styles, spacing, radii, shadows, motion) from the theme the
// runtime holds. A colour property may name a token ("accent", "surface")
// or be literal ("#2563eb"); a theme package replaces the token values and
// every component follows, with the semantic tree unchanged (S2's promise).
export module openmui.view:theme;

import std;
import openmui.core;
import :element;

export namespace omui {

struct TextToken {
    float                    size = 14;
    float                    weight = 400;
    float                    line_height = 1.4f;  // a multiple of the size
    float                    letter_spacing = 0;
    std::vector<std::string> families{"Inter"};
};

struct ShadowToken {
    Point offset{0, 2};
    float blur = 8;
    float spread = 0;
    Color color{0, 0, 0, 0.15f};
};

class Theme {
public:
    std::string name = "light";
    bool        dark = false;
    std::map<std::string, Color, std::less<>>       colors;
    std::map<std::string, TextToken, std::less<>>   text;
    std::map<std::string, float, std::less<>>       spacing;
    std::map<std::string, float, std::less<>>       radius;
    std::map<std::string, ShadowToken, std::less<>> shadows;
    std::map<std::string, Transition, std::less<>>  motion;

    // A token name, or a literal #rgb / #rrggbb / #rrggbbaa.
    [[nodiscard]] Color color(std::string_view token_or_hex, Color fallback = Color::black()) const;
    [[nodiscard]] const TextToken& text_style(std::string_view token) const;
    [[nodiscard]] float space(std::string_view token, float fallback = 0) const;
    [[nodiscard]] float corner(std::string_view token, float fallback = 0) const;
    [[nodiscard]] Transition transition(std::string_view token) const;

    [[nodiscard]] static Theme light();
    [[nodiscard]] static Theme dark_theme();
};

}  // namespace omui
