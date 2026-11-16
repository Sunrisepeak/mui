// gongfu.studio:tool — making fighters as an agent's subject, `gongfu`: its catalog's operations (gongfu.ops.omui.ts) —
// fighter.check, .bake, .embed, .sheet; stage.picture; art.cut, art.picture; roster.versus. The `gongfu-studio`
// command is this subject behind the tool kernel's entries (mlibs.agent:host): `gongfu-studio fighter.check
// showcase/fighting/fighters`, `gongfu-studio mcp`; each report a record.
export module gongfu.studio:tool;

import std;
export import mlibs.agent;
import :catalog;

export namespace gongfu::studio {

class Studio final : public mlibs::agent::Subject {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "gongfu"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "gongfu.studio@1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true, .control = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return ops::catalog; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override;
    [[nodiscard]] std::string_view listing() const noexcept override { return ops::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return 0; }  // its state is the files it reads and writes
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;
};

}  // namespace gongfu::studio
