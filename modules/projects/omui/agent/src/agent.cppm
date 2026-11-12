// openmui.agent — the agent endpoint of an application (spec §9, plan §11):
// openmui.tree@0.1 as JSON lines over local IPC, served between frames.
export module omui.agent;

export import omui.core;
export import omui.view;
export import omui.semantic;
export import :catalog;
export import :design;
export import :generate;
export import :broker;
export import :tasks;
export import :present;
export import :protocol;
export import :subject;
export import :endpoint;
export import :api;
