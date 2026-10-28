// openmui.agent — the agent endpoint of an application (spec §9, plan §11):
// openmui.tree@0.1 as JSON lines over local IPC, served between frames.
export module openmui.agent;

export import openmui.core;
export import openmui.view;
export import openmui.semantic;
export import :protocol;
export import :endpoint;
