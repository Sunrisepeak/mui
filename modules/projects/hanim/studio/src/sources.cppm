// hanim.studio:sources — animation documents as a design's sources (D-160's L0; hanim's design adapter, D-204): the
// `*.animation.omui.json` of a directory, for Live Design's history and a workspace (mlibs.agent's Workspace) to
// hold beside other domains. An edit is checked whole before anything is written — every document must read and
// open — and what runs is what the documents do: each one sampled at sixteen moments across its length, every
// object's drawn state hashed. Its contract is mlibs.agent.sources@1 (mlibs.agent.cts).
export module hanim.studio:sources;

import std;
import mlibs.agent;

export namespace hanim::studio {

class DocumentSources final : public mlibs::agent::Sources {
public:
    // The documents of `directory` (each `<name>.animation.omui.json`, the directory's own, not below it).
    explicit DocumentSources(std::filesystem::path directory) : directory_(std::move(directory)) {}
    [[nodiscard]] std::map<std::string, std::string> texts() override;
    // Every document read and opened first; a problem in one refuses the edit, its place said, nothing written.
    // Then each that differs written, each no longer among them removed: {"changed":[…],"removed":[…]}.
    std::expected<std::string, std::string> apply(const std::map<std::string, std::string>& texts, Reason reason) override;
    // What the documents do: each sampled at sixteen moments (a loop without end: its first ten seconds), hashed.
    [[nodiscard]] std::string state() override;

private:
    std::filesystem::path directory_;
};

}  // namespace hanim::studio
