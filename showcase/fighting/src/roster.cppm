// The fighters this program carries: their files (fighters/*.fighter.gongfu.toml)
// read once, and how each looks.
export module openmui.showcase.fighting:roster;

import std;
import gongfu.data;
import gongfu.present2d;
import openmui.raster;

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
        std::vector<omui::raster::Rgba8> pixels;
    };
    std::map<std::string, Art, std::less<>> art;
};

// The art of the stage named (empty: player 1's home; "plain": none); none when there is none.
[[nodiscard]] const gongfu::present2d::Stage* stage_art(std::string_view arena, std::string_view player1);

// Read on first use; the same for the whole program.
[[nodiscard]] const Roster& roster();

// A fighter's index by its files' name; the first for an empty or unknown one.
[[nodiscard]] std::size_t fighter_index(std::string_view name);

}  // namespace omui::fighting
