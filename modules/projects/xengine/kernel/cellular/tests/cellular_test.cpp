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
