// The tally counter: its state, its view in C++, and its view document
// loaded and kept up to date with the file.
export module tally;

import std;
import openmui;

export namespace tally {

struct State {
    omui::Signal<int> count = omui::signal(0);
    omui::Signal<int> goal = omui::signal(10);
};

// The view, built in C++: the same tree as ui/tally.view.omui.json.
[[nodiscard]] omui::View view(const State& s);

// The document version, reloaded when its file changes (checked every
// `poll`); the count survives a reload.
class Document {
public:
    [[nodiscard]] static std::expected<Document, std::string> open(omui::Runtime& rt, std::filesystem::path file);
    // Mounts it at "main" and starts watching the file.
    void start(omui::Runtime& rt, omui::seconds poll = omui::seconds(0.1));
    [[nodiscard]] omui::viewdoc::View& view() noexcept { return *view_; }
    // Reloads now if the file changed; true when it did and the text was good.
    bool reload_if_changed();
    [[nodiscard]] const std::string& status() const noexcept { return status_; }

private:
    void watch();

    omui::Runtime*                         rt_ = nullptr;
    std::filesystem::path                  file_;
    std::optional<omui::viewdoc::View>     view_;
    std::filesystem::file_time_type        stamp_{};
    omui::seconds                          poll_{0.1};
    std::string                            status_;
};

}  // namespace tally
