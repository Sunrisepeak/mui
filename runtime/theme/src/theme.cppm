// openmui.theme — themes as data (view-data-files §3; component-binding §11).
//
//     # default.theme.omui.toml
//     [theme.dark]
//     extends = "light"
//     dark    = true
//     [theme.dark.colors]
//     background = "#0b1120"
//
//     theme::apply(rt, {"dark", "compact", "reduced_motion"});
//
// A file declares themes and overlays. A theme is a whole set of design
// tokens — colours, text styles, spacing, radii, shadows, motion — possibly
// as the changes it makes to the theme it `extends`. An overlay is a layer
// over any theme that changes only what it names: a density (compact), the
// user's wish for reduced motion. Composing a theme with overlays gives the
// runtime's Theme; components read only tokens, so the semantic tree is the
// same under every composition (S2's promise).
//
// Every mistake in a file is reported with its line and column, a message
// and, where there is one, the fix — all of them in one reading.
export module openmui.theme;

import std;
import openmui.core;
import openmui.view;

export namespace omui::theme {

struct Diagnostic {
    std::string file;
    int         line = 0;
    int         column = 0;
    std::string message;
    std::string hint;

    // file:line:column: message (hint)
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
    std::map<std::string, Color, std::less<>>       colors;
    std::map<std::string, TextPatch, std::less<>>   text;
    std::map<std::string, float, std::less<>>       spacing;
    std::map<std::string, float, std::less<>>       radius;
    std::map<std::string, ShadowToken, std::less<>> shadows;
    std::map<std::string, Transition, std::less<>>  motion;
};

// The themes and overlays a file declares (its text; `file` names it in the
// diagnostics), or everything wrong with it.
[[nodiscard]] std::expected<std::vector<Layer>, Diagnostics> read(std::string_view source, std::string_view file);

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
    [[nodiscard]] std::expected<Theme, std::string> compose(std::span<const std::string> layers) const;

private:
    mutable std::mutex         mutex_;
    std::vector<Layer>         layers_;
};

// The program's library: theme packages add theirs to it at start; the C ABI
// (omuiTheme) composes from it.
[[nodiscard]] Library& library();

// Composes from the program's library and gives the runtime the result.
std::expected<void, std::string> apply(Runtime& rt, std::span<const std::string> layers);

}  // namespace omui::theme
