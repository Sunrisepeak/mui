import std;
import mlibs.simulation;
import xengine.cellular;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace ca=xengine::cellular;
namespace {
std::vector<ca::Rule> reaction_rules() {
    using C=ca::Condition;using W=ca::WriteKind;
    return {
        {{C::self({1,{1,1}}),C::self({2,{3,0xffffffffu}})},{{1,W::set,2}}},
        {{C::self({1,{1,1}})},{{2,W::add_saturating,1}}},
        {{C::self({0,{1,1}}),C::self({1,{0,0}}),C::neighbors({1,{1,1}},ca::cardinal,{1,4})},{{1,W::set,1},{2,W::set,0}}}
    };
}
std::vector<std::uint32_t> initial(ca::Shape shape) {
    const auto n=shape.width*shape.height;std::vector<std::uint32_t> a(n*3);
    std::fill_n(a.begin(),n,1u);
    for(std::uint32_t y=0;y<shape.height;++y)a[y*shape.width+shape.width/2]=0;
    a[n+(shape.height/2)*shape.width+2]=1;
    return a;
}
// Independent scalar fixture oracle; no Program encoding, tile loop or helpers.
std::vector<std::uint32_t> reference(ca::Shape shape,const std::vector<std::uint32_t>& a) {
    auto b=a;const int w=static_cast<int>(shape.width),h=static_cast<int>(shape.height),n=w*h;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const int i=y*w+x;
        if(a[static_cast<std::size_t>(n+i)]==1) {
            if(a[static_cast<std::size_t>(2*n+i)]>=3)b[static_cast<std::size_t>(n+i)]=2;
            else ++b[static_cast<std::size_t>(2*n+i)];
        } else if(a[static_cast<std::size_t>(i)]==1 && a[static_cast<std::size_t>(n+i)]==0) {
            bool active=false;
            for(const auto [dx,dy]:std::array<std::pair<int,int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}})
                if(x+dx>=0 && x+dx<w && y+dy>=0 && y+dy<h)active|=a[static_cast<std::size_t>(n+(y+dy)*w+x+dx)]==1;
            if(active){b[static_cast<std::size_t>(n+i)]=1;b[static_cast<std::size_t>(2*n+i)]=0;}
        }
    }
    return b;
}
struct Model {
    struct State {ca::Program program;ca::Shape shape;std::vector<std::uint32_t> current,next;};
    struct Input {bool ignite=false;std::uint32_t x=0,y=0;};
    static void step(State& s,const Input& in) {
        if(in.ignite && in.x<s.shape.width && in.y<s.shape.height)s.current[s.shape.width*s.shape.height+in.y*s.shape.width+in.x]=1;
        auto result=s.program.step(s.shape,s.current,s.next);expect(result.has_value());s.current.swap(s.next);
    }
};
}
// A world of cells that move, for the exchange's cases: channel 0 how heavy a cell is, 1 how it may move, 2 a
// number of its own (it goes with it), 3 a mark on the place (it stays). The kinds are this test's, not the
// kernel's: it knows weights and mobilities only.
struct Kind {
    std::uint32_t weight, mobility;
};
constexpr Kind air{10, ca::falls}, sand{100, ca::falls | ca::slides}, water{50, ca::falls | ca::slides | ca::spreads},
    oil{40, ca::falls | ca::slides | ca::spreads}, smoke{2, ca::falls | ca::spreads}, stone{1000, 0};
struct World {
    ca::Shape shape;
    std::vector<std::uint32_t> cells, next;
    ca::Program program;
    std::uint64_t tick = 0;
    World(ca::Shape size, std::uint32_t seed = 7) : shape(size), cells(static_cast<std::size_t>(size.width) * size.height * 4), next(cells.size()) {
        const std::array<ca::Exchange, 1> moving{{{.weight = 0, .mobility = 1, .carried = {0, 1, 2}, .seed = seed}}};
        program = *ca::Program::prepare(4, ca::Stages{.exchanges = moving});
        for (std::uint32_t y = 0; y < size.height; ++y)
            for (std::uint32_t x = 0; x < size.width; ++x) put(x, y, air);
    }
    [[nodiscard]] std::size_t area() const { return static_cast<std::size_t>(shape.width) * shape.height; }
    [[nodiscard]] std::size_t at(std::uint32_t x, std::uint32_t y) const { return static_cast<std::size_t>(y) * shape.width + x; }
    void put(std::uint32_t x, std::uint32_t y, Kind kind) {
        cells[at(x, y)] = kind.weight;
        cells[area() + at(x, y)] = kind.mobility;
        cells[2 * area() + at(x, y)] = static_cast<std::uint32_t>(at(x, y)) + 1000;  // whose cell it was at the start
        cells[3 * area() + at(x, y)] = x * 3 + y;                                    // of the place
    }
    [[nodiscard]] std::uint32_t weight(std::uint32_t x, std::uint32_t y) const { return cells[at(x, y)]; }
    [[nodiscard]] bool is(std::uint32_t x, std::uint32_t y, Kind kind) const { return weight(x, y) == kind.weight; }
    // One step; how many cells changed.
    std::uint64_t step(ca::Edge edge = ca::Edge::clamp, ca::Shape tile = {32, 32}, bool reverse = false) {
        const auto stepped = program.step(shape, cells, next, {.tile = tile, .boundary = {edge, 0}, .reverse_tiles = reverse, .tick = tick++});
        expect(fatal(stepped.has_value()));
        cells.swap(next);
        return stepped->changed;
    }
    // The whole cells there are, whatever their places: what an exchange must keep.
    [[nodiscard]] std::vector<std::array<std::uint32_t, 3>> census() const {
        std::vector<std::array<std::uint32_t, 3>> all;
        for (std::size_t cell = 0; cell < area(); ++cell) all.push_back({cells[cell], cells[area() + cell], cells[2 * area() + cell]});
        std::ranges::sort(all);
        return all;
    }
    [[nodiscard]] std::string picture() const {
        std::string out;
        for (std::uint32_t y = 0; y < shape.height; ++y) {
            for (std::uint32_t x = 0; x < shape.width; ++x)
                out += is(x, y, air) ? '.' : is(x, y, sand) ? 's' : is(x, y, water) ? '~' : is(x, y, oil) ? 'o' : is(x, y, smoke) ? '^' : '#';
            out += '\n';
        }
        return out;
    }
};

// One field stepped two ways from the same cells: every tile run, and sparsely (Execution::activity) — which
// must be the same bits, step after step, whatever sleeps.
struct Twice {
    const ca::Program& program;
    ca::Shape shape, tile;
    ca::Boundary boundary;
    bool alternate = false;
    std::vector<std::uint32_t> dense, sparse, dense_next, sparse_next;
    std::vector<std::uint8_t> activity, dense_changed, sparse_changed;
    std::uint64_t tick = 0, cells_run = 0, tiles_slept = 0;
    ca::Statistics last;
    Twice(const ca::Program& of, ca::Shape size, ca::Shape tiles_of, ca::Boundary edge, std::vector<std::uint32_t> cells, bool in_turn = false)
        : program(of), shape(size), tile(tiles_of), boundary(edge), alternate(in_turn), dense(cells), sparse(std::move(cells)),
          dense_next(dense.size()), sparse_next(dense.size(), 0xdeadbeefu) {
        const auto grid = ca::tiles(shape, tile);
        activity.assign(static_cast<std::size_t>(grid.width) * grid.height, 0);
        dense_changed.assign(activity.size(), 0);
        sparse_changed.assign(activity.size(), 0);
    }
    [[nodiscard]] std::size_t asleep() const { return static_cast<std::size_t>(std::ranges::count_if(activity, [](std::uint8_t byte) { return ca::asleep(byte); })); }
    // A cell written from outside, in both; the sparse one is told.
    void write(std::uint32_t channel, std::uint32_t x, std::uint32_t y, std::uint32_t value) {
        const std::size_t index = static_cast<std::size_t>(channel) * shape.width * shape.height + static_cast<std::size_t>(y) * shape.width + x;
        dense[index] = sparse[index] = value;
        program.wake(shape, tile, {x, y, 1, 1}, activity, boundary.edge);
    }
    // One step of both; whether they agree: the cells, how many changed, and the tiles that did.
    bool step(bool reverse = false) {
        const auto one = program.step(shape, dense, dense_next, {.tile = tile, .boundary = boundary, .reverse_tiles = reverse, .changed_tiles = dense_changed, .tick = tick});
        const auto other = program.step(shape, sparse, sparse_next,
            {.tile = tile, .boundary = boundary, .reverse_tiles = reverse, .changed_tiles = sparse_changed, .tick = tick, .activity = activity, .alternate = alternate});
        ++tick;
        if (!one || !other) return false;
        dense.swap(dense_next);
        sparse.swap(sparse_next);
        last = *other;
        cells_run += other->cells;
        tiles_slept += other->asleep;
        return dense == sparse && one->changed == other->changed && dense_changed == sparse_changed &&
               other->cells <= one->cells && std::ranges::none_of(activity, [](std::uint8_t byte) { return byte & 0xf0; });
    }
};

