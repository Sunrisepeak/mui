// omui.designer:view_sources — view documents as a design's sources (D-160's L0; the interface's design adapter for
// a workspace, D-204): the `*.view.xdef.json` of a directory, beside another domain's sources in mlibs.agent's
// Workspace. An edit is read whole against the registered components before anything is written; what runs is each
// document in its canonical form (omui.viewdoc's format), hashed. Its contract is mlibs.agent.sources@1.
export module omui.designer:view_sources;

import std;
import mlibs.agent;

export namespace omui::designer {

class ViewSources final : public mlibs::agent::Sources {
public:
    // The view documents of `directory` (each `<name>.view.xdef.json`, the directory's own, not below it).
    explicit ViewSources(std::filesystem::path directory) : directory_(std::move(directory)) {}
    [[nodiscard]] std::map<std::string, std::string> texts() override;
    // Every document read first; an error in one refuses the edit, at its line and column, nothing written. Then
    // each that differs written, each no longer among them removed: {"changed":[…],"removed":[…]}.
    std::expected<std::string, std::string> apply(const std::map<std::string, std::string>& texts, Reason reason) override;
    // What runs: each document in its canonical form, hashed (a document that does not read, said so).
    [[nodiscard]] std::string state() override;

private:
    std::filesystem::path directory_;
};

}  // namespace omui::designer
