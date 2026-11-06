// The fighters this program carries: their files (fighters/*.fighter.gongfu.toml)
// read once, and how each looks.
export module openmui.showcase.fighting:roster;

import std;
import gongfu.data;
import gongfu.present2d;
import xrender.raster;
import xrender.vector;

export namespace omui::fighting {

struct Roster {
    gongfu::SystemDef                      system;
    std::vector<std::string>               names;     // by its files' name: kata, mo
    std::vector<gongfu::Fighter>           fighters;
    std::vector<gongfu::present2d::Look>   looks;   // two per fighter: its first palette (the left side's) and its second (the right's)
    std::vector<std::string>               problems;  // a file that did not read, with its line and column
    // The stages' art, by name (stages/*.stage.gongfu.toml), and each fighter's home: the stage a versus plays on.
    std::map<std::string, gongfu::present2d::Stage, std::less<>> stages;
    std::map<std::string, std::string, std::less<>>              homes;
    // The fighters' pictures by their file (fighters/art/*.png), decoded once
    // (premultiplied): a stage registers them with its renderer's images.
    struct Art {
        int                              width = 0, height = 0;
        std::vector<xrender::raster::Rgba8> pixels;
    };
    std::map<std::string, Art, std::less<>> art;
    // Their reliefs (normal maps), by the same file, for pictures a look gives one.
    std::map<std::string, Art, std::less<>> normals;
    // The fighters' pictures were left out of this build (a page fetches them after it starts): arrive_art.
    bool art_later = false;
};

// The art of the stage named (empty: player 1's home; "plain": none); none when there is none.
[[nodiscard]] const gongfu::present2d::Stage* stage_art(std::string_view arena, std::string_view player1);

// Read on first use; the same for the whole program.
[[nodiscard]] const Roster& roster();

// Where a page writes the fighters' pictures it fetches after the program
// starts (the build's `art-later` feature on the Web; dist-toy's `later`
// files), and `.ready` beside them once all are there.
inline constexpr std::string_view later_art_dir = "/later/fighters-art";
// Whether the pictures are still to come; and, once they are in `dir`, they
// decoded into the roster (on the UI thread; true when they are, or were
// always, there). Each arrival is a new generation: a registration of an older
// one is made again (registered()), and what drew with it binds again.
[[nodiscard]] bool art_pending();
bool arrive_art(const std::filesystem::path& dir = std::filesystem::path(later_art_dir));
[[nodiscard]] std::uint32_t art_generation() noexcept;

// The roster's pictures (and their reliefs) registered with a renderer's
// image store, by file — shared by every component drawing with that store
// (the stage, the select screen): registered by the first, removed when the
// last lets go.
struct Registered {
    xrender::ImageStore*                                      store = nullptr;
    std::map<std::string, xrender::ImageRef, std::less<>>     art, normals;
    std::uint32_t                                                  generation = 0;  // the pictures' (art_generation) it registered
    Registered() = default;
    Registered(const Registered&) = delete;
    Registered& operator=(const Registered&) = delete;
    ~Registered();
    [[nodiscard]] xrender::ImageRef picture(std::string_view file) const;
    // A look's or a stage's pictures given their references (a stage's files are beside it, under `prefix`).
    void bind(std::vector<gongfu::present2d::Picture>& pictures, std::string_view prefix = {}) const;
};
[[nodiscard]] std::shared_ptr<const Registered> registered(xrender::ImageStore& store);

// A fighter's index by its files' name; the first for an empty or unknown one.
[[nodiscard]] std::size_t fighter_index(std::string_view name);

}  // namespace omui::fighting
