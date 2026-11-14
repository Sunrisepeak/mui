module xengine.cellular.present.world;
import std;
import mlibs.hash;

namespace xengine::cellular {
namespace raster = xrender::raster;

struct WorldPicture::Data {
    xrender::ImageStore* store = nullptr;
    const xengine::matter::Program* program = nullptr;
    xengine::matter::Look look;
    Shape shape;
    PictureOptions options;
    std::optional<FieldPresenter> view;
    std::optional<decor::Decor> layer;
    Light light = Light::glow;
    bool alive = true;
    std::string light_why = "as made", scenery_why = "as made";
    bool layer_running = false;     // the layer was stepped at the last show (else it starts again)
    std::size_t scenery = 0;        // cells asked for
    Flipbook flames;
    struct Block { std::uint32_t emitting = 0, sum_x = 0, sum_y = 0; };
    std::vector<Block> blocks;  // baked: the cells that emit, a block of 8 × 8, and where — of the last show
    std::uint64_t frame = 0;
    std::uint64_t differing = 0;
    std::vector<float> centres;         // what flies, of the last show
    std::vector<std::uint32_t> loose;
};

WorldPicture::WorldPicture(std::unique_ptr<Data> data) noexcept : data_(std::move(data)) {}
WorldPicture::WorldPicture(WorldPicture&&) noexcept = default;
WorldPicture& WorldPicture::operator=(WorldPicture&&) noexcept = default;
WorldPicture::~WorldPicture() {
    if (!data_ || !data_->store) return;
    for (const xrender::ImageRef frame : data_->flames.frames) data_->store->remove(frame);
}

std::expected<WorldPicture, std::string> WorldPicture::make(xrender::ImageStore& store, const xengine::matter::Program& program, const xengine::matter::Look& look, Shape shape,
                                                            PictureOptions options) {
    auto data = std::make_unique<Data>();
    data->store = &store;
    data->program = &program;
    data->look = look;
    data->shape = shape;
    data->options = options;
    auto view = FieldPresenter::make(store, shape, static_cast<std::uint32_t>(program.channels().size()), look.shade(), options.tile);
    if (!view) return std::unexpected(view.error());
    data->view.emplace(std::move(*view));
    auto layer = decor::Decor::make(program, shape, {.tile = options.tile, .seed = options.seed, .fill = options.fill});
    if (!layer) return std::unexpected(layer.error());
    data->layer.emplace(std::move(*layer));
    data->light = options.best_light;
    data->alive = options.scenery_alive;
    const Shape grid = tiles(shape, {8, 8});
    data->blocks.assign(static_cast<std::size_t>(grid.width) * grid.height, {});
    return WorldPicture(std::move(data));
}

void WorldPicture::set(std::size_t ladder, std::uint8_t lowered, std::string reason) {
    Data& d = *data_;
    if (reason.empty()) reason = "set";
    if (ladder == light_ladder) {
        const Light asked = static_cast<Light>(std::min<std::uint8_t>(lowered, 2)), allowed = std::max(asked, d.options.best_light);
        if (allowed != asked)
            reason = std::format("{} (asked for {}: this device shows {} at most)", reason, light_names[static_cast<std::size_t>(asked)], light_names[static_cast<std::size_t>(allowed)]);
        d.light = allowed;
        d.light_why = std::move(reason);
    } else if (ladder == scenery_ladder) {
        const bool asked = lowered == 0, allowed = asked && d.options.scenery_alive;
        if (allowed != asked) reason = std::format("{} (asked for alive: this device shows scenery still)", reason);
        d.alive = allowed;
        d.scenery_why = std::move(reason);
    }
}
Light WorldPicture::light() const noexcept { return data_->light; }
bool WorldPicture::scenery_alive() const noexcept { return data_->alive; }

void WorldPicture::scenery(Tile region, std::uint32_t material) {
    Data& d = *data_;
    d.layer->plan(region, material);
    d.scenery += static_cast<std::size_t>(region.width) * region.height;
    d.layer_running = false;  // placed at the next show
}

const Flipbook& WorldPicture::flames() const noexcept { return data_->flames; }
const FieldPresenter& WorldPicture::view() const noexcept { return *data_->view; }
std::span<const std::uint32_t> WorldPicture::shown() const noexcept { return data_->layer->cells(); }

void WorldPicture::show(const rigid::MatterWorld& world) {
    Data& d = *data_;
    const auto cells = world.cells();
    if (d.alive) {
        if (!d.layer_running) d.layer->restart(cells, d.options.warm);
        else d.layer->step(cells);
        d.layer_running = true;
        d.differing = d.layer->last(cells).differing;
    } else {
        d.layer->still(cells);
        d.layer_running = false;
        d.differing = d.layer->last(cells).differing;
    }
    (void)d.view->present(d.layer->cells());
    // What flies is the world's: drawn at every step.
    world.flying().centres(d.centres);
    d.loose.assign(world.flying().values().begin(), world.flying().values().end());
    // Baked: where the look emits, by blocks of 8 × 8 cells — read from the light the presenter keeps.
    std::ranges::fill(d.blocks, Data::Block{});
    if (d.light == Light::baked && d.view->glows())
        if (const raster::Surface* light = d.store->get(d.view->light())) {
            const Shape grid = tiles(d.shape, {8, 8});
            for (std::uint32_t y = 0; y < d.shape.height; ++y)
                for (std::uint32_t x = 0; x < d.shape.width; ++x)
                    if (light->at(static_cast<int>(x), static_cast<int>(y)).a) {
                        Data::Block& block = d.blocks[static_cast<std::size_t>(y / 8) * grid.width + x / 8];
                        ++block.emitting, block.sum_x += x, block.sum_y += y;
                    }
        }
    ++d.frame;
}

void WorldPicture::draw(xrender::DisplayList& list, mlibs::geom::Rect dest) const {
    const Data& d = *data_;
    d.view->draw(list, dest);
    d.view->draw_loose(list, dest, d.centres, d.loose);
    const float across = dest.width / static_cast<float>(d.shape.width), down = dest.height / static_cast<float>(d.shape.height);
    if (d.light == Light::glow) {
        d.view->draw_glow(list, dest, d.options.glow * across, d.options.strength);
    } else if (d.light == Light::baked && !d.flames.frames.empty()) {
        // A baked frame over each block that emits, the more of it the more of the block does; which frame by
        // the time and the block, so that neighbours do not flicker as one.
        const Shape grid = tiles(d.shape, {8, 8});
        const raster::Surface* first = d.store->get(d.flames.frames.front());
        if (!first) return;
        const mlibs::geom::Rect source{0, 0, static_cast<float>(first->width()), static_cast<float>(first->height())};
        list.push_layer(d.options.strength, xrender::BlendMode::plus);
        for (std::uint32_t by = 0; by < grid.height; ++by)
            for (std::uint32_t bx = 0; bx < grid.width; ++bx) {
                const Data::Block& block = d.blocks[static_cast<std::size_t>(by) * grid.width + bx];
                if (!block.emitting) continue;
                const std::uint64_t turn = d.frame / static_cast<std::uint64_t>(std::max(1, d.flames.every)) + bx * 7u + by * 13u;
                const xrender::ImageRef image = d.flames.frames[turn % d.flames.frames.size()];
                // The frame's own source of light over the middle of what emits in the block.
                const float at_x = (static_cast<float>(block.sum_x) / static_cast<float>(block.emitting) + 0.5f - d.flames.centre_x) * across;
                const float at_y = (static_cast<float>(block.sum_y) / static_cast<float>(block.emitting) + 0.5f - d.flames.centre_y) * down;
                const float share = std::clamp(static_cast<float>(block.emitting) / 12.0f, 0.4f, 1.0f);
                list.draw_image(image, source, {dest.x + at_x, dest.y + at_y, d.flames.cells_wide * across, d.flames.cells_high * down}, xrender::Sampling::linear, share);
            }
        list.pop_layer();
    }
}

std::expected<void, std::string> WorldPicture::bake(const rigid::MatterWorld& scene, int frames, int every, int warm) {
    Data& d = *data_;
    if (frames < 1 || every < 1 || warm < 0) return std::unexpected(std::string("a flipbook has a frame at least, a step between frames, and no negative warming"));
    if (&scene.program() != d.program) return std::unexpected(std::string("the scene to bake is of another program"));
    auto branch = scene.branch();  // the caller's scene is not stepped
    if (!branch) return std::unexpected(branch.error());
    rigid::MatterWorld& world = **branch;
    const Shape shape = world.shape();
    constexpr int scale = 4;  // pixels a cell in a baked frame
    xrender::ImageStore images;
    auto view = FieldPresenter::make(images, shape, static_cast<std::uint32_t>(d.program->channels().size()), d.look.shade(), d.options.tile);
    if (!view) return std::unexpected(view.error());
    if (!view->glows()) return std::unexpected(std::string("the look emits no light: nothing to bake"));
    for (int frame = 0; frame < warm; ++frame) world.step();
    const float width = static_cast<float>(shape.width) * scale, height = static_cast<float>(shape.height) * scale;
    std::vector<std::vector<raster::Rgba8>> baked;
    double emitting = 0, sum_x = 0, sum_y = 0;
    mlibs::hash::Fnv1a key;
    xrender::CpuRenderer renderer;
    xrender::DisplayList list;
    for (int frame = 0; frame < frames; ++frame) {
        for (int step = 0; step < every; ++step) world.step();
        (void)view->present(world.cells());
        if (const raster::Surface* light = images.get(view->light()))
            for (int y = 0; y < light->height(); ++y)
                for (int x = 0; x < light->width(); ++x)
                    if (light->at(x, y).a) emitting += 1, sum_x += x + 0.5, sum_y += y + 0.5;
        // The light alone, blurred as the rich form blurs it, over nothing.
        list.clear();
        view->draw_glow(list, {0, 0, width, height}, d.options.glow * scale, 1.0f);
        raster::Surface out(static_cast<int>(width), static_cast<int>(height));
        (void)renderer.render(list, out, {&images, nullptr});
        baked.emplace_back(out.pixels().begin(), out.pixels().end());
        for (const raster::Rgba8 pixel : out.pixels()) key.le(pixel.r, 1).le(pixel.g, 1).le(pixel.b, 1).le(pixel.a, 1);
    }
    if (emitting <= 0) return std::unexpected(std::string("nothing in the scene emitted light while it was baked"));
    for (const xrender::ImageRef frame : d.flames.frames) d.store->remove(frame);
    d.flames = {};
    for (auto& pixels : baked) d.flames.frames.push_back(d.store->add(static_cast<int>(width), static_cast<int>(height), std::move(pixels)));
    d.flames.cells_wide = static_cast<float>(shape.width), d.flames.cells_high = static_cast<float>(shape.height);
    d.flames.centre_x = static_cast<float>(sum_x / emitting), d.flames.centre_y = static_cast<float>(sum_y / emitting);
    d.flames.every = every;
    d.flames.key = key.value();
    view->release();
    return {};
}

std::string WorldPicture::describe() const {
    const Data& d = *data_;
    std::size_t emitting_blocks = 0;
    for (const Data::Block& block : d.blocks) emitting_blocks += block.emitting != 0;
    const auto quoted = [](std::string_view text) {
        std::string out;
        for (const char c : text) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out;
    };
    return std::format(R"({{"light":"{}","light_why":"{}","best_light":"{}","scenery":"{}","scenery_why":"{}","flames":{},"baked_frames":{},"placed":{},"differing":{}}})",
                       light_names[static_cast<std::size_t>(d.light)], quoted(d.light_why), light_names[static_cast<std::size_t>(d.options.best_light)], scenery_names[d.alive ? 0 : 1],
                       quoted(d.scenery_why), d.light == Light::baked ? emitting_blocks : 0, d.flames.frames.size(), d.scenery, d.differing);
}

}  // namespace xengine::cellular
