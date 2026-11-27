// hanim.timeline.tool — the timeline every editor shares (hanim.timeline) as an agent's subject, `timeline`: its
// catalog's operations (timeline.ops.xdef.ts) over a *.timeline.xdef.json — checked, read at a time, edited, an
// edit taken back. The `hanim-timeline` command is this subject behind the tool kernel's entries
// (mlibs.agent:host): `hanim-timeline timeline.edit --file F --edit split --track 0 --clip 0 --at 15f`,
// `hanim-timeline mcp`. Times are the file's: 90f, 1.5s, 250ms, 123fl. An edit's history is a sidecar beside the
// file (.<file>.history.xdef.jsonl, the last 64).
export module hanim.timeline.tool;

import std;
export import mlibs.agent;
export import :catalog;

export namespace hanim::timeline::tool {

class Timelines final : public mlibs::agent::Subject {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "timeline"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "hanim.timeline@1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true, .control = true, .history = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override;
    [[nodiscard]] std::string_view listing() const noexcept override { return tool::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return 0; }  // its state is the files it is given
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;
};

}  // namespace hanim::timeline::tool