// Encoding 3's world for the cases below: a key (channel 0) into a table of three rows, and what the rows' cells
// carry — a level (1), heat (2), a flag (3), an age no reaction touches (4). The columns: what a cell gains a
// step, the limit it grows to, the row it becomes, where its level starts again, how much it glows to its
// neighbours. The names are this test's: the kernel knows a key and numbers.
constexpr std::uint32_t ember = 0, ash = 1, calm = 2, no_row = 77;
ca::Amount number(std::uint32_t value) { return {{ca::Read::constant, value}}; }
ca::Amount value_of(ca::Read read, std::uint32_t index) { return {{read, index}}; }
ca::Reactions embers(std::uint32_t ash_starts_at = 0) {
    using R = ca::Read;
    using C = ca::Compare;
    ca::Reactions staged;
    staged.table = {.key = 0, .columns = 5, .values = {5, 20, ash, 7, 3, /* ash */ 0, 0, ash, ash_starts_at, 0, /* calm */ 0, 0, calm, 0, 1}};
    staged.near = {ca::cardinal.begin(), ca::cardinal.end()};
    staged.derived = {1};  // the limit, as heat leaves it
    staged.adjustments = {{.when = {{value_of(R::channel, 2), number(50), C::greater}}, .derived = 0, .raise = false, .amount = number(5)}};
    staged.reactions = {
        // It grows while under its limit, and is flagged while it does.
        {.when = {{value_of(R::channel, 1), value_of(R::derived, 0), C::less}}, .changes = {{1, ca::ChangeKind::add, value_of(R::lookup, 0)}}, .flag = 3},
        // From 10 on it also cools, and grows one more: added to what the first adds.
        {.when = {{value_of(R::channel, 1), number(10), C::greater_equal}}, .changes = {{2, ca::ChangeKind::take, number(2)}, {1, ca::ChangeKind::add, number(1)}}},
        // Beside enough glow, its heat is the levels beside it.
        {.when = {{value_of(R::near_lookup, 4), number(4), C::greater_equal}}, .changes = {{2, ca::ChangeKind::set, value_of(R::near_channel, 1)}}},
        // At its limit it becomes what its row says.
        {.when = {{value_of(R::channel, 1), value_of(R::derived, 0), C::greater_equal}, {value_of(R::lookup, 1), number(0), C::greater}}, .become = 2},
        // A second set of the same channel: the first stands.
        {.when = {{value_of(R::near_lookup, 4), number(4), C::greater_equal}}, .changes = {{2, ca::ChangeKind::set, number(99)}}},
    };
    staged.resets = {{.channel = 1, .initial = 1, .from = 3}};
    return staged;
}
// What preparing says of a staged program: accepted, or its code and where.
std::string staged_refusal(const ca::Reactions& staged, std::uint32_t channels = 5, std::vector<ca::Transfer> transfers = {}, std::vector<ca::Exchange> exchanges = {}) {
    const auto program = ca::Program::prepare(channels, ca::Stages{.transfers = transfers, .exchanges = exchanges, .reactions = &staged});
    return program ? std::string("accepted") : std::format("{} at {}", program.error().description().name, ca::describe_where(program.error().where));
}

