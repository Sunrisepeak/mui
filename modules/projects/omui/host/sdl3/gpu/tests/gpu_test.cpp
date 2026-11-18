// omui.host.sdl3.gpu: installed, the host's slot has it; the host's tests draw with it.
import std;
import mlibs.compose;
import mlibs.rhi;
import omui.host.sdl3;
import omui.host.sdl3.gpu;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"host.sdl3.gpu", [] {
    "installed, the slot has the provider: named, with every function the host calls"_test = [] {
        expect(mlibs::compose::process().get(omui::gpu_slot) == nullptr) << "absent until installed";
        omui::host::sdl3::gpu::install();
        const omui::GpuService* service = mlibs::compose::process().get(omui::gpu_slot);
        expect(fatal(service != nullptr));
        expect(service->name == "sdl-gpu" && service->begin && service->create && service->claim && service->renderer);
        omui::host::sdl3::gpu::install();  // a second installation changes nothing
        expect(mlibs::compose::process().get(omui::gpu_slot) == service);
    };
}};
