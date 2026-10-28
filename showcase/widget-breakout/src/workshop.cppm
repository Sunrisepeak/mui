// openmui.showcase.widget_breakout:workshop — level 6's gifts as scripts
// (gifts/*): wide.ts and pierce.ts in TypeScript, slow.lua in Lua. Each
// script registers a verb `gift.<name>` answering its gift's rules; the
// workshop runs them on openmui's script host (openmui.lang), and applying an
// edited one reloads it in place (hot), asks its verb again, checks what it
// answers and hands the rules over — a script never runs in a step: the
// rules reach the simulation as a step's input. What did not load, answer or
// pass keeps the rules there were, and says why.
export module openmui.showcase.widget_breakout:workshop;

import std;
import openmui;
import openmui.showcase.widget_breakout.sim;

export namespace omui::breakout {

// A gift's script.
struct GiftScript {
    std::string gift;      // "wide": its verb is gift.wide
    std::string language;  // "ts", "lua"
    std::string file;      // "wide.ts"
    std::string shipped;   // its text as the game carries it
    std::string source;    // as last applied
};

// What an application of a script came to.
struct Applied {
    std::expected<Rules, std::string> rules;  // the rules with this script's part, or why not
    double                            ms = 0; // from the edit to the rules checked
};

class Workshop : public std::enable_shared_from_this<Workshop> {
public:
    // The script host on this runtime; every gift script loaded.
    explicit Workshop(Runtime& runtime);
    ~Workshop();
    Workshop(const Workshop&) = delete;
    Workshop& operator=(const Workshop&) = delete;

    [[nodiscard]] std::span<const GiftScript> scripts() const noexcept { return scripts_; }
    // The rules as the scripts answer now (the rest: `base`'s). Each verb asked on the runtime's thread.
    [[nodiscard]] std::expected<Rules, std::string> rules(Rules base);
    // Applies a script's edited text: reloaded in place, its verb asked, the
    // answer checked; `done` is told on the runtime's thread (a frame or a few later).
    void apply(std::size_t script, std::string source, Rules base, std::function<void(Applied)> done);
    // The marked parameter of a script (`← tune`): its line's value, and the text with it changed.
    [[nodiscard]] static std::optional<double> tuned(std::string_view source);
    [[nodiscard]] static std::string retune(std::string_view source, double value);

    struct Impl;
    struct Pending;

private:
    void look(std::shared_ptr<Pending> p);  // an application's run, until it ran
    std::unique_ptr<Impl> impl_;
    std::vector<GiftScript> scripts_;
};

}  // namespace omui::breakout
