module openmui.showcase.fighting;

import std;
import gongfu.data;
import gongfu.present2d;
import openmui.image;
import openmui.raster;
import openmui.render.vector;

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
        for (const auto& [file, text] : files::stage_files) {
            const std::string_view name = file.substr(0, file.find('.'));
            auto art = p2::read_stage(text, file);
            if (!art) {
                for (const auto& d : art.error()) out.problems.push_back(d.text());
                continue;
            }
            out.stages.emplace(std::string(name), std::move(*art));
        }
        // Every picture the program carries, by its file beside the look or HUD that names it.
        std::vector<std::pair<std::string, std::string_view>> files_of_art;
        for (const auto& [name, bytes] : files::art_files) files_of_art.emplace_back(std::format("art/{}", name), bytes);
        for (const auto& [name, bytes] : files::hud_files) files_of_art.emplace_back(std::format("hud/{}", name), bytes);
        for (const auto& [name, bytes] : files::stage_art_files) files_of_art.emplace_back(std::format("stages/art/{}", name), bytes);
        // Each picture decoded, and its relief made where a look asks for one, a file to a thread (their results the same however they run).
        std::map<std::string_view, p2::Picture::Relief> reliefs;
        for (const p2::Look& look : out.looks)
            for (const p2::Picture& pic : look.pictures)
                if (pic.relief) reliefs.emplace(pic.file, *pic.relief);
        struct Decoded {
            std::expected<omui::imageio::Image, std::string> image = std::unexpected(std::string());
            std::optional<omui::imageio::Image>              normal;
        };
        std::vector<Decoded> decoded(files_of_art.size());
        omui::Tasks::shared().parallel_for(files_of_art.size(), 1, [&](std::size_t from, std::size_t to) {
            for (std::size_t i = from; i < to; ++i) {
                const auto& [file, bytes] = files_of_art[i];
                Decoded& d = decoded[i];
                d.image = omui::imageio::decode({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
                if (!d.image) continue;
                if (const auto r = reliefs.find(file); r != reliefs.end())
                    d.normal = omui::imageio::normal_map(*d.image, {.bevel = r->second.bevel, .detail = r->second.detail, .smooth = r->second.smooth,
                                                                     .strength = r->second.strength});
                omui::imageio::premultiply(*d.image);  // the renderer's pixels are premultiplied
            }
        });
        auto art_of = [](const omui::imageio::Image& img) {
            Roster::Art a{img.width, img.height, {}};
            a.pixels.resize(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height));
            std::memcpy(a.pixels.data(), img.rgba.data(), img.rgba.size());
            return a;
        };
        for (std::size_t i = 0; i < files_of_art.size(); ++i) {
            const std::string& file = files_of_art[i].first;
            if (!decoded[i].image) {
                out.problems.push_back(std::format("{}: {}", file, decoded[i].image.error()));
                continue;
            }
            out.art.emplace(std::string(file), art_of(*decoded[i].image));
            if (decoded[i].normal) out.normals.emplace(std::string(file), art_of(*decoded[i].normal));
        }
        out.homes = {{"mo", "study"}, {"ni", "street"}, {"kun", "rooftop"}};  // the rest's to come (quality design §5)
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

Registered::~Registered() {
    if (!store) return;
    for (const auto& [file, ref] : art) store->remove(ref);
    for (const auto& [file, ref] : normals) store->remove(ref);
}

omui::render::ImageRef Registered::picture(std::string_view file) const {
    const auto it = art.find(file);
    return it == art.end() ? omui::render::ImageRef{} : it->second;
}

void Registered::bind(std::vector<p2::Picture>& pictures, std::string_view prefix) const {
    for (p2::Picture& pic : pictures) {
        const std::string file = std::format("{}{}", prefix, pic.file);
        pic.ref = picture(file);
        if (const auto n = normals.find(file); n != normals.end()) pic.normal = n->second;
    }
}

std::shared_ptr<const Registered> registered(omui::render::ImageStore& store) {
    // By store: one registration while anything holds it (the UI thread's, like the store).
    static std::map<omui::render::ImageStore*, std::weak_ptr<const Registered>> held;
    if (auto alive = held[&store].lock()) return alive;
    auto r = std::make_shared<Registered>();
    r->store = &store;
    for (const auto& [file, a] : roster().art) r->art.emplace(file, store.add(a.width, a.height, a.pixels));
    for (const auto& [file, a] : roster().normals) r->normals.emplace(file, store.add(a.width, a.height, a.pixels));
    held[&store] = r;
    return r;
}

std::size_t fighter_index(std::string_view name) {
    const auto& names = roster().names;
    const auto it = std::ranges::find(names, name);
    return it == names.end() ? 0 : static_cast<std::size_t>(it - names.begin());
}

}  // namespace omui::fighting
