module xengine.cellular.rhi.cts.api;
import std;
import xengine.cellular;
import xengine.cellular.rhi;
import mlibs.hash;
import mlibs.math;
import xengine.cellular.matter;
import xengine.matter.json;

namespace xengine::cellular::gpu::cts {
namespace r=mlibs::rhi;
namespace {
using C=Condition;using W=WriteKind;
struct Buffers {
    r::Device& device;r::Buffer a,b;
    ~Buffers(){if(a)device.destroy(a);if(b)device.destroy(b);}
};
r::Result<void> compare(r::Device& device,r::Buffer buffer,std::span<const std::uint32_t> expected,
        std::string_view label,std::uint32_t tick,Totals& report,mlibs::hash::Fnv1a& hash) {
    std::vector<std::uint32_t> actual(expected.size());
    auto read=device.read(buffer,0,std::as_writable_bytes(std::span(actual)));if(!read)return read;
    for(std::size_t i=0;i<actual.size();++i) {
        if(actual[i]!=expected[i])return r::fail(r::Errc::backend,std::format("{} tick {} value {}: GPU {} != CPU {}",label,tick,i,actual[i],expected[i]));
        hash.le(actual[i],4);
    }
    ++report.comparisons;report.compared_values+=actual.size();return {};
}
// A program stepped on the CPU and recorded on the device from the same state, tick after tick (the tick
// advancing: where cells move, the cut takes its four positions and the choices change), compared every `batch`.
r::Result<void> exercise(r::Device& device,std::string_view label,Shape shape,const Program& program,
        std::vector<std::uint32_t> initial,Boundary boundary,std::uint32_t ticks,
        std::uint32_t batch,Totals& report,mlibs::hash::Fnv1a& hash) {
    const std::uint32_t channels=program.channels();
    const Program* const p=&program;
    auto kernel=Kernel::prepare(device,program);if(!kernel)return std::unexpected(kernel.error());
    Buffers buffers{device};
    const r::BufferDesc desc{.size=static_cast<std::uint32_t>(initial.size()*4),.usage=r::BufferUsage::compute_read|r::BufferUsage::compute_write};
    auto a=device.create(desc);if(!a)return std::unexpected(a.error());buffers.a=*a;
    auto b=device.create(desc);if(!b)return std::unexpected(b.error());buffers.b=*b;
    Field in{&device,*a,shape,channels},out{&device,*b,shape,channels};
    r::CommandList list;list.upload(*a,0,std::span<const std::uint32_t>(initial));
    auto submitted=device.submit(list);if(!submitted)return std::unexpected(submitted.error());list.clear();
    auto expected=initial,next=initial;
    for(std::uint32_t t=1;t<=ticks;++t) {
        if(!p->step(shape,expected,next,{.tile={7,3},.boundary=boundary,.reverse_tiles=true,.tick=t-1}))return r::fail(r::Errc::invalid,"CPU CTS step failed");
        expected.swap(next);
        auto recorded=(*kernel)->record(list,in,out,boundary,t-1);if(!recorded)return recorded;
        std::swap(in,out);++report.steps;
        if(t%batch==0 || t==ticks) {
            auto sub=device.submit(list);if(!sub)return std::unexpected(sub.error());list.clear();
            auto checked=compare(device,in.buffer,expected,label,t,report,hash);if(!checked)return checked;
        }
    }
    // A host-owned checkpoint is restored through the same RHI, then replayed.
    // No hidden GPU state or second simulation clock is required by Kernel.
    list.upload(in.buffer,0,std::span<const std::uint32_t>(initial));expected=initial;
    for(std::uint32_t t=1;t<=3;++t) {
        if(!p->step(shape,expected,next,{.boundary=boundary,.tick=t-1}))return r::fail(r::Errc::invalid,"CPU replay failed");
        expected.swap(next);auto recorded=(*kernel)->record(list,in,out,boundary,t-1);if(!recorded)return recorded;
        std::swap(in,out);++report.steps;
    }
    submitted=device.submit(list);if(!submitted)return std::unexpected(submitted.error());list.clear();
    auto checked=compare(device,in.buffer,expected,label,3,report,hash);if(!checked)return checked;

    // Invalid recordings leave an existing command and all its payload intact.
    Buffers invalid_buffers{device};
    auto small=device.create(r::BufferDesc{.size=4,.usage=r::BufferUsage::compute_read});
    if(!small)return std::unexpected(small.error());invalid_buffers.a=*small;
    auto wrong_usage=device.create(r::BufferDesc{.size=desc.size,.usage=r::BufferUsage::vertex});
    if(!wrong_usage)return std::unexpected(wrong_usage.error());invalid_buffers.b=*wrong_usage;
    list.upload(in.buffer,0,std::span<const std::uint32_t>(initial));
    const auto before=list.hash();const auto count=list.size();
    const std::vector<std::uint32_t> one_too_many(channels+1,0);
    for(unsigned invalid=0;invalid<9;++invalid) {
        auto bad_in=in,bad_out=out;auto edge=boundary;
        if(invalid==0)bad_in.owner=nullptr;
        if(invalid==1)bad_out=in;
        if(invalid==2)bad_in.channels=0;
        if(invalid==3)bad_out.shape.width=0;
        if(invalid==4)bad_in.buffer={};
        if(invalid==5)edge.edge=static_cast<Edge>(3);
        if(invalid==6) {bad_in.buffer=*small;bad_in.shape={2,2};bad_out.shape={2,2};}
        if(invalid==7)bad_in.buffer=*wrong_usage;
        if(invalid==8)edge=Boundary{Edge::constant,0,one_too_many};  // a bath of the wrong size
        auto refused=(*kernel)->record(list,bad_in,bad_out,edge);
        if(refused || list.size()!=count || list.hash()!=before)return r::fail(r::Errc::invalid,"invalid recording changed the caller's command list");
        ++report.validation_checks;
    }
    ++report.scenarios;return {};
}
r::Result<void> exercise(r::Device& device,std::string_view label,Shape shape,std::uint32_t channels,
        std::span<const Rule> rules,std::vector<std::uint32_t> initial,Boundary boundary,std::uint32_t ticks,
        std::uint32_t batch,Totals& report,mlibs::hash::Fnv1a& hash) {
    auto program=Program::prepare(channels,rules);if(!program)return r::fail(r::Errc::invalid,"invalid CTS fixture");
    return exercise(device,label,shape,*program,std::move(initial),boundary,ticks,batch,report,hash);
}
// A field of `channels` channels, each value one of `choices` by a number of (seed, index) alone.
std::vector<std::uint32_t> scattered(Shape shape,std::uint32_t channels,std::uint32_t seed,std::span<const std::uint32_t> choices) {
    std::vector<std::uint32_t> state(static_cast<std::size_t>(shape.width)*shape.height*channels);
    for(std::size_t index=0;index<state.size();++index)
        state[index]=choices[mlibs::math::mix32(seed^mlibs::math::mix32(static_cast<std::uint32_t>(index)))%choices.size()];
    return state;
}
struct Bench {
    r::Device&          device;
    Totals              totals;
    mlibs::hash::Fnv1a  hash;
};
}  // namespace

void declare(r::Device& device, mlibs::cts::Against who, Totals* totals, std::source_location at) {
    if (who.subject.empty()) who.subject = device.caps().device;
    if (!who.has) who.has = [&device](std::string_view cap) { return cap == "compute" && device.caps().compute; };
    Bench bench{device, {}, {}};
    std::vector<mlibs::cts::Case> all;
    auto scenario = [&](std::string id, auto body) {
        all.push_back({.id = std::move(id), .contract = "xengine.cellular.gpu@1", .level = mlibs::cts::Level::exact, .needs = {"compute"},
                       .body = [body, &bench](mlibs::cts::Check& c) {
                           const r::Result<void> result = body(bench);
                           c.that(result.has_value(), result ? std::string() : result.error().message);
                       }});
    };
    const std::vector<Rule> reaction{
        {{C::self({1,{1,1}}),C::self({2,{3,0xffffffffu}})},{{1,W::set,2}}},
        {{C::self({1,{1,1}})},{{2,W::add_saturating,1}}},
        {{C::self({0,{1,1}}),C::self({1,{0,0}}),C::neighbors({1,{1,1}},cardinal,{1,4})},{{1,W::set,1},{2,W::set,0}}}
    };
    constexpr std::array<std::pair<Edge, std::string_view>, 3> edges{{{Edge::constant, "constant"}, {Edge::clamp, "clamp"}, {Edge::wrap, "wrap"}}};
    for (const auto& [edge, edge_name] : edges) {
        const Shape shape{19,11};const auto cells=shape.width*shape.height;std::vector<std::uint32_t> state(cells*3);
        std::fill_n(state.begin(),cells,1u);
        for(std::uint32_t y=0;y<shape.height;++y)state[y*shape.width+9]=0;
        state[cells+5*shape.width+2]=1;
        scenario(std::format("cellular.reaction.{}", edge_name), [=](Bench& b) {
            return exercise(b.device,"reaction",shape,3,reaction,state,{edge},24,1,b.totals,b.hash); });
        scenario(std::format("cellular.resident-batch.{}", edge_name), [=](Bench& b) {
            return exercise(b.device,"resident-batch",shape,3,reaction,state,{edge},24,12,b.totals,b.hash); });
    }
    const std::array<Offset,3> offsets{{{-32,0},{0,-32},{32,32}}};
    const std::vector<Rule> varied{
        {{C::self({0,{7,7}})},{}}, // empty first-match barrier
        {{C::neighbors({0,{7,7}},offsets,{1,3})},{{0,W::add_saturating,0xfffffff0u},{1,W::subtract_saturating,19}}},
        {{},{{0,W::set,7},{1,W::add_saturating,9}}}
    };
    for(const auto shape:std::array<Shape,3>{{{1,1},{3,2},{17,9}}})for (const auto& [edge, edge_name] : edges) {
        std::vector<std::uint32_t> state(shape.width*shape.height*2,2);
        for(std::size_t i=0;i<state.size();i+=3)state[i]=7;
        scenario(std::format("cellular.wide-halo.{}x{}.{}", shape.width, shape.height, edge_name), [=](Bench& b) {
            return exercise(b.device,"wide-halo",shape,2,varied,state,{edge,7},8,1,b.totals,b.hash); });
    }
    // The field's pixels on the device (D-132): stepped there, painted there, read back only to compare
    // with the CPU's rule (pixel_of) over the CPU's field — every cell, every few ticks, bit for bit.
    scenario("cellular.paint", [reaction](Bench& b) -> r::Result<void> {
        const Shape shape{37,23};const auto cells=shape.width*shape.height;
        std::vector<std::uint32_t> state(cells*3);
        std::fill_n(state.begin(),cells,1u);
        for(std::uint32_t y=0;y<shape.height;++y)state[y*shape.width+15]=0;
        state[cells+11*shape.width+2]=1;
        const std::vector<PixelSwatch> swatches{{{{1,{1,1}}},0xff1a7affu},{{{1,{2,2}}},0xff202a3au},{{{0,{1,1}}},0xffb8a38fu},{{{2,{5,9}}},0x80402010u}};
        const std::uint32_t fallback=0xff201810u;
        auto p=Program::prepare(3,reaction);if(!p)return r::fail(r::Errc::invalid,"fixture");
        auto kernel=Kernel::prepare(b.device,*p);if(!kernel)return std::unexpected(kernel.error());
        auto painter=Painter::prepare(b.device,swatches,fallback,3);if(!painter)return std::unexpected(painter.error());
        Buffers buffers{b.device};
        const r::BufferDesc desc{.size=static_cast<std::uint32_t>(state.size()*4),.usage=r::BufferUsage::compute_read|r::BufferUsage::compute_write};
        auto a=b.device.create(desc);if(!a)return std::unexpected(a.error());buffers.a=*a;
        auto c=b.device.create(desc);if(!c)return std::unexpected(c.error());buffers.b=*c;
        auto image=b.device.create(r::TextureDesc{r::Format::rgba8_unorm,shape.width,shape.height,r::TextureUsage::compute_write|r::TextureUsage::sampled,"cellular.image"});
        if(!image)return std::unexpected(image.error());
        Field in{&b.device,*a,shape,3},out{&b.device,*c,shape,3};
        r::CommandList list;list.upload(*a,0,std::span<const std::uint32_t>(state));
        auto expected=state,next=state;
        r::Result<void> result{};
        for(std::uint32_t t=1;t<=12 && result;++t) {
            if(!p->step(shape,expected,next))return r::fail(r::Errc::invalid,"CPU step");
            expected.swap(next);
            if(auto recorded=(*kernel)->record(list,in,out);!recorded)return recorded;
            std::swap(in,out);
            if(t%4)continue;
            if(auto painted=(*painter)->record(list,in,*image);!painted)return painted;
            auto submitted=b.device.submit(list);if(!submitted)return std::unexpected(submitted.error());list.clear();
            auto bytes=b.device.read(*image,r::Region{0,0,shape.width,shape.height});if(!bytes)return std::unexpected(bytes.error());
            for(std::uint32_t k=0;k<cells;++k) {
                std::uint32_t got=0;std::memcpy(&got,bytes->data()+k*4,4);
                const auto want=pixel_of(swatches,fallback,expected,cells,k);
                if(got!=want){result=r::fail(r::Errc::backend,std::format("tick {} cell {}: GPU {:08x} != CPU {:08x}",t,k,got,want));break;}
                b.hash.le(got,4);
            }
            ++b.totals.comparisons;b.totals.compared_values+=cells;
        }
        b.device.destroy(*image);
        if(!result)return result;
        // A field of another device's, a texture of the wrong size: refused, nothing appended.
        auto small=b.device.create(r::TextureDesc{r::Format::rgba8_unorm,4,4,r::TextureUsage::compute_write,"cellular.small"});
        if(!small)return std::unexpected(small.error());
        const auto count=list.size();
        const bool refused=!(*painter)->record(list,in,*small) && !(*painter)->record(list,Field{nullptr,in.buffer,shape,3},*small) && list.size()==count;
        b.device.destroy(*small);
        if(!refused)return r::fail(r::Errc::invalid,"an invalid painting was recorded");
        ++b.totals.validation_checks;++b.totals.scenarios;return {};
    });
    // Encoding 2 — aggregates and writes with terms: a neighbourhood's sum, least and greatest, counts as amounts,
    // values near the largest number so that every saturation is met; on every edge and against a bath.
    const std::array<Offset,4> wide{{{-3,0},{0,-2},{5,5},{-1,1}}};
    using T=Term;
    const std::vector<Rule> aggregated{
        Rule{{C::sum(0,surrounding,{10,0xffffffffu}),C::self({1,{0,3}})},{Write{1,W::set,1,{T::sum(0,cardinal,3,2),T::minimum(2,surrounding).taken()}}}},
        Rule{{C::maximum(2,cardinal,{5,0xffffffffu})},{Write{2,W::add_saturating,0,{T::count({0,{1,3}},surrounding,2),T::self(0).taken()}}}},
        Rule{{C::minimum(0,wide,{0,2})},{Write{0,W::subtract_saturating,1,{T::maximum(1,cardinal,1,1)}},Write{1,W::set,0xfffffff0u,{T::self(2,0xffffffffu)}}}},
    };
    const std::array<std::uint32_t,8> values{0,1,2,3,5,9,200,0xfffffff0u};
    const std::vector<std::uint32_t> bath3{4,0xfffffff0u,7};
    struct Edged { Edge edge; std::string_view name; bool bath; };
    constexpr std::array<Edged,4> edged{{{Edge::constant,"constant",false},{Edge::clamp,"clamp",false},{Edge::wrap,"wrap",false},{Edge::constant,"bath",true}}};
    for(const auto& [edge,edge_name,bath]:edged) {
        const Shape shape{23,13};
        const auto state=scattered(shape,3,11,values);
        scenario(std::format("cellular.aggregates.{}",edge_name),[=](Bench& b) {
            const Boundary boundary=bath ? Boundary{Edge::constant,0,bath3} : Boundary{edge,3};
            return exercise(b.device,"aggregates",shape,3,aggregated,state,boundary,16,1,b.totals,b.hash); });
    }
    // Encoding 2 — transfers: a quantity conducted by a channel (the lesser of a pair's, bounded) over the four
    // beside, another over the eight around at a conductance of 1; a rule writes the first in the same step.
    const std::vector<Rule> heating{Rule{{C::self({1,{2,2}}),C::self({2,{5,0xffffffffu}})},{Write{0,W::add_saturating,4000}}}};
    const std::vector<Transfer> flows{
        {.channel=0,.offsets={cardinal.begin(),cardinal.end()},.multiply=1,.shift=3,.by=1,.by_limit=2},
        {.channel=2,.offsets={surrounding.begin(),surrounding.end()},.multiply=1,.shift=3},
    };
    for(const auto& [edge,edge_name,bath]:edged) {
        const Shape shape{21,15};
        const std::array<std::uint32_t,6> quantities{0,0,1,2,9000,123456};
        auto state=scattered(shape,3,23,quantities);
        const auto area=shape.width*shape.height;
        for(std::uint32_t cell=0;cell<area;++cell)state[area+cell]=mlibs::math::mix32(cell*7+1)%3;  // the conductance: 0, 1 or 2
        scenario(std::format("cellular.transfer.{}",edge_name),[=](Bench& b) -> r::Result<void> {
            auto program=Program::prepare(3,Stages{.rules=heating,.transfers=flows});if(!program)return r::fail(r::Errc::invalid,"fixture");
            const Boundary boundary=bath ? Boundary{Edge::constant,0,bath3} : Boundary{edge,500};
            return exercise(b.device,"transfer",shape,*program,state,boundary,40,5,b.totals,b.hash); });
    }
    // Encoding 2 — the exchange: cells that fall, slide and spread change places in blocks whose cut takes its four
    // positions; three channels go with a cell, one stays; a rule changes a carried channel first (what the stages
    // before made of a cell is what moves). Odd and even sizes, every edge (a wrapped one of an even size goes round).
    const std::vector<Rule> marking{Rule{{C::self({0,{5,0xffffffffu}})},{Write{2,W::add_saturating,1},Write{3,W::set,1}}}};
    const std::vector<Exchange> moving{{.weight=0,.mobility=1,.carried={0,1,2},.seed=7}};
    for(const auto shape:std::array<Shape,4>{{{17,9},{16,10},{2,2},{1,5}}})for(const auto& [edge,edge_name,bath]:edged) {
        if(bath)continue;
        const auto area=shape.width*shape.height;
        const std::array<std::uint32_t,6> weights{0,1,2,5,9,9},mobilities{0,1,3,5,1,5},tags{0,1,2,3,4,5};
        auto state=scattered(shape,4,31,tags);
        for(std::uint32_t cell=0;cell<area;++cell) {
            const auto pick=mlibs::math::mix32(cell^0x51u)%6;
            state[cell]=weights[pick];state[area+cell]=mobilities[pick];
        }
        scenario(std::format("cellular.exchange.{}x{}.{}",shape.width,shape.height,edge_name),[=](Bench& b) -> r::Result<void> {
            auto program=Program::prepare(4,Stages{.rules=marking,.exchanges=moving});if(!program)return r::fail(r::Errc::invalid,"fixture");
            return exercise(b.device,"exchange",shape,*program,state,{edge,0},24,1,b.totals,b.hash); });
    }
    // Encoding 3 — the standard definition of matter lowered (xengine.cellular's feature `matter`): a table looked
    // up by each cell's material, every reaction that holds acting, conducted flows, cells that move by the
    // table's weight and mobility. Every material scattered over a room, some cells hot enough to light, against
    // a bath of air (an open sky), nothing beyond, and a wrapped edge.
    {
        auto standard=xengine::matter::standard();
        const bool known=standard.has_value();
        const auto matter=known ? std::make_shared<xengine::matter::Program>(std::move(*standard)) : nullptr;
        const Shape shape{48,32};
        const auto area=static_cast<std::size_t>(shape.width)*shape.height;
        std::vector<std::uint32_t> room,outside;
        if(known) {
            const auto air=*matter->material("air");
            room=matter->fresh(area,air);outside=matter->fresh(1,air);
            const auto materials=static_cast<std::uint32_t>(matter->materials().size());
            const auto temperature=*matter->channel("temperature");
            for(std::size_t cell=0;cell<area;++cell) {
                const auto pick=mlibs::math::mix32(static_cast<std::uint32_t>(cell)*0x9e3779b1u);
                if(cell<area/3 && pick%4)continue;  // air above
                matter->place(room,area,cell,pick%materials);
                if(pick%11==0)room[temperature*area+cell]=1300*256;  // hot enough to light what burns
            }
        }
        constexpr std::array<std::pair<std::string_view,int>,3> rooms{{{"open",0},{"nothing-beyond",1},{"wrap",2}}};
        for(const auto& [room_name,kind]:rooms)
            scenario(std::format("cellular.matter.standard.{}",room_name),[=](Bench& b) -> r::Result<void> {
                if(!matter)return r::fail(r::Errc::invalid,"the standard definition of matter did not load");
                auto program=lower(*matter,{.seed=5});if(!program)return r::fail(r::Errc::invalid,std::format("the standard definition did not lower: {}",program.error().text()));
                const Boundary boundary=kind==0 ? open_to(outside) : kind==1 ? nothing_beyond : Boundary{Edge::wrap,0};
                return exercise(b.device,"matter",shape,*program,room,boundary,64,8,b.totals,b.hash); });
    }
    const std::vector<std::uint32_t> identity{0,1,7,0xffffffffu};
    scenario("cellular.identity", [=](Bench& b) { return exercise(b.device,"identity",{2,2},1,{},identity,{},2,1,b.totals,b.hash); });
    scenario("cellular.unprepared-refused", [](Bench& b) -> r::Result<void> {
        ++b.totals.validation_checks;
        if (Kernel::prepare(b.device, Program{})) return r::fail(r::Errc::invalid, "an unprepared plan was accepted");
        return {};
    });
    mlibs::cts::against(
        std::move(who),
        [&all, at] {
            for (mlibs::cts::Case& one : all) mlibs::cts::declare(std::move(one), at);
        },
        at);
    bench.totals.process_hash = bench.hash.value();
    if (totals) *totals = bench.totals;
}
}
