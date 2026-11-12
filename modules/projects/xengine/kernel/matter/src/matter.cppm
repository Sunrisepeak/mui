// xengine.matter — what things are made of, the state each instance carries, and the laws that change
// it (D-165). Physics moves things; this is what they are and become.
//
//   A material is a row of properties (density, ignition point, what it becomes as a gas …).
//   An instance of matter carries state channels (its material, temperature, wetness, charge …).
//   A law reads properties and state, never a material by name: fire does not know wood. It raises
//   temperature, and whatever has fuel, an ignition point below that and oxygen beside it burns.
//
//     auto program = matter::Program::prepare(definition);   // the plan, prepared once (from JSON: xengine.matter.json)
//     std::vector<std::uint32_t> state = program->fresh(instances, *program->material("air")), next = state;
//     program->place(state, instances, 7, *program->material("wood"));
//     matter::Workspace workspace;
//     auto stepped = program->step({instances, pairs}, state, next, workspace);      // one tick, on any carrier
//     if (stepped) state.swap(next);
//
// The carrier is whoever has instances and says which are beside which, with a weight: a lattice's
// cells and neighbourhoods, bodies and their contacts, particles and their near ones, a sample of
// an ambient field. The laws are the same on all of them. The step here is the reference: every
// value is an unsigned 32-bit integer, every sum saturates, every law reads the step's input — so
// it is the same bits wherever it runs, and what a carrier's own faster path computes (the
// lattice's program, the plan's T1.7) is held to it.
export module xengine.matter;

import std;
import mlibs.error;

export namespace xengine::matter {

// ── properties ───────────────────────────────────────────────────────────

// How a material is held together: what a carrier that moves things reads (a lattice lets a liquid
// level and a gas rise), and what `phase == liquid` in a law compares.
enum class Phase : std::uint8_t { solid, soft, granular, liquid, gas };
inline constexpr std::array<std::string_view, 5> phase_names{"solid", "soft", "granular", "liquid", "gas"};

// What a column holds: a number, a phase, or another material (what this one becomes).
enum class ColumnKind : std::uint8_t { number, phase, material };

// A property every material has a value of. Laws read a number as an integer: the value × 2^fraction,
// rounded — so a fraction (a friction of 0.3) is exact where it is used and the same on every machine.
struct Column {
    std::string   name;
    ColumnKind    kind = ColumnKind::number;
    std::string   unit;          // for a reader: "K", "kg/m³", "64ths a step"
    std::string   doc;
    std::uint32_t fraction = 0;  // bits after the point in the integer a law reads
    double        least = 0, most = 4294967295.0;
    double        initial = 0;   // a material that does not say has this
};

using MaterialId = std::uint32_t;
// No material: what a column of kind `material` holds when a thing does not become anything.
inline constexpr MaterialId none = 0xffffffffu;

// ── a definition: what is written (C++ here; JSON through read) ───────────

// A property's value as written: a number, or the name of a phase or of a material.
struct Property {
    std::string                       column;
    std::variant<double, std::string> value;
};
struct MaterialSpec {
    std::string              name;
    std::vector<Property>    properties;
    std::vector<std::string> tags;
};
// A state channel every instance carries: an unsigned 32-bit number — its value × 2^fraction, as a column's.
struct ChannelSpec {
    std::string   name;
    std::string   unit;
    std::string   doc;
    double        initial = 0;  // what a new instance has, in its unit
    std::uint32_t fraction = 0;  // bits after the point (temperature: 8 — a 256th of a kelvin flows)
    std::string   from;         // a column: a new instance has its material's value of it instead, where that is not 0
    bool          resets = false;  // back to `initial` when the instance becomes another material
    std::string   rise, fall;   // events when it leaves 0 and when it returns to it ("ignite", "go_out")
};
// An amount a law adds to, takes from or sets a channel to (or raises, lowers a property by): a sum.
struct Effect {
    std::string target;
    std::string amount;
};
// A law, as written. Which kind it is follows from what it says:
//   transfer   `transfer` (a channel) flows between neighbours, by the lesser of their `by` properties;
//   adjust     `raise` / `lower`: a property as the other laws read it, changed by state (wet: harder to light);
//   reaction   `add` / `take` / `set` on channels, `flag`, and with `become` a conversion: the instance
//              becomes the material its own property names (what it is boiled, frozen, burnt).
// `when` is a condition: comparisons joined by `&&`, each between two sums of channels, properties,
// what is near (`near.oxygen`: the neighbours' sum) and whole numbers — `temperature >= ignition &&
// burned < fuel && near.oxygen > 0`. Without one a law always holds.
// A law is written in units (`temperature >= 373` is kelvin; `raise: {ignition: "wetness * 40"}` is 40 K
// for each of wetness): preparing brings every term of a comparison, and every amount, to one number
// of bits after the point — its target's, or the finest a comparison's terms have.
struct LawSpec {
    std::string         name;
    std::string         when;
    std::string         transfer, by;
    std::uint32_t       multiply = 1, shift = 6;  // a transfer's share of a difference: by × multiply / 2^shift
    std::vector<Effect> add, take, set;
    std::vector<Effect> raise, lower;
    std::string         flag;    // a channel that is 1 while the law holds for an instance and 0 while it does not
    std::string         become;  // a column of kind `material`
    std::string         event;   // what a conversion is called ("boil", "freeze")
    bool                recipe = false;  // a special case that may name materials; counted, and said as such
};
struct Definition {
    std::vector<Column>       columns;
    std::vector<ChannelSpec>  channels;
    std::vector<MaterialSpec> materials;
    std::vector<LawSpec>      laws;

