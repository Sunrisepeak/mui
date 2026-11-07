module xengine.simulation.cellular.rhi.api;
import std;
import mlibs.trace;
import :rules_shaders;
import :paint_shaders;

namespace xengine::simulation::cellular::gpu {
namespace r=mlibs::rhi;
r::ShaderFormats shader_formats() {return shaders::rules::evolve.formats();}
Kernel::~Kernel() {if(rules_)device_.destroy(rules_);if(pipeline_)device_.destroy(pipeline_);}
r::Result<std::unique_ptr<Kernel>> Kernel::prepare(r::Device& device,const Program& program) {
    mlibs::trace::Span span("simulation.cellular.gpu.prepare","simulation");
    if(!program.channels() || program.words().empty() || program.words()[0]!=1)
        return std::unexpected(r::Error{r::Errc::invalid,"unprepared cellular program or unsupported encoding"});
    if(!device.caps().compute)return std::unexpected(r::Error{r::Errc::unsupported,"cellular execution requires compute"});
    auto result=std::unique_ptr<Kernel>(new Kernel(device,program));
    auto pipeline=device.create(r::ComputePipelineDesc{&shaders::rules::evolve,"simulation.cellular"});
    if(!pipeline)return std::unexpected(pipeline.error());result->pipeline_=*pipeline;
    auto rules=device.create(r::BufferDesc{.size=static_cast<std::uint32_t>(program.words().size_bytes()),.usage=r::BufferUsage::compute_read,.label="cellular.rules"});
    if(!rules)return std::unexpected(rules.error());result->rules_=*rules;
    r::CommandList upload;upload.upload(*rules,0,program.words());
    auto submitted=device.submit(upload);if(!submitted)return std::unexpected(submitted.error());
    result->ready_=*submitted;return result;
}
r::Result<void> Kernel::record(r::CommandList& list,Field input,Field output,Boundary boundary) const {
    mlibs::trace::Span span("simulation.cellular.gpu.record","simulation");
    const auto invalid=[](std::string message){return std::unexpected(r::Error{r::Errc::invalid,std::move(message)});};
    if(input.owner!=&device_ || output.owner!=&device_)return invalid("cellular field belongs to another device");
    if(input.channels!=program_.channels() || output.channels!=input.channels || input.shape.width!=output.shape.width || input.shape.height!=output.shape.height)
        return invalid("cellular field shape/channels do not match");
    auto count=program_.values(input.shape);if(!count)return invalid(count.error().text());
    if(boundary.edge>Edge::wrap)return invalid("invalid cellular boundary");
    if(input.buffer==output.buffer)return invalid("cellular input/output must be disjoint");
    const auto in=device_.describe(input.buffer),out=device_.describe(output.buffer);
    if(!in || !out || !device_.describe(rules_) || !device_.describe(pipeline_))return invalid("stale cellular resource");
    if(in->size<*count*4 || out->size<*count*4 || !r::has(in->usage,r::BufferUsage::compute_read) || !r::has(out->usage,r::BufferUsage::compute_write))
        return invalid("cellular buffer size or usage mismatch");
    const std::array<std::uint32_t,8> uniforms{input.shape.width,input.shape.height,input.channels,static_cast<std::uint32_t>(boundary.edge),boundary.value,0,0,0};
    const std::array writes{output.buffer};
    list.begin_compute({},writes);list.set_pipeline(pipeline_);
    list.bind_storage(r::Stage::compute,0,input.buffer);list.bind_storage(r::Stage::compute,1,rules_);
    list.push_uniforms(r::Stage::compute,0,uniforms);
    list.dispatch(1+(input.shape.width-1)/8,1+(input.shape.height-1)/8);
    list.end_compute();return {};
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
