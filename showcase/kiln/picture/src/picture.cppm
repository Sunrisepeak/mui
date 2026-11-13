// Kiln on screen: a state of the game as a display list.
//
//   auto picture = kiln::Picture::make(images, kiln::level::first());
//   picture->show(state);                     // each frame: reads the state
//   picture->draw(list, {0, 0, 768, 384});
//
// The world is shown by xengine.cellular.present's ladders (D-178): what the level calls scenery is simulated
// beside the world where there is room and drawn as placed where there is not; the light is a glow, or below
// that frames of a flame baked once from the glow's own form of a hearth, or the shade alone. The game's state
// is read, never written: the same game at every step. Over the world: what flies, the wand's bolts, the exit,
// and Mui — a pixel cat, each pixel a rectangle.
export module omui.showcase.kiln.picture;
import std;
export import omui.showcase.kiln.sim;
export import xengine.cellular.present.world;

export namespace kiln {
namespace ca = xengine::cellular;

class Picture {
public:
    // `best_light`, `scenery_alive`: what this device shows at most.
    [[nodiscard]] static std::expected<Picture, std::string> make(xrender::ImageStore& store, const Level& level, ca::Light best_light = ca::Light::glow, bool scenery_alive = true);

    // The world's part, with its ladders (`ca::WorldPicture::ladders()` for a governor), set and describe().
    [[nodiscard]] ca::WorldPicture& world() noexcept { return *world_; }
    [[nodiscard]] const ca::WorldPicture& world() const noexcept { return *world_; }
    void show(const State& state);
    // The whole cavern into `dest` (a cell is dest.width / the cavern's width across).
    void draw(xrender::DisplayList& list, mlibs::geom::Rect dest) const;

private:
    Picture() = default;
    std::optional<ca::WorldPicture> world_;
    ca::Shape shape_;
    Cat cat_;
    std::vector<Bolt> bolts_;
    ca::Tile exit_;
    std::uint64_t frame_ = 0;
    Outcome outcome_ = Outcome::playing;
};

}  // namespace kiln
