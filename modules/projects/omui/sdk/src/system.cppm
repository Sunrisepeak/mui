// openmui:system — an application's surfaces outside its own view (W7,
// D-140): files chosen, notifications, a tray, a menu bar. Each is native
// (the system's own, where the host has it) or drawn (the interface's own:
// everywhere, the same on every platform, and an agent operates it as it
// operates the rest of the tree). The window's content is drawn either way.
//
//     omui::System system(rt, {.files = omui::Surfacing::drawn});
//     system.choose_files({.title = "Open a picture", .filters = {{"Pictures", "png;jpg"}}},
//                         [](std::vector<std::filesystem::path> chosen) { … });   // none: cancelled
//     system.notify({.title = "Saved", .body = "notes.md"});
//
// The same, for agents and scripts, as verbs (system.ops.omui.ts): a file
// choice answers at once with its handle; what was chosen is read when the
// person has chosen.
export module omui:system;

import std;
import omui.core;
import omui.view;

export namespace omui {

enum class Surfacing : std::uint8_t { native, drawn };

using FileFilter = Runtime::FileFilter;
using FileChoice = Runtime::FileChoice;
using FilesChosen = Runtime::FilesChosen;
using Notice = Runtime::Notice;
using MenuEntry = Runtime::MenuEntry;

struct MenuGroup {
    std::string            label;  // "File"
    std::vector<MenuEntry> entries;
};

// Which of each, where both can be: native ones are the host's
// (Runtime::native_surfaces); a missing one is drawn.
struct SystemOptions {
    Surfacing files = Surfacing::native;
    Surfacing notifications = Surfacing::native;
    Surfacing menu_bar = Surfacing::drawn;  // a native menu bar is macOS's alone: drawn for now
    seconds   notice_shown{5.0};            // how long a drawn notice stays
};

class System {
public:
    System(Runtime& runtime, SystemOptions options = {});
    ~System();
    System(const System&) = delete;
    System& operator=(const System&) = delete;

    // The drawn picker unless the native one is chosen and there.
    void choose_files(FileChoice choice, FilesChosen chosen);
    void notify(Notice notice);
    // A tray icon's menu (native only: a drawn window has no tray); false where there is none.
    bool set_tray(std::vector<MenuEntry> entries, std::string tooltip);
    // The window's menu bar (drawn: a bar above the application).
    void set_menu_bar(std::vector<MenuGroup> groups);

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
};

}  // namespace omui
