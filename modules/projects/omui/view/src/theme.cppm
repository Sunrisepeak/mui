// openmui.view:theme — design tokens.
//
// Components never hard-code a colour, a size or a curve: they read tokens
// (colours, text styles, spacing, radii, shadows, motion) from the theme the
// runtime holds. A colour property may name a token ("accent", "surface")
// or be literal ("#2563eb"); a theme package replaces the token values and
// every component follows, with the semantic tree unchanged (S2's promise).
export module omui.view:theme;

import std;
import omui.core;
import :element;
import xrender.vector;

export namespace omui {

struct TextToken {
    float                    size = 14;
    float                    weight = 400;
    float                    line_height = 1.4f;  // a multiple of the size
    float                    letter_spacing = 0;
    std::vector<std::string> families{"Inter"};
};

// A shadow layer: moved by `offset`, blurred by `blur` (CSS: σ = blur / 2),
// grown by `spread`, in `color`.
struct ShadowLayer {
    Point offset{0, 2};
    float blur = 8;
    float spread = 0;
    Color color{0, 0, 0, 0.15f};
};
// A shadow token: the key light's shadow (its own fields), and under it an
// optional ambient one — wide, soft and faint, as light from all round
// lifts an object off the page — painted first (paint_shadow).
struct ShadowToken {
    Point offset{0, 2};
    float blur = 8;
    float spread = 0;
    Color color{0, 0, 0, 0.15f};
    std::optional<ShadowLayer> ambient;
    // How much of the colour of what casts it the shadow takes, 0 … 1: an
    // indigo button's shadow a little indigo, not grey.
    float tint = 0;
};

// Reusable operations compiled into one seekable motion. They share a
// clock; unlike a slot's candidate list, every operation here is applied.
enum class MotionPrimitive : std::uint16_t {
    match_drawing = 1, match_glyphs = 2, fade = 4, lift = 8, panel_lift = 16,
    grow = 32, check_grow = 64, stretch = 128, stagger = 256, distance = 512, readable_text = 1024,
    write = 2048, drop = 4096, noise = 8192, reshape = 16384, press = 32768
};

// A motion a node's slot holds (universal motion §5, D-93): none, or a morph
// between the node's two drawings — what it drew before a change and what
// it draws after (hanim.visual). Components write no animation; the slot
// moves whatever they draw.
struct MotionSpec {
    enum class Kind : std::uint8_t { none, morph };
    enum class Order : std::uint8_t { together, document, distance };
    enum class TextTransition : std::uint8_t { glyphs, replace, automatic };
    // External placement belongs to layout; state/content still morphs. An
    // explicit morph animates model layout, except in a constraints_changed
    // transaction. animate opts out; follow_layout always follows allocation.
    enum class PlacementPolicy : std::uint8_t { automatic, animate, follow_layout };
    Kind       kind = Kind::morph;
    Transition curve = Transition::spring_with();
    Order      order = Order::together;
    double     stagger = 0;      // seconds between pieces, in `order`
    bool       glyphs = true;    // false: legacy whole-run crossfade
    TextTransition text = TextTransition::glyphs; // explicit glyph matching remains compatible
    float      text_rise = 0.35f; // whole-run replacement travel, in ems
    double     minimum_text_contrast = 4.5; // opaque unchanged text on a provable solid carrier
    bool       meaning = true;   // a node whose key changed found again by its role and name
    // What appears and what vanishes (hanim.core's presences, spec M-10):
    // appearing from `appear`'s pose — lower, a little smaller — and within
    // its opacity window; vanishing within `vanish`'s; writing its strokes or
    // dropping in when `appear` does. Played over this motion's curve.
    hanim::Presence appear = *hanim::presence("fade_in", {.fade = 0.3});
    hanim::Presence vanish = *hanim::presence("fade_out", {.fade = 0.6});
    float      stretch = 0;      // a moving fill drawn out along its way (a tab's lens)
    PlacementPolicy placement = PlacementPolicy::automatic;
    bool       local_presence = false;  // appear about each part, rather than the whole control
    std::uint16_t primitives = 0;  // the built-in operations this recipe composes
    // While it moves, all it draws goes through an effect (universal motion
    // §6.2; component × effect × motion): the effect, its numbers, and the
    // one that rises to `peak` half-way and back — at rest it is not there.
    // Below the effect's tiers the motion does not fit (a slot's next
    // candidate is taken).
    struct Travel {
        std::string                                effect;
        std::vector<std::pair<std::string, float>> params;
        std::string                                param;
        float                                      peak = 0;
        std::string                                color;  // the theme's colour by name for the effect's (its _r, _g, _b)
        friend bool operator==(const Travel&, const Travel&) = default;
    };
    std::optional<Travel> travel;
    std::string           name;  // a library's motion ("simple", "liquid", "ink"); empty: written out
    hanim::PointPath      vertex_path; // matched path geometry only; reduced motion uses the linear default
    hanim::PointPath      translation_path; // a moving piece's way (noise); reduced motion goes straight
    bool                  reshape = false; // a replaced glyph's outline morphs into its replacement's
    float                 press = 0;       // a state change under the pointer presses the drawing in by this, ringing out
    friend bool operator==(const MotionSpec&, const MotionSpec&);
};

// Components describe state; recipes combine shared operations in their
// theme rules. Matching includes geometry and paint, glyph matching keeps
// common letters, and presence/ordering operations add their own movement.
[[nodiscard]] MotionSpec compose_motion(std::initializer_list<MotionPrimitive> parts,
                                        Transition timing = Transition::spring_with());
[[nodiscard]] std::optional<MotionPrimitive> motion_primitive(std::string_view name);
[[nodiscard]] std::string_view motion_primitive_name(MotionPrimitive primitive);

struct ComponentMotionRecipe {
    std::string_view component, update, state, enter, exit;
};
[[nodiscard]] std::span<const ComponentMotionRecipe> default_component_motion_recipes();

// Which nodes a slot's motion is for: "*" (every node), a component type's
// name ("toggle"), or "role:<role>" ("role:switch"). The slots: "update"
// (the node drew something else: a value, a text, a dialog opened), "enter"
// (its first drawing, inserted where the window was already drawn), "exit"
// (removed: it stays until its motion rests), "state" (only hover, press or
// focus changed in it), "reorder" (the same children in another order) and
// "replace" (none of the children it had). A slot holding nothing of its
// own — state, reorder, replace — moves by the update slot's.
//
// A slot holds a motion, or candidates (a strategy): `spec`, then
// `instead`, in order. The person's choice (Runtime::set_motion_choice) is
// taken when the slot offers it; else the first that fits the device (its
// travel effect within the effects' tier).
struct MotionRule {
    enum class Layer : std::uint8_t { framework, style, style_targeted, application };
    std::string             target;
    std::string             slot = "update";
    MotionSpec              spec;
    std::vector<MotionSpec> instead;
    Layer                  layer = Layer::application;
};

// The slots are the specification's (registry/motion.toml [[slot]], M-7), in its order.
inline constexpr auto motion_slots = [] {
    std::array<std::string_view, omui::spec::motion_slots.size()> names{};
    for (std::size_t i = 0; i < names.size(); ++i) names[i] = omui::spec::motion_slots[i].name;
    return names;
}();

// Component-specific defaults compose the shared operations: selection,
// values, text, surfaces and presence have different recipes. Editing and
// pointer tracking stay immediate; focus and other feedback still move.
[[nodiscard]] std::vector<MotionRule> default_motion_rules();

// The framework's motion library, by name: "morph" (the standard spring),
// "simple" (a short tween, nothing drawn out), "spring" (a gentle spring),
// "liquid" (a gentle spring, a fill drawn out like a drop, the shapes
// merging on the way: metaball, from T3) and "ink" (an unhurried tween, the
// line wavering on the way: wobble, from T2).
[[nodiscard]] std::map<std::string, MotionSpec, std::less<>> default_motion_library();

class Theme {
public:
    std::string name = "light";
    bool        dark = false;
    // How dense controls are: their heights, paddings and items scale by it
    // (1, the default; 0.8, compact). Whole pixels: see scaled().
    float       density = 1;
    // Corners' continuous curvature, 0 (circular) … 1 (iOS's: about 0.6):
    // every rounded box and every component's own corners (radii()).
    float       corner_smoothing = 0;
    [[nodiscard]] CornerRadii radii(float radius) const noexcept { return CornerRadii::all(radius).smoothed(corner_smoothing); }
    std::map<std::string, Color, std::less<>>       colors;
    std::map<std::string, TextToken, std::less<>>   text;
    std::map<std::string, float, std::less<>>       spacing;
    std::map<std::string, float, std::less<>>       radius;
    std::map<std::string, ShadowToken, std::less<>> shadows;
    std::map<std::string, Transition, std::less<>>  motion;
    // Defaults, style, targeted style, then application rules. Within a
    // layer a type beats a role, then "*"; the later equal match wins.
    std::vector<MotionRule>                         motion_rules = default_motion_rules();
    // Motions by name, which a slot's motion may use ("liquid", {use = "ink", stagger = 0.02}).
    std::map<std::string, MotionSpec, std::less<>>  motion_library = default_motion_library();

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
// "morph", a library's name ("liquid"), either with a curve token after it
// ("morph motion.spring.gentle"), or {use, curve, order ("together",
// "document", "distance"), stagger, glyphs, text (glyphs/replace/auto), text_rise (ems), text_contrast (0 or 1..4.5), meaning, rise, grow, stretch,
// vertex_path = {bend: fraction} or {control1: [along, normal], control2: [along, normal]},
// travel = {effect, param, peak, <numbers>…}} — `use` a library's name the
// rest changes. Nullopt when it is none of these (the caller says why). A
// `compose = ["match", "readable-text", "fade", "lift", "grow", "stretch", "stagger"]`
// readable-text keeps substantial contiguous edits (Save -> Saved), and
// otherwise replaces the whole run without interleaving unrelated letters.
// applies reusable operations together, unlike the list of candidates.
// Explicit text:"replace" always replaces; text:"glyphs" keeps expressive LCS.
// A node's `motion` property is one (its update slot's), a list of them
// (candidates), or a map of them by slot: {update, enter, exit, state,
// reorder, replace}.
[[nodiscard]] std::optional<MotionSpec> parse_motion(const Value& v, const Theme& theme);
// One, or a list of candidates.
[[nodiscard]] std::optional<std::vector<MotionSpec>> parse_motions(const Value& v, const Theme& theme);

// A shadow token under a rounded box: its ambient layer first, then its key
// light's — every box, menu and dialog paints its shadow so. `opacity`
// scales both (a menu fading in); `object`, the colour of what casts it,
// tints both by the token's tint.
void paint_shadow(xrender::DisplayList& list, const Rect& box, const CornerRadii& radii, const ShadowToken& shadow, float opacity = 1,
                  Color object = Color::transparent());
}  // namespace omui
