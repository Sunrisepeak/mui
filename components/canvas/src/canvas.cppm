// openmui.ui.canvas — an infinite canvas (canvas-markdown §1, OMUI_EXT_canvas).
//
//     import openmui.ui.canvas;
//     auto& wall = omui::world::scene("wall");
//     wall.kind("note", {.tiers = {{0, Tier::Draw::box}, {120, Tier::Draw::custom}}, .draw = draw_note});
//     wall.add({.kind = "note", .x = 120, .y = 80, .width = 240, .height = 160, .key = "todo"});
//     omui::canvas({.id = "board", .scene = "wall"});
//
// The scene (:scene) holds nodes by column with a grid index; the camera
// (:camera) looks at it in double precision; the component (:view) draws
// what it sees, by each node's tier, and makes the largest live. The
// interface is contract/canvas.contract.omui.toml.
export module openmui.ui.canvas;

export import openmui.core;
export import openmui.view;
export import :canvas_contract;
export import :scene;
export import :camera;
export import :textures;
export import :view;
