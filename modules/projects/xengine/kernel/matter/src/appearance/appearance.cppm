// xengine.matter.appearance — what a state looks like, as data (D-168).
//
// A thing carries a material and state; what is seen of it is a recipe's — a world's style pack, apart from
// its laws, so that another pack changes the look and nothing of what happens:
//
//   * a material's base colour and grain — what the pack says of it, or else its phase's: a material the
//     pack has never heard of looks like the granular, liquid or solid thing it is;
//   * states: each reads one channel along a range — a number in the channel's unit, or the thing's own
//     property (burned, against its fuel) — and moves the colour toward one of its own, perhaps as light.
//     Like a law, a state never names a material: a new one is charred, wet, frosted and aglow like the rest.
//
//     // world.appearance.omui.ts
//     export default defineAppearance({
//       phases:    { granular: { color: "#b9a377", grain: 0.16 }, liquid: { color: "#3b7fd9c8" } },
//       materials: { wood: { color: "#8a5a2b", grain: 0.12 } },
//       states: [
//         { name: "charred", by: "burned", to: "fuel", color: "#17120f", mix: 0.85 },
//         { name: "glows",   by: "temperature", from: 800, to: 1600, color: "#ff5a14", mix: 0.7, emits: true },
//       ],
//     });
//
//     auto look = matter::look_document(program, text, "world.appearance.omui.json");   // or standard_look(program)
//
// A Look is numbers — a colour a material, each state's channel, range and colour as the channel is stored —
// and knows no renderer: a carrier's presenter lowers it to its own path (the lattice's: a cellular::Shade,
// by xengine.cellular.present's feature `matter`), and whoever draws bodies reads the same Look.
export module xengine.matter.appearance;

import std;
import mlibs.json;
export import xengine.matter;
export import :standard_appearance_snapshot;

export namespace xengine::matter {

// ── as written ─────────────────────────────────────────────────────────

// A base look: a colour ("#rrggbb" or "#rrggbbaa") and how uneven it is, 0 … 1 (one place lighter, the next darker).
struct BaseSpec {
    std::string           name;  // a material, or a phase
    std::string           color;
    std::optional<double> grain;
};
// A state's look: along `by` (a channel) from `from` to `to`, toward `color` by at most `mix`.
struct StateSpec {
    std::string                       name;
    std::string                       by;
    double                            from = 0;                // in the channel's unit
    std::variant<double, std::string> to = 0.0;                // in the channel's unit, or a property of the thing itself
    std::string                       color;
    double                            mix = 1;                 // 0 … 1
    bool                              emits = false;           // light of its own
    std::vector<std::string>          of;                      // phases it shows on (none said: all)
};
struct Appearance {
    std::vector<BaseSpec>  phases;
    std::vector<BaseSpec>  materials;
    std::vector<StateSpec> states;
    std::string            nothing;  // what is no material at all (not said: clear)
};

// ── prepared against a program ─────────────────────────────────────────────

// Colours are straight RGBA8, red in the lowest byte (0xAABBGGRR).
struct StateLook {
    std::string                name;
    std::uint32_t              channel = 0;
    std::uint32_t              from = 0;   // as the channel is stored (× 2^fraction)
    std::vector<std::uint32_t> to;         // a material each, as stored; `from`: the state does not show on it
    std::uint32_t              color = 0;  // its alpha: the most of it (`mix`)
    bool                       emits = false;
};
class Look {
public:
    Look() = default;
    // Names resolved against the program, numbers brought to the channels' units. Refused with a code and a
    // pointer into the appearance ("/states/2/by"); a material the pack colours and the program has not is
    // no failure (a pack serves many worlds) — it is in unused().
    [[nodiscard]] static std::expected<Look, Failure> prepare(const Program& program, const Appearance& appearance);

    [[nodiscard]] std::span<const std::uint32_t> base() const noexcept { return base_; }      // a material each
    [[nodiscard]] std::span<const std::uint8_t> grain() const noexcept { return grain_; }     // 0 … 255
    [[nodiscard]] std::span<const StateLook> states() const noexcept { return states_; }
    [[nodiscard]] std::uint32_t nothing() const noexcept { return nothing_; }
    // Whether the pack says a material's look itself (else: its phase's).
    [[nodiscard]] bool named(MaterialId material) const noexcept { return material < named_.size() && named_[material] != 0; }
    [[nodiscard]] std::span<const std::string> unused() const noexcept { return unused_; }
    // The look as JSON: each material's colour and where it is from, each state with its range in units —
    // what an agent reads to know why a thing looks as it does.
    [[nodiscard]] std::string describe(const Program& program) const;

private:
    std::vector<std::uint32_t> base_;
    std::vector<std::uint8_t>  grain_, named_;
    std::vector<StateLook>     states_;
    std::vector<std::string>   unused_;
    std::uint32_t              nothing_ = 0;
};

// An appearance from its JSON snapshot ({"appearance": {phases?, materials?, states?, nothing?}}: what a
// *.appearance.omui.ts data module evaluates to). Problems by pointer.
[[nodiscard]] std::expected<Appearance, std::vector<mlibs::json::Problem>> read_appearance(std::string_view text, std::string_view file);
// Read and prepared against a program; a problem of preparing is placed in the text like one of reading.
[[nodiscard]] std::expected<Look, std::vector<mlibs::json::Problem>> look_document(const Program& program, std::string_view text, std::string_view file);
// The standard appearance (standard.appearance.omui.ts: the standard materials' colours, and the states
// every world shares), prepared against a program — the standard one or any on the standard channels.
[[nodiscard]] std::expected<Look, std::vector<mlibs::json::Problem>> standard_look(const Program& program);

}  // namespace xengine::matter
