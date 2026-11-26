module omui.showcase.swarm.window;

import std;

namespace swarm::picture {

namespace {

namespace geom = mlibs::geom;

constexpr geom::Color ground = geom::Color::hex(0x0b1020), lines = geom::Color::hex(0x141c33), rim = geom::Color::hex(0x2a3557);
constexpr geom::Color player_colour = geom::Color::hex(0x5ce1e6), gun_colour = geom::Color::hex(0xd8fbff), bullet_colour = geom::Color::hex(0xffe08a);
// A wave's enemies by its number, round the list: each wave told apart from the one before.
constexpr std::array<geom::Color, 6> wave_colours{geom::Color::hex(0xff6b6b), geom::Color::hex(0xffa94d), geom::Color::hex(0xf783ac),
                                                  geom::Color::hex(0xb197fc), geom::Color::hex(0x69db7c), geom::Color::hex(0xffd43b)};
constexpr float grid = 6;  // units between the floor's lines

}  // namespace

void draw(const State& state, mrender::DisplayList& list, geom::Rect dest) {
    const float across = dest.width / arena_width, down = dest.height / arena_height;
    const auto place = [&](Vec2 at) { return geom::Point{dest.x + at.x * across, dest.y + at.y * down}; };
    const auto disc = [&](Vec2 at, float radius, geom::Color colour) {
        const geom::Point centre = place(at);
        mrender::Path path;
        path.add_ellipse({centre.x - radius * across, centre.y - radius * down, 2 * radius * across, 2 * radius * down});
        list.fill_path(path, mrender::FillRule::nonzero, mrender::Paint::solid(colour));
    };
    list.fill_rect(dest, ground);
    for (float x = grid; x < arena_width; x += grid) list.fill_rect({dest.x + x * across, dest.y, 1, dest.height}, lines);
    for (float y = grid; y < arena_height; y += grid) list.fill_rect({dest.x, dest.y + y * down, dest.width, 1}, lines);
    list.stroke_round_rect(dest, {}, 2, mrender::Paint::solid(rim));

    state.field.each<Body, Enemy>([&](meng::Entity, const Body& body, const Enemy& enemy) {
        const geom::Color colour = wave_colours[(enemy.wave + wave_colours.size() - 1) % wave_colours.size()];
        disc(body.at, enemy_radius, colour);
        disc(body.at, enemy_radius * 0.45f, geom::Color{colour.r * 0.55f, colour.g * 0.55f, colour.b * 0.55f, 1});
    });
    state.field.each<Body, Bullet>([&](meng::Entity, const Body& body, const Bullet&) { disc(body.at, bullet_radius, bullet_colour); });

    // The player, and the gun's line toward where it aims; while a touch costs nothing it blinks (eight steps on, eight off).
    const bool faded = state.safe_for > 0 && (state.safe_for / 8) % 2 == 1;
    if (faded) list.push_layer(0.35f);
    const geom::Point from = place(state.player), to = place(state.player + state.aim * (player_radius * 1.8f));
    mrender::Path gun;
    gun.move_to(from).line_to(to);
    list.stroke_path(gun, {.width = 0.7f * across, .cap = mrender::LineCap::round}, mrender::Paint::solid(gun_colour));
    disc(state.player, player_radius, player_colour);
    if (faded) list.pop_layer();
}

}  // namespace swarm::picture
