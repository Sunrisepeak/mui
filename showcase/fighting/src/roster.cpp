module omui.showcase.fighting;

import std;
import gongfu.data;
import gongfu.present2d;
import mlibs.image;
import xrender.raster;
import xrender.vector;

namespace omui::fighting {

namespace p2 = gongfu::present2d;

namespace {

// Pictures decoded (premultiplied), each with its relief where a look asks for one, a file to a
// thread (their results the same however they run), into the roster's.
void decode_art(Roster& out, const std::vector<std::pair<std::string, std::string_view>>& files_of_art) {
    std::map<std::string_view, p2::Picture::Relief> reliefs;
    for (const p2::Look& look : out.looks)
        for (const p2::Picture& pic : look.pictures)
            if (pic.relief) reliefs.emplace(pic.file, *pic.relief);
    struct Decoded {
        std::expected<mlibs::image::Image, std::string> image = std::unexpected(std::string());
        std::optional<mlibs::image::Image>              normal;
    };
    std::vector<Decoded> decoded(files_of_art.size());
    omui::Tasks::shared().parallel_for(files_of_art.size(), 1, [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
            const auto& [file, bytes] = files_of_art[i];
            Decoded& d = decoded[i];
            d.image = mlibs::image::decode({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
            if (!d.image) continue;
            if (const auto r = reliefs.find(file); r != reliefs.end())
                d.normal = mlibs::image::normal_map(*d.image, {.bevel = r->second.bevel, .detail = r->second.detail, .smooth = r->second.smooth,
                                                                 .strength = r->second.strength});
            mlibs::image::premultiply(*d.image);  // the renderer's pixels are premultiplied
        }
    });
    auto art_of = [](const mlibs::image::Image& img) {
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
        out.art.insert_or_assign(std::string(file), art_of(*decoded[i].image));
        if (decoded[i].normal) out.normals.insert_or_assign(std::string(file), art_of(*decoded[i].normal));
    }
}

Roster& the_roster() {
    static Roster r = [] {
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
        decode_art(out, files_of_art);
        out.art_later = files::art_files.empty() && !files::fighters.empty();  // left out of this build: fetched after it starts (arrive_art)
        out.homes = {{"mo", "study"}, {"ni", "street"}, {"kun", "rooftop"}, {"li", "greenhouse"}, {"yue", "gate"}, {"ke", "clock"}};  // the quality design §5
        return out;
    }();
    return r;
}

std::atomic<std::uint32_t> generation{0};

}  // namespace

const Roster& roster() { return the_roster(); }

std::uint32_t art_generation() noexcept { return generation.load(); }

bool art_pending() { return the_roster().art_later; }

bool arrive_art(const std::filesystem::path& dir) {
    Roster& r = the_roster();
    if (!r.art_later) return true;
    std::error_code ec;
    // The page's: there once it has written every file (`.ready` beside the directory, last).
    if (dir == std::filesystem::path(later_art_dir) && !std::filesystem::exists(dir.parent_path() / ".ready", ec)) return false;
    std::vector<std::pair<std::string, std::filesystem::path>> names;  // each file's name (UTF-8, not the code page's) and where it is
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".webp") {
            const std::u8string n = e.path().filename().u8string();
            names.emplace_back(std::string(reinterpret_cast<const char*>(n.data()), n.size()), e.path());
        }
    if (ec || names.empty()) return false;
    std::ranges::sort(names);
    std::vector<std::string> bytes(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::ifstream in(names[i].second, std::ios::binary);
        bytes[i].assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::vector<std::pair<std::string, std::string_view>> files;
    for (std::size_t i = 0; i < names.size(); ++i) files.emplace_back(std::format("art/{}", names[i].first), bytes[i]);
    decode_art(r, files);
    r.art_later = false;
    ++generation;
    return true;
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

xrender::ImageRef Registered::picture(std::string_view file) const {
    const auto it = art.find(file);
    return it == art.end() ? xrender::ImageRef{} : it->second;
}

void Registered::bind(std::vector<p2::Picture>& pictures, std::string_view prefix) const {
    for (p2::Picture& pic : pictures) {
        const std::string file = std::format("{}{}", prefix, pic.file);
        pic.ref = picture(file);
        if (const auto n = normals.find(file); n != normals.end()) pic.normal = n->second;
    }
}

std::shared_ptr<const Registered> registered(xrender::ImageStore& store) {
    // By store: one registration while anything holds it (the UI thread's, like the store) — again once more pictures arrived.
    static std::map<xrender::ImageStore*, std::weak_ptr<const Registered>> held;
    if (auto alive = held[&store].lock(); alive && alive->generation == art_generation()) return alive;
    auto r = std::make_shared<Registered>();
    r->store = &store;
    r->generation = art_generation();
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
