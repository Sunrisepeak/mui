// openmui.gen.read — the registry and contract files, read and checked.
//
// Every problem is a diagnostic with a file, line and column, a message and,
// where one exists, the fix (view-data-files §2, R6). Reading never stops at
// the first problem: one run reports everything wrong with a contract.
export module openmui.gen.read;

import std;
import openmui.gen.model;

export namespace omui::gen {

struct Diagnostics {
    std::vector<Diagnostic> list;

    void error(Loc loc, std::string message, std::string hint = {});
    void warning(Loc loc, std::string message, std::string hint = {});
    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] std::string text() const;  // file:line:col: error: message (hint: …)
    [[nodiscard]] std::string json() const;  // [{"severity", "file", "line", "column", "message", "hint"}]
};

// spec/registry: every *.toml there and components/*.contract.omui.toml.
[[nodiscard]] Registry read_registry(const std::filesystem::path& dir, Diagnostics& diags);

// The contracts of one *.contract.omui.toml, checked against the registry
// (roles, actions, effects, motion tokens exist; effects are never weaker
// than an action's default; key bindings name actions or commands).
[[nodiscard]] std::vector<Contract> read_contracts(const std::filesystem::path& file, const Registry& registry,
                                                   Diagnostics& diags);

// The native modules of one *.native.omui.toml (component-binding §8),
// checked: names, types (scalars, records the module declares, T[], T?),
// effects the registry knows and that need no answer (an answer is a verb's).
[[nodiscard]] std::vector<NativeModule> read_natives(const std::filesystem::path& file, const Registry& registry,
                                                     Diagnostics& diags);

}  // namespace omui::gen
