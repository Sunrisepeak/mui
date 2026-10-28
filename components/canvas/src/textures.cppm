// openmui.ui.canvas:textures — the canvas's textures of its nodes (canvas-
// markdown §3.2 T1–T2, §3.3).
//
// A node of a kind with a content() — or a view(), laid out and painted by
// an offscreen runtime — is drawn, from some size on screen up, as a texture
// of that content: rasterised in the background, on the shared
// task pool, at a mip level — a power of two of the world's scale — that the
// screen's resolution asks for. Until that level is ready the nearest ready
// one is stretched (sharper first), so a fast zoom shows something at once
// and sharpens when it stops; before any is ready, the node is a box.
//
// What is asked for is issued by priority — screen area, then nearness to
// the view's centre — at most a few jobs at a time; what comes back enters
// the image store within a per-frame budget of bytes (the uploads a GPU
// renderer makes of it); the textures are kept within a memory budget,
// least recently drawn first out. A content revision that moves
// (Scene::touch) makes a node's textures stale.
export module openmui.ui.canvas:textures;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.text;
import :scene;

export namespace omui::world {

class Textures {
public:
    struct Options {
        std::size_t budget = std::size_t{256} << 20;  // bytes of textures kept
        std::size_t upload = std::size_t{16} << 20;   // bytes entering the store per frame
        std::size_t jobs = 0;                          // in flight at once (0: twice the pool's threads)
        int         max_side = 4096;                   // a texture's largest side, pixels
    };
    Textures();
    ~Textures();
    Textures(const Textures&) = delete;
    Textures& operator=(const Textures&) = delete;

    void configure(const Options& o) noexcept { options_ = o; }

    // The texture to draw for a node this frame: the wanted level if ready,
    // else the nearest ready one (sharper first). `frame` marks it used.
    struct Pick {
        render::ImageRef image;
        int              level = 0;
        bool             exact = false;
    };
    [[nodiscard]] std::optional<Pick> pick(std::string_view key, std::uint64_t revision, int level, std::uint64_t frame);

    // The frame wants `level` of a node and it is not ready: a candidate.
    void want(std::string_view key, std::uint64_t revision, int level, NodeRef node, float priority);
    // The frame's candidates, by priority, as jobs (their content built here,
    // on the calling thread — a view as an element, laid out on the pool;
    // rasterised on the pool) in the theme given.
    void issue(const Scene& scene, const render::ImageStore& images, typeset::FontCollection* fonts, const Theme& theme);

    // What came back, into the store within the frame's upload budget, and
    // the budget of bytes kept; true when something new can be drawn. `wait`
    // (a deterministic run): every job issued is waited for first.
    bool collect(render::ImageStore& images, std::uint64_t frame, bool wait = false);
    // Every texture out of the store (the canvas goes, its scene or its theme changes).
    void clear(render::ImageStore& images);

    [[nodiscard]] bool busy() const;  // jobs in flight or results waiting

    struct Stats {
        std::size_t textures = 0, bytes = 0, jobs = 0, waiting = 0, entered = 0, evicted = 0;
    };
    [[nodiscard]] Stats stats() const;

    // The mip level a world scale asks for (2^level texels per world unit).
    [[nodiscard]] static int level_for(double texels_per_unit) noexcept;

private:
    struct Level {
        render::ImageRef image;
        std::size_t      bytes = 0;
        std::uint64_t    used = 0;
    };
    struct Entry {
        std::uint64_t        revision = 0;
        std::map<int, Level> levels;
    };
    struct Candidate {
        std::string   key;
        std::uint64_t revision;
        int           level;
        NodeRef       node;
        float         priority;
    };
    struct Done;
    struct Inbox;
    struct Offscreen;

    Options                                       options_;
    std::unordered_map<std::string, Entry>        entries_;
    std::vector<Candidate>                        candidates_;
    std::set<std::tuple<std::string, std::uint64_t, int>> in_flight_;
    std::shared_ptr<Inbox>                        inbox_;
    std::shared_ptr<Offscreen>                    offscreen_;  // runtimes that lay views out on the pool
    std::size_t                                   bytes_ = 0;
    Stats                                         last_;
    std::uint64_t                                 next_order_ = 0;
};

}  // namespace omui::world
