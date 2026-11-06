// An Inspector workspace, not another simulation engine or package.
export module omui.inspector:cellular;
import std;
import omui;

export namespace omui::inspector::cellular {

// All project edits and time controls have the same operations in the GUI,
// CLI and Agent protocol. Lazy preparation keeps an unopened workspace cheap.
class Session : public std::enable_shared_from_this<Session> {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Runtime* runtime = nullptr; // borrowed; the host owns its lifetime
    Signal<std::int64_t> changed = signal(std::int64_t{0});
    Signal<std::string> problem = signal(std::string()), draft = signal(std::string());
    Signal<std::string> brush = signal(std::string("spark"));
    Signal<bool> details = signal(false);
    Signal<std::int64_t> cursor_x = signal(std::int64_t{32}), cursor_y = signal(std::int64_t{18});

    // Read: describe, snapshot, project, config, inspect. Write: load, configure, paint,
    // advance, seek, reset, play. Optional revision guards mutations.
    [[nodiscard]] VerbResult perform(std::string_view operation, const Value& args = {});
    [[nodiscard]] Value snapshot();
    [[nodiscard]] Value project();
    [[nodiscard]] std::string state_hash();
    [[nodiscard]] Size shape();
    [[nodiscard]] Color color(std::uint32_t x, std::uint32_t y);
    void pause();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void ensure();
    void refresh_controls();
    void schedule();
};

[[nodiscard]] Value example_project();
void add_verbs(AppSpec&, const std::shared_ptr<Session>&);
[[nodiscard]] Element view(const std::shared_ptr<Session>&, float width, bool chinese = false);
[[nodiscard]] AppSpec app(const std::shared_ptr<Session>&, bool chinese = false);
// A batch file contains project (optional), commands, and optional samples.
// Exports are real omui headless frames, a WebP replay and a saved project.
int batch(const std::filesystem::path& input, const std::filesystem::path& prefix, std::ostream&);
}
