// The fighters this program carries: their files (fighters/*.fighter.gongfu.toml)
// read once, and how each looks.
export module openmui.showcase.fighting:roster;

import std;
import gongfu.data;
import gongfu.present2d;

export namespace omui::fighting {

struct Roster {
    gongfu::SystemDef                      system;
    std::vector<std::string>               names;     // by its files' name: kata, mo
    std::vector<gongfu::Fighter>           fighters;
    std::vector<gongfu::present2d::Look>   looks;   // two per fighter: its first palette (the left side's) and its second (the right's)
    std::vector<std::string>               problems;  // a file that did not read, with its line and column
};

// Read on first use; the same for the whole program.
[[nodiscard]] const Roster& roster();

// A fighter's index by its files' name; the first for an empty or unknown one.
[[nodiscard]] std::size_t fighter_index(std::string_view name);

}  // namespace omui::fighting
