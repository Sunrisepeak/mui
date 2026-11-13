// xengine.cellular.present — a cellular field on screen (D-132).
//
// The field's cells, coloured by a palette (the first swatch whose tests the
// cell passes), become the pixels of one image in an xrender ImageStore: a
// pixel a cell, drawn by one command however large the field. A present
// recolours only the tiles that changed — those a step's changed-tile mask
// marks (Execution::changed_tiles), or those that differ from the cells
// presented last — and updates the image in place with exactly those
// rectangles, so a renderer re-uploads (GPU) and redraws (CPU damage) only
// them.
//
//     auto view = FieldPresenter::make(store, shape, channels, palette);
//     view->present(cells);                          // after each step (or with its mask)
//     view->draw(list, {x, y, width, height});       // one DrawImage, nearest sampling
//
// By a shade instead of a palette (D-168), a cell's colour is computed — its row's base, tints along its
// channels (cellular::Shade) — and what glows is a second image, the field's light, kept by the same tiles:
//
//     auto view = FieldPresenter::make(store, shape, channels, shade);
//     view->draw(list, dest);
//     view->draw_glow(list, dest, 6);                // the light, blurred and added over the field
export module xengine.cellular.present;

import std;
export import xengine.cellular;
export import xrender.vector;
export import xrender.raster;
export import mlibs.geom;

export namespace xengine::cellular {

// A colour for the cells that pass all its tests (none: every cell).
struct Swatch {
    std::vector<Test> when;
    mlibs::geom::Color color;
};
struct Palette {
    std::vector<Swatch> swatches;  // the first that matches
    mlibs::geom::Color fallback = mlibs::geom::Color::transparent();
};

// What a present did: the tiles it recoloured, the rectangles the image's
// update named, the cells it compared.
struct PresentReport {
    std::size_t tiles = 0, rectangles = 0;
    bool whole = false;
};

class FieldPresenter {
public:
    // A presenter of a field of `shape` (cells of `channels` channels,
    // channel-major as Program::step takes them) into `store`, recolouring
    // by tiles of `tile`. Fails on an empty shape, no channels, a palette
    // test naming a channel the field has not, or an empty tile. The image
    // stays in the store until release() (a presenter may outlive its store:
    // it never touches the store after it is gone unless asked to).
    [[nodiscard]] static std::expected<FieldPresenter, std::string> make(xrender::ImageStore& store, Shape shape, std::uint32_t channels,
                                                                         Palette palette, Shape tile = {32, 32});
    // The same by a shade: each cell's colour computed (shade_of), and — where a tint emits — the field's
    // light as a second image in the store. Fails as above, and on a shade the field cannot be shaded by (check).
    [[nodiscard]] static std::expected<FieldPresenter, std::string> make(xrender::ImageStore& store, Shape shape, std::uint32_t channels, Shade shade,
                                                                         Shape tile = {32, 32});
    FieldPresenter(FieldPresenter&&) noexcept;
    FieldPresenter& operator=(FieldPresenter&&) noexcept;
    ~FieldPresenter();

    // The cells now, and which tiles changed since the last present (one byte
    // a tile, as Execution::changed_tiles; several steps' masks or-ed).
    std::expected<PresentReport, std::string> present(std::span<const std::uint32_t> cells, std::span<const std::uint8_t> changed_tiles);
    // The cells now; the tiles that changed are found by comparing with the
    // cells presented last (the first present: all of them).
    std::expected<PresentReport, std::string> present(std::span<const std::uint32_t> cells);
    // Takes the image out of the store (which must still be alive); the presenter is empty after.
    void release();
    // A new palette: every tile recoloured at the next present.
    std::expected<void, std::string> recolor(Palette palette);
    // A new shade for a presenter made by one (another style pack: the look changes, the cells do not);
    // every tile recoloured at the next present. One that emits where the last did not, or the reverse, is
    // refused: the light is an image the presenter has or has not.
    std::expected<void, std::string> reshade(Shade shade);

    // The image, drawn into `dest` with nearest sampling: one command.
    void draw(xrender::DisplayList& list, mlibs::geom::Rect dest, float opacity = 1) const;
    // The field's light over `dest`, where the shade emits: blurred by `radius` logical pixels and added to
    // what is under it (a layer, the built-in blur, BlendMode::plus), `strength` of it. Nothing without light.
    void draw_glow(xrender::DisplayList& list, mlibs::geom::Rect dest, float radius, float strength = 1) const;
    // Cells off the grid — in flight — drawn where they are: a square of a cell at each centre (`centres`: x, y
    // in cells, pair after pair), shaded as a cell of the field is, from its channels (`values`: the field's
    // channels a cell, cell after cell). One rectangle a cell; nothing for a presenter by a palette.
    void draw_loose(xrender::DisplayList& list, mlibs::geom::Rect dest, std::span<const float> centres, std::span<const std::uint32_t> values) const;
    [[nodiscard]] xrender::ImageRef image() const noexcept;
    // The light's image; an empty reference where the presenter has none (a palette, or a shade no tint of which emits).
    [[nodiscard]] xrender::ImageRef light() const noexcept;
    [[nodiscard]] bool glows() const noexcept;
    // The shade of one cell of `cells` (what the two images hold there); nothing for a presenter by a palette.
    [[nodiscard]] Shaded shaded(std::span<const std::uint32_t> cells, std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] Shape shape() const noexcept;
    [[nodiscard]] Shape tiles() const noexcept;
    // The palette as pixels (premultiplied RGBA8, red lowest): what a device's
    // painter (cellular.rhi's Painter) is prepared with to draw the same field.
    [[nodiscard]] std::span<const PixelSwatch> pixels() const noexcept;
    [[nodiscard]] std::uint32_t fallback_pixel() const noexcept;
    // The colour of one cell of `cells`, by the palette (what the image holds there); by a shade, its pixel
    // as a colour — not premultiplied, to the nearest 255th.
    [[nodiscard]] mlibs::geom::Color color(std::span<const std::uint32_t> cells, std::uint32_t x, std::uint32_t y) const noexcept;

private:
    struct Data;
    explicit FieldPresenter(std::unique_ptr<Data>) noexcept;
    std::unique_ptr<Data> data_;
};

}  // namespace xengine::cellular
