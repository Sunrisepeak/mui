// mrender.tool — the renderer without a GUI as an agent's subject, `mrender`: its catalog's operations
// (mrender.ops.xdef.ts) — effect.check, effect.preview, conformance.run, device.describe, model.look, fidelity.run.
// The `mrender` command is this subject behind the tool kernel's entries (mlibs.agent:host):
// `mrender model.look --model Duck.glb --out duck.png`, `mrender mcp`.
export module mrender.tool;

import std;
export import mlibs.agent;
export import :catalog;

export namespace mrender::tool {

class Renderer final : public mlibs::agent::Subject {
public:
    // `self`: this program, which fidelity.run runs once a scenario (model.look, each in a process of its own).
    explicit Renderer(std::filesystem::path self = {}) : self_(std::move(self)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "mrender"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "mrender.tool@1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override;
    [[nodiscard]] std::string_view listing() const noexcept override { return tool::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return 0; }  // it keeps nothing between requests
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    std::filesystem::path self_;
};

}  // namespace mrender::tool
