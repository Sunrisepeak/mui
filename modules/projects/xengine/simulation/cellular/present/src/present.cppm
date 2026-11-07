// xengine.simulation.cellular.present — a cellular field on screen (D-132).
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
export module xengine.simulation.cellular.present;

import std;
export import xengine.simulation.cellular;
export import xrender.vector;
export import xrender.raster;
export import mlibs.geom;

export namespace xengine::simulation::cellular {

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

    // The image, drawn into `dest` with nearest sampling: one command.
    void draw(xrender::DisplayList& list, mlibs::geom::Rect dest, float opacity = 1) const;
    [[nodiscard]] xrender::ImageRef image() const noexcept;
    [[nodiscard]] Shape shape() const noexcept;
    [[nodiscard]] Shape tiles() const noexcept;
    // The colour of one cell of `cells`, by the palette (what the image holds there).
    [[nodiscard]] mlibs::geom::Color color(std::span<const std::uint32_t> cells, std::uint32_t x, std::uint32_t y) const noexcept;

private:
    struct Data;
    explicit FieldPresenter(std::unique_ptr<Data>) noexcept;
    std::unique_ptr<Data> data_;
};

}  // namespace xengine::simulation::cellular