    // The columns and channels every definition starts from, no material and no law: mechanics (density,
    // friction, restitution, compliance, shape_stiffness), heat (thermal, ignition, melting, boiling,
    // warmth, starts_at), burning (fuel, burn_rate, heat, oxygen), water (moist, porous), electricity
    // (conduct), phase and what a material becomes (frozen, melted, boiled, condensed, burnt); channels
    // material, temperature, wetness, charge, burned, burning, damage.
    [[nodiscard]] static Definition standard();
};

// ── errors ───────────────────────────────────────────────────────────────

enum class Errc : std::uint8_t { name, value, expression, law, names_material, limit, unprepared, shape, pair, unstable };
[[nodiscard]] constexpr mlibs::error::Description describe(Errc code) noexcept {
    using Kind = mlibs::error::Kind;  // not `using mlibs::error::Kind;`: that is an ICE in GCC 16 (import_entity_module)
    switch (code) {
        case Errc::name: return {"matter.name", Kind::invalid_argument, "a name that is not there, or is there twice"};
        case Errc::value: return {"matter.value", Kind::invalid_argument, "a value of the wrong kind or out of its column's range"};
        case Errc::expression: return {"matter.expression", Kind::invalid_argument, "a condition or an amount that is not well formed"};
        case Errc::law: return {"matter.law", Kind::invalid_argument, "a law that says two kinds of thing, or nothing"};
        case Errc::names_material: return {"matter.names-material", Kind::invalid_argument, "a law that names a material: laws read properties"};
        case Errc::limit: return {"matter.limit", Kind::refused, "beyond the definition's limits"};
        case Errc::unprepared: return {"matter.unprepared", Kind::invalid_argument, "the program was not prepared"};
        case Errc::shape: return {"matter.shape", Kind::invalid_argument, "state that does not match the instances and channels"};
        case Errc::pair: return {"matter.pair", Kind::invalid_argument, "a pair that names an instance not there, or one beside itself"};
        case Errc::unstable: return {"matter.unstable", Kind::refused, "a transfer that could take more than an instance has: too many neighbours for its share"};
    }
    return {"matter.unknown", Kind::internal, "unknown"};
}
// Where: a JSON pointer into the definition ("/laws/3/when", "/materials/wood/ignition").
using Failure = mlibs::error::Error<Errc, std::string>;

struct Limits {
    std::uint32_t max_columns = 64, max_channels = 16, max_materials = 4096, max_laws = 128;
    std::uint32_t max_terms = 16;  // in one sum
};

// ── the plan ─────────────────────────────────────────────────────────────

enum class LawKind : std::uint8_t { transfer, adjust, reaction, conversion };
inline constexpr std::array<std::string_view, 4> law_kind_names{"transfer", "adjust", "reaction", "conversion"};

// What a term of a sum reads.
enum class Source : std::uint8_t { constant, property, channel, near_property, near_channel };
// One term: the value read, × multiply, >> shift; added or taken.
struct Term {
    Source        source = Source::constant;
    std::uint32_t index = 0;  // the column or channel; a constant's value
    std::uint32_t multiply = 1, shift = 0;
    bool          negative = false;
};
// A sum: what is added, less what is taken, each side saturating and the difference stopping at 0.
struct Sum {
    std::vector<Term> terms;
};
enum class Compare : std::uint8_t { greater_equal, less_equal, greater, less, equal, not_equal };
struct Test {
    Sum     left, right;
    Compare compare = Compare::greater_equal;
};
enum class Change : std::uint8_t { add, take, set };
struct Write {
    std::uint32_t target = 0;  // a channel; for an adjust law a column
    Change        change = Change::add;
    Sum           amount;
};
// A law as the plan holds it. Everything a carrier needs to run it, or to lower it to a path of its own.
struct Law {
    std::string        name;
    LawKind            kind = LawKind::reaction;
    bool               recipe = false;
    std::string        text;  // its condition as written (a transfer: none)
    std::vector<Test>  when;
    std::vector<Write> writes;
    std::optional<std::uint32_t> flag;    // a channel
    std::optional<std::uint32_t> become;  // a column of kind `material`
    std::optional<std::uint32_t> event;   // an event's name (Program::event_names)
    // A transfer: the channel that flows, the column it flows by, its share.
    std::uint32_t quantity = 0, by = 0, multiply = 1, shift = 6;
};
struct Channel {
    std::string   name, unit, doc;
    std::uint32_t initial = 0;   // as stored: × 2^fraction
    std::uint32_t fraction = 0;
    std::optional<std::uint32_t> from;  // a column
    bool          resets = false;
    std::optional<std::uint32_t> rise, fall;  // event names
};
struct Material {
    std::string              name;
    std::vector<std::string> tags;
};

// ── a carrier, a step ────────────────────────────────────────────────────

// Two instances beside each other, and how much: a lattice's neighbours 1, a contact its area.
struct Pair {
    std::uint32_t first = 0, second = 0, weight = 1;
};
struct Carrier {
    std::size_t           instances = 0;
    std::span<const Pair> pairs;
};
enum class EventKind : std::uint8_t { became, rose, fell };
// What happened to an instance in a step, for whoever plays a sound or keeps a score: it became another
// material (by which law, from what to what), or a channel left 0 or returned to it.
struct Event {
    EventKind     kind = EventKind::became;
    std::uint32_t instance = 0;
    std::uint32_t name = 0;     // Program::event_names; the largest number: unnamed
    std::uint32_t law = 0;      // became: the law
    std::uint32_t channel = 0;  // rose, fell: the channel
    std::uint32_t from = 0, to = 0;  // became: materials; rose, fell: the channel's values
};
struct Report {
    std::uint64_t instances = 0, pairs = 0;
    std::uint64_t held = 0;     // times a reaction or conversion held for an instance
    std::uint64_t became = 0;   // instances that became another material
    std::uint64_t changed = 0;  // instances any of whose channels changed
};
// A step's working space, kept between steps so that a step allocates nothing once it has grown.
class Workspace {
public:
    Workspace() = default;

private:
    friend class Program;
    std::vector<std::uint32_t> near_, gained_, lost_, degree_, row_, plus_, minus_, base_;
    std::vector<std::uint8_t>  set_;
};

class Program {
public:
    Program() = default;

