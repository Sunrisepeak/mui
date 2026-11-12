export module xengine.cellular.api;
import std;
import mlibs.error;

export namespace xengine::cellular {
struct Range {
    std::uint32_t low = 0, high = std::numeric_limits<std::uint32_t>::max();
};
struct Test { std::uint32_t channel = 0; Range value; };
struct Offset {
    std::int32_t x = 0, y = 0;
    bool operator==(const Offset&) const = default;
};
inline constexpr std::array<Offset,4> cardinal{{{-1,0},{1,0},{0,-1},{0,1}}};
inline constexpr std::array<Offset,8> surrounding{{{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}}};
// What a condition asks of a cell: its own value (`self`), or an aggregate over a neighbourhood of one channel —
// how many of the neighbours' values are in `test.value` (`neighbors`), or the sum, the least or the greatest of
// those that are (`sum`, `minimum`, `maximum`; every value by default). The aggregate is in `count` or the
// condition does not hold. A sum saturates; of no value the least is the largest number and the greatest is 0.
enum class ConditionKind : std::uint32_t { self, neighbors, sum, minimum, maximum };
struct Condition {
    ConditionKind kind = ConditionKind::self;
    Test test;
    std::vector<Offset> offsets;
    Range count;
    [[nodiscard]] static Condition self(Test test) { return {.test=test}; }
    [[nodiscard]] static Condition neighbors(Test test, std::span<const Offset> offsets, Range count) {
        return {.kind=ConditionKind::neighbors,.test=test,.offsets={offsets.begin(),offsets.end()},.count=count};
    }
    // An aggregate of `channel` over `offsets` within `result`.
    [[nodiscard]] static Condition sum(std::uint32_t channel, std::span<const Offset> offsets, Range result) {
        return {.kind=ConditionKind::sum,.test={channel,{}},.offsets={offsets.begin(),offsets.end()},.count=result};
    }
    [[nodiscard]] static Condition minimum(std::uint32_t channel, std::span<const Offset> offsets, Range result) {
        return {.kind=ConditionKind::minimum,.test={channel,{}},.offsets={offsets.begin(),offsets.end()},.count=result};
    }
    [[nodiscard]] static Condition maximum(std::uint32_t channel, std::span<const Offset> offsets, Range result) {
        return {.kind=ConditionKind::maximum,.test={channel,{}},.offsets={offsets.begin(),offsets.end()},.count=result};
    }
};
// A term of a write's amount (encoding 2): a source — the cell's own value of a channel, or an aggregate of a
// channel over a neighbourhood, as a condition's — times `multiply`, shifted right by `shift`, added to the amount
// or (`negative`) taken from it. 32-bit unsigned arithmetic throughout, every step saturating, so a device computes
// the same bits: source × multiply stops at the largest number before the shift.
enum class SourceKind : std::uint32_t { self, count, sum, minimum, maximum };
struct Term {
    SourceKind kind = SourceKind::self;
    Test test;                    // the channel; and, for an aggregate, the values that take part (all by default)
    std::vector<Offset> offsets;  // an aggregate's neighbourhood; none for `self`
    std::uint32_t multiply = 1, shift = 0;
    bool negative = false;
    [[nodiscard]] static Term self(std::uint32_t channel, std::uint32_t multiply = 1, std::uint32_t shift = 0) {
        return {.kind=SourceKind::self,.test={channel,{}},.multiply=multiply,.shift=shift};
    }
    [[nodiscard]] static Term sum(std::uint32_t channel, std::span<const Offset> offsets, std::uint32_t multiply = 1, std::uint32_t shift = 0) {
        return {.kind=SourceKind::sum,.test={channel,{}},.offsets={offsets.begin(),offsets.end()},.multiply=multiply,.shift=shift};
    }
    [[nodiscard]] static Term minimum(std::uint32_t channel, std::span<const Offset> offsets, std::uint32_t multiply = 1, std::uint32_t shift = 0) {
        return {.kind=SourceKind::minimum,.test={channel,{}},.offsets={offsets.begin(),offsets.end()},.multiply=multiply,.shift=shift};
    }
    [[nodiscard]] static Term maximum(std::uint32_t channel, std::span<const Offset> offsets, std::uint32_t multiply = 1, std::uint32_t shift = 0) {
        return {.kind=SourceKind::maximum,.test={channel,{}},.offsets={offsets.begin(),offsets.end()},.multiply=multiply,.shift=shift};
    }
    [[nodiscard]] static Term count(Test test, std::span<const Offset> offsets, std::uint32_t multiply = 1, std::uint32_t shift = 0) {
        return {.kind=SourceKind::count,.test=test,.offsets={offsets.begin(),offsets.end()},.multiply=multiply,.shift=shift};
    }
    // The same term, taken from the amount.
    [[nodiscard]] Term taken() const { Term out=*this;out.negative=true;return out; }
};
// A write: the channel's next value from an amount — `value`, and with terms a linear expression of the cell and
// its neighbourhood: plus = value + the positive terms, minus = the negative terms (each sum saturating).
//   set                  next = plus − minus, not below 0
//   add_saturating       next = old + plus (not above the largest number), then − minus (not below 0)
//   subtract_saturating  next = old − plus (not below 0), then + minus (not above the largest number)
enum class WriteKind : std::uint32_t { set, add_saturating, subtract_saturating };
struct Write {
    std::uint32_t channel = 0;
    WriteKind kind = WriteKind::set;
    std::uint32_t value = 0;
    std::vector<Term> terms;
};
struct Rule {
    std::vector<Condition> when; // all must match; empty is unconditional
    std::vector<Write> writes;  // unique channels; empty is a first-match barrier
};
// A transfer (encoding 2): a quantity that flows between a cell and each cell of its neighbourhood, from the one
// that has more to the one that has less — and the amount that leaves one is the amount that arrives at the other,
// because both compute it from the same two values. Between two cells it is
//   (their difference × the pair's conductance × multiply) >> shift
// where the conductance is 1, or with `by` the lesser of the two cells' values of that channel, at most `by_limit`
// (a cell that conducts nothing takes and gives nothing). Preparation holds `by_limit × multiply × neighbours` to
// at most 2^shift: no cell can give more than it has or take more than it can hold, so nothing saturates and what
// the field holds of the quantity is what it held — exactly, where no rule writes the channel in the same step
// (a rule's write comes first; the flows, of the input's values, are added to it and saturate).
// The neighbourhood has with each offset its opposite. At a wrapped edge the quantity goes round; at a clamped one
// nothing crosses (the cell beyond is the cell itself); at a constant one the outside is a bath at that value.
struct Transfer {
    std::uint32_t channel = 0;
    std::vector<Offset> offsets;
    std::uint32_t multiply = 1, shift = 3;
    std::optional<std::uint32_t> by;
    std::uint32_t by_limit = 1;
};
// How a cell may move (the values of an exchange's mobility channel): bits.
inline constexpr std::uint32_t falls = 1;    // it sinks below a lighter cell that may move too — and so rises above a heavier one
inline constexpr std::uint32_t slides = 2;   // resting, it goes diagonally down past a lighter neighbour (a powder's slope)
inline constexpr std::uint32_t spreads = 4;  // resting, it changes places sideways with a lighter neighbour (a liquid's level)
// An exchange (encoding 2): cells that move. The field is cut into blocks of 2 × 2, and the cut shifts each tick
// through its four positions (Execution::tick); within a block cells change places — whole cells: every carried
// channel goes with its cell — by how heavy they are and how they may move, as the input says:
//   1  in each column, a cell that falls sinks below a lighter one that falls;
//   2  a cell that slides, resting, changes places with the lighter cell diagonally below, past a lighter one beside it;
//   3  in each row, a cell that spreads changes places with a lighter one that falls — or not, with equal chance,
//      by a number of (seed, tick, block, row) alone, so the same field at the same tick moves the same way.
// A cell moves once a tick. Each cell is in one block, so no two contend for a place and blocks may be done in
// any order or at once; what moves is conserved by construction. A block that reaches over the edge sits the
// tick out (at a wrapped edge of an even size it goes round). The kernel knows weights and mobilities, no
// material: sand, water, oil and smoke are an application's values of the two channels.
struct Exchange {
    std::uint32_t weight = 0, mobility = 0;  // the channels
    std::vector<std::uint32_t> carried;      // the channels that go with a cell; none given: every channel
    std::uint32_t seed = 0;
};
// A program's stages: the rules (a cell's own transitions) and the transfers (what flows between cells) read the
// same immutable input; then an exchange, decided from that input too, moves what they made of each cell.
struct Stages {
    std::span<const Rule> rules;
    std::span<const Transfer> transfers;
    std::span<const Exchange> exchanges;  // one at most
};
struct Limits {
    std::uint32_t max_channels = 16, max_rules = 128, max_words = 65536;
    std::uint32_t max_values = 64 * 1024 * 1024; // per input/output field, uint32 values
};
enum class Errc : std::uint8_t { unprepared, channels, condition, range, offset, duplicate_write, limit, shape, alias, tile, invalid_limits, transfer, exchange, whole_field };
// The codes as the error contract describes them (mlibs.error).
[[nodiscard]] constexpr mlibs::error::Description describe(Errc e) noexcept {
    using Kind = mlibs::error::Kind;  // not `using mlibs::error::Kind;`: that is an ICE in GCC 16 (import_entity_module)
    switch (e) {
        case Errc::unprepared: return {"cellular.unprepared", Kind::invalid_argument, "the program was not prepared"};
        case Errc::channels: return {"cellular.channels", Kind::invalid_argument, "a channel out of range"};
        case Errc::condition: return {"cellular.condition", Kind::invalid_argument, "a malformed condition or write"};
        case Errc::range: return {"cellular.range", Kind::invalid_argument, "a range whose low is above its high"};
        case Errc::offset: return {"cellular.offset", Kind::invalid_argument, "a neighbour offset beyond ±32 or repeated"};
        case Errc::duplicate_write: return {"cellular.duplicate-write", Kind::invalid_argument, "two writes to one channel"};
        case Errc::limit: return {"cellular.limit", Kind::refused, "beyond the program's limits"};
        case Errc::invalid_limits: return {"cellular.invalid-limits", Kind::invalid_argument, "limits that cannot hold a program"};
        case Errc::shape: return {"cellular.shape", Kind::invalid_argument, "fields that do not match the shape"};
        case Errc::alias: return {"cellular.alias", Kind::invalid_argument, "input and output overlap"};
        case Errc::tile: return {"cellular.tile", Kind::invalid_argument, "a tile outside the field or empty"};
        case Errc::transfer: return {"cellular.transfer", Kind::invalid_argument, "a transfer that is not symmetric, or that could take more than a cell has"};
        case Errc::exchange: return {"cellular.exchange", Kind::invalid_argument, "an exchange that carries a channel twice, or not its own weight and mobility"};
        case Errc::whole_field: return {"cellular.whole-field", Kind::invalid_argument, "a program that moves cells steps the whole field, not a tile of it"};
    }
    return {"cellular.?", Kind::internal};
}
// Where in a program a failure is: the rule, and the item (a condition or a write) in it — or the transfer.
struct Location {
    std::size_t rule = 0, item = 0;
    std::optional<std::size_t> transfer, exchange;
};
[[nodiscard]] inline std::string describe_where(const Location& l) {
    if (l.exchange) return std::format("exchange {}", *l.exchange);
    return l.transfer ? std::format("transfer {}", *l.transfer) : std::format("rule {}, item {}", l.rule, l.item);
}
using Failure = mlibs::error::Error<Errc, Location>;
struct Shape { std::uint32_t width = 0, height = 0; };
struct Tile { std::uint32_t x = 0, y = 0, width = 0, height = 0; };
enum class Edge : std::uint32_t { constant, clamp, wrap };
struct Boundary { Edge edge = Edge::constant; std::uint32_t value = 0; };
struct Execution {
    Shape tile{32,32};
    Boundary boundary;
    bool reverse_tiles = false;
    // Optional: one byte a tile, row by row (tiles(shape, tile) of them), set
    // to 1 where a cell of the tile changed this step and 0 where none did —
    // what a presenter re-uploads and a renderer redraws (D-132).
    std::span<std::uint8_t> changed_tiles = {};
    // Which step this is, for a program that moves cells: where the blocks are cut, and its choices.
    std::uint64_t tick = 0;
};
// How many tiles of `tile` cover `shape`: columns, rows.
[[nodiscard]] constexpr Shape tiles(Shape shape, Shape tile) noexcept {
    if (!tile.width || !tile.height || !shape.width || !shape.height) return {};
    return {1 + (shape.width - 1) / tile.width, 1 + (shape.height - 1) / tile.height};
}
struct Statistics { std::uint64_t cells = 0, matched = 0, changed = 0; };

// A field's colours (D-132): the first swatch whose tests a cell passes gives
// its pixel — 32 bits as a renderer stores them (premultiplied RGBA8, red in
// the lowest byte); none: the fallback. The one rule every presenter keeps,
// on the CPU (cellular.present) and on a device (cellular.rhi's Painter).
struct PixelSwatch {
    std::vector<Test> when;
    std::uint32_t pixel = 0;
};
// The pixel of cell `index` of `cells` (channel-major, `area` cells a channel).
[[nodiscard]] inline std::uint32_t pixel_of(std::span<const PixelSwatch> swatches, std::uint32_t fallback,
                                            std::span<const std::uint32_t> cells, std::size_t area, std::size_t index) noexcept {
    for (const auto& swatch : swatches) {
        bool all = true;
        for (const auto& test : swatch.when) {
            const auto value = cells[test.channel * area + index];
            if (value < test.value.low || value > test.value.high) { all = false; break; }
        }
        if (all) return swatch.pixel;
    }
    return fallback;
}

class Program {
public:
    Program() = default;
    Program(const Program&) = default;
    Program& operator=(const Program&) = default;
    Program(Program&& other) noexcept { *this=std::move(other); }
    Program& operator=(Program&& other) noexcept {
        if(this!=&other) {
            channels_=std::exchange(other.channels_,0);halo_=std::exchange(other.halo_,0);rules_=std::exchange(other.rules_,0);transfers_=std::exchange(other.transfers_,0);moves_=std::exchange(other.moves_,false);
            limits_=other.limits_;words_=std::move(other.words_);
        }
        return *this;
    }
    // Rules are tried in author order; the first matching rule writes this
    // cell. All conditions and writes read the same immutable input version.
    // Every unmentioned channel is copied. Publication/swap belongs to host.
    [[nodiscard]] static std::expected<Program,Failure> prepare(std::uint32_t channels,
        std::span<const Rule> rules, Limits limits = {});
    // The same with the stages after the rules (a program with a transfer is of encoding 2).
    [[nodiscard]] static std::expected<Program,Failure> prepare(std::uint32_t channels, Stages stages, Limits limits = {});
    [[nodiscard]] std::uint32_t transfer_count() const noexcept { return transfers_; }
    // Whether cells move (an exchange): such a program steps the whole field, by Execution::tick.
    [[nodiscard]] bool moves() const noexcept { return moves_; }
    [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::uint32_t halo() const noexcept { return halo_; }
    [[nodiscard]] std::uint32_t rule_count() const noexcept { return rules_; }
    [[nodiscard]] Limits limits() const noexcept { return limits_; }
    // Read-only, versioned uint32 operator encoding consumed by optional
    // backends. No arbitrary bytecode constructor; prepare validates it once.
    // Its first word is the encoding: 1 for rules of states, thresholds and
    // counted neighbourhoods with constant writes — what every backend runs —
    // and 2 when a rule aggregates (sum, minimum, maximum), a write has terms, or there is a transfer or an exchange.
    [[nodiscard]] std::span<const std::uint32_t> words() const noexcept { return words_; }
    [[nodiscard]] std::uint32_t encoding() const noexcept { return words_.empty() ? 0 : words_[0]; }
    [[nodiscard]] std::expected<std::size_t,Failure> values(Shape shape) const noexcept;
    // Channel-major uint32 fields: [channel][y][x]. Input/output must be
    // disjoint and exactly shaped; failures never touch output. No allocations.
    [[nodiscard]] std::expected<Statistics,Failure> step(Shape shape,
        std::span<const std::uint32_t> input, std::span<std::uint32_t> output, Execution = {}) const noexcept;
    // Independent output tile; reads its logical halo from the full immutable
    // input. Hosts may batch disjoint tiles in taskflow. Outside output stays
    // untouched. This is not streamed/sparse physical halo exchange.
    [[nodiscard]] std::expected<Statistics,Failure> step_tile(Shape shape, Tile tile,
        std::span<const std::uint32_t> input, std::span<std::uint32_t> output, Boundary = {}) const noexcept;
private:
    [[nodiscard]] std::expected<void,Failure> validate(Shape, std::span<const std::uint32_t>,
        std::span<std::uint32_t>, Boundary) const noexcept;
    [[nodiscard]] Statistics run_tile(Shape, Tile, std::span<const std::uint32_t>,
        std::span<std::uint32_t>, Boundary) const noexcept;
    void exchange(Shape, std::span<const std::uint32_t>, std::span<std::uint32_t>, Boundary, std::uint64_t tick) const noexcept;
    std::uint32_t channels_ = 0, halo_ = 0, rules_ = 0, transfers_ = 0;
    bool moves_ = false;
    Limits limits_;
    std::vector<std::uint32_t> words_;
};
}

