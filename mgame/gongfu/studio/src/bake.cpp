module gongfu.studio;

import std;
import gongfu;
import gongfu.data;
import gongfu.present2d;

namespace gongfu::studio {

namespace {

namespace p2 = gongfu::present2d;

std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

// A box rounded outward to the grid.
Box rounded(const Box& b, float grid) {
    const double g = grid;
    const double l = std::floor(b.x.to_double() / g) * g, bt = std::floor(b.y.to_double() / g) * g;
    const double r = std::ceil((b.x + b.w).to_double() / g) * g, t = std::ceil((b.y + b.h).to_double() / g) * g;
    return {Fixed::of(static_cast<std::int32_t>(l)), Fixed::of(static_cast<std::int32_t>(bt)), Fixed::of(static_cast<std::int32_t>(r - l)),
            Fixed::of(static_cast<std::int32_t>(t - bt))};
}

}  // namespace

data::Baked bake(const Fighter& fighter, const p2::Look& look, std::string from, BakeOptions options) {
    data::Baked out{fighter.def.name, std::move(from), {}};
    p2::Pose pose;
    for (std::size_t m = 0; m < fighter.def.moves.size(); ++m) {
        const MoveDef& move = fighter.def.moves[m];
        if (!move.hurt.empty()) continue;  // written by hand: the designer's
        const Box& base = move.stance == Stance::crouching ? fighter.def.hurt_crouching : move.stance == Stance::air ? fighter.def.hurt_air
                                                                                                                     : fighter.def.hurt_standing;
        // The stance's resting pose: what its box was drawn around.
        FighterState at_rest;
        at_rest.mode = move.stance == Stance::crouching ? Mode::crouch : move.stance == Stance::air ? Mode::air : Mode::idle;
        at_rest.facing = 1;
        p2::Pose rest;
        p2::pose(look, fighter, at_rest, 0, rest);
        std::vector<HurtDef> ranges;
        for (std::int32_t f = 1; f <= move.total; ++f) {
            FighterState s;
            s.mode = Mode::move;
            s.move = static_cast<std::int32_t>(m);
            s.move_frame = f;
            s.facing = 1;
            p2::pose(look, fighter, s, 0, pose);
            std::array<Box, 4> boxes{};
            const std::size_t n = p2::reach(look, pose, rest, base, options.scale, options.margin, boxes);
            if (n < 2) continue;
            std::vector<Box> frame{base};
            for (std::size_t k = 1; k < n; ++k) frame.push_back(rounded(boxes[k], options.grid));
            if (!ranges.empty() && ranges.back().frames.to == f - 1 && ranges.back().boxes == frame) ranges.back().frames.to = f;
            else ranges.push_back({{f, f}, std::move(frame)});
        }
        if (!ranges.empty()) out.moves.emplace_back(move.name, std::move(ranges));
    }
    return out;
}

std::string fingerprint(std::string_view fighter_text, std::string_view look_text) {
    std::uint64_t h = 1469598103934665603ULL;
    auto eat = [&](std::string_view s) {
        for (unsigned char c : s) h = (h ^ c) * 1099511628211ULL;
        h = (h ^ 0xff) * 1099511628211ULL;  // the boundary between the two
    };
    eat(fighter_text);
    eat(look_text);
    return std::format("fnv1a:{:016x}", h);
}

std::vector<Files> fighters_in(const std::filesystem::path& dir) {
    std::vector<Files> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string file = utf8(e.path().filename());
        constexpr std::string_view suffix = ".fighter.gongfu.toml";
        if (!file.ends_with(suffix)) continue;
        const std::string name = file.substr(0, file.size() - suffix.size());
        const auto look = dir / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()) + u8".look.gongfu.toml");
        if (!std::filesystem::exists(look, ec)) continue;  // no look: nothing to bake from
        out.push_back({name, e.path(), look, dir / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()) + u8".boxes.gongfu.toml")});
    }
    std::ranges::sort(out, {}, &Files::name);
    return out;
}

std::expected<std::string, std::vector<std::string>> baked_text(const Files& files) {
    const std::string fighter_text = read(files.fighter), look_text = read(files.look);
    std::vector<std::string> problems;
    auto fighter = data::read_fighter(fighter_text, utf8(files.fighter.filename()));
    auto look = p2::read_look(look_text, utf8(files.look.filename()));
    if (!fighter)
        for (const auto& d : fighter.error()) problems.push_back(d.text());
    if (!look)
        for (const auto& d : look.error()) problems.push_back(d.text());
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return data::write_boxes(bake(*fighter, *look, fingerprint(fighter_text, look_text)));
}

namespace {

// A raw string literal holding `text`: its delimiter one the text does not contain.
std::string literal(std::string_view text) {
    std::string d = "gongfu";
    while (text.find(")" + d + "\"") != std::string_view::npos) d += "_";
    return std::format("R\"{}({}){}\"", d, text, d);
}

}  // namespace

std::expected<std::string, std::vector<std::string>> partition(const std::filesystem::path& dir, const EmbedOptions& o) {
    std::vector<std::string> problems;
    struct One {
        std::string name, fighter, look, boxes;
    };
    std::vector<One> all;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string file = utf8(e.path().filename());
        constexpr std::string_view suffix = ".fighter.gongfu.toml";
        if (!file.ends_with(suffix)) continue;
        One one{file.substr(0, file.size() - suffix.size()), read(e.path()), {}, {}};
        const auto look = dir / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(one.name.data()), one.name.size()) + u8".look.gongfu.toml");
        const auto boxes = dir / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(one.name.data()), one.name.size()) + u8".boxes.gongfu.toml");
        if (std::filesystem::exists(look, ec)) {
            one.look = read(look);
            auto want = baked_text({one.name, e.path(), look, boxes});
            if (!want) {
                problems.insert(problems.end(), want.error().begin(), want.error().end());
            } else if (!std::filesystem::exists(boxes, ec) || read(boxes) != *want) {
                problems.push_back(std::format("{}.boxes.gongfu.toml is not what {}'s look and moves make now: run `gongfu-studio bake {}`", one.name, one.name, utf8(dir)));
            } else {
                one.boxes = *want;
            }
        }
        all.push_back(std::move(one));
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    std::ranges::sort(all, {}, &One::name);
    std::string s = std::format("// Generated by gongfu-studio from {}: the fighters this program carries. Do not edit:\n"
                                "// change the files (and `gongfu-studio bake` their hurtboxes); the build writes this again.\n"
                                "export module {}:gongfu_fighters;\n\nimport std;\n\nexport namespace {} {{\n\n"
                                "struct FighterFiles {{\n    std::string_view name, fighter, look, boxes;\n}};\n\n"
                                "inline constexpr std::array<FighterFiles, {}> fighters{{{{\n",
                                utf8(dir.filename()), o.module, o.ns, all.size());
    for (const One& one : all)
        s += std::format("    {{\"{}\",\n     {},\n     {},\n     {}}},\n", one.name, literal(one.fighter), literal(one.look), literal(one.boxes));
    s += std::format("}}}};\n\n}}  // namespace {}\n", o.ns);
    return s;
}

}  // namespace gongfu::studio