    [[nodiscard]] static std::expected<Program, Failure> prepare(const Definition& definition, Limits limits = {});

    // ── what it holds ──
    [[nodiscard]] std::span<const Column> columns() const noexcept { return columns_; }
    [[nodiscard]] std::span<const Channel> channels() const noexcept { return channels_; }
    [[nodiscard]] std::span<const Material> materials() const noexcept { return materials_; }
    [[nodiscard]] std::span<const Law> laws() const noexcept { return laws_; }
    [[nodiscard]] std::span<const std::string> event_names() const noexcept { return events_; }
    [[nodiscard]] std::optional<std::uint32_t> column(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> channel(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<MaterialId> material(std::string_view name) const noexcept;
    // A material's property as the laws read it (a phase its index, a material its id or `none`), and as written.
    [[nodiscard]] std::uint32_t value(MaterialId material, std::uint32_t column) const noexcept;
    [[nodiscard]] double number(MaterialId material, std::uint32_t column) const noexcept;
    [[nodiscard]] Phase phase(MaterialId material) const noexcept;
    // How many laws are special cases that name materials: the measure that should stay near none.
    [[nodiscard]] std::size_t recipes() const noexcept;
    // The plan as JSON: columns, channels, materials with their values, laws with their kinds — what an
    // agent reads to know what a world is made of.
    [[nodiscard]] std::string describe() const;

    // ── state: channel-major, [channel][instance]; channel 0 is the material ──
    [[nodiscard]] std::size_t values(std::size_t instances) const noexcept { return instances * channels_.size(); }
    // Instances of one material, every channel at its initial value.
    [[nodiscard]] std::vector<std::uint32_t> fresh(std::size_t instances, MaterialId material) const;
    // One instance made anew of a material.
    void place(std::span<std::uint32_t> state, std::size_t instances, std::size_t instance, MaterialId material) const noexcept;

    // One tick: every law read from `input`, the result in `output` (disjoint, the same shape). Events
    // are appended when a vector is given. A failure writes nothing.
    [[nodiscard]] std::expected<Report, Failure> step(Carrier carrier, std::span<const std::uint32_t> input, std::span<std::uint32_t> output,
                                                      Workspace& workspace, std::vector<Event>* events = nullptr) const noexcept;
    // What a new instance of a material has in a channel.
    [[nodiscard]] std::uint32_t initial(MaterialId material, std::uint32_t channel) const noexcept;

private:
    [[nodiscard]] std::uint32_t evaluate(const Sum& sum, std::span<const std::uint32_t> properties, std::span<const std::uint32_t> input,
                                         std::size_t instances, std::size_t instance, std::span<const std::uint32_t> near) const noexcept;
    [[nodiscard]] bool holds(const Law& law, std::span<const std::uint32_t> properties, std::span<const std::uint32_t> input, std::size_t instances,
                             std::size_t instance, std::span<const std::uint32_t> near) const noexcept;

    std::vector<Column>        columns_;
    std::vector<Channel>       channels_;
    std::vector<Material>      materials_;
    std::vector<Law>           laws_;
    std::vector<std::string>   events_;
    std::vector<std::uint32_t> values_;   // [material][column], as the laws read them
    std::vector<double>        numbers_;  // [material][column], as written
    // What is near, that some law reads: each a slot of the workspace — a property or a channel summed over neighbours.
    struct Near {
        Source        source = Source::near_property;
        std::uint32_t index = 0;
    };
    std::vector<Near>          near_;
    std::vector<std::uint32_t> transfers_;  // the laws that are transfers, in order
    std::vector<std::uint32_t> flags_;      // the channels some law flags
    std::uint32_t              phase_column_ = 0;
    bool                       adjusts_ = false;
};

}  // namespace xengine::matter
