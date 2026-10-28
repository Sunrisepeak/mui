// The fighters this program carries: their files (fighters/*.fighter.gongfu.toml)
// read once, and how each looks.
export module openmui.showcase.fighting:roster;

import std;
import gongfu.data;
import gongfu.present2d;

export namespace omui::fighting {

struct Roster {
    gongfu::SystemDef                      system;
    std::vector<gongfu::Fighter>           fighters;
    std::vector<gongfu::present2d::Look>   looks;   // two per fighter: the left side's and the right's colours
    std::vector<std::string>               problems;  // a file that did not read, with its line and column
};

// Read on first use; the same for the whole program.
[[nodiscard]] const Roster& roster();

// A body in a colour: eleven bones, capsules and an ellipse head, its clips.
[[nodiscard]] gongfu::present2d::Look body_look(std::uint32_t body, std::uint32_t limbs);

}  // namespace omui::fighting
