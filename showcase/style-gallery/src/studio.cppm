// openmui.showcase.styles — S2, the style workshop.
//
// One real interface — a settings panel and a dashboard over a wallpaper —
// shown in a style pack (openmui.theme.styles); a sidebar to switch packs
// (a cross-fade), an editor for the pack shown: its accent, its corners, each
// of its effects' parameters, applied as they change; and an export that
// writes the edited pack as a *.style.omui.toml. Agents get the same as the
// person: `styles.show`, `styles.export`, every control by its id.
export module openmui.showcase.styles;

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

    // The pack shown, and its draft (what the editor changed).
    [[nodiscard]] const std::string& current() const noexcept { return current_; }
    [[nodiscard]] const theme::Style& draft() const noexcept { return draft_; }
    omui::Signal<std::int64_t> revision = omui::signal(std::int64_t{0});  // grows with every change: the editor rebuilds

    // Shows a pack (`crossfade`: seen as one change). Unknown: why.
    std::expected<void, std::string> show(std::string_view name, bool crossfade = true);
    // Edits to the draft, applied at once.
    std::expected<void, std::string> set_color(std::string_view token, std::string_view hex);
    std::expected<void, std::string> set_radius(std::string_view token, double value);
    std::expected<void, std::string> set_param(std::string_view selector, std::string_view param, double value);
    // The draft as a file in export_dir: the path written.
    std::expected<std::filesystem::path, std::string> export_style();

    // The preview's own state (a settings panel that works).
    omui::Signal<bool>        notifications = omui::signal(true), cellular = omui::signal(false), backup = omui::signal(true);
    omui::Signal<double>      volume = omui::signal(0.6);
    omui::Signal<std::string> name = omui::signal(std::string("Ada Lovelace"));
    omui::Signal<std::string> region = omui::signal(std::string("eu"));
    omui::Signal<std::int64_t> saved = omui::signal(std::int64_t{0});

private:
    std::expected<void, std::string> apply_draft();
    std::string  current_;
    theme::Style draft_;
};

[[nodiscard]] omui::AppSpec app(Studio& studio, std::string_view first = "glass");

}  // namespace omui::styles
