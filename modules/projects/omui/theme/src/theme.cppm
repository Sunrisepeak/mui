// omui.theme — themes as data (view-data-files §3; component-binding §11).
//
//     // default.theme.xdef.ts
//     export default defineTheme({
//       dark: { extends: "light", dark: true, colors: { background: "#0b1120" } },
//     });
//
//     theme::apply(rt, {"dark", "compact", "reduced_motion"});
//
// A file declares themes and overlays; the library reads the JSON its
// module evaluates to (`{ "theme": { "dark": … } }`, D-139: omui-gen embeds
// it in a package, the player evaluates an application's). A theme is a whole set of design
// tokens — colours, text styles, spacing, radii, shadows, motion — possibly
// as the changes it makes to the theme it `extends`. An overlay is a layer
// over any theme that changes only what it names: a density (compact), the
// user's wish for reduced motion. Composing a theme with overlays gives the
// runtime's Theme; components read only tokens, so the semantic tree is the
// same under every composition (S2's promise).
//
// Every mistake in a file is reported at its JSON pointer (and the line and
// column of the JSON), a message and, where there is one, the fix — all of
// them in one reading.
export module omui.theme;

import std;
import mlibs.error;
import omui.core;
import omui.view;

export namespace omui::theme {

struct Diagnostic {
    std::string file;
    int         line = 0;
    int         column = 0;
    std::string message;
    std::string hint;
    std::string at;  // the JSON pointer of what it is about: "/theme/dark/colors/accent"

    // file:line:column: at: message (hint)
    [[nodiscard]] std::string text() const;
};

using Diagnostics = std::vector<Diagnostic>;

// A text style as a layer writes it: the fields it sets.
struct TextPatch {
    std::optional<float>                    size;
    std::optional<float>                    weight;
    std::optional<float>                    line_height;
    std::optional<float>                    letter_spacing;
    std::optional<std::vector<std::string>> families;
};

// What a file declares about one theme or overlay: the tokens it sets.
struct Layer {
    std::string         name;
    std::string         doc;
    std::string         file;
    std::string         extends;   // a theme's base, or nothing
    bool                overlay = false;
    std::optional<bool> dark;
    std::optional<float> density;  // controls' metrics, scaled (compact: 0.8)
    std::optional<float> corner_smoothing;  // corners' continuous curvature, 0 … 1
    std::map<std::string, Color, std::less<>>       colors;
    std::map<std::string, TextPatch, std::less<>>   text;
    std::map<std::string, float, std::less<>>       spacing;
    std::map<std::string, float, std::less<>>       radius;
    std::map<std::string, ShadowToken, std::less<>> shadows;
    std::map<std::string, Transition, std::less<>>  motion;
};

// The themes and overlays a file declares (its JSON; `file` names it in the
// diagnostics), or everything wrong with it.
[[nodiscard]] std::expected<std::vector<Layer>, Diagnostics> read(std::string_view source, std::string_view file);

// A style pack (S2, D-69): a theme to start from, the tokens it changes, and
// the effects it puts on the window, on component types and on roles —
// glass, neon, CRT, ink on paper, liquid — the semantic tree the same under
// every one (S2's promise: only pixels change).
//
//     // neon.style.xdef.ts
//     export default defineStyle({
//       neon: {
//         theme: "dark",
//         colors: { accent: "#22d3ee" },
//         effects: {
//           button: { effect: "neon", radius: 12 },
//           "role:switch": { effect: "metaball", smoothness: 14 },
//           window: { effect: "crt", curvature: 0.06 },
//         },
//         motions: {
//           update: ["liquid", "simple"],          // what its controls' update slots hold (candidates)
//           "role:dialog": { enter: "spring" },    // a type's or a role's slots
//         },
//       },
//     });
//
// Its motions (universal motion §5.2, the style's layer over the
// framework's): a slot's name for every node the rules move (each rule of
// that slot, or — a slot with none — each the update slot's rules move),
// or a type's or role's table of slots; each a motion, or candidates.
struct StyleMotion {
    std::string target;  // empty: every node the rules move
    std::string slot;
    Value       motion;
};

struct Style {
    std::string              name;
    std::string              doc;
    std::string              file;
    std::vector<std::string> layers;      // the theme, then overlays ("dark", "compact")
    Layer                    tokens;      // what it changes: an overlay over them
    std::vector<EffectRule>  effects;     // window, a type, or role:<role>
    std::vector<StyleMotion> motions;     // what its slots hold
    double                   crossfade = 0.35;  // seconds: how a switch to it is seen
};

// The styles a file declares, or everything wrong with it (unknown effects
// and parameters with their nearest names).
[[nodiscard]] std::expected<std::vector<Style>, Diagnostics> read_styles(std::string_view source, std::string_view file);
// A style as its JSON (`{ "style": { <name>: … } }`, indented): read_styles of
// it gives it back (an editor exports what was changed; numbers shortest,
// keys in order).
[[nodiscard]] std::string write_style(const Style& style);

// Why a theme or a style cannot be composed (mlibs.error): its code, the file or name where, and what was found.
enum class ThemeError : std::uint8_t { no_layers, unknown_theme, overlay_first, cycle, unknown_base, extends_overlay, unknown_overlay, not_an_overlay, unknown_token, unknown_style, style_base, bad_motion };
[[nodiscard]] constexpr mlibs::error::Description describe(ThemeError error) noexcept {
    using Kind = mlibs::error::Kind;
    switch (error) {
        case ThemeError::no_layers: return {"omui.theme.no-layers", Kind::invalid_argument, "name a theme, then any overlays over it", "name a theme first, then any overlays: [\"material3\", \"compact\"]"};
        case ThemeError::unknown_theme: return {"omui.theme.unknown-theme", Kind::not_found, "no theme of that name", "use a theme the packs declare (`omui.theme`'s list), or add its file"};
        case ThemeError::overlay_first: return {"omui.theme.overlay-first", Kind::invalid_argument, "an overlay comes after a theme", "put a theme first and the overlay after it"};
        case ThemeError::cycle: return {"omui.theme.cycle", Kind::invalid_argument, "a theme extends itself", "break the loop of `extends`: a theme's chain must end at one that extends nothing"};
        case ThemeError::unknown_base: return {"omui.theme.unknown-base", Kind::not_found, "a theme extends one no file declares", "declare the base theme, or extend one a file declares"};
        case ThemeError::extends_overlay: return {"omui.theme.extends-overlay", Kind::invalid_argument, "a theme extends a theme, not an overlay", "extend a theme; apply the overlay after it instead"};
        case ThemeError::unknown_overlay: return {"omui.theme.unknown-overlay", Kind::not_found, "no overlay of that name", "use an overlay the packs declare, or add its file"};
        case ThemeError::not_an_overlay: return {"omui.theme.not-an-overlay", Kind::invalid_argument, "after the theme, only overlays", "after the theme, list only overlays"};
        case ThemeError::unknown_token: return {"omui.theme.unknown-token", Kind::invalid_argument, "a layer sets a token the theme under it does not have", "set only tokens the theme beneath declares, or declare the token in the theme"};
        case ThemeError::unknown_style: return {"omui.theme.unknown-style", Kind::not_found, "no style of that name", "use a style the theme declares"};
        case ThemeError::style_base: return {"omui.theme.style-base", Kind::failed, "a style's layers cannot be composed", "read the cause beneath: its way out says what to correct in the style's layers", true};
        case ThemeError::bad_motion: return {"omui.theme.bad-motion", Kind::invalid_argument, "a style names a motion that is not one", "name a motion the theme's motion table has"};
    }
    return {"omui.theme.?", Kind::internal};
}
using ThemeFailure = mlibs::error::Error<ThemeError>;

class Library {
public:
    // Adds what a file declares. A name another file already declared is
    // refused; a file added again replaces what it declared before (a saved
    // edit), all or nothing.
    std::expected<void, Diagnostics> add(std::string_view source, std::string_view file);
    std::expected<void, Diagnostics> add_file(const std::filesystem::path& file);

