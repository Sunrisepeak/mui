// openmui.layout — a layout engine: flexbox, grid, z-stacks and absolute
// positioning, incremental and deterministic (spec §4, V-2).
//
//   :style   what a node asks of layout (a CSS subset, border-box sizing)
//   :engine  the layout tree, measure functions, caching, computation
//
// Standard library and openmui.geom only: any tree of boxes can use it.
export module openmui.layout;

export import :style;
export import :engine;
