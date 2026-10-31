// openmui.render — the rendering architecture (D-81).
//
//   :pool    textures by what they are, made once and handed out again in
//            later frames; one nobody asked for in a while is released
//   :graph   a frame as passes over the device's textures: each says what
//            it reads and writes, a texture made for the frame is checked to
//            be written before it is read, a pass whose work reaches no
//            output is left out, and each runs in a trace span of its name
//
// The rendering features record into it: render.vector (the display list's
// GPU renderer) today; sprites, meshes and particles share the same device,
// pool and order (2D and 3D in one frame).
export module openmui.render;

export import :pool;
export import :graph;
