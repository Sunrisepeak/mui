module xengine.cellular.rhi.api;
import std;
import mlibs.trace;
import mlibs.math;
import :rules_shaders;
import :paint_shaders;

namespace xengine::cellular::gpu {
namespace r=mlibs::rhi;
r::ShaderFormats shader_formats() {return shaders::rules::evolve.formats();}
namespace {
// Where a prepared program's stages are in its words — walked once, as Program::prepare wrote them: the stages
// after the rules, the exchange, and (encoding 3) the table and the reactions' parts. In the order of the
// shader's uniforms from `encoding` on.
std::array<std::uint32_t,16> layout_of(std::span<const std::uint32_t> words) {
    std::array<std::uint32_t,16> layout{};
    layout[0]=words[0];
    if(words[0]<2)return layout;
    const std::uint32_t rules=words[2];
    std::uint32_t at=words[4+rules];
    layout[1]=at;
    for(std::uint32_t transfers=words[at++],t=0;t<transfers;++t)at+=6+2*words[at+5];
    const std::uint32_t exchanges=words[at++];
    layout[2]=exchanges ? at : 0;
    if(exchanges)at+=4+words[at+3];
    if(words[0]<3)return layout;
    // The table: key, rows, columns, the values; the near neighbourhood; its slots; the derived columns.
    layout[3]=at;
    const std::uint32_t key=words[at],rows=words[at+1],columns=words[at+2];
    const std::uint32_t table=at+3,near=table+rows*columns,slots=near+1+2*words[near];
    const std::uint32_t derived=slots+1+2*words[slots]+1,derived_count=words[derived-1];
    // The adjustments and the reactions: each a count, then entries that say where they end.
    const auto past=[&](std::uint32_t list) {
        std::uint32_t end=list+1;
        for(std::uint32_t count=words[list],index=0;index<count;++index)end=words[end];
        return end;
    };
    const std::uint32_t adjustments=derived+derived_count,reactions=past(adjustments),flags=past(reactions),resets=flags+1+words[flags];
    layout[4]=key;layout[5]=rows;layout[6]=columns;layout[7]=table;layout[8]=near;layout[9]=slots;layout[10]=derived;layout[11]=derived_count;
    layout[12]=adjustments;layout[13]=reactions;layout[14]=flags;layout[15]=resets;
    return layout;
}
}  // namespace
Kernel::~Kernel() {if(rules_)device_.destroy(rules_);if(pipeline_)device_.destroy(pipeline_);if(exchange_)device_.destroy(exchange_);}
r::Result<std::unique_ptr<Kernel>> Kernel::prepare(r::Device& device,const Program& program) {
    mlibs::trace::Span span("simulation.cellular.gpu.prepare","simulation");
    if(!program.channels() || program.words().empty())return std::unexpected(r::Error{r::Errc::invalid,"unprepared cellular program"});
    if(program.encoding()<1 || program.encoding()>3)
        return std::unexpected(r::Error{r::Errc::unsupported,std::format("a cellular program of encoding {} is not known to the device's kernel",program.encoding())});
    if(program.channels()>32)return std::unexpected(r::Error{r::Errc::unsupported,"the device's kernel runs at most 32 channels"});
    if(!device.caps().compute)return std::unexpected(r::Error{r::Errc::unsupported,"cellular execution requires compute"});
    auto result=std::unique_ptr<Kernel>(new Kernel(device,program));
    result->layout_=layout_of(program.words());
    // The device's kernel keeps which reactions hold in 64 bits (no array a shader indexes at run time).
    if(program.encoding()==3 && program.words()[result->layout_[13]]>64)
        return std::unexpected(r::Error{r::Errc::unsupported,"the device's kernel runs at most 64 reactions"});
    auto pipeline=device.create(r::ComputePipelineDesc{&shaders::rules::evolve,"simulation.cellular"});
    if(!pipeline)return std::unexpected(pipeline.error());result->pipeline_=*pipeline;
    if(program.moves()) {
        auto exchange=device.create(r::ComputePipelineDesc{&shaders::rules::exchange,"simulation.cellular.exchange"});
        if(!exchange)return std::unexpected(exchange.error());result->exchange_=*exchange;
    }
    auto rules=device.create(r::BufferDesc{.size=static_cast<std::uint32_t>(program.words().size_bytes()),.usage=r::BufferUsage::compute_read,.label="cellular.rules"});
    if(!rules)return std::unexpected(rules.error());result->rules_=*rules;
    r::CommandList upload;upload.upload(*rules,0,program.words());
    auto submitted=device.submit(upload);if(!submitted)return std::unexpected(submitted.error());
    result->ready_=*submitted;return result;
}
r::Result<void> Kernel::record(r::CommandList& list,Field input,Field output,Boundary boundary,std::uint64_t tick) const {
    mlibs::trace::Span span("simulation.cellular.gpu.record","simulation");
    const auto invalid=[](std::string message){return std::unexpected(r::Error{r::Errc::invalid,std::move(message)});};
    if(input.owner!=&device_ || output.owner!=&device_)return invalid("cellular field belongs to another device");
    if(input.channels!=program_.channels() || output.channels!=input.channels || input.shape.width!=output.shape.width || input.shape.height!=output.shape.height)
        return invalid("cellular field shape/channels do not match");
    auto count=program_.values(input.shape);if(!count)return invalid(count.error().text());
    if(boundary.edge>Edge::wrap)return invalid("invalid cellular boundary");
    if(!boundary.values.empty() && boundary.values.size()!=program_.channels())return invalid("a boundary's values are one a channel");
    if(input.buffer==output.buffer)return invalid("cellular input/output must be disjoint");
    const auto in=device_.describe(input.buffer),out=device_.describe(output.buffer);
    if(!in || !out || !device_.describe(rules_) || !device_.describe(pipeline_))return invalid("stale cellular resource");
    if(in->size<*count*4 || out->size<*count*4 || !r::has(in->usage,r::BufferUsage::compute_read) || !r::has(out->usage,r::BufferUsage::compute_write))
        return invalid("cellular buffer size or usage mismatch");
    // The shader's Params: the shape and boundary, the tick as the exchange takes it, the program's layout, the bath.
    std::array<std::uint32_t,24+32> uniforms{};
    const std::uint32_t tick_key=program_.moves() ? mlibs::math::mix32(program_.words()[layout_[2]+2]^mlibs::math::mix32(mlibs::math::fold32(tick))) : 0u;
    const std::uint32_t cut=static_cast<std::uint32_t>(tick&1)|static_cast<std::uint32_t>((tick>>1&1)<<1);
    const std::array<std::uint32_t,8> head{input.shape.width,input.shape.height,input.channels,static_cast<std::uint32_t>(boundary.edge),boundary.value,
                                           boundary.values.empty() ? 0u : 1u,tick_key,cut};
    std::ranges::copy(head,uniforms.begin());
    std::ranges::copy(layout_,uniforms.begin()+8);
    std::ranges::copy(boundary.values,uniforms.begin()+24);
    const std::array writes{output.buffer};
    list.begin_compute({},writes);list.set_pipeline(pipeline_);
    list.bind_storage(r::Stage::compute,0,input.buffer);list.bind_storage(r::Stage::compute,1,rules_);
    list.push_uniforms(r::Stage::compute,0,uniforms);
    list.dispatch(1+(input.shape.width-1)/8,1+(input.shape.height-1)/8);
    list.end_compute();
    if(program_.moves()) {
        // The blocks of this tick's cut, a thread each, moving what the pass before made of each cell.
        const std::uint32_t blocks_x=(input.shape.width+1)/2,blocks_y=(input.shape.height+1)/2;
        list.begin_compute({},writes);list.set_pipeline(exchange_);
        list.bind_storage(r::Stage::compute,0,input.buffer);list.bind_storage(r::Stage::compute,1,rules_);
        list.push_uniforms(r::Stage::compute,0,uniforms);
        list.dispatch(1+(blocks_x-1)/8,1+(blocks_y-1)/8);
        list.end_compute();
    }
    return {};
}

Painter::~Painter() {if(palette_)device_.destroy(palette_);if(pipeline_)device_.destroy(pipeline_);}
r::Result<std::unique_ptr<Painter>> Painter::prepare(r::Device& device,std::span<const PixelSwatch> swatches,std::uint32_t fallback,std::uint32_t channels) {
    mlibs::trace::Span span("simulation.cellular.gpu.painter","simulation");
    if(!channels)return std::unexpected(r::Error{r::Errc::invalid,"a field has a channel at least"});
    if(!device.caps().compute)return std::unexpected(r::Error{r::Errc::unsupported,"painting a field requires compute"});
    std::vector<std::uint32_t> words{static_cast<std::uint32_t>(swatches.size()),fallback};
    for(const auto& swatch:swatches) {
        words.push_back(swatch.pixel);words.push_back(static_cast<std::uint32_t>(swatch.when.size()));
        for(const auto& test:swatch.when) {
            if(test.channel>=channels)return std::unexpected(r::Error{r::Errc::invalid,std::format("a swatch tests channel {}; the field has {}",test.channel,channels)});
            words.insert(words.end(),{test.channel,test.value.low,test.value.high});
        }
    }
    auto result=std::unique_ptr<Painter>(new Painter(device,channels));
    auto pipeline=device.create(r::ComputePipelineDesc{&shaders::paint::paint,"simulation.cellular.paint"});
    if(!pipeline)return std::unexpected(pipeline.error());result->pipeline_=*pipeline;
    auto palette=device.create(r::BufferDesc{.size=static_cast<std::uint32_t>(words.size()*4),.usage=r::BufferUsage::compute_read,.label="cellular.palette"});
    if(!palette)return std::unexpected(palette.error());result->palette_=*palette;
    r::CommandList upload;upload.upload(*palette,0,std::span<const std::uint32_t>(words));
    auto submitted=device.submit(upload);if(!submitted)return std::unexpected(submitted.error());
    result->ready_=*submitted;return result;
}
r::Result<void> Painter::record(r::CommandList& list,Field cells,r::Texture image) const {
    const auto invalid=[](std::string message){return std::unexpected(r::Error{r::Errc::invalid,std::move(message)});};
    if(cells.owner!=&device_)return invalid("the field belongs to another device");
    if(cells.channels!=channels_ || !cells.shape.width || !cells.shape.height)return invalid("the field's shape or channels do not match the painter's");
    const auto buffer=device_.describe(cells.buffer);const auto texture=device_.describe(image);
    const auto values=static_cast<std::uint64_t>(cells.shape.width)*cells.shape.height*cells.channels;
    if(!buffer || buffer->size<values*4 || !r::has(buffer->usage,r::BufferUsage::compute_read))return invalid("the field's buffer is too small or not readable by compute");
    if(!texture || texture->format!=r::Format::rgba8_unorm || texture->width!=cells.shape.width || texture->height!=cells.shape.height ||
       !r::has(texture->usage,r::TextureUsage::compute_write))
        return invalid("the image is an rgba8 texture of the field's size that compute writes");
    const std::array<std::uint32_t,4> uniforms{cells.shape.width,cells.shape.height,0,0};
    const std::array writes{image};
    list.begin_compute(writes,{});list.set_pipeline(pipeline_);
    list.bind_storage(r::Stage::compute,0,cells.buffer);list.bind_storage(r::Stage::compute,1,palette_);
    list.push_uniforms(r::Stage::compute,0,uniforms);
    list.dispatch(1+(cells.shape.width-1)/8,1+(cells.shape.height-1)/8);
    list.end_compute();return {};
}
}
