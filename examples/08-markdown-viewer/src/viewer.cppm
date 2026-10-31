// The viewer: one document at a time, read from disk; the headings as a table
// of contents that marks where the reader is; links to other documents open
// here (Back returns), links to the web go to the system's browser — an agent
// asks first (the link's effect is external) — and a saved file reloads.
export module viewer;

import std;
import openmui;
import openmui.ui.markdown;
import mcpplibs.os;

export namespace viewer {

namespace os = mcpplibs::os;

struct Heading {
    std::string anchor, text;
    int         level = 1;
    friend bool operator==(const Heading&, const Heading&) = default;
};

struct Model {
    omui::Signal<std::string>          path{std::string()};    // the document shown (UTF-8)
    omui::Signal<std::string>          source{std::string()};
    omui::Signal<std::string>          status{std::string()};
    omui::Signal<std::string>          current{std::string()};  // the anchor in view
    omui::Signal<std::vector<Heading>> toc{std::vector<Heading>{}};
    std::vector<std::string>           back;                    // documents to return to
    os::fs::stamp                      seen{};
    // What opens a web address; the system's browser by default.
    std::function<void(std::string_view)> browse;

    // Opens a file, or a folder's index.md / README.md.
    bool open(const std::filesystem::path& where, bool remember = true);
    void reload_if_changed();
    void watch(omui::Runtime& rt);       // reload_if_changed twice a second, from now on
    void follow(std::string_view href);  // a link of the document
    bool go_back();
};

[[nodiscard]] omui::AppSpec app(Model& m);

}  // namespace viewer
