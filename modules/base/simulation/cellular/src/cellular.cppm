export module openmui.simulation.cellular;
import std;

export namespace omui::simulation::cellular {
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
enum class ConditionKind : std::uint32_t { self, neighbors };
struct Condition {
    ConditionKind kind = ConditionKind::self;
    Test test;
    std::vector<Offset> offsets;
    Range count;
    [[nodiscard]] static Condition self(Test test) { return {.test=test}; }
    [[nodiscard]] static Condition neighbors(Test test, std::span<const Offset> offsets, Range count) {
        return {.kind=ConditionKind::neighbors,.test=test,.offsets={offsets.begin(),offsets.end()},.count=count};
    }
};
enum class WriteKind : std::uint32_t { set, add_saturating, subtract_saturating };
struct Write { std::uint32_t channel = 0; WriteKind kind = WriteKind::set; std::uint32_t value = 0; };
struct Rule {
    std::vector<Condition> when; // all must match; empty is unconditional
    std::vector<Write> writes;  // unique channels; empty is a first-match barrier
};
struct Limits {
    std::uint32_t max_channels = 16, max_rules = 128, max_words = 65536;
    std::uint32_t max_values = 64 * 1024 * 1024; // per input/output field, uint32 values
};
enum class Error : std::uint8_t { unprepared, channels, condition, range, offset, duplicate_write, limit, shape, alias, tile };
struct Failure { Error code; std::size_t rule = 0, item = 0; };
[[nodiscard]] std::string_view error_name(Error) noexcept;
struct Shape { std::uint32_t width = 0, height = 0; };
struct Tile { std::uint32_t x = 0, y = 0, width = 0, height = 0; };
enum class Edge : std::uint32_t { constant, clamp, wrap };
struct Boundary { Edge edge = Edge::constant; std::uint32_t value = 0; };
struct Execution {
    Shape tile{32,32};
    Boundary boundary;
    bool reverse_tiles = false;
};
struct Statistics { std::uint64_t cells = 0, matched = 0, changed = 0; };

class Program {
public:
    Program() = default;
    Program(const Program&) = default;
    Program& operator=(const Program&) = default;
    Program(Program&& other) noexcept { *this=std::move(other); }
    Program& operator=(Program&& other) noexcept {
        if(this!=&other) {
            channels_=std::exchange(other.channels_,0);halo_=std::exchange(other.halo_,0);rules_=std::exchange(other.rules_,0);
            limits_=other.limits_;words_=std::move(other.words_);
        }
        return *this;
    }
    // Rules are tried in author order; the first matching rule writes this
    // cell. All conditions and writes read the same immutable input version.
    // Every unmentioned channel is copied. Publication/swap belongs to host.
    [[nodiscard]] static std::expected<Program,Failure> prepare(std::uint32_t channels,
        std::span<const Rule> rules, Limits limits = {});
    [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::uint32_t halo() const noexcept { return halo_; }
    [[nodiscard]] std::uint32_t rule_count() const noexcept { return rules_; }
    [[nodiscard]] Limits limits() const noexcept { return limits_; }
    // Read-only, versioned uint32 operator encoding consumed by optional
    // backends. No arbitrary bytecode constructor; prepare validates it once.
    [[nodiscard]] std::span<const std::uint32_t> words() const noexcept { return words_; }
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
    std::uint32_t channels_ = 0, halo_ = 0, rules_ = 0;
    Limits limits_;
    std::vector<std::uint32_t> words_;
};
}
