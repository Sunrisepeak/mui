module omui.showcase.kiln.picture;
import std;

namespace kiln {
namespace geom = mlibs::geom;

namespace {
// What a flame is baked from: a post of wood alight on a floor of stone, alone.
std::expected<std::unique_ptr<rigid::MatterWorld>, std::string> hearth() {
    const auto& program = matter();
    auto world = rigid::MatterWorld::make(program, {.shape = {24, 24}, .tile = {8, 8}, .seed = 2, .fill = "air"});
    if (!world) return world;
    (*world)->place({0, 21, 24, 3}, *program.material("stone"));
    (*world)->place({10, 13, 4, 8}, *program.material("wood"));
    (*world)->set(*program.channel("temperature"), {10, 19, 4, 2}, 1000 * 256);
    return world;
}
const xengine::matter::Look& look() {
    static const xengine::matter::Look value = *xengine::matter::standard_look(matter());
    return value;
}
// Mui, 6 × 10 pixels over her 3 × 5 cells, facing right: `#` her coat, `e` an eye, `n` her nose, `w` her chest.
constexpr std::array<std::string_view, 10> mui{
    "#...#.",
    "##.##.",
    "######",
    "#e##e#",
    "###n##",
    ".####.",
    ".#ww#.",
    "##ww##",
    "######",
    "#.##.#",
};
}  // namespace

std::expected<Picture, std::string> Picture::make(xrender::ImageStore& store, const Level& level, ca::Light best_light, bool scenery_alive) {
    const auto& program = matter();
    Picture picture;
    std::uint32_t width = 0;
    for (const auto row : level.rows) width = std::max(width, static_cast<std::uint32_t>(row.size()));
    picture.shape_ = {width, static_cast<std::uint32_t>(level.rows.size())};
    auto world = ca::WorldPicture::make(store, program, look(), picture.shape_, {.fill = "air", .seed = 23, .best_light = best_light, .scenery_alive = scenery_alive});
    if (!world) return std::unexpected(world.error());
    picture.world_.emplace(std::move(*world));
    for (const Scenery& scenery : level.scenery) {
        const auto material = program.material(scenery.material);
        if (!material) return std::unexpected(std::format("the definition has no `{}` for the scenery", scenery.material));
        picture.world_->scenery(scenery.region, *material);
    }
    auto scene = hearth();
    if (!scene) return std::unexpected(scene.error());
    if (auto baked = picture.world_->bake(**scene); !baked) return std::unexpected(baked.error());
    return picture;
}

void Picture::show(const State& state) {
    if (!state.world) return;
    world_->show(*state.world);
    cat_ = state.cat, bolts_ = state.bolts, exit_ = state.exit, frame_ = state.frame, outcome_ = state.outcome;
}

void Picture::draw(xrender::DisplayList& list, geom::Rect dest) const {
    const float across = dest.width / static_cast<float>(shape_.width), down = dest.height / static_cast<float>(shape_.height);
    list.fill_rect(dest, geom::Color::hex(0x0d1117));
    // The exit: a doorway of warm light behind the world.
    list.fill_rect({dest.x + static_cast<float>(exit_.x) * across, dest.y + static_cast<float>(exit_.y) * down, static_cast<float>(exit_.width) * across, static_cast<float>(exit_.height) * down},
                   geom::Color::hex(0xf2c14e).with_alpha(0.35f + 0.1f * static_cast<float>((frame_ / 20) % 2)));
    world_->draw(list, dest);
    // The wand's bolts: a bright cell each, by what they carry.
    for (const Bolt& bolt : bolts_) {
        const geom::Color color = bolt.spell == Spell::fire ? geom::Color::hex(0xffb347) : bolt.spell == Spell::water ? geom::Color::hex(0x7fd4ff) : geom::Color::hex(0xe8e8e8);
        list.fill_rect({dest.x + (bolt.x - 0.5f) * across, dest.y + (bolt.y - 0.5f) * down, across, down}, color);
    }
    // Mui: her box is 3 cells wide and 5 high over her feet; two pixels a cell.
    const float left = dest.x + (cat_.x - static_cast<float>(cat_width) * 0.5f) * across, top = dest.y + (cat_.y - static_cast<float>(cat_height)) * down;
    const float pixel_x = across * 0.5f, pixel_y = down * 0.5f;
    const bool flash = cat_.hurt < 6 && (frame_ / 2) % 2 == 0;
    const geom::Color coat = flash ? geom::Color::hex(0xffffff) : outcome_ == Outcome::lost ? geom::Color::hex(0x555b66) : geom::Color::hex(0x2b2d42);
    for (std::size_t row = 0; row < mui.size(); ++row)
        for (std::size_t column = 0; column < mui[row].size(); ++column) {
            const char what = mui[row][cat_.facing_right ? column : mui[row].size() - 1 - column];
            if (what == '.') continue;
            const geom::Color color = what == 'e' ? geom::Color::hex(0xf2c14e) : what == 'n' ? geom::Color::hex(0xef8fa0) : what == 'w' ? geom::Color::hex(0xedf2f4) : coat;
            list.fill_rect({left + static_cast<float>(column) * pixel_x, top + static_cast<float>(row) * pixel_y, pixel_x, pixel_y}, color);
        }
}

}  // namespace kiln
