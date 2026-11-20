// omui.host.sdl3.gpu — the GPU path of a desktop window (mlibs.compose): SDL_GPU's device and xrender's renderer
// on it, installed at omui.host.sdl3's slot (`omui::gpu_slot`). A package of its own: an application built
// without it draws with the CPU and carries neither the backend nor the renderer's shaders. omui.sdk installs it
// (feature `gpu`, in the worlds `app` and `full`).
//
//     omui::host::sdl3::gpu::install();   // once, before a host is made
export module omui.host.sdl3.gpu;

export namespace omui::host::sdl3::gpu {

// Installs the GPU path in the process's composition.
void install();

}  // namespace omui::host::sdl3::gpu
