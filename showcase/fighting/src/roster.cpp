module openmui.showcase.fighting;

import std;
import gongfu.data;
import gongfu.present2d;
import openmui.image;
import openmui.raster;

namespace omui::fighting {

namespace p2 = gongfu::present2d;

const Roster& roster() {
    static const Roster r = [] {
        Roster out;
        out.system.round_frames = 99 * 60;
        for (const files::FighterFiles& f : files::fighters) {
            auto fighter = gongfu::data::read_fighter(f.fighter, std::format("{}.fighter.gongfu.toml", f.name), f.boxes,
                                                      std::format("{}.boxes.gongfu.toml", f.name));
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
            out.names.emplace_back(f.name);
            out.fighters.push_back(std::move(*fighter));
            out.looks.push_back(std::move(*look));
            out.looks.push_back(std::move(right));
        }
        for (const auto& [name, text] :
             std::array<std::pair<std::string_view, std::string_view>, 2>{{{"study", files::study_stage}, {"street", files::street_stage}}}) {
            auto art = p2::read_stage(text, std::format("{}.stage.gongfu.toml", name));
            if (!art) {
                for (const auto& d : art.error()) out.problems.push_back(d.text());
                continue;
            }
            out.stages.emplace(std::string(name), std::move(*art));
        }
        for (const auto& [file, bytes] : std::array<std::pair<std::string_view, std::string_view>, 17>{{{"art/mo-parts.webp", files::mo_parts_art},
                                                                     {"art/mo-faces.webp", files::mo_faces_art},
                                                                     {"art/mo-arms.webp", files::mo_arms_art},
                                                                     {"art/ni-parts.webp", files::ni_parts_art},
                                                                     {"art/ni-cutin-eyes.webp", files::ni_cutin_eyes_art},
                                                                     {"art/ni-cutin-strike.webp", files::ni_cutin_strike_art},
                                                                     {"art/ni-finisher.webp", files::ni_finisher_art},
                                                                     {"art/kun-parts.webp", files::kun_parts_art},
                                                                     {"art/kun-arms.webp", files::kun_arms_art},
                                                                     {"art/kun-faces.webp", files::kun_faces_art},
                                                                     {"art/kun-cutin-eyes.webp", files::kun_cutin_eyes_art},
                                                                     {"art/kun-cutin-strike.webp", files::kun_cutin_strike_art},
                                                                     {"art/kun-finisher.webp", files::kun_finisher_art},
                                                                     {"art/mo-fx.webp", files::mo_fx_art},
                                                                     {"art/mo-cutin-eyes.webp", files::mo_cutin_eyes_art},
                                                                     {"art/mo-cutin-strike.webp", files::mo_cutin_strike_art},
                                                                     {"art/mo-finisher.webp", files::mo_finisher_art}}}) {
            auto img = omui::imageio::decode({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
            if (!img) {
                out.problems.push_back(std::format("{}: {}", file, img.error()));
                continue;
            }
            omui::imageio::premultiply(*img);  // the renderer's pixels are premultiplied
            Roster::Art a{img->width, img->height, {}};
            a.pixels.resize(static_cast<std::size_t>(img->width) * static_cast<std::size_t>(img->height));
            std::memcpy(a.pixels.data(), img->rgba.data(), img->rgba.size());
            out.art.emplace(std::string(file), std::move(a));
        }
        out.homes = {{"mo", "study"}, {"ni", "street"}};  // the rest's to come (quality design §5)
        return out;
    }();
    return r;
}

const p2::Stage* stage_art(std::string_view arena, std::string_view player1) {
    const Roster& r = roster();
    if (arena == "plain") return nullptr;
    std::string_view name = arena;
    if (name.empty()) {
        const auto home = r.homes.find(player1.empty() ? std::string_view(r.names.empty() ? "" : r.names.front()) : player1);
        if (home == r.homes.end()) return nullptr;
        name = home->second;
    }
    const auto it = r.stages.find(name);
    return it == r.stages.end() ? nullptr : &it->second;
}

std::size_t fighter_index(std::string_view name) {
    const auto& names = roster().names;
    const auto it = std::ranges::find(names, name);
    return it == names.end() ? 0 : static_cast<std::size_t>(it - names.begin());
}

}  // namespace omui::fighting
