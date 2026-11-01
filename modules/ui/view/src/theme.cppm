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

// A motion a node's slot holds (universal motion §5, D-93): none, or a morph
// between the node's two drawings — what it drew before a change and what
// it draws after (openmui.motion). Components write no animation; the slot
// moves whatever they draw.
struct MotionSpec {
    enum class Kind : std::uint8_t { none, morph };
    enum class Order : std::uint8_t { together, document, distance };
    Kind       kind = Kind::morph;
    Transition curve = Transition::spring_with();
    Order      order = Order::together;
    double     stagger = 0;      // seconds between pieces, in `order`
    bool       glyphs = true;    // text changes glyph by glyph
    bool       meaning = true;   // a node whose key changed found again by its role and name
    float      rise = 0;         // what appears (or vanishes) comes from lower by this
    float      grow = 1;         // … and scaled by this, about the middle of the drawing
    float      stretch = 0;      // a moving fill drawn out along its way (a tab's lens)
    friend bool operator==(const MotionSpec&, const MotionSpec&) = default;
};

// Which nodes a slot's motion is for: "*" (every node), a component type's
// name ("toggle"), or "role:<role>" ("role:switch"). The slots: "update"
// (the node drew something else: a value, a state, an order, a text, a
// dialog opened), "enter" (its first drawing, inserted where the window was
// already drawn) and "exit" (removed: it stays until its motion rests).
struct MotionRule {
    std::string target;
    std::string slot = "update";
    MotionSpec  spec;
};

// The framework's default rules: every control's drawing moves on a snappy
// spring when it changes (a toggle's thumb, a tab's lens, a value's fill,
// a button's colour); what opens and closes rises a little and grows.
[[nodiscard]] std::vector<MotionRule> default_motion_rules();

class Theme {
public:
    std::string name = "light";
    bool        dark = false;
    // How dense controls are: their heights, paddings and items scale by it
    // (1, the default; 0.8, compact). Whole pixels: see scaled().
    float       density = 1;
    std::map<std::string, Color, std::less<>>       colors;
    std::map<std::string, TextToken, std::less<>>   text;
    std::map<std::string, float, std::less<>>       spacing;
    std::map<std::string, float, std::less<>>       radius;
    std::map<std::string, ShadowToken, std::less<>> shadows;
    std::map<std::string, Transition, std::less<>>  motion;
    // What each slot holds, by node (the first rule that fits wins: a type's,
    // then a role's, then "*"); the framework's defaults move every control.
    std::vector<MotionRule>                         motion_rules = default_motion_rules();

    // A token name, or a literal #rgb / #rrggbb / #rrggbbaa.
    [[nodiscard]] Color color(std::string_view token_or_hex, Color fallback = Color::black()) const;
    [[nodiscard]] const TextToken& text_style(std::string_view token) const;
    [[nodiscard]] float space(std::string_view token, float fallback = 0) const;
    [[nodiscard]] float corner(std::string_view token, float fallback = 0) const;
    [[nodiscard]] Transition transition(std::string_view token) const;
    // A control's metric at this density, in whole pixels (exactly v at 1).
    [[nodiscard]] float scaled(float v) const noexcept { return density == 1 ? v : std::round(v * density); }

    [[nodiscard]] static Theme light();
    [[nodiscard]] static Theme dark_theme();
};

// A slot's motion as a property, a style or a rule writes it: "none",
// "morph", "morph <curve token>" ("morph motion.spring.gentle"), or
// {use, curve, order ("together", "document", "distance"), stagger, glyphs,
// meaning, rise, grow}. Nullopt when it is none of these (the caller says
// why). A node's `motion` property is one (its update slot's) or a map of
// them by slot: {update, enter, exit}.
[[nodiscard]] std::optional<MotionSpec> parse_motion(const Value& v, const Theme& theme);
}  // namespace omui
