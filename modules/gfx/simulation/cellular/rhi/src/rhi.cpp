module xengine.simulation.cellular.rhi.api;
import std;
import mlibs.trace;
import :rules_shaders;

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
    auto count=program_.values(input.shape);if(!count)return invalid(std::string(error_name(count.error().code)));
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
}
