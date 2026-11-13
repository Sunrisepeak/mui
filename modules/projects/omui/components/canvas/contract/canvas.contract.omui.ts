// An infinite canvas (canvas-markdown §1, OMUI_EXT_canvas): a scene of nodes
// in world space — thousands of windows, notes, shapes — seen through a
// camera. The mechanism only: pan by dragging (or a trackpad's two fingers),
// zoom about the cursor (the wheel, a pinch), a fling that glides to a stop,
// zoom limits, named views; how nodes look at each size is their kind's, and
// selection, dragging, handles, snapping, connectors and the minimap are
// canvas-kit's behaviours. The semantic tree states what is on screen; `find`
// searches the whole scene.
import { defineContract } from "openmui/define";

export default defineContract({
  canvas: {
    role: "canvas",
    spec: "0.1",
    doc: "An infinite canvas: a scene of nodes seen through a camera that pans and zooms.",
    props: {
      scene: { type: "string", default: "", doc: "The scene it shows, by name (world::scene(name) fills it)." },
      x: { type: "f64", default: 0, bindable: true, doc: "The world point at the centre of the view, x." },
      y: { type: "f64", default: 0, bindable: true, doc: "The world point at the centre of the view, y." },
      zoom: { type: "f64", default: 1, bindable: true, doc: "Screen pixels per world unit." },
      min_zoom: { type: "f64", default: 0.01, doc: "The farthest out the camera goes." },
      max_zoom: { type: "f64", default: 16, doc: "The farthest in the camera goes." },
      inertia: { type: "bool", default: true, doc: "A fling glides on and slows to a stop." },
      background: { type: "color", default: "background", doc: "Behind the scene." },
      label: { type: "string", default: "", doc: "The canvas's accessible name." },
      cache_mb: {
        type: "f64",
        default: 256,
        doc: "Memory for nodes' textures (MiB); beyond it the least recently drawn go, and their nodes fall back a tier.",
      },
      behaviors: {
        type: "list<string>",
        default: [],
        doc: "What else it does, by name: canvas-kit's select, drag, resize, snap, connect, grid, minimap, delete, a program's own, or a declarative behaviour's (*.behavior.omui.ts).",
      },
      upload_mb: {
        type: "f64",
        default: 4,
        doc: "Textures entering the frame at most (MiB a frame): what a GPU uploads stays within a frame's budget (measured on an RTX 4080: a quarter to half a millisecond a MiB; 4 leave a 120 Hz frame most of its 8.3 ms).",
      },
      arrive_glyphs: {
        type: "f64",
        default: 8192,
        doc: "Glyphs of lists entering the frame at most (content: \"lists\"): lists the pool returns together enter over a few frames, in the order asked for, so no one frame prepares all their glyphs (measured on an RTX 4080: a quarter of a millisecond a thousand). The first always enters.",
      },
      content: {
        type: "string",
        default: "textures",
        doc: "How a node's content is drawn at the texture tier: `textures` (rasterised on the task pool a level of zoom, the nearest ready one stretched until the level's is in — what a CPU renderer wants), or `lists` (its display list made once a revision and drawn under the camera's transform every frame: nothing stretched and nothing swapped while the camera zooms, each shape where it is — what a GPU renderer that covers shapes from their outlines wants, D-188; a content that cannot be drawn so keeps its textures).",
      },
      promotion: {
        type: "string",
        default: "at_rest",
        bindable: true,
        doc: "When a node whose tier is live goes live: `at_rest` once the camera has rested a moment (while it moves, nodes not live yet keep their textures, so a flight never waits on their layout), `always` at once, moving or not (what live nodes cost while the camera moves, seen), `never` (textures only; the focused node still goes live).",
      },
      bounded: {
        type: "bool",
        default: false,
        doc: "The camera keeps to the scene: never farther out than all of it, never panned past its edges; a wheel or a trackpad that would take it farther goes on to the page (a canvas in a page scrolls with it).",
      },
      start: {
        type: "string",
        default: "",
        doc: "Where the camera starts: `fit` shows the whole scene once the canvas has its size and its scene has nodes, `fit:<key>` the node with that key (a region of it), `preset:<name>` one of the scene's named views; empty, x, y and zoom.",
      },
    },
    events: {
      camera_changed: { args: { x: "f64", y: "f64", zoom: "f64" }, coalesce: "frame" },
      activated: { args: { key: "string" } },
      selection_changed: { args: { keys: "list<string>" } },
      moved: { args: { keys: "list<string>", dx: "f64", dy: "f64" } },
      resized: { args: { key: "string", x: "f64", y: "f64", width: "f64", height: "f64" } },
      connected: { args: { edge: "string", from: "string", to: "string" } },
      disconnected: { args: { edge: "string", from: "string", to: "string" } },
      deleted: { args: { keys: "list<string>" } },
    },
    actions: {
      pan_to: { effect: "read", args: { x: "f64", y: "f64" }, doc: "Move the view's centre to a world point (animated)." },
      zoom_to: { effect: "read", args: { zoom: "f64" }, doc: "Zoom about the view's centre (animated)." },
      fit: {
        effect: "read",
        args: { target: "any" },
        doc: "Show `all` the scene, the node with this key, or the nodes with these keys (a list: all of them), whole (animated).",
      },
      focus_node: {
        effect: "read",
        args: { key: "string" },
        doc: "Bring the node with this key to the centre at a zoom that shows it whole, and focus it.",
      },
      camera_preset: { effect: "read", args: { name: "string" }, doc: "Go to one of the scene's named views (animated)." },
      move_by: {
        effect: "reversible",
        args: { keys: "list<string>", dx: "f64", dy: "f64" },
        doc: "Moves the nodes with these keys by (dx, dy) world units — what a drag of them does; a drag is undone by its opposite.",
      },
      find: {
        effect: "read",
        args: { query: "any" },
        doc: "The keys of the scene's nodes matching {in_rect: [x, y, w, h], near: [x, y], radius, name, kind, role}, nearest first when `near` is given — over the whole scene, not only what is on screen.",
      },
    },
  },
});
