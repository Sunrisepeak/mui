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
    // With a table (encoding 3): the conductance is a column of it, looked up by each cell's key — and a cell
    // whose key is no row conducts nothing, with or without a column.
    std::optional<std::uint32_t> by_column;
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
    std::uint32_t weight = 0, mobility = 0;  // the channels — or, `by_table`, the table's columns
    std::vector<std::uint32_t> carried;      // the channels that go with a cell; none given: every channel
    std::uint32_t seed = 0;
    bool by_table = false;  // encoding 3: how heavy a cell is and how it may move are looked up by its key (no row: it stays)
};
// ── encoding 3: a table, and reactions (D-167) ───────────────────────────
// What a cell is, looked up: one channel is a key into a table of rows × columns, and a value a reaction reads
// may be a column of the cell's row — or of its neighbours' rows, summed. A cell whose key is no row takes no
// part: no reaction acts on it, nothing flows to or from it, and it reads as nothing to its neighbours. The
// kernel knows a key and numbers; that a row is a material is whoever wrote the table's.
struct Table {
    std::uint32_t key = 0;      // the channel
    std::uint32_t columns = 0;
    std::vector<std::uint32_t> values;  // [row][column]; rows = values.size() / columns
};
// What a term of an amount reads: a number, a channel of the cell, a column of its row as written (`lookup`) or
// as the adjustments leave it (`derived`: an index into Reactions::derived), or the neighbourhood's sum of a
// channel or of a column.
enum class Read : std::uint32_t { constant, channel, lookup, derived, near_channel, near_lookup };
// One term: the value read, × multiply, >> shift; added or taken.
struct Operand {
    Read read = Read::constant;
    std::uint32_t index = 0;  // a constant: its value
    std::uint32_t multiply = 1, shift = 0;
    bool negative = false;
};
// A sum: what is added less what is taken, each side saturating, the difference stopping at 0.
using Amount = std::vector<Operand>;
enum class Compare : std::uint32_t { greater_equal, less_equal, greater, less, equal, not_equal };
struct Comparison {
    Amount left, right;
    Compare compare = Compare::greater_equal;
};
enum class ChangeKind : std::uint32_t { add, take, set };
struct Change {
    std::uint32_t channel = 0;
    ChangeKind kind = ChangeKind::add;
    Amount amount;
};
// A derived value changed where a condition holds — judged, like its amount, on the row as written.
struct Adjustment {
    std::vector<Comparison> when;
    std::uint32_t derived = 0;
    bool raise = true;
    Amount amount;
};
// Every reaction whose condition holds acts (not the first only): what they add and take accumulates, the first
// to set a channel gives its base, a flag's channel is 1 while one that flags it holds and 0 while none does,
// and the first with `become` whose column names another row makes the cell that row.
struct Reaction {
    std::vector<Comparison> when;
    std::vector<Change> changes;
    std::optional<std::uint32_t> flag;    // a channel
    std::optional<std::uint32_t> become;  // a column holding a row
};
// A channel that starts again when a cell becomes another row: at the new row's value of a column where that
// is not 0, else at `initial`.
struct Reset {
    std::uint32_t channel = 0, initial = 0;
    std::optional<std::uint32_t> from;
};
// The reactions stage: after the rules, of the same input. A cell's channel is then
//   base (the first set; else what the rules made of it) + added − taken, then what flowed out and in;
// and a cell that becomes another row has its key changed and its resets applied.
struct Reactions {
    Table table;
    std::vector<Offset> near;            // the neighbourhood the near_* sums are over; with each offset its opposite
    std::vector<std::uint32_t> derived;  // each a column: the value looked up, then adjusted
    std::vector<Adjustment> adjustments;
    std::vector<Reaction> reactions;
    std::vector<Reset> resets;
};
// A program's stages: the rules (a cell's own transitions) and the transfers (what flows between cells) read the
// same immutable input; then an exchange, decided from that input too, moves what they made of each cell.
struct Stages {
    std::span<const Rule> rules;
    std::span<const Transfer> transfers;
    std::span<const Exchange> exchanges;  // one at most
    const Reactions* reactions = nullptr;  // encoding 3
};
struct Limits {
    std::uint32_t max_channels = 16, max_rules = 128, max_words = 65536;
    std::uint32_t max_values = 64 * 1024 * 1024; // per input/output field, uint32 values
};
enum class Errc : std::uint8_t { unprepared, channels, condition, range, offset, duplicate_write, limit, shape, alias, tile, invalid_limits, transfer, exchange, whole_field, table, reaction };
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
        case Errc::table: return {"cellular.table", Kind::invalid_argument, "a table that is not rows of its columns, or a column or key that is not there"};
        case Errc::reaction: return {"cellular.reaction", Kind::invalid_argument, "a reaction that reads or changes what is not there, or changes a channel twice"};
    }
    return {"cellular.?", Kind::internal};
}
// Where in a program a failure is: the rule, and the item (a condition or a write) in it — or the transfer.
struct Location {
    std::size_t rule = 0, item = 0;
    std::optional<std::size_t> transfer, exchange;
    std::optional<std::size_t> reaction, adjustment;
};
[[nodiscard]] inline std::string describe_where(const Location& l) {
    if (l.reaction) return std::format("reaction {}", *l.reaction);
    if (l.adjustment) return std::format("adjustment {}", *l.adjustment);
    if (l.exchange) return std::format("exchange {}", *l.exchange);
    return l.transfer ? std::format("transfer {}", *l.transfer) : std::format("rule {}, item {}", l.rule, l.item);
}
using Failure = mlibs::error::Error<Errc, Location>;
struct Shape { std::uint32_t width = 0, height = 0; };
struct Tile { std::uint32_t x = 0, y = 0, width = 0, height = 0; };
enum class Edge : std::uint32_t { constant, clamp, wrap };
// What is beyond the field's edge. `constant`: one value for every channel — or, with `values` (one a channel;
// the caller's, alive for the step), a cell of its own: a bath the flows reach and the conditions see, the same
// at every edge (an open sky is air at its temperature). Cells do not leave the field either way.
struct Boundary {
    Edge edge = Edge::constant;
    std::uint32_t value = 0;
    std::span<const std::uint32_t> values;
};
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
    // Optional — sparse execution (D-167): one byte a tile, as changed_tiles, that the caller keeps between the
    // steps of one field (zeroed: every tile awake). The step reads it and writes it: a tile at rest — nothing
    // in it or within the program's reach of it changed, and nothing in it could have moved — is asleep
    // (asleep(byte)) and is not run; its cells are the input's. What changes within reach of it wakes it for the
    // next step. The output is the same bits as without. Whoever writes cells from outside wakes them
    // (Program::wake); a field begun anew, another program, tile or edge: zero it.
    std::span<std::uint8_t> activity = {};
    // With `activity`: the output is the buffer this field's input of one step ago was in (two buffers taking
    // turns), so a sleeping tile's cells are already there and are not copied — a step costs what is awake.
    bool alternate = false;
};
// Whether a tile's byte of Execution::activity says it sleeps.
[[nodiscard]] constexpr bool asleep(std::uint8_t activity) noexcept { return (activity & 0x0f) == 0x0f; }
// How many tiles of `tile` cover `shape`: columns, rows.
[[nodiscard]] constexpr Shape tiles(Shape shape, Shape tile) noexcept {
    if (!tile.width || !tile.height || !shape.width || !shape.height) return {};
    return {1 + (shape.width - 1) / tile.width, 1 + (shape.height - 1) / tile.height};
}
// What a step did: the cells it ran (all of them, but for tiles asleep), those a rule matched, those that
// changed, and the tiles it left asleep.
struct Statistics { std::uint64_t cells = 0, matched = 0, changed = 0, asleep = 0; };
// What a quantity does from outside to a rectangle of cells: over the cells just beyond its left side less those
// just beyond its right (x), and those above less those below (y), the sum of `values` — one channel's cells. A
// pressure's net push on what is inside, in the channel's units: positive x is toward greater x, positive y
// toward greater y. Cells beyond the field are nothing. An observation: what stands in the field (a body, which
// the kernel does not know) takes it as it will.
struct Push {
    std::int64_t x = 0, y = 0;
};
[[nodiscard]] Push push_on(Shape shape, std::span<const std::uint32_t> values, Tile region) noexcept;

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
            staged_=std::exchange(other.staged_,{});
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
    // Whether it has a table and reactions (encoding 3), and how many rows the table has.
    [[nodiscard]] bool reacts() const noexcept { return staged_.at != 0; }
    [[nodiscard]] std::uint32_t rows() const noexcept { return staged_.rows; }
    [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::uint32_t halo() const noexcept { return halo_; }
    // How far a cell's next value reads, in cells: the halo, and one cell where cells move.
    [[nodiscard]] std::uint32_t reach() const noexcept { return std::max(halo_, moves_ ? 1u : 0u); }
    [[nodiscard]] std::uint32_t rule_count() const noexcept { return rules_; }
    [[nodiscard]] Limits limits() const noexcept { return limits_; }
    // Read-only, versioned uint32 operator encoding consumed by optional
    // backends. No arbitrary bytecode constructor; prepare validates it once.
    // Its first word is the encoding: 1 for rules of states, thresholds and
    // counted neighbourhoods with constant writes — what every backend runs —
    // 2 when a rule aggregates (sum, minimum, maximum), a write has terms, or there is a transfer or an exchange,
    // and 3 with a table and reactions.
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
    // Cells of `edited` were written from outside a step: the tiles of `activity` (Execution::activity, tiles of
    // `tile`) they are in or within reach of are awake again. Nothing where the region is not in the field.
    void wake(Shape shape, Shape tile, Tile edited, std::span<std::uint8_t> activity, Edge edge = Edge::constant) const noexcept;
private:
    [[nodiscard]] std::expected<void,Failure> validate(Shape, std::span<const std::uint32_t>,
        std::span<std::uint32_t>, Boundary) const noexcept;
    [[nodiscard]] Statistics run_tile(Shape, Tile, std::span<const std::uint32_t>,
        std::span<std::uint32_t>, Boundary) const noexcept;
    void exchange(Shape, std::span<const std::uint32_t>, std::span<std::uint32_t>, const Execution&) const noexcept;
    // Encoding 3: where the reactions' parts are in the words (`at` 0: none).
    struct Staged {
        std::uint32_t at = 0, key = 0, rows = 0, columns = 0, table = 0, near = 0, slots = 0, slot_count = 0, derived = 0, derived_count = 0, adjustments = 0, reactions = 0,
                      flags = 0, resets = 0;
    };
    // The reactions checked and written into the words; `halo` grows to their neighbourhood.
    [[nodiscard]] std::expected<void, Failure> encode(const Reactions& reactions, std::uint32_t& halo);
    // One cell's reactions, onto what the rules made of it in `out`: the row it becomes, or the largest number.
    [[nodiscard]] std::uint32_t react(Shape, std::uint32_t x, std::uint32_t y, std::span<const std::uint32_t> in, std::span<std::uint32_t> out, Boundary,
                                      Statistics&) const noexcept;
    Staged staged_;
    std::uint32_t channels_ = 0, halo_ = 0, rules_ = 0, transfers_ = 0;
    bool moves_ = false;
    Limits limits_;
    std::vector<std::uint32_t> words_;
};
}

// The implementation's own, shared by its units (not exported).
namespace xengine::cellular::detail {
inline constexpr std::uint32_t largest = std::numeric_limits<std::uint32_t>::max();
// A table's column in a word that is otherwise a channel: this bit.
inline constexpr std::uint32_t of_table = 0x80000000u;
[[nodiscard]] constexpr std::uint32_t saturating_add(std::uint32_t a, std::uint32_t b) noexcept { return a + std::min(b, largest - a); }
[[nodiscard]] constexpr std::uint32_t saturating_multiply(std::uint32_t a, std::uint32_t by) noexcept { return by && a > largest / by ? largest : a * by; }
// The words of a neighbourhood: its size, then each offset as two words (x + 32, y + 32).
void put_offsets(std::vector<std::uint32_t>& words, std::span<const Offset> offsets);
// A neighbourhood as a program takes it: at most 128 offsets, each within ±32, none twice. What is wrong with it,
// and how far it reaches.
[[nodiscard]] std::optional<Errc> check_offsets(std::span<const Offset> offsets, std::uint32_t& halo);
}  // namespace xengine::cellular::detail