UTest cases{"simulation.cellular", [] {
    "state/threshold/neighborhood rules agree with a scalar oracle across tile sizes and both orders"_test = [] {
        const ca::Shape shape{19,11};auto rules=reaction_rules();auto prepared=ca::Program::prepare(3,rules);
        expect(prepared.has_value());if(!prepared)return;
        rules.clear();expect(prepared->halo()==1 && prepared->channels()==3);
        for(auto tile:std::array<ca::Shape,4>{{{1,1},{7,3},{32,32},{19,11}}})for(bool reverse:{false,true}) {
            auto state=initial(shape),next=state;
            for(int frame=0;frame<24;++frame) {
                const auto expected=reference(shape,state);
                const auto step=prepared->step(shape,state,next,{.tile=tile,.reverse_tiles=reverse});
                expect(step && step->cells==209 && next==expected);
                state.swap(next);
            }
            // The inert separator prevents the wave crossing into the right half.
            for(std::uint32_t y=0;y<shape.height;++y)for(std::uint32_t x=10;x<shape.width;++x)expect(state[209+y*shape.width+x]==0);
        }
    };
    "the changed-tile mask marks exactly the tiles where a cell changed; a mask of the wrong size is refused"_test = [] {
        const ca::Shape shape{19,11};const auto rules=reaction_rules();const auto prepared=ca::Program::prepare(3,rules);
        expect(prepared.has_value());if(!prepared)return;
        for(const auto tile:std::array<ca::Shape,3>{{{4,4},{7,3},{19,11}}}) {
            const auto grid=ca::tiles(shape,tile);
            auto state=initial(shape),next=state;
            std::vector<std::uint8_t> mask(grid.width*grid.height,7);
            for(int frame=0;frame<16;++frame) {
                expect(prepared->step(shape,state,next,{.tile=tile,.changed_tiles=mask}).has_value());
                for(std::uint32_t ty=0;ty<grid.height;++ty)for(std::uint32_t tx=0;tx<grid.width;++tx) {
                    bool differs=false;
                    for(std::uint32_t c=0;c<3;++c)for(std::uint32_t y=ty*tile.height;y<std::min(shape.height,(ty+1)*tile.height);++y)
                        for(std::uint32_t x=tx*tile.width;x<std::min(shape.width,(tx+1)*tile.width);++x) {
                            const auto i=c*209+y*shape.width+x;differs|=state[i]!=next[i];
                        }
                    expect(mask[ty*grid.width+tx]==(differs?1:0)) << std::format("tile {},{} of {}x{} at frame {}",tx,ty,tile.width,tile.height,frame);
                }
                state.swap(next);
            }
            std::vector<std::uint8_t> wrong(grid.width*grid.height+1);
            const auto refused=prepared->step(shape,state,next,{.tile=tile,.changed_tiles=wrong});
            expect(!refused && refused.error().code==ca::Errc::tile);
        }
        expect(ca::tiles({1024,1024},{32,32}).width==32 && ca::tiles({1000,7},{32,32}).width==32 && ca::tiles({1000,7},{32,32}).height==1);
    };
    "partial tile writes leave the rest untouched; constant/clamp/wrap read the full logical halo"_test = [] {
        using C=ca::Condition;const std::array<ca::Offset,1> left{{{-1,0}}};
        const std::array rules{ca::Rule{{C::neighbors({0,{9,9}},left,{1,1})},{{0,ca::WriteKind::set,1}}}};
        const auto p=ca::Program::prepare(1,rules);expect(p.has_value());if(!p)return;
        const std::vector<std::uint32_t> input{2,3,9,4,5,6};
        std::vector<std::uint32_t> output(6,77);
        expect(p->step_tile({3,2},{0,0,1,1},input,output,{ca::Edge::wrap}).has_value());
        expect(output==std::vector<std::uint32_t>{1,77,77,77,77,77});
        expect(p->step({3,2},input,output,{.boundary={ca::Edge::clamp}}).has_value());expect(output==input);
        expect(p->step({3,2},input,output,{.boundary={ca::Edge::constant,9}}).has_value());
        expect(output==std::vector<std::uint32_t>{1,3,9,1,5,6});
    };
    "first match and saturating writes are explicit; errors preserve output and moved plans are unprepared"_test = [] {
        const std::array rules{ca::Rule{{},{{0,ca::WriteKind::add_saturating,10},{1,ca::WriteKind::subtract_saturating,10}}},ca::Rule{{},{{0,ca::WriteKind::set,3}}}};
        auto p=ca::Program::prepare(2,rules);expect(p.has_value());if(!p)return;
        std::vector<std::uint32_t> input{0xfffffffau,7},output{44,55};
        const auto result=p->step({1,1},input,output);expect(result && result->changed==1 && result->matched==1);
        expect(output==std::vector<std::uint32_t>{0xffffffffu,0});
        output={44,55};expect(!p->step({2,1},input,output));expect(output==std::vector<std::uint32_t>{44,55});
        expect(!p->step({1,1},input,input));expect(input==std::vector<std::uint32_t>{0xfffffffau,7});
        expect(!p->step({1,1},input,output,{.tile={0,1}}));expect(output==std::vector<std::uint32_t>{44,55});
        auto moved=std::move(*p);expect(!p->step({1,1},input,output));expect(moved.step({1,1},input,output).has_value());
        auto bad=reaction_rules();bad[0].writes.push_back(bad[0].writes[0]);
        expect(ca::Program::prepare(3,bad).error().code==ca::Errc::duplicate_write);
        bad=reaction_rules();bad[2].when[2].offsets[0].x=33;expect(!ca::Program::prepare(3,bad));
        expect(!ca::Program::prepare(0,rules));expect(!ca::Program::prepare(2,rules,{.max_words=5}));
    };
    "encoding 2 — aggregates: a neighbourhood's sum, least and greatest in a condition, against a scalar oracle"_test = [] {
        using C = ca::Condition;
        using W = ca::WriteKind;
        // Channel 0 is a height. A cell that is the least of its four neighbours and itself (a pit) is marked 1 in
        // channel 1; one whose neighbours sum to 12 or more, 2; one beside a 9, 3. First match wins.
        const std::vector<ca::Rule> rules{
            {{C::minimum(0, ca::cardinal, {4, 0xffffffffu}), C::self({0, {0, 3}})}, {{1, W::set, 1}}},
            {{C::sum(0, ca::cardinal, {12, 0xffffffffu})}, {{1, W::set, 2}}},
            {{C::maximum(0, ca::cardinal, {9, 9})}, {{1, W::set, 3}}},
            {{}, {{1, W::set, 0}}},
        };
        const auto program = ca::Program::prepare(2, rules);
        expect(fatal(program.has_value()));
        expect(eq(program->encoding(), 2u)) << "a rule aggregates: encoding 2";
        const ca::Shape shape{5, 4};
        const std::vector<std::uint32_t> heights{4, 4, 4, 4, 4,
                                                 4, 1, 4, 9, 4,
                                                 4, 4, 2, 4, 4,
                                                 4, 4, 4, 4, 0};
        std::vector<std::uint32_t> field(heights.size() * 2, 7), next(field.size());
        std::ranges::copy(heights, field.begin());
        for (const ca::Edge edge : {ca::Edge::constant, ca::Edge::clamp, ca::Edge::wrap}) {
            const ca::Boundary boundary{edge, 5};
            expect(fatal(program->step(shape, field, next, {.tile = {2, 3}, .boundary = boundary}).has_value()));
            const auto at = [&](int x, int y) -> std::uint32_t {
                if (x < 0 || x >= 5 || y < 0 || y >= 4) {
                    if (edge == ca::Edge::constant) return 5;
                    if (edge == ca::Edge::clamp) x = std::clamp(x, 0, 4), y = std::clamp(y, 0, 3);
                    else x = (x + 5) % 5, y = (y + 4) % 4;
                }
                return heights[static_cast<std::size_t>(y * 5 + x)];
            };
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 5; ++x) {
                    const std::array<std::uint32_t, 4> around{at(x - 1, y), at(x + 1, y), at(x, y - 1), at(x, y + 1)};
                    const std::uint32_t least = std::ranges::min(around), greatest = std::ranges::max(around);
                    const std::uint32_t sum = around[0] + around[1] + around[2] + around[3];
                    const std::uint32_t want = least >= 4 && at(x, y) <= 3 ? 1 : sum >= 12 ? 2 : greatest == 9 ? 3 : 0;
                    expect(eq(next[20 + static_cast<std::size_t>(y * 5 + x)], want)) << std::format("edge {}, cell {},{}", static_cast<int>(edge), x, y);
                    expect(eq(next[static_cast<std::size_t>(y * 5 + x)], heights[static_cast<std::size_t>(y * 5 + x)])) << "the channel no rule writes is copied";
                }
        }
    };

    "encoding 2 — a write's terms: heat spreads as the average of a cell and its neighbours, and what it loses is taken"_test = [] {
        using T = ca::Term;
        using W = ca::WriteKind;
        // Channel 0: next = (4 × self + the four neighbours) / 8. Channel 1: old + 2 × how many neighbours are hot (≥ 100),
        // less a quarter of itself.
        const std::vector<ca::Rule> rules{{{},
                                           {{0, W::set, 0, {T::self(0, 4, 3), T::sum(0, ca::cardinal, 1, 3)}},
                                            {1, W::add_saturating, 0, {T::count({0, {100, 0xffffffffu}}, ca::cardinal, 2), T::self(1, 1, 2).taken()}}}}};
        const auto program = ca::Program::prepare(2, rules);
        expect(fatal(program.has_value()));
        expect(eq(program->encoding(), 2u) && eq(program->halo(), 1u));
        const ca::Shape shape{4, 3};
        std::vector<std::uint32_t> field(24, 0), next(24);
        field[5] = 800;                      // one hot cell at (1, 1)
        for (std::size_t i = 12; i < 24; ++i) field[i] = 10;
        expect(fatal(program->step(shape, field, next).has_value()));
        const auto heat = [&](int x, int y) { return next[static_cast<std::size_t>(y * 4 + x)]; };
        const auto mark = [&](int x, int y) { return next[12 + static_cast<std::size_t>(y * 4 + x)]; };
        expect(eq(heat(1, 1), 400u)) << "(4 × 800) >> 3, and nothing from its cold neighbours";
        expect(eq(heat(0, 1), 100u) && eq(heat(2, 1), 100u) && eq(heat(1, 0), 100u) && eq(heat(1, 2), 100u)) << "800 >> 3 from the one hot neighbour";
        expect(eq(heat(3, 1), 0u) && eq(heat(0, 0), 0u)) << "a step reaches its neighbourhood, no further";
        expect(eq(mark(0, 1), 10u + 2u - 2u) && eq(mark(1, 1), 10u - 2u)) << "plus is added, minus (10 >> 2) taken";
        // The heat is the same whatever the tiles and their order: one immutable input, one output.
        std::vector<std::uint32_t> other(24);
        expect(fatal(program->step(shape, field, other, {.tile = {1, 2}, .reverse_tiles = true}).has_value()));
        expect(other == next);
    };

    "encoding 2 — every step saturates in 32 bits, so a device computes the same: products, sums, both ends"_test = [] {
        using T = ca::Term;
        using W = ca::WriteKind;
        const std::uint32_t most = 0xffffffffu;
        const std::vector<ca::Rule> rules{{{},
                                           {{0, W::set, 5, {T::self(0, 3)}},                             // 5 + 3 × self: stops at the largest number
                                            {1, W::set, 1, {T::self(1).taken()}},                         // 1 − self: not below 0
                                            {2, W::subtract_saturating, 7, {T::self(0, 1, 31).taken()}},  // old − 7, then + (self₀ >> 31)
                                            {3, W::add_saturating, most, {T::self(3, 2, 1).taken()}}}}};  // old + most stops; then − ((2 × self₃, stopped) >> 1)
        const auto program = ca::Program::prepare(4, rules);
        expect(fatal(program.has_value()));
        const ca::Shape shape{2, 1};
        //                                   channel 0        1     2      3
        std::vector<std::uint32_t> field{0x60000000u, most,  9, 0,  3, 4,  100, most}, next(8);
        expect(fatal(program->step(shape, field, next).has_value()));
        expect(eq(next[0], most) && eq(next[1], most)) << "3 × 0x60000000 and 3 × most stop at the largest number";
        expect(eq(next[2], 0u) && eq(next[3], 1u)) << "1 − 9 is 0; 1 − 0 is 1";
        expect(eq(next[4], 0u) && eq(next[5], 0u + 1u)) << "3 − 7 is 0, plus 0x60000000 >> 31 = 0; 4 − 7 is 0, plus most >> 31 = 1";
        expect(eq(next[6], most - 100u) && eq(next[7], most - 0x7fffffffu)) << "the sum stops, then (2 × 100) >> 1 and (2 × most, stopped) >> 1 are taken";
    };

    "the encoding is 1 unless a rule aggregates or a write has terms; malformed terms are refused where they are"_test = [] {
        using C = ca::Condition;
        using T = ca::Term;
        using W = ca::WriteKind;
        const std::vector<ca::Rule> plain{{{C::neighbors({0, {1, 1}}, ca::surrounding, {3, 3})}, {{0, W::set, 1}}}};
        const auto first = ca::Program::prepare(1, plain);
        expect(fatal(first.has_value()));
        expect(eq(first->encoding(), 1u)) << "what every backend runs";
        expect(eq(first->words().size(), std::size_t{4 + 1 + 4 + 7 + 16 + 3})) << "the words of encoding 1 are what they were";
        const auto refused = [](std::vector<ca::Rule> rules, std::uint32_t channels = 2) {
            const auto program = ca::Program::prepare(channels, rules);
            return program ? std::string("accepted") : std::format("{} at {}", program.error().description().name, ca::describe_where(program.error().where));
        };
        expect(eq(refused({{{}, {{0, W::set, 0, {T::self(0, 1, 32)}}}}}), std::string("cellular.condition at rule 0, item 0"))) << "a shift beyond 31";
        expect(eq(refused({{{}, {{0, W::set, 0}, {1, W::set, 0, {T::sum(2, ca::cardinal)}}}}}), std::string("cellular.channels at rule 0, item 1")));
        const std::array<ca::Offset, 1> far{{{33, 0}}};
        expect(eq(refused({{{}, {{0, W::set, 0, {T::maximum(0, far)}}}}}), std::string("cellular.offset at rule 0, item 0")));
        ca::Term both = T::self(0);
        both.offsets = {{1, 0}};
        expect(eq(refused({{{}, {{0, W::set, 0, {both}}}}}), std::string("cellular.condition at rule 0, item 0"))) << "the cell's own value has no neighbourhood";
        expect(eq(refused({{{C::sum(0, ca::cardinal, {5, 1})}, {}}}), std::string("cellular.range at rule 0, item 0")));
        expect(eq(refused({{{}, {{0, W::set, 0, std::vector<ca::Term>(33, T::self(0))}}}}), std::string("cellular.limit at rule 0, item 0"))) << "33 terms";
    };

    "a transfer conserves: heat spreads from a hot cell, what the field holds is what it held, step after step"_test = [] {
        // One quantity, flowing to the four neighbours: an eighth of each difference a step.
        const std::array<ca::Transfer, 1> heat{{{.channel = 0, .offsets = {ca::cardinal.begin(), ca::cardinal.end()}, .multiply = 1, .shift = 3}}};
        const auto program = ca::Program::prepare(1, ca::Stages{.transfers = heat});
        expect(fatal(program.has_value()));
        expect(eq(program->encoding(), 2u) && eq(program->halo(), 1u) && eq(program->transfer_count(), 1u));
        const ca::Shape shape{16, 9};
        const auto total = [](const std::vector<std::uint32_t>& field) { return std::accumulate(field.begin(), field.end(), std::uint64_t{0}); };
        for (const ca::Edge edge : {ca::Edge::wrap, ca::Edge::clamp}) {  // round the edge, or nothing across it
            std::vector<std::uint32_t> field(16 * 9, 0), next(field.size());
            field[4 * 16 + 5] = 4'000'000'000u;  // nearly all a cell can hold
            field[2 * 16 + 14] = 123'457u;
            const std::uint64_t held = total(field);
            std::uint32_t hottest = 4'000'000'000u;
            for (int step = 0; step < 300; ++step) {
                const auto stepped = program->step(shape, field, next, {.tile = {5, 4}, .boundary = {edge, 0}, .reverse_tiles = step % 2 == 1});
                expect(fatal(stepped.has_value()));
                field.swap(next);
                if (total(field) != held) {
                    expect(false) << std::format("edge {}, step {}: {} is not {}", static_cast<int>(edge), step, total(field), held);
                    break;
                }
                const std::uint32_t now = std::ranges::max(field);
                expect(now <= hottest) << "the hottest cell never gets hotter";
                hottest = now;
            }
            // 300 steps is about six times the slowest mode's time on this field: spread, not yet at rest.
            expect(std::ranges::max(field) < 40'000'000u && std::ranges::min(field) > 0u)
                << std::format("the hottest is under a hundredth of where it began ({}), and every cell has some ({})", std::ranges::max(field), std::ranges::min(field));
        }
        // To rest: on a ring of six, half of each difference a step to each of two neighbours. It stops when no pair
        // differs by 2 or more — whole numbers, so what is left over stays where it is — and still holds what it held.
        {
            const std::array<ca::Transfer, 1> ring{{{.channel = 0, .offsets = {{-1, 0}, {1, 0}}, .multiply = 1, .shift = 1}}};
            const auto small = ca::Program::prepare(1, ca::Stages{.transfers = ring});
            expect(fatal(small.has_value()));
            std::vector<std::uint32_t> field{1000, 0, 0, 7, 0, 0}, next(6);
            int steps = 0;
            for (; steps < 10'000; ++steps) {
                const auto stepped = small->step({6, 1}, field, next, {.boundary = {ca::Edge::wrap, 0}});
                expect(fatal(stepped.has_value()));
                field.swap(next);
                if (!stepped->changed) break;
            }
            expect(steps < 10'000) << "it comes to rest";
            expect(eq(total(field), std::uint64_t{1007}));
            for (std::size_t cell = 0; cell < 6; ++cell) {
                const std::uint32_t here = field[cell], beside = field[(cell + 1) % 6];
                expect((here > beside ? here - beside : beside - here) < 2u) << std::format("cells {} and its neighbour: {} and {}", cell, here, beside);
            }
        }
        // A constant edge is a bath: at 0 the heat leaves through it.
        std::vector<std::uint32_t> field(16 * 9, 1000), next(field.size());
        for (int step = 0; step < 50; ++step) {
            expect(fatal(program->step(shape, field, next, {.boundary = {ca::Edge::constant, 0}}).has_value()));
            field.swap(next);
        }
        expect(total(field) < 16u * 9u * 1000u && field[4 * 16 + 8] > field[0]) << "cooler at the edge than in the middle";
    };

    "a transfer by a conductance: a wall that conducts nothing keeps each side's heat; a poor conductor slows it"_test = [] {
        // Channel 0 heat, channel 1 how well a cell conducts (0 … 4). The pair's conductance is the lesser of the two.
        const std::array<ca::Transfer, 1> heat{{{.channel = 0, .offsets = {ca::cardinal.begin(), ca::cardinal.end()}, .multiply = 1, .shift = 4, .by = 1, .by_limit = 4}}};
        const auto program = ca::Program::prepare(2, ca::Stages{.transfers = heat});
        expect(fatal(program.has_value()));
        const ca::Shape shape{9, 3};
        const auto run = [&](std::uint32_t wall) {
            std::vector<std::uint32_t> field(27 * 2, 0), next(field.size());
            for (std::size_t cell = 0; cell < 27; ++cell) field[27 + cell] = cell % 9 == 4 ? wall : 4;  // a wall down the middle column
            for (std::uint32_t y = 0; y < 3; ++y) field[y * 9 + 1] = 90'000;                            // heat on the left
            for (int step = 0; step < 60; ++step) {
                expect(fatal(program->step(shape, field, next, {.boundary = {ca::Edge::clamp, 0}}).has_value()));
                field.swap(next);
            }
            std::uint64_t left = 0, right = 0;
            for (std::uint32_t y = 0; y < 3; ++y)
                for (std::uint32_t x = 0; x < 9; ++x) (x < 4 ? left : x > 4 ? right : left) += field[y * 9 + x];
            expect(eq(left + right, std::uint64_t{270'000})) << "conserved, whatever conducts";
            for (std::size_t cell = 0; cell < 27; ++cell) expect(eq(field[27 + cell], cell % 9 == 4 ? wall : 4u)) << "the conductance is no one's to change";
            return right;
        };
        const std::uint64_t through_nothing = run(0), through_poor = run(1), through_good = run(4);
        expect(eq(through_nothing, std::uint64_t{0})) << "nothing crosses a cell that conducts nothing";
        expect(through_poor > 0 && through_poor < through_good) << std::format("{} < {}", through_poor, through_good);
    };

    "rules and transfers in one step read the same input; a transfer that could take more than a cell has is refused"_test = [] {
        using C = ca::Condition;
        using W = ca::WriteKind;
        // Channel 1 marks a cell that was hot (≥ 500) in the input — the rule sees the heat before it flowed.
        const std::array<ca::Rule, 1> rules{{{{C::self({0, {500, 0xffffffffu}})}, {{1, W::set, 1}}}}};
        const std::array<ca::Transfer, 1> heat{{{.channel = 0, .offsets = {ca::cardinal.begin(), ca::cardinal.end()}, .shift = 2}}};
        const auto program = ca::Program::prepare(2, ca::Stages{.rules = rules, .transfers = heat});
        expect(fatal(program.has_value()));
        std::vector<std::uint32_t> field(9 * 2, 0), next(field.size());
        field[4] = 800;
        const auto stepped = program->step({3, 3}, field, next, {.boundary = {ca::Edge::clamp, 0}});
        expect(fatal(stepped.has_value()));
        expect(eq(next[4], 0u) && eq(next[1], 200u) && eq(next[3], 200u) && eq(next[5], 200u) && eq(next[7], 200u)) << "a quarter to each of four: all of it";
        expect(eq(next[9 + 4], 1u) && eq(next[9 + 1], 0u)) << "marked from the input's heat";
        expect(eq(stepped->matched, std::uint64_t{1}) && eq(stepped->changed, std::uint64_t{5})) << "a cell a flow changed is changed";

        const auto refused = [](ca::Transfer transfer, std::uint32_t channels = 2) {
            const std::array<ca::Transfer, 1> one{{std::move(transfer)}};
            const auto program = ca::Program::prepare(channels, ca::Stages{.transfers = one});
            return program ? std::string("accepted") : std::format("{} at {}", program.error().description().name, ca::describe_where(program.error().where));
        };
        const std::vector<ca::Offset> four{ca::cardinal.begin(), ca::cardinal.end()};
        expect(eq(refused({.channel = 0, .offsets = four, .shift = 1}), std::string("cellular.transfer at transfer 0"))) << "four neighbours each taking half";
        expect(eq(refused({.channel = 0, .offsets = four, .multiply = 3, .shift = 3}), std::string("cellular.transfer at transfer 0"))) << "4 × 3 above 8";
        expect(eq(refused({.channel = 0, .offsets = four, .shift = 4, .by = 1, .by_limit = 5}), std::string("cellular.transfer at transfer 0"))) << "5 × 4 above 16";
        expect(eq(refused({.channel = 0, .offsets = {{1, 0}}, .shift = 3}), std::string("cellular.transfer at transfer 0"))) << "an offset without its opposite";
        expect(eq(refused({.channel = 0, .offsets = {{0, 0}}, .shift = 3}), std::string("cellular.transfer at transfer 0"))) << "a cell is not its own neighbour";
        expect(eq(refused({.channel = 0, .offsets = {}, .shift = 3}), std::string("cellular.transfer at transfer 0")));
        expect(eq(refused({.channel = 2, .offsets = four}), std::string("cellular.channels at transfer 0")));
        expect(eq(refused({.channel = 0, .offsets = four, .by = 7}), std::string("cellular.channels at transfer 0")));
        const std::array<ca::Transfer, 2> twice{{{.channel = 0, .offsets = four}, {.channel = 0, .offsets = four}}};
        const auto both = ca::Program::prepare(1, ca::Stages{.transfers = twice});
        expect(!both && both.error().code == ca::Errc::transfer && both.error().where.transfer == std::size_t{1}) << "one flow a quantity";
    };

    "an exchange: a column of sand falls and comes to rest as a pile — every grain kept, none hanging, none on a slope it would slide down"_test = [] {
        World world({11, 8});
        for (std::uint32_t y = 0; y < 6; ++y) world.put(5, y, sand);
        const auto before = world.census();
        int quiet = 0, ticks = 0;
        for (; ticks < 400 && quiet < 8; ++ticks) quiet = world.step() ? 0 : quiet + 1;  // at rest: nothing moved through the cut's four places, twice
        expect(quiet == 8) << "it comes to rest\n" + world.picture();
        expect(world.census() == before) << "every cell that was there is there";
        std::uint32_t grains = 0, tallest = 0;
        for (std::uint32_t y = 0; y < 8; ++y)
            for (std::uint32_t x = 0; x < 11; ++x) {
                if (!world.is(x, y, sand)) continue;
                ++grains;
                tallest = std::max(tallest, 8 - y);
                if (y == 7) continue;
                expect(!world.is(x, y + 1, air)) << std::format("a grain hangs at {},{}\n{}", x, y, world.picture());
                for (const int side : {-1, 1}) {
                    const std::uint32_t beside = x + static_cast<std::uint32_t>(side);
                    if (beside >= 11) continue;
                    expect(!(world.is(beside, y, air) && world.is(beside, y + 1, air))) << std::format("a grain at {},{} would slide\n{}", x, y, world.picture());
                }
            }
        expect(eq(grains, 6u));
        expect(tallest <= 3u) << "a pile, not the column it was\n" + world.picture();
        std::println("{} ticks to rest:\n{}", ticks, world.picture());
    };

    "an exchange keeps every cell: a soup of sand, water, oil, smoke and stone, tick after tick — and what is of the place stays"_test = [] {
        World world({13, 9});
        const std::array<Kind, 6> kinds{air, sand, water, oil, smoke, stone};
        for (std::uint32_t y = 0; y < 9; ++y)
            for (std::uint32_t x = 0; x < 13; ++x) world.put(x, y, kinds[(x * 7 + y * 13 + x * y) % 6]);
        const auto before = world.census();
        std::vector<std::pair<std::uint32_t, std::uint32_t>> stones;
        for (std::uint32_t y = 0; y < 9; ++y)
            for (std::uint32_t x = 0; x < 13; ++x)
                if (world.is(x, y, stone)) stones.emplace_back(x, y);
        std::uint64_t moved = 0;
        for (int tick = 0; tick < 200; ++tick) {
            moved += world.step(tick % 3 == 0 ? ca::Edge::wrap : ca::Edge::clamp);  // an odd size: a block over the edge sits out, wrapped or not
            if (world.census() != before) {
                expect(false) << std::format("tick {}: a cell was lost or made", tick);
                break;
            }
        }
        expect(moved > 100u) << "and they did move";
        for (const auto [x, y] : stones) expect(world.is(x, y, stone)) << "what may not move did not";
        for (std::uint32_t y = 0; y < 9; ++y)
            for (std::uint32_t x = 0; x < 13; ++x) expect(eq(world.cells[3 * world.area() + world.at(x, y)], x * 3 + y)) << "a channel that is not carried stays";
    };

    "an exchange: water finds its level, oil lies on it, smoke goes up — by weight and mobility alone"_test = [] {
        World world({12, 8});
        for (std::uint32_t y = 2; y < 8; ++y)
            for (std::uint32_t x = 0; x < 4; ++x) world.put(x, y, water);  // 24 of water, a block on the left
        for (std::uint32_t y = 6; y < 8; ++y)
            for (std::uint32_t x = 8; x < 12; ++x) world.put(x, y, oil);    // 8 of oil at the bottom right
        for (std::uint32_t x = 5; x < 8; ++x) world.put(x, 7, smoke);       // 3 of smoke on the floor
        const auto before = world.census();
        for (int tick = 0; tick < 1500; ++tick) world.step();
        expect(world.census() == before);
        // Water: 24 cells in a tank 12 wide are two full rows. Its surface moves (a liquid never quite rests), so
        // the measure is where it is: nearly all of it in the two lowest rows, none above the fourth.
        std::uint32_t water_low = 0, water_high = 0, oil_above_water = 0, oil_cells = 0, smoke_top = 0;
        for (std::uint32_t y = 0; y < 8; ++y)
            for (std::uint32_t x = 0; x < 12; ++x) {
                if (world.is(x, y, water)) (y >= 6 ? water_low : water_high) += 1, expect(y >= 4u) << "water above the fourth row\n" + world.picture();
                if (world.is(x, y, oil)) ++oil_cells, oil_above_water += y <= 5 ? 1 : 0;
                if (world.is(x, y, smoke)) smoke_top += y <= 1 ? 1 : 0;
            }
        expect(water_low >= 22u) << std::format("{} of 24 in the two lowest rows\n{}", water_low, world.picture());
        expect(eq(oil_cells, 8u) && oil_above_water >= 7u) << std::format("{} of 8 of oil above the water's two rows\n{}", oil_above_water, world.picture());
        expect(eq(smoke_top, 3u)) << "the smoke is under the ceiling\n" + world.picture();
        std::println("after 1500 ticks:\n{}", world.picture());
    };

    "an exchange is the same field at the same tick: whatever the tiles, their order, and run again; another seed spreads another way"_test = [] {
        const auto run = [](std::uint32_t seed, ca::Shape tile, bool reverse) {
            World world({12, 8}, seed);
            for (std::uint32_t y = 2; y < 8; ++y)
                for (std::uint32_t x = 0; x < 4; ++x) world.put(x, y, water);
            for (std::uint32_t y = 0; y < 3; ++y) world.put(9, y, sand);
            for (int tick = 0; tick < 120; ++tick) world.step(ca::Edge::clamp, tile, reverse);
            return world.cells;
        };
        const auto first = run(7, {32, 32}, false);
        expect(run(7, {32, 32}, false) == first) << "run again";
        expect(run(7, {5, 3}, true) == first) << "tiles and their order are the rules' matter, not the result's";
        expect(run(8, {32, 32}, false) != first) << "another seed";
    };

    "a program that moves cells steps the whole field; its changed tiles are where cells changed; a malformed exchange is refused"_test = [] {
        World world({8, 8});
        world.put(3, 0, sand);
        std::vector<std::uint8_t> changed(4, 9);
        const auto stepped = world.program.step(world.shape, world.cells, world.next, {.tile = {4, 4}, .boundary = {ca::Edge::clamp, 0}, .changed_tiles = changed, .tick = 0});
        expect(fatal(stepped.has_value()));
        expect(eq(stepped->changed, std::uint64_t{2})) << "the grain and the air it changed places with";
        expect(changed == std::vector<std::uint8_t>{1, 0, 0, 0}) << "the tile they are in";
        expect(world.program.moves());
        const auto tile = world.program.step_tile(world.shape, {0, 0, 4, 4}, world.cells, world.next);
        expect(!tile && tile.error().code == ca::Errc::whole_field) << "a tile of it alone is refused";

        const auto refused = [](std::vector<ca::Exchange> exchanges, std::uint32_t channels = 3) {
            const auto program = ca::Program::prepare(channels, ca::Stages{.exchanges = exchanges});
            return program ? std::string("accepted") : std::format("{} at {}", program.error().description().name, ca::describe_where(program.error().where));
        };
        expect(eq(refused({{.weight = 3}}), std::string("cellular.channels at exchange 0")));
        expect(eq(refused({{.weight = 0, .mobility = 1, .carried = {0, 1, 5}}}), std::string("cellular.channels at exchange 0")));
        expect(eq(refused({{.weight = 0, .mobility = 1, .carried = {0, 1, 1}}}), std::string("cellular.exchange at exchange 0"))) << "a channel carried twice";
        expect(eq(refused({{.weight = 0, .mobility = 1, .carried = {0, 2}}}), std::string("cellular.exchange at exchange 0"))) << "what decides a move goes with the cell";
        expect(eq(refused({{.weight = 0, .mobility = 1}, {.weight = 0, .mobility = 1}}), std::string("cellular.limit at exchange 1"))) << "one exchange a program";
        expect(eq(refused({{.weight = 0, .mobility = 1}}), std::string("accepted")));
    };

    "sparse — a fire burns out and the field sleeps: only the tiles the front is in are run, and the cells are the same bits as running all"_test = [] {
        const ca::Shape shape{40, 24};
        const auto rules = reaction_rules();
        const auto program = ca::Program::prepare(3, rules);
        expect(fatal(program.has_value()));
        expect(eq(program->reach(), 1u));
        for (const ca::Shape tile : {ca::Shape{4, 4}, ca::Shape{7, 5}, ca::Shape{16, 16}})
            for (const ca::Edge edge : {ca::Edge::constant, ca::Edge::clamp, ca::Edge::wrap})
                for (const bool in_turn : {false, true}) {
                    Twice both(*program, shape, tile, {edge, 0}, initial(shape), in_turn);
                    const std::size_t all = both.activity.size();
                    int steps = 0;
                    for (; steps < 400 && both.asleep() != all; ++steps)
                        expect(fatal(both.step(steps % 3 == 1))) << std::format("step {} of tiles {}x{}, edge {}, in turn {}", steps, tile.width, tile.height, static_cast<int>(edge), in_turn);
                    expect(fatal(both.asleep() == all)) << "the fire is out and every tile sleeps";
                    // A fire's front is a line: with tiles small beside the field, much of it was not run. (A tile that
                    // changed wakes those beside it: six tiles of 16 round a wrapped edge are all beside the fire.)
                    const std::uint64_t dense_cells = static_cast<std::uint64_t>(steps) * shape.width * shape.height;
                    expect(both.cells_run <= dense_cells);
                    if (tile.width < 16) expect(both.tiles_slept > 0u && both.cells_run < dense_cells) << std::format("{} of {} cells run", both.cells_run, dense_cells);
                    if (tile.width == 4 && edge == ca::Edge::constant && in_turn)
                        std::println("a fire on 40 x 24 in tiles of 4 x 4: out and asleep after {} steps, {} of {} cells run", steps, both.cells_run, dense_cells);
                    // Asleep, a step runs nothing and changes nothing — and with two buffers in turn writes nothing.
                    for (int rest = 0; rest < 3; ++rest) {
                        expect(both.step());
                        expect(eq(both.last.cells, std::uint64_t{0}) && eq(both.last.changed, std::uint64_t{0}) && eq(both.last.asleep, std::uint64_t{all}));
                    }
                    // Lit again from outside: the tile and those that read it wake, the rest sleep on.
                    both.write(1, 1, 1, 0);
                    both.write(0, 1, 1, 1);
                    both.write(1, 2, 1, 1);
                    const std::size_t woken = all - both.asleep();
                    expect(woken >= 1u && woken < all) << std::format("{} of {} tiles woken by a cell", woken, all);
                    for (int after = 0; after < 60; ++after) expect(fatal(both.step())) << std::format("step {} after the edit", after);
                }
    };

    "sparse — a tile wakes for what changes within the program's reach of it, further than the next tile and round a wrapped edge"_test = [] {
        // A signal that jumps three cells to the right each step: a cell is lit when the cell three to its left is.
        using C = ca::Condition;
        const std::array<ca::Offset, 1> three_left{{{-3, 0}}};
        const std::vector<ca::Rule> rules{
            {{C::self({0, {1, 1}})}, {{0, ca::WriteKind::set, 0}}},
            {{C::neighbors({0, {1, 1}}, three_left, {1, 1})}, {{0, ca::WriteKind::set, 1}}},
        };
        const auto program = ca::Program::prepare(1, rules);
        expect(fatal(program.has_value()));
        expect(eq(program->reach(), 3u));
        for (const ca::Shape shape : {ca::Shape{17, 3}, ca::Shape{16, 2}})
            for (const ca::Shape tile : {ca::Shape{2, 2}, ca::Shape{3, 1}, ca::Shape{5, 3}})
                for (const ca::Edge edge : {ca::Edge::wrap, ca::Edge::constant}) {
                    std::vector<std::uint32_t> cells(static_cast<std::size_t>(shape.width) * shape.height, 0);
                    cells[shape.width + 1] = 1;
                    Twice both(*program, shape, tile, {edge, 0}, cells, true);
                    for (int steps = 0; steps < 40; ++steps)
                        expect(fatal(both.step())) << std::format("step {} on {}x{}, tiles {}x{}, edge {}", steps, shape.width, shape.height, tile.width, tile.height, static_cast<int>(edge));
                    // Round a wrapped edge it never ends; off a constant one it is gone and the field sleeps.
                    if (edge == ca::Edge::constant) expect(eq(both.asleep(), both.activity.size()));
                    else expect(both.asleep() < both.activity.size() && both.tiles_slept > 0u);
                }
        // An edit wakes what reads it: in a row of tiles of 2, a cell and reach 3 are the two tiles either side.
        std::vector<std::uint8_t> activity(8, 0x0f);
        program->wake({16, 1}, {2, 1}, {7, 0, 1, 1}, activity);
        expect(activity == std::vector<std::uint8_t>{0x0f, 0x0f, 0, 0, 0, 0, 0x0f, 0x0f}) << "cells 4 to 10";
        std::ranges::fill(activity, std::uint8_t{0x0f});
        program->wake({16, 1}, {2, 1}, {0, 0, 1, 1}, activity, ca::Edge::wrap);
        expect(activity == std::vector<std::uint8_t>{0, 0, 0x0f, 0x0f, 0x0f, 0x0f, 0, 0}) << "round the edge: cells 13 to 3";
        std::ranges::fill(activity, std::uint8_t{0x0f});
        program->wake({16, 1}, {2, 1}, {0, 0, 1, 1}, activity, ca::Edge::clamp);
        expect(activity == std::vector<std::uint8_t>{0, 0, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f});
        program->wake({16, 1}, {2, 1}, {16, 0, 4, 1}, activity);  // not in the field
        program->wake({16, 1}, {2, 1}, {3, 0, 0, 1}, activity);   // no cells
        expect(eq(std::ranges::count(activity, std::uint8_t{0}), std::ptrdiff_t{2}));
        std::vector<std::uint32_t> field(16), next(16);
        std::vector<std::uint8_t> wrong(7);
        const auto refused = program->step({16, 1}, field, next, {.tile = {2, 1}, .activity = wrong});
        expect(!refused && refused.error().code == ca::Errc::tile) << "a byte a tile, no more and no fewer";
    };

    "sparse — cells that move: a pile at rest sleeps, a liquid that could still move does not, a grain dropped in wakes its way down; the same bits throughout"_test = [] {
        for (const ca::Shape tile : {ca::Shape{2, 2}, ca::Shape{3, 3}, ca::Shape{4, 4}, ca::Shape{5, 3}})
            for (const ca::Edge edge : {ca::Edge::clamp, ca::Edge::wrap})
                for (const bool in_turn : {false, true}) {
                    // Sand on a floor of stone, on the left; nothing on the right.
                    World world({16, 12});
                    for (std::uint32_t x = 0; x < 16; ++x) world.put(x, 11, stone);
                    for (std::uint32_t y = 2; y < 8; ++y) world.put(3, y, sand);
                    Twice both(world.program, world.shape, tile, {edge, 0}, world.cells, in_turn);
                    const std::size_t all = both.activity.size();
                    const auto where = [&](int step) { return std::format("step {}, tiles {}x{}, edge {}, in turn {}", step, tile.width, tile.height, static_cast<int>(edge), in_turn); };
                    for (int steps = 0; steps < 120; ++steps) expect(fatal(both.step(steps % 2 == 1))) << where(steps);
                    expect(fatal(eq(both.asleep(), all))) << "the pile is at rest: every tile sleeps — " + where(120);
                    expect(both.step() && eq(both.last.cells, std::uint64_t{0}));
                    // A grain let go at the top right: its tiles wake as it falls, the pile's sleep on.
                    both.write(0, 13, 0, sand.weight);
                    both.write(1, 13, 0, sand.mobility);
                    std::uint64_t most_awake = 0;
                    for (int steps = 0; steps < 60; ++steps) {
                        expect(fatal(both.step())) << "the grain — " + where(steps);
                        most_awake = std::max<std::uint64_t>(most_awake, all - both.last.asleep);
                    }
                    expect(most_awake < all) << std::format("{} of {} tiles awake at the most", most_awake, all);
                    expect(eq(both.asleep(), all)) << "and it lies on the floor";
                    expect(eq(both.sparse[10 * 16 + 13], sand.weight)) << "where it fell";
                }
        // Water with a surface it cannot fill never rests: where it is stays awake, and the cells stay the same
        // bits as every tile run — a tile that slept on a coin's refusal would part from them.
        for (const ca::Shape tile : {ca::Shape{2, 2}, ca::Shape{4, 4}, ca::Shape{8, 4}}) {
            World world({16, 12});
            for (std::uint32_t y = 6; y < 12; ++y)
                for (std::uint32_t x = 0; x < 3; ++x) world.put(x, y, water);  // 18 of water: one row of 16 and two over
            for (std::uint32_t y = 0; y < 3; ++y) world.put(12, y, sand);
            for (std::uint32_t x = 5; x < 8; ++x) world.put(x, 11, smoke);
            Twice both(world.program, world.shape, tile, {ca::Edge::clamp, 0}, world.cells, true);
            for (int steps = 0; steps < 600; ++steps) expect(fatal(both.step())) << std::format("step {} of the water, tiles {}x{}", steps, tile.width, tile.height);
            expect(both.asleep() < both.activity.size()) << "the water's surface is awake";
            expect(both.tiles_slept > 0u) << "and the air above it is not";
            if (tile.width == 4) std::println("water, sand and smoke on 16 x 12 in tiles of 4 x 4: {} of {} tiles asleep after 600 ticks, {} cells run of {}", both.asleep(), both.activity.size(), both.cells_run, 600 * 192);
        }
    };

    "a boundary with a value a channel is a bath beyond every edge: heat flows out to it by the conductance it has, and what is near sees its row"_test = [] {
        // Channel 0 heat, channel 1 how well a cell conducts. Every cell at 1000; a sixteenth of a difference × the
        // conductance flows to each neighbour.
        const std::array<ca::Transfer, 1> heat{{{.channel = 0, .offsets = {ca::cardinal.begin(), ca::cardinal.end()}, .multiply = 1, .shift = 4, .by = 1, .by_limit = 4}}};
        const auto program = ca::Program::prepare(2, ca::Stages{.transfers = heat});
        expect(fatal(program.has_value()));
        const ca::Shape shape{5, 3};
        std::vector<std::uint32_t> field(15 * 2, 1000), next(field.size());
        std::fill_n(field.begin() + 15, 15, 4u);
        const auto after = [&](ca::Boundary boundary) {
            expect(fatal(program->step(shape, field, next, {.boundary = boundary}).has_value()));
            return std::array{next[0], next[2], next[7]};  // a corner, an edge, the middle
        };
        const std::array<std::uint32_t, 2> cold{0, 4}, sealed{0, 0}, warm{1000, 4};
        expect(after({ca::Edge::constant, 0, cold}) == std::array{500u, 750u, 1000u}) << "a quarter of 1000 to each side that is outside";
        expect(after({ca::Edge::constant, 0, sealed}) == std::array{1000u, 1000u, 1000u}) << "an outside that conducts nothing takes nothing";
        expect(after({ca::Edge::constant, 0, warm}) == std::array{1000u, 1000u, 1000u}) << "nor one as warm";
        expect(after({ca::Edge::constant, 7}) == std::array{504u, 752u, 1000u}) << "one value for every channel, as before: an outside at 7 that conducts 7 (bounded by 4)";
        const std::array<std::uint32_t, 3> three{0, 4, 9};
        const auto wrong = program->step(shape, field, next, {.boundary = {ca::Edge::constant, 0, three}});
        expect(!wrong && wrong.error().code == ca::Errc::shape) << "a value a channel, no more and no fewer";
        // With a table: the bath is a row like any other to what is near it.
        const ca::Reactions staged = embers();
        const auto reacting = ca::Program::prepare(5, ca::Stages{.reactions = &staged});
        expect(fatal(reacting.has_value()));
        const std::vector<std::uint32_t> lone{calm, 4, 0, 0, 0};
        std::vector<std::uint32_t> made(5);
        const std::array<std::uint32_t, 5> among_embers{ember, 8, 0, 0, 0};
        expect(fatal(reacting->step({1, 1}, lone, made, {.boundary = {ca::Edge::constant, 0, among_embers}}).has_value()));
        expect(eq(made[2], 32u)) << "four embers beside it, each of level 8: its heat is their levels";
        expect(fatal(reacting->step({1, 1}, lone, made, {.boundary = {ca::Edge::constant, no_row}}).has_value()));
        expect(eq(made[2], 0u)) << "and with nothing beyond, nothing";
    };

    "what a quantity does to a rectangle of cells from outside: the sides' sums, left less right and above less below"_test = [] {
        // One channel's cells, 6 x 4; the rectangle is the 2 x 2 at (2, 1).
        const ca::Shape shape{6, 4};
        const std::vector<std::uint32_t> pressure{
            0, 0, 5,  7,  0, 0,
            0, 9, 0,  0,  2, 0,
            0, 4, 0,  0,  1, 0,
            0, 0, 30, 10, 0, 0,
        };
        const ca::Push push = ca::push_on(shape, pressure, {2, 1, 2, 2});
        expect(eq(push.x, std::int64_t{9 + 4 - 2 - 1}) && eq(push.y, std::int64_t{5 + 7 - 30 - 10})) << "toward +x, and upward (less y)";
        // At the field's edge the side beyond it is nothing; a rectangle outside it, or values of another size, push nothing.
        const ca::Push corner = ca::push_on(shape, pressure, {0, 0, 2, 2});
        expect(eq(corner.x, std::int64_t{-5 - 0}) && eq(corner.y, std::int64_t{-0 - 4}));
        const ca::Push whole = ca::push_on(shape, pressure, {0, 0, 6, 4});
        expect(eq(whole.x, std::int64_t{0}) && eq(whole.y, std::int64_t{0}));
        const ca::Push beyond = ca::push_on(shape, pressure, {4, 2, 9, 9});
        expect(eq(beyond.x, std::int64_t{0 + 10}) && eq(beyond.y, std::int64_t{2 + 0})) << "clipped to the field: its right and its bottom are the field's, with nothing beyond";
        expect(eq(ca::push_on(shape, pressure, {6, 0, 1, 1}).x, std::int64_t{0}) && eq(ca::push_on(shape, std::span(pressure).first(5), {1, 1, 1, 1}).x, std::int64_t{0}));
    };

    "encoding 3 — a table and reactions: every reaction that holds acts, against numbers worked by hand"_test = [] {
        const ca::Reactions staged = embers();
        const auto program = ca::Program::prepare(5, ca::Stages{.reactions = &staged});
        expect(fatal(program.has_value())) << (program ? "" : ca::describe_where(program.error().where));
        expect(eq(program->encoding(), 3u));
        expect(program->reacts() && eq(program->rows(), 3u) && eq(program->halo(), 1u) && !program->moves());
        // A row of an ember, a calm cell and an ember; under them three cells of no row.
        const ca::Shape shape{3, 2};
        const std::vector<std::uint32_t> start{
            ember, calm, ember, no_row, no_row, no_row,  // what each is
            8,     4,    19,    1000,   1000,   1000,    // level
            60,    0,    10,    5,      5,      5,       // heat
            0,     1,    0,     1,      1,      1,       // flag
            5,     6,    7,     9,      9,      9,       // age
        };
        const ca::Boundary edge{ca::Edge::constant, 99};  // beyond the edge: no row either
        const auto after = [&](int steps, ca::Shape tile, bool reverse, const ca::Program& of, std::uint64_t* matched = nullptr) {
            std::vector<std::uint32_t> cells = start, next(cells.size());
            for (int step = 0; step < steps; ++step) {
                const auto stepped = of.step(shape, cells, next, {.tile = tile, .boundary = edge, .reverse_tiles = reverse});
                expect(fatal(stepped.has_value()));
                if (matched) *matched = stepped->matched;
                cells.swap(next);
            }
            return cells;
        };
        std::uint64_t matched = 0;
        // 1: the hot ember's limit is 15 (20 less 5): 8 < 15, it gains 5 and is flagged. The calm cell has a glow of
        //    3 + 3 beside it: its heat is the levels beside it, 8 + 19 — the cell below is no row and adds nothing —
        //    and the second set (99) does not stand; nothing flags it, so its flag falls. The other ember: 19 < 20
        //    gains 5, and 19 ≥ 10 cools by 2 and gains 1 more.
        expect(after(1, {3, 2}, false, *program, &matched) == std::vector<std::uint32_t>{
            ember, calm, ember, no_row, no_row, no_row,
            13,    4,    25,    1000,   1000,   1000,
            60,    27,   8,     5,      5,      5,
            1,     0,    1,     1,      1,      1,
            5,     6,    7,     9,      9,      9});
        expect(eq(matched, std::uint64_t{5})) << "the reactions that held: 1, 2 and 2";
        // 2: the first ember gains 5 + 1 and cools; the other, at 25 ≥ 20, becomes ash — its level starts again
        //    (ash says no start: the reset's own, 1) after what the reactions made of it, its heat does not.
        expect(after(2, {3, 2}, false, *program) == std::vector<std::uint32_t>{
            ember, calm, ash, no_row, no_row, no_row,
            19,    4,    1,   1000,   1000,   1000,
            58,    38,   6,   5,      5,      5,
            1,     0,    0,   1,      1,      1,
            5,     6,    7,   9,      9,      9});
        // 3: the first ember is at 19 ≥ 15 and becomes ash; the calm cell has a glow of 3 beside it now: its heat stays.
        const std::vector<std::uint32_t> rest{
            ash, calm, ash, no_row, no_row, no_row,
            1,   4,    1,   1000,   1000,   1000,
            56,  38,   6,   5,      5,      5,
            0,   0,    0,   1,      1,      1,
            5,   6,    7,   9,      9,      9};
        expect(after(3, {3, 2}, false, *program) == rest);
        expect(after(9, {3, 2}, false, *program) == rest) << "and there it rests";
        // Whatever the tiles and their order.
        for (const ca::Shape tile : {ca::Shape{1, 1}, ca::Shape{2, 1}, ca::Shape{2, 2}})
            for (const bool reverse : {false, true})
                for (int steps = 1; steps <= 3; ++steps) expect(after(steps, tile, reverse, *program) == after(steps, {3, 2}, false, *program));
        // A row that says where its level starts: there, not at the reset's own.
        const ca::Reactions other = embers(9);
        const auto restarted = ca::Program::prepare(5, ca::Stages{.reactions = &other});
        expect(fatal(restarted.has_value()));
        expect(eq(after(2, {3, 2}, false, *restarted)[6 + 2], 9u));
    };

    "encoding 3 — a column as a transfer's conductance is the channel it would be; a cell of no row conducts nothing"_test = [] {
        // The same wall as the transfer by a channel above — there the conductance is a channel's value, here a
        // column of the row each cell's key names. The two are the same bits, step after step.
        const std::vector<ca::Offset> beside{ca::cardinal.begin(), ca::cardinal.end()};
        const std::array<ca::Transfer, 1> by_channel{{{.channel = 0, .offsets = beside, .multiply = 1, .shift = 4, .by = 1, .by_limit = 4}}};
        const std::array<ca::Transfer, 1> by_column{{{.channel = 0, .offsets = beside, .multiply = 1, .shift = 4, .by_limit = 4, .by_column = 0}}};
        const ca::Shape shape{9, 3};
        for (const std::uint32_t wall : {0u, 1u, 4u}) {
            ca::Reactions staged;
            staged.table = {.key = 1, .columns = 1, .values = {4, wall}};  // row 0 conducts well, row 1 is the wall
            const auto plain = ca::Program::prepare(2, ca::Stages{.transfers = by_channel});
            const auto tabled = ca::Program::prepare(2, ca::Stages{.transfers = by_column, .reactions = &staged});
            expect(fatal(plain.has_value() && tabled.has_value()));
            std::vector<std::uint32_t> one(27 * 2, 0), other(27 * 2, 0), next(27 * 2);
            for (std::size_t cell = 0; cell < 27; ++cell) {
                one[27 + cell] = cell % 9 == 4 ? wall : 4;
                other[27 + cell] = cell % 9 == 4 ? 1 : 0;
            }
            for (std::uint32_t y = 0; y < 3; ++y) one[y * 9 + 1] = other[y * 9 + 1] = 90'000;
            // And a cell that is no row, full of heat: to the plain program a cell that conducts nothing.
            one[27 + 7] = 0;
            other[27 + 7] = no_row;
            one[7] = other[7] = 1'000'000;
            for (int step = 0; step < 60; ++step) {
                expect(fatal(plain->step(shape, one, next, {.boundary = {ca::Edge::clamp, 0}}).has_value()));
                one.swap(next);
                expect(fatal(tabled->step(shape, other, next, {.boundary = {ca::Edge::clamp, 0}}).has_value()));
                other.swap(next);
                if (!std::ranges::equal(std::span(one).first(27), std::span(other).first(27))) {
                    expect(false) << std::format("wall {}: the heat differs at step {}", wall, step);
                    break;
                }
            }
            expect(eq(other[7], 1'000'000u)) << "what is no row kept its own";
            expect(eq(std::accumulate(other.begin(), other.begin() + 27, std::uint64_t{0}), std::uint64_t{1'270'000})) << "and the field what it held";
        }
    };

    "encoding 3 — cells that move by their rows' weight and mobility move as cells that carry the two"_test = [] {
        // The soup above, twice: each cell carrying its weight and mobility, and each carrying a key into a table
        // of the six kinds. Tick after tick the same cell is in the same place.
        World world({13, 9});
        const std::array<Kind, 6> kinds{air, sand, water, oil, smoke, stone};
        ca::Reactions staged;
        staged.table = {.key = 0, .columns = 2, .values = {}};
        for (const Kind kind : kinds) staged.table.values.insert(staged.table.values.end(), {kind.weight, kind.mobility});
        const std::array<ca::Exchange, 1> moving{{{.weight = 0, .mobility = 1, .carried = {0, 1}, .seed = 7, .by_table = true}}};
        const auto tabled = ca::Program::prepare(3, ca::Stages{.exchanges = moving, .reactions = &staged});
        expect(fatal(tabled.has_value()));
        expect(tabled->moves() && eq(tabled->reach(), 1u));
        const std::size_t area = world.area();
        std::vector<std::uint32_t> cells(area * 3), next(cells.size());
        for (std::uint32_t y = 0; y < 9; ++y)
            for (std::uint32_t x = 0; x < 13; ++x) {
                const std::uint32_t kind = (x * 7 + y * 13 + x * y) % 7;  // the seventh: no row — it stays, as a cell of no weight that may not move
                world.put(x, y, kind < 6 ? kinds[kind] : Kind{0, 0});
                cells[world.at(x, y)] = kind < 6 ? kind : no_row;
                cells[area + world.at(x, y)] = world.cells[2 * area + world.at(x, y)];
                cells[2 * area + world.at(x, y)] = world.cells[3 * area + world.at(x, y)];
            }
        std::uint64_t moved = 0;
        for (std::uint64_t tick = 0; tick < 200; ++tick) {
            const ca::Edge edge = tick % 3 == 0 ? ca::Edge::wrap : ca::Edge::clamp;
            const auto stepped = tabled->step(world.shape, cells, next, {.boundary = {edge, 0}, .tick = tick});
            expect(fatal(stepped.has_value()));
            cells.swap(next);
            const std::uint64_t changed = world.step(edge);
            moved += stepped->changed;
            bool same = stepped->changed == changed;
            for (std::size_t cell = 0; cell < area && same; ++cell) {
                const std::uint32_t key = cells[cell];
                same = (key == no_row ? 0u : kinds[key].weight) == world.cells[cell] && cells[area + cell] == world.cells[2 * area + cell] && cells[2 * area + cell] == world.cells[3 * area + cell];
            }
            if (!same) {
                expect(false) << std::format("tick {}: the two fields part", tick);
                break;
            }
        }
        expect(moved > 100u) << "and they did move";
    };

    "encoding 3 — a malformed table or reaction is refused where it is"_test = [] {
        using R = ca::Read;
        const auto with = [](auto change) {
            ca::Reactions staged = embers();
            change(staged);
            return staged;
        };
        expect(eq(staged_refusal(embers()), std::string("accepted")));
        const auto code = [](const ca::Reactions& staged, std::uint32_t channels = 5) {
            const auto program = ca::Program::prepare(channels, ca::Stages{.reactions = &staged});
            return program ? std::string("accepted") : std::string(program.error().description().name);
        };
        expect(eq(code(with([](ca::Reactions& staged) { staged.table.values.pop_back(); })), std::string("cellular.table"))) << "not rows of its columns";
        expect(eq(code(with([](ca::Reactions& staged) { staged.table.columns = 0; })), std::string("cellular.table")));
        expect(eq(code(with([](ca::Reactions& staged) { staged.table.key = 5; })), std::string("cellular.channels"))) << "a key that is no channel";
        expect(eq(code(with([](ca::Reactions& staged) { staged.near = {{1, 0}, {0, 1}}; })), std::string("cellular.offset"))) << "what is near is near from both ends";
        expect(eq(code(with([](ca::Reactions& staged) { staged.derived = {5}; })), std::string("cellular.table")));
        expect(eq(code(with([](ca::Reactions& staged) { staged.resets[0].channel = 0; })), std::string("cellular.channels"))) << "the key does not start again: it is what changed";
        expect(eq(code(with([](ca::Reactions& staged) { staged.resets[0].from = 5; })), std::string("cellular.table")));
        expect(eq(code(embers(), 40), std::string("cellular.limit"))) << "32 channels at most with reactions";
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.reactions[0].changes[0].amount = value_of(R::lookup, 5); })), std::string("cellular.table at reaction 0")));
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.reactions[1].changes[0].channel = 0; })), std::string("cellular.reaction at reaction 1"))) << "what a cell is changes by becoming";
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.reactions[1].changes[1].channel = 2; })), std::string("cellular.reaction at reaction 1"))) << "a channel changed twice by one reaction";
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.reactions[1].changes[0].channel = 3; })), std::string("cellular.reaction at rule 0, item 0"))) << "a flag is no one's to add to";
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.reactions[3].become = 5; })), std::string("cellular.table at reaction 3")));
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.reactions[0].flag = 0; })), std::string("cellular.channels at reaction 0")));
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.reactions[2].when[0].left = value_of(R::near_channel, 5); })), std::string("cellular.channels at reaction 2")));
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.reactions[0].when[0].right = value_of(R::derived, 1); })), std::string("cellular.reaction at reaction 0"))) << "a derived value that is not there";
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.near.clear(); })), std::string("cellular.reaction at reaction 2"))) << "what is near, with no neighbourhood";
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.adjustments[0].amount = value_of(R::near_lookup, 4); })), std::string("cellular.reaction at adjustment 0")))
            << "an adjustment reads the cell alone";
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.adjustments[0].when[0].left = value_of(R::derived, 0); })), std::string("cellular.reaction at adjustment 0")))
            << "and its row as written";
        expect(eq(staged_refusal(with([](ca::Reactions& staged) { staged.adjustments[0].derived = 1; })), std::string("cellular.reaction at adjustment 0")));
        // The other stages with a table.
        const std::vector<ca::Offset> beside{ca::cardinal.begin(), ca::cardinal.end()};
        expect(eq(staged_refusal(embers(), 5, {{.channel = 2, .offsets = beside, .shift = 4, .by_limit = 4, .by_column = 4}}), std::string("accepted")));
        expect(eq(staged_refusal(embers(), 5, {{.channel = 2, .offsets = beside, .shift = 4, .by_limit = 4, .by_column = 5}}), std::string("cellular.table at transfer 0")));
        expect(eq(staged_refusal(embers(), 5, {{.channel = 2, .offsets = beside, .shift = 4, .by = 1, .by_limit = 4, .by_column = 4}}), std::string("cellular.table at transfer 0")))
            << "by a channel or by a column";
        expect(eq(staged_refusal(embers(), 5, {{.channel = 0, .offsets = beside, .shift = 4}}), std::string("cellular.transfer at transfer 0"))) << "what a cell is does not flow";
        expect(eq(staged_refusal(embers(), 5, {}, {{.weight = 1, .mobility = 4, .by_table = true}}), std::string("accepted")));
        expect(eq(staged_refusal(embers(), 5, {}, {{.weight = 1, .mobility = 5, .by_table = true}}), std::string("cellular.table at exchange 0")));
        expect(eq(staged_refusal(embers(), 5, {}, {{.weight = 1, .mobility = 4, .carried = {1, 2}, .by_table = true}}), std::string("cellular.exchange at exchange 0")))
            << "the key goes with the cell";
        const std::array<ca::Transfer, 1> without{{{.channel = 0, .offsets = beside, .shift = 4, .by_column = 0}}};
        const auto no_table = ca::Program::prepare(2, ca::Stages{.transfers = without});
        expect(!no_table && no_table.error().code == ca::Errc::table) << "a column, and no table";
        const std::array<ca::Exchange, 1> lookup{{{.weight = 0, .mobility = 1, .by_table = true}}};
        const auto no_rows = ca::Program::prepare(2, ca::Stages{.exchanges = lookup});
        expect(!no_rows && no_rows.error().code == ca::Errc::table);
    };

    "encoding 3, sparse — embers burn out and the field sleeps; one put back wakes its tile; the same bits as running all"_test = [] {
        const ca::Reactions staged = embers();
        const auto program = ca::Program::prepare(5, ca::Stages{.reactions = &staged});
        expect(fatal(program.has_value()));
        const ca::Shape shape{24, 16};
        const std::size_t area = 24 * 16;
        std::vector<std::uint32_t> start(area * 5, 0);
        for (std::uint32_t y = 0; y < 16; ++y)
            for (std::uint32_t x = 0; x < 24; ++x) {
                const std::size_t cell = y * 24 + x;
                // Embers on the left half, at every level; the right half is calm from the start (and under 10:
                // a calm cell at 10 would grow for ever, by the second reaction).
                start[cell] = x < 12 && (x * 7 + y * 13 + x * y) % 3 == 0 ? ember : calm;
                start[area + cell] = start[cell] == ember ? (x * 5 + y * 3) % 20 : (x * 5 + y * 3) % 10;
                start[2 * area + cell] = (x * 11 + y) % 70;
            }
        for (const ca::Shape tile : {ca::Shape{4, 4}, ca::Shape{8, 8}, ca::Shape{5, 3}}) {
            for (const bool alternate : {false, true}) {
                Twice both(*program, shape, tile, {ca::Edge::constant, 99}, start, alternate);
                int steps = 0;
                for (; steps < 40 && both.asleep() < both.activity.size(); ++steps) expect(fatal(both.step(steps % 2 == 1))) << std::format("step {}, tiles {}x{}", steps, tile.width, tile.height);
                expect(eq(both.asleep(), both.activity.size())) << "every ember ash: the field is asleep";
                expect(std::ranges::none_of(std::span(both.sparse).first(area), [](std::uint32_t key) { return key == ember; }));
                // A tile is roused by the tile beside it, not by the cell: in tiles of 8 every tile is beside one
                // that burns, and all are run to the end.
                if (tile.width < 8) expect(both.cells_run < static_cast<std::uint64_t>(steps) * area) << "the calm half was not run while the embers burnt";
                if (tile.width == 4 && !alternate)
                    std::println("embers on 24 x 16 in tiles of 4 x 4: asleep after {} steps, {} cells run of {}", steps, both.cells_run, static_cast<std::uint64_t>(steps) * area);
                expect(fatal(both.step() && both.step()));
                expect(eq(both.last.cells, std::uint64_t{0})) << "asleep, nothing is run";
                // An ember put back among the calm: its tile and those within reach run again, until it is ash too.
                both.write(0, 20, 9, ember);
                both.write(1, 20, 9, 3);
                expect(both.asleep() < both.activity.size());
                for (int more = 0; more < 12; ++more) expect(fatal(both.step())) << std::format("after the ember, step {}", more);
                expect(eq(both.sparse[9 * 24 + 20], ash));
                expect(eq(both.asleep(), both.activity.size())) << "and asleep again";
            }
        }
    };

    "the shared owned-state history rebuilds cellular states and inputs without a second checkpoint engine"_test = [] {
        const ca::Shape shape{19,11};const auto p=ca::Program::prepare(3,reaction_rules());expect(p.has_value());if(!p)return;
        auto state=initial(shape);
        mlibs::simulation::Timeline<Model> history({*p,shape,state,state},30,5);
        std::vector<std::vector<std::uint32_t>> frames{state};
        for(int f=0;f<25;++f) {history.step({f==8,15,5});frames.push_back(history.state().current);}
        for(int f:{25,0,8,15,10,24}) {expect(history.seek(f));expect(history.state().current==frames[static_cast<std::size_t>(f)]);}
        expect(history.seek(8));history.step({false});expect(history.state().current!=frames[9]);
        expect(!history.seek(10));expect(history.seek(8));history.step({true,15,5});expect(history.state().current==frames[9]);
    };
}};
