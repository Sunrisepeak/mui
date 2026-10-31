// openmui.showcase.markdown_wall — S1, the Markdown wall (canvas-markdown §3).
//
// Documents (a folder's *.md, or the benchmark's synthetic set) become a
// scene: a workspace per folder — a backdrop with a label strip — and on it
// a window per document, on shelves. A window's tiers (§3.2):
//
//   T0  < 40 px     a block and a title strip: two instanced rounded boxes
//   T1  40–900 px   a texture of its page (Kind::view), rasterised on the pool
//                   at the mip level the screen asks for — the nearest ready
//                   one stretched meanwhile, so a fast zoom is never blank
//   T3  ≥ 900 px    a live document (or as soon as it is focused): the same
//                   element as the texture, so nothing shifts when it goes live
//
// The canvas holds canvas-kit's select, drag (windows by their title bars),
// snap and minimap, and two declarative behaviours embedded from
// behaviors/: group-drag (a workspace by its label, with what lies on it) and
// views (1: one to one; 2: the overview). A HUD in immediate mode reads the
// canvas's statistics; `stream` writes into windows at a language model's
// pace, live windows every tick and textures at most four times a second.
export module openmui.showcase.markdown_wall;

import std;
import openmui;
import openmui.ui.canvas;
import openmui.ui.canvas.kit;
import openmui.ui.markdown;
// behaviors/*.behavior.omui.toml, embedded by build.mcpp (openmui.build.embed).
export import :group_drag_behavior;
export import :views_behavior;

export namespace omui::wall {

struct Document {
    std::string key;     // stable: a hash of its path
    std::string title;   // its first heading, else its file's name
    std::string path;    // relative to the folder, '/' between parts
    std::string group;   // its folder ("." for the top)
    std::string source;  // the Markdown
};

// Every *.md under a folder, sorted by path.
[[nodiscard]] std::expected<std::vector<Document>, std::string> read_folder(const std::filesystem::path& folder);
// The benchmark's documents: README-like pages, long ones, code, Chinese,
// tables — the same for a seed on every machine; `groups` workspaces.
[[nodiscard]] std::vector<Document> synthetic(std::size_t count, std::uint64_t seed = 1, std::size_t groups = 0);

// The documents into a scene (cleared first): workspaces in a grid, windows
// on shelves inside them; registers the kinds "workspace" and "window".
// `live` is where a window's live source comes from (a streamed one's).
struct Geometry {
    double window = 640;        // a window's width (world units: pixels at 1:1)
    double gap = 48;            // between windows
    double margin = 64;         // inside a workspace
    double label = 72;          // a workspace's label strip
    double between = 360;       // between workspaces
    float  texture_from = 160;  // a window's size on screen (px) from which its page is a texture; below, a block and its title
};
class Model;
void build(Model& model, omui::world::Scene& scene, const Geometry& g = {});

// The height a window gets for its text.
[[nodiscard]] double window_height(const Document& d) noexcept;
[[nodiscard]] std::string key_of(std::string_view path);

class Model {
public:
    Model() = default;
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    ~Model();

    std::vector<Document> docs;
    std::string           title = "Markdown wall";
    std::string           scene = "wall";
    omui::Runtime*        runtime = nullptr;
    // The camera, both ways (the HUD shows the zoom).
    omui::Signal<double>  x = omui::signal(0.0), y = omui::signal(0.0), zoom = omui::signal(1.0);
    omui::Signal<bool>    hud = omui::signal(true);

    [[nodiscard]] const Document* document(std::string_view key) const;
    // A window's live text (a signal a live window binds), made on first use:
    // the streamed ones change; the rest are their documents'.
    [[nodiscard]] omui::Signal<std::string> live(std::string_view key);
    // Its text now (a texture's), without making a signal.
    [[nodiscard]] std::string text(std::string_view key) const;

