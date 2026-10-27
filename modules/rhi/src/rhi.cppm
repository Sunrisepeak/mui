// openmui.rhi — the GPU abstraction openmui's renderer is written once on
// (architecture §8.2, D-33, D-58): resources by description, shaders as code
// in each backend's format with their resource counts, pipelines, and a
// frame's work as a recorded CommandList that a Device validates and runs.
// Backends (rhi-sdl first) implement Device; nothing here knows of one.
export module openmui.rhi;

export import :types;
export import :commands;
export import :device;
