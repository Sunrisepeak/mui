// xtools.audition — a timeline's gain automation heard and seen, offline, as an agent's subject `audition`: its
// catalog's operation (audition.ops.xdef.ts), audition.automation. A composition of hanim.timeline, meng.audio and
// an omui view (D-204), reached through xtools: `xtools audition.automation curve.timeline.xdef.ts --track music
// --clip fade --out out/fade`.
export module xtools.audition;

import std;
export import mlibs.agent;
export import :catalog;

export namespace xtools::audition {

// The operation as a command writes it: one record out (the report), what a person reads to `err`; 0 done, 1
// failed, 2 a request that is not one.
int automation(const mlibs::agent::Arguments& arguments, std::ostream& out, std::ostream& err);

class Audition final : public mlibs::agent::Subject {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "audition"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xtools.audition@1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return audition::guide; }
    [[nodiscard]] std::string_view listing() const noexcept override { return audition::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return 0; }  // it keeps nothing between requests
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;
};

}  // namespace xtools::audition