    // ── streaming (§3.4) ──
    // `windows` windows (those nearest the view's centre) written into at
    // `rate` tokens a second each, until stop_stream().
    void stream(std::size_t windows, double rate = 50);
    void stop_stream();
    [[nodiscard]] std::size_t streaming() const noexcept { return streams_.size(); }
    [[nodiscard]] std::uint64_t tokens() const noexcept { return tokens_; }

    // The canvas, when mounted (at `canvas_path`).
    [[nodiscard]] omui::Canvas* canvas() const;
    std::string canvas_path = "main/wall";

    // A link followed in a live window: another document of the wall (a
    // path relative to the window's, `..` and all) is brought into view and
    // focused; an address opens where the system opens addresses; a heading
    // of the same document is the document's own (it scrolls there).
    // Answers the key of the window it went to, if any.
    std::optional<std::string> follow(std::string_view from_key, std::string_view href);
    // The document at a path relative to the folder, '/' between parts.
    [[nodiscard]] const Document* at_path(std::string_view path) const;

    // ── reading: open, back and forward, search ──
    // A window brought into view to read (focused, live), the camera before
    // it remembered for back(); forward is forgotten.
    void open(std::string_view key);
    // The camera as it was before the last open (back), or after it (forward).
    bool back();
    bool forward();
    [[nodiscard]] bool can_back() const noexcept { return !back_.empty(); }
    [[nodiscard]] bool can_forward() const noexcept { return !forward_.empty(); }
    struct Found {
        std::string key, title, path, line;  // line: where the words are (a title or path match: its first line)
    };
    // The documents with these words (any case), titles first, then paths, then text: at most `limit`.
    [[nodiscard]] std::vector<Found> search(std::string_view words, std::size_t limit = 8) const;
    omui::Signal<bool>        searching = omui::signal(false);  // the search box shown (Ctrl+F, /)
    omui::Signal<std::string> query = omui::signal(std::string());
    void open_search();   // the box shown, the keys its (Ctrl+F, /, the toolbar)
    void close_search();  // the box put away, the keys the wall's again
    std::string query_path = "main/middle/finder/query";

    // ── keeping: the arrangement and the camera, and the documents' edits ──
    // Where the folder's own state lives (its `.openmui/wall.layout.omui.json`);
    // empty: nothing kept (made documents, a page).
    std::filesystem::path folder;
    bool                  restored = false;  // the camera came back from it (the canvas starts there, not fitted)
    // Put the arrangement and the camera back as last kept (after build): whether there was one.
    bool restore();
    // Keep them (soon: changes a moment apart are kept once).
    void keep();
    // A document's edit (a task ticked) written to its file, whole or not at all.
    std::expected<void, std::string> save_document(std::string_view key);
    [[nodiscard]] std::filesystem::path layout_file() const;
    omui::Signal<std::string> said = omui::signal(std::string());  // the last thing kept or failed, for the HUD

private:
    struct Stream {
        std::string   key;
        double        owed = 0;     // tokens due, not yet written
        std::uint64_t next = 0;     // the next token of its text
        double        touched = -1; // when its texture was last made stale
    };
    void tick();
    void schedule();
    std::vector<Stream>                                        streams_;
    std::unordered_map<std::string, omui::Signal<std::string>> live_;
    std::uint64_t                                              timer_ = 0;
    double                                                     rate_ = 50;
    omui::seconds                                              last_{0};
    std::uint64_t                                              tokens_ = 0;
    struct Camera {
        double x = 0, y = 0, zoom = 1;
    };
    std::vector<Camera>                                        back_, forward_;
    std::uint64_t                                              keep_timer_ = 0;
    void go(const Camera& c);
    omui::FrameDemandLease                                     pace_;  // while streaming: 60 a second is enough for text
    std::shared_ptr<int>                                       alive_ = std::make_shared<int>(0);  // a pending tick's: expired once the model is gone
};

// Registers the canvas, canvas-kit and the wall's own behaviours.
void register_all();

[[nodiscard]] omui::AppSpec app(Model& model);

}  // namespace omui::wall