    [[nodiscard]] std::vector<std::string> themes() const;    // whole themes, by name
    [[nodiscard]] std::vector<std::string> overlays() const;  // overlays, by name
    [[nodiscard]] std::optional<Layer> find(std::string_view name) const;

    // A theme with overlays over it, in order: {"dark", "compact"}. The first
    // is a theme (its `extends` chain applied from the root up), the rest are
    // overlays. The result is named by its layers: "dark+compact".
    [[nodiscard]] std::expected<Theme, ThemeFailure> compose(std::span<const std::string> layers) const;

    // Style packs: added like themes (a file again replaces what it declared).
    std::expected<void, Diagnostics> add_styles(std::string_view source, std::string_view file);
    [[nodiscard]] std::vector<std::string> styles() const;
    [[nodiscard]] std::optional<Style> find_style(std::string_view name) const;
    // A style's theme: its layers composed, its tokens over them.
    [[nodiscard]] std::expected<Theme, ThemeFailure> compose_style(std::string_view name) const;

private:
    mutable std::mutex         mutex_;
    std::vector<Layer>         layers_;
    std::vector<Style>         styles_;
};

// The program's library: theme packages add theirs to it at start; the C ABI
// (omuiTheme) composes from it.
[[nodiscard]] Library& library();

// Composes from the program's library and gives the runtime the result.
std::expected<void, ThemeFailure> apply(Runtime& rt, std::span<const std::string> layers);
// A style: its theme and its effects to the runtime, seen as a cross-fade
// from what was on screen (unless `crossfade` is false: a first frame).
std::expected<void, ThemeFailure> apply_style(Runtime& rt, std::string_view name, bool crossfade = true);
// A style for a theme scope only (Runtime::set_scope): the nodes whose
// theme_scope is `scope` take it, the cross-fade covers their area, and the
// rest of the window keeps its own.
std::expected<void, ThemeFailure> apply_style(Runtime& rt, std::string_view name, std::string_view scope, bool crossfade = true);

}  // namespace omui::theme
