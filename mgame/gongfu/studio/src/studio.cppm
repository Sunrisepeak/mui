// gongfu.studio — making fighters (D-85 §8.5).
//
//     auto baked = gongfu::studio::bake(fighter, look, gongfu::studio::fingerprint(fighter_file, look_file));
//     write(gongfu::data::write_boxes(baked));            // <name>.boxes.gongfu.toml
//
// Baking (G-5): every frame of every move is posed by the look's clip for
// it; where a limb reaches out of the stance's box (an arm in a jab, a leg in
// a sweep), that frame gets the box and the limb's — rounded out to a grid,
// so the frames of a motion that look alike share one range. The simulation
// reads the result and never samples the animation.
export module gongfu.studio;

import std;
export import gongfu.data;
export import gongfu.present2d;

export namespace gongfu::studio {

struct BakeOptions {
    float grid = 5;      // boxes rounded outward to this many pixels
    float scale = 0.85f; // how much of a limb's thickness is hittable
    float margin = 12;   // a limb reaching out less than this is within the body's box
};

[[nodiscard]] data::Baked bake(const Fighter& fighter, const present2d::Look& look, std::string from, BakeOptions options = {});

// What a boxes file was baked from: the hash of the fighter's and the look's text.
[[nodiscard]] std::string fingerprint(std::string_view fighter_text, std::string_view look_text);

// One fighter's files in a directory: `<name>.fighter.gongfu.toml`,
// `<name>.look.gongfu.toml`, `<name>.boxes.gongfu.toml`.
struct Files {
    std::string           name;
    std::filesystem::path fighter, look, boxes;
};
[[nodiscard]] std::vector<Files> fighters_in(const std::filesystem::path& dir);

// What a boxes file should hold now; a problem with the fighter or the look when they do not read.
[[nodiscard]] std::expected<std::string, std::vector<std::string>> baked_text(const Files& files);

// The fighters of a directory carried in a program (what gongfu.rules
// builds): a module partition `<module>:gongfu_fighters` holding each one's
// files as text,
//
//     export namespace <ns> {
//     struct FighterFiles { std::string_view name, fighter, look, boxes; };
//     inline constexpr std::array<FighterFiles, N> fighters{…};
//     }
//
// — refused, with what to run, when a boxes file is not what its fighter
// and look make now.
struct EmbedOptions {
    std::string module;  // the program's module (openmui.showcase.fighting)
    std::string ns;      // the namespace of the texts (omui::fighting::files)
};
[[nodiscard]] std::expected<std::string, std::vector<std::string>> partition(const std::filesystem::path& dir, const EmbedOptions& options);

// A contact sheet: every clip of a look, a cell each — its move's first
// active frame solid (else the clip's middle), its first and last frames
// faint, and on the solid one the hit boxes (red) and hurtboxes (blue). PNG
// bytes, and the clips in the order of the cells.
struct Sheet {
    std::vector<std::uint8_t> png;
    std::vector<std::string>  cells;
    int                       width = 0, height = 0;
};
[[nodiscard]] std::expected<Sheet, std::string> sheet(const Fighter& fighter, const present2d::Look& look, int columns = 6);

}  // namespace gongfu::studio
