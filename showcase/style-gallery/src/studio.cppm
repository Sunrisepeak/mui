// openmui.showcase.style_gallery — S2, the style workshop.
//
// One real interface — a settings panel and a dashboard over a wallpaper —
// shown in a style pack (openmui.theme.styles); a sidebar to switch packs
// (a cross-fade), an editor for the pack shown: its accent, its corners, each
// of its effects' parameters, applied as they change; and an export that
// writes the edited pack as a *.style.omui.toml. Agents get the same as the
// person: `styles.show`, `styles.export`, every control by its id.
//
// In a window of its own (app()) a pack styles the whole runtime; inside
// another program (the site) it is a theme scope (Runtime::set_scope): the
// workshop in the pack shown, the program around it in its own theme.
export module openmui.showcase.style_gallery;

import std;
import openmui;
import openmui.theme;

export namespace omui::styles {

class Studio {
public:
    Studio();
    Studio(const Studio&) = delete;
    Studio& operator=(const Studio&) = delete;

    omui::Runtime*        runtime = nullptr;
    std::filesystem::path export_dir;  // where exports go (default: the working directory)
    std::string           scope;       // the theme scope packs go to (empty: the whole runtime)
    bool                  can_export = true;  // the Export button (a page has nowhere to write)

    // The pack shown, and its draft (what the editor changed).
    [[nodiscard]] const std::string& current() const noexcept { return current_; }
    [[nodiscard]] const theme::Style& draft() const noexcept { return draft_; }
    omui::Signal<std::int64_t> revision = omui::signal(std::int64_t{0});  // grows with every change: the editor rebuilds

    // Shows a pack (`crossfade`: seen as one change). Unknown: why.
    std::expected<void, std::string> show(std::string_view name, bool crossfade = true);
    // Edits to the draft, applied at once; each is a step of the workshop's
    // own history (a drag's many changes of one value are one step). A value
    // refused leaves the draft as it was and says why (`problem`).
    std::expected<void, std::string> set_color(std::string_view token, std::string_view hex);
    std::expected<void, std::string> set_radius(std::string_view token, double value);
    std::expected<void, std::string> set_param(std::string_view selector, std::string_view param, double value);
    // One edit by name, as the editor's fields are named: "accent", "corners",
    // or "<selector>/<param>" ("role:region/blur"); the value a colour's text
    // or a number. What an agent uses: a refused value answers why.
    std::expected<void, std::string> set(std::string_view what, const omui::Value& value);
    // The history of edits to the pack shown: back, forward, and back to the
    // pack as it was shipped (itself a step: undone like any other).
    std::expected<void, std::string> undo();
    std::expected<void, std::string> redo();
    std::expected<void, std::string> reset();
    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] bool edited() const noexcept;  // the draft differs from the pack as shipped
    // The pack as shipped (what reset goes back to, what fixes each range).
    [[nodiscard]] const theme::Style& original() const noexcept { return original_; }
    // The draft as a file in export_dir: the path written.
    std::expected<std::filesystem::path, std::string> export_style();
    omui::Signal<std::string> problem = omui::signal(std::string());  // the last refused edit, in words (empty: none)
    omui::Signal<std::string> accent_text = omui::signal(std::string());  // the accent as typed (kept while it is not a colour)

    // The preview's own state (a settings panel that works).
    omui::Signal<bool>        notifications = omui::signal(true), cellular = omui::signal(false), backup = omui::signal(true);
    omui::Signal<double>      volume = omui::signal(0.6);
    omui::Signal<std::string> name = omui::signal(std::string("Ada Lovelace"));
    omui::Signal<std::string> region = omui::signal(std::string("eu"));
    omui::Signal<std::int64_t> saved = omui::signal(std::int64_t{0});
    // Save keeps the settings as they are; Cancel puts them back to the last
    // saved; the panel says which (status) and whether anything changed.
    void save_settings();
    void cancel_settings();
    [[nodiscard]] bool settings_changed() const;
    omui::Signal<std::string> status = omui::signal(std::string());
    // The floating tab bar's section (the navigation layer over the content: liquid glass in the glass pack).
    omui::Signal<std::string> section = omui::signal(std::string("overview"));

private:
    struct Saved {
        bool        notifications = true, cellular = false, backup = true;
        double      volume = 0.6;
        std::string name, region;
    };
    std::expected<void, std::string> apply_draft();
    std::expected<void, std::string> edit(std::string key, const std::function<std::expected<void, std::string>(theme::Style&)>& change);
    std::string                 current_;
    theme::Style                draft_, original_;
    std::vector<theme::Style>   undo_, redo_;
    std::string                 last_key_;       // the field the last step changed: more of it joins that step
    double                      last_at_ = -1e9;  // when (the runtime's seconds)
    std::optional<std::uint64_t> last_gesture_;  // and in which drag (none: typing, keys)
    // Each pack's history, kept while another is shown.
    struct Kept {
        std::vector<theme::Style> undo, redo;
    };
    std::map<std::string, Kept, std::less<>> kept_;
    Saved                       saved_{true, false, true, 0.6, "Ada Lovelace", "eu"};
};

[[nodiscard]] omui::AppSpec app(Studio& studio, std::string_view first = "glass");
// The workshop itself — the packs, the interface, the editor — for a program
// to place (in the studio's scope, when it has one), and its verbs.
// `narrow` (a phone): the packs a row above, the interface, then the editor, one column scrolling.
[[nodiscard]] omui::Element view(Studio& studio, bool narrow = false);
void add_verbs(omui::AppSpec& spec, Studio& studio);

}  // namespace omui::styles
