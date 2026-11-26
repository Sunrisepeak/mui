// oweb:tool — a site without opening a page, as an agent's subject, `oweb`: its catalog's operations
// (oweb.ops.xdef.ts) — site.build, routes.list, page.read. The `oweb` command is this subject behind the tool
// kernel's entries (mlibs.agent:host): `oweb site.build --site docs --out dist`, `oweb mcp`.
export module oweb:tool;

import std;
export import mlibs.agent;
import :catalog;

export namespace oweb {

class Sites final : public mlibs::agent::Subject {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "oweb"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "oweb@1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override;
    [[nodiscard]] std::string_view listing() const noexcept override { return oweb::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return 0; }  // it keeps nothing between requests
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;
};

}  // namespace oweb
