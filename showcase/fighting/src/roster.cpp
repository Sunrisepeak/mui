module openmui.showcase.fighting;

import std;
import hanim;
import gongfu.data;
import gongfu.present2d;

namespace omui::fighting {

namespace {

namespace p2 = gongfu::present2d;

enum Bones : std::int32_t { hip, torso, head, upper_back, fore_back, upper_front, fore_front, thigh_back, shin_back, thigh_front, shin_front };

hanim::Track<double> keys(std::initializer_list<std::pair<int, double>> frame_values) {
    hanim::Track<double> t;
    for (const auto& [frame, value] : frame_values) t.key(hanim::seconds(frame / 60.0), value, hanim::ease::out_cubic);
    return t;
}

p2::Clip clip(std::int32_t frames, bool loop, std::initializer_list<std::pair<Bones, hanim::Track<double>>> bones,
              hanim::Track<double> rise = {}) {
    p2::Clip c{.frames = frames, .loop = loop};
    c.angles.resize(11);
    for (const auto& [b, t] : bones) c.angles[static_cast<std::size_t>(b)] = t;
    c.rise = std::move(rise);
    return c;
}

}  // namespace

p2::Look body_look(std::uint32_t body, std::uint32_t limbs) {
    p2::Look l;
    const auto colour = gongfu::present2d::Color::hex(body), limb = gongfu::present2d::Color::hex(limbs);
    const float up = 1.5707964f, down = 3.1415927f;
    l.name = "body";
    l.bones = {
        {"hip", -1, {0, 92}, 10, up},
        {"torso", hip, {10, 0}, 56, 0},
        {"head", torso, {58, 0}, 24, 0},
        {"upper_back", torso, {50, 7}, 30, -2.5f},
        {"fore_back", upper_back, {30, 0}, 28, 1.1f},
        {"upper_front", torso, {50, -7}, 30, -2.3f},
        {"fore_front", upper_front, {30, 0}, 28, 1.6f},
        {"thigh_back", hip, {0, 7}, 46, down - 0.3f},
        {"shin_back", thigh_back, {46, 0}, 48, -0.25f},
        {"thigh_front", hip, {0, -7}, 46, down + 0.35f},
        {"shin_front", thigh_front, {46, 0}, 48, 0.35f},
    };
    l.parts = {
        {upper_back, p2::Shape::capsule, 9, 7, {}, limb, 0},  {fore_back, p2::Shape::capsule, 7, 6, {}, limb, 0},
        {thigh_back, p2::Shape::capsule, 11, 9, {}, limb, 0}, {shin_back, p2::Shape::capsule, 9, 7, {}, limb, 0},
        {torso, p2::Shape::capsule, 18, 15, {}, colour, 1},   {hip, p2::Shape::capsule, 16, 16, {}, colour, 1},
        {head, p2::Shape::ellipse, 15, 13, {}, colour, 2},    {thigh_front, p2::Shape::capsule, 11, 9, {}, colour, 3},
        {shin_front, p2::Shape::capsule, 9, 7, {}, colour, 3}, {upper_front, p2::Shape::capsule, 9, 7, {}, colour, 4},
        {fore_front, p2::Shape::capsule, 7, 6, {}, colour, 4},
    };
    l.clips["idle"] = clip(60, true, {{torso, keys({{0, 0}, {30, 0.04}, {60, 0}})}}, keys({{0, 0}, {30, -2}, {60, 0}}));
    l.clips["walk_forward"] = clip(32, true,
                                   {{thigh_front, keys({{0, 0.35}, {16, -0.35}, {32, 0.35}})},
                                    {thigh_back, keys({{0, -0.35}, {16, 0.35}, {32, -0.35}})},
                                    {shin_front, keys({{0, 0}, {8, 0.4}, {16, 0}, {32, 0}})},
                                    {shin_back, keys({{0, 0}, {16, 0}, {24, 0.4}, {32, 0}})}},
                                   keys({{0, 0}, {8, 2}, {16, 0}, {24, 2}, {32, 0}}));
    l.clips["walk_back"] = l.clips["walk_forward"];
    l.clips["crouch"] = clip(1, true, {{thigh_front, keys({{0, -0.9}})}, {shin_front, keys({{0, 1.5}})}, {thigh_back, keys({{0, -0.6}})},
                                       {shin_back, keys({{0, 1.2}})}, {torso, keys({{0, -0.15}})}},
                             keys({{0, -34}}));
    l.clips["prejump"] = l.clips["crouch"];
    l.clips["air"] = clip(1, true, {{thigh_front, keys({{0, -0.9}})}, {shin_front, keys({{0, 1.4}})}, {thigh_back, keys({{0, -0.2}})},
                                    {shin_back, keys({{0, 0.9}})}});
    l.clips["5LP"] = clip(12, false,
                          {{upper_front, keys({{0, 0}, {3, 0.75}, {5, 0.75}, {11, 0}})},
                           {fore_front, keys({{0, 0}, {3, -1.55}, {5, -1.55}, {11, 0}})},
                           {torso, keys({{0, 0}, {3, -0.12}, {11, 0}})}});
    l.clips["5HP"] = clip(24, false,
                          {{upper_back, keys({{0, 0}, {6, 1.9}, {9, 1.9}, {22, 0}})},
                           {fore_back, keys({{0, 0}, {6, -1.0}, {9, -1.0}, {22, 0}})},
                           {torso, keys({{0, 0}, {4, 0.15}, {7, -0.25}, {22, 0}})}});
    l.clips["2LK"] = clip(14, false,
                          {{thigh_front, keys({{0, -0.9}, {4, -1.6}, {7, -1.6}, {13, -0.9}})}, {shin_front, keys({{0, 1.5}, {4, 0.1}, {7, 0.1}, {13, 1.5}})},
                           {thigh_back, keys({{0, -0.6}})}, {shin_back, keys({{0, 1.2}})}, {torso, keys({{0, -0.2}})}},
                          keys({{0, -34}}));
    l.clips["6HP"] = clip(26, false,
                          {{upper_front, keys({{0, 0}, {12, 2.6}, {16, 0.6}, {24, 0}})},
                           {fore_front, keys({{0, 0}, {12, -0.4}, {16, -1.2}, {24, 0}})},
                           {torso, keys({{0, 0}, {12, 0.3}, {16, -0.35}, {24, 0}})}},
                          keys({{0, 0}, {14, 8}, {17, 0}}));
    l.clips["236P"] = clip(40, false, {{upper_front, keys({{0, 0}, {7, 1.0}, {30, 1.0}, {39, 0}})},
                                        {upper_back, keys({{0, 0}, {7, 1.3}, {30, 1.3}, {39, 0}})},
                                        {fore_front, keys({{0, 0}, {7, -1.4}, {30, -1.4}, {39, 0}})},
                                        {fore_back, keys({{0, 0}, {7, -1.2}, {30, -1.2}, {39, 0}})}});
    l.clips["623P"] = clip(48, false, {{upper_front, keys({{0, 0}, {4, 2.9}, {20, 2.9}, {40, 0}})},
                                        {fore_front, keys({{0, 0}, {4, -0.3}, {40, 0}})},
                                        {thigh_back, keys({{0, 0}, {6, 0.6}, {24, 0.6}, {34, 0}})}});
    l.clips["throw"] = clip(30, false, {{upper_front, keys({{0, 0}, {3, 1.2}, {12, 1.2}, {20, -0.8}, {29, 0}})},
                                         {upper_back, keys({{0, 0}, {3, 1.0}, {12, 1.0}, {29, 0}})}});
    l.clips["j.HK"] = clip(20, false, {{thigh_front, keys({{0, -0.9}, {5, 0.6}, {12, 0.6}, {19, -0.9}})},
                                        {shin_front, keys({{0, 1.4}, {5, 0}, {12, 0}, {19, 1.4}})}});
    l.clips["hitstun"] = clip(20, false, {{torso, keys({{0, 0.4}, {20, 0}})}, {head, keys({{0, 0.35}, {20, 0}})}});
    l.clips["blockstun"] = clip(16, false, {{upper_front, keys({{0, 1.2}})}, {fore_front, keys({{0, -2.0}})}, {torso, keys({{0, 0.12}, {16, 0}})}});
    l.clips["knockdown"] = clip(30, false, {{hip, keys({{0, 1.4}})}, {torso, keys({{0, 0.2}})}}, keys({{0, -70}}));
    l.clips["wakeup"] = clip(10, false, {{hip, keys({{0, 1.4}, {10, 0}})}}, keys({{0, -70}, {10, 0}}));
    l.clips["thrown"] = l.clips["hitstun"];
    l.clips["ko"] = l.clips["knockdown"];
    l.clips["landing"] = l.clips["crouch"];
    return l;
}

const Roster& roster() {
    static const Roster r = [] {
        Roster out;
        out.system.round_frames = 99 * 60;
        for (const auto& [file, text] : {std::pair{std::string_view("kata.fighter.gongfu.toml"), files::kata_fighter}}) {
            auto f = gongfu::data::read_fighter(text, file);
            if (!f) {
                for (const auto& d : f.error()) out.problems.push_back(d.text());
                continue;
            }
            out.fighters.push_back(std::move(*f));
            out.looks.push_back(body_look(0x1f2430, 0x3a4152));  // ink
            out.looks.push_back(body_look(0xe8dcc4, 0xb7aa90));  // paper
        }
        return out;
    }();
    return r;
}

}  // namespace omui::fighting
