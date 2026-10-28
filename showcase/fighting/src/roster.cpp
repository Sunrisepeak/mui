module openmui.showcase.fighting;

import std;
import gongfu.data;
import gongfu.present2d;

namespace omui::fighting {

namespace p2 = gongfu::present2d;

const Roster& roster() {
    static const Roster r = [] {
        Roster out;
        out.system.round_frames = 99 * 60;
        struct Files {
            std::string_view name, fighter, look;
        };
        for (const Files& f : {Files{"kata", files::kata_fighter, files::kata_look}}) {
            auto fighter = gongfu::data::read_fighter(f.fighter, std::format("{}.fighter.gongfu.toml", f.name));
            auto look = p2::read_look(f.look, std::format("{}.look.gongfu.toml", f.name));
            if (!fighter || !look) {
                if (!fighter)
                    for (const auto& d : fighter.error()) out.problems.push_back(d.text());
                if (!look)
                    for (const auto& d : look.error()) out.problems.push_back(d.text());
                continue;
            }
            p2::bind(*look, *fighter);
            p2::Look right = *look;
            p2::use_palette(right, 1);
            out.fighters.push_back(std::move(*fighter));
            out.looks.push_back(std::move(*look));
            out.looks.push_back(std::move(right));
        }
        return out;
    }();
    return r;
}

}  // namespace omui::fighting
