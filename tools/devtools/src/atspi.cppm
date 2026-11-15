// omui.devtools.atspi — applications read and operated through AT-SPI, as a
// screen reader does (D-21). The accessibility bus is turned on (as Orca turns
// it on); each example runs on SDL's offscreen driver, and this client —
// speaking only AT-SPI's D-Bus protocol (mpal's D-Bus) — finds it in the
// registry, walks its tree, reads the names, roles, states and values a screen
// reader would speak, and operates it:
//
// - the tally counter (examples/03-counter): "+1" pressed through the Action
//   interface, the goal slider read and set through Value;
// - the notes (examples/07-headless-and-agent): the draft field's text read
//   through Text, written through EditableText, its caret and selection moved
//   as a screen reader's cursor keys and selection commands do, and the note
//   added with the button.
//
// Linux (AccessKit's adapter); needs at-spi2-core and a session bus
// (tools/ci/atspi.sh runs it in dbus-run-session).
export module omui.devtools.atspi;

import std;

export namespace omui::devtools::atspi {

// AT-SPI's name for a role number (atspi-constants.h), "role N" for one this check does not speak of.
[[nodiscard]] std::string role_name(std::uint32_t role);

struct Check {
    bool        ok = false;
    std::string what;
};
struct Node {
    std::string name, role;
    std::int32_t depth = 0;
};
struct Report {
    std::vector<Check>                       checks;
    std::map<std::string, std::vector<Node>> trees;  // by window: Tally, Notes
    std::vector<std::string>                 actions;  // the +1 button's
    std::optional<double>                    goal_value;
    std::string                              word_at_5;
    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] std::string json() const;
};

// Both examples checked (`notes` empty: the tally alone); the report says every check, passed or not.
[[nodiscard]] Report check(const std::filesystem::path& tally, const std::filesystem::path& notes);

}  // namespace omui::devtools::atspi
