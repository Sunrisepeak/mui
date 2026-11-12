module xengine.cellular.rhi.cts.api;
import std;
import xengine.cellular;
import xengine.cellular.rhi;
import mlibs.hash;

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
r::Result<void> exercise(r::Device& device,std::string_view label,Shape shape,std::uint32_t channels,
        std::span<const Rule> rules,std::vector<std::uint32_t> initial,Boundary boundary,std::uint32_t ticks,
        std::uint32_t batch,Totals& report,mlibs::hash::Fnv1a& hash) {
    auto p=Program::prepare(channels,rules);if(!p)return r::fail(r::Errc::invalid,"invalid CTS fixture");
    auto kernel=Kernel::prepare(device,*p);if(!kernel)return std::unexpected(kernel.error());
    Buffers buffers{device};
    const r::BufferDesc desc{.size=static_cast<std::uint32_t>(initial.size()*4),.usage=r::BufferUsage::compute_read|r::BufferUsage::compute_write};
    auto a=device.create(desc);if(!a)return std::unexpected(a.error());buffers.a=*a;
    auto b=device.create(desc);if(!b)return std::unexpected(b.error());buffers.b=*b;
    Field in{&device,*a,shape,channels},out{&device,*b,shape,channels};
    r::CommandList list;list.upload(*a,0,std::span<const std::uint32_t>(initial));
    auto submitted=device.submit(list);if(!submitted)return std::unexpected(submitted.error());list.clear();
    auto expected=initial,next=initial;
    for(std::uint32_t t=1;t<=ticks;++t) {
        if(!p->step(shape,expected,next,{.tile={7,3},.boundary=boundary,.reverse_tiles=true}))return r::fail(r::Errc::invalid,"CPU CTS step failed");
        expected.swap(next);
        auto recorded=(*kernel)->record(list,in,out,boundary);if(!recorded)return recorded;
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
        if(!p->step(shape,expected,next,{.boundary=boundary}))return r::fail(r::Errc::invalid,"CPU replay failed");
        expected.swap(next);auto recorded=(*kernel)->record(list,in,out,boundary);if(!recorded)return recorded;
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
    for(unsigned invalid=0;invalid<8;++invalid) {
        auto bad_in=in,bad_out=out;auto edge=boundary;
        if(invalid==0)bad_in.owner=nullptr;
        if(invalid==1)bad_out=in;
        if(invalid==2)bad_in.channels=0;
        if(invalid==3)bad_out.shape.width=0;
        if(invalid==4)bad_in.buffer={};
        if(invalid==5)edge.edge=static_cast<Edge>(3);
        if(invalid==6) {bad_in.buffer=*small;bad_in.shape={2,2};bad_out.shape={2,2};}
        if(invalid==7)bad_in.buffer=*wrong_usage;
        auto refused=(*kernel)->record(list,bad_in,bad_out,edge);
        if(refused || list.size()!=count || list.hash()!=before)return r::fail(r::Errc::invalid,"invalid recording changed the caller's command list");
        ++report.validation_checks;
    }
    ++report.scenarios;return {};
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
