module xengine.cellular.rigid;
import std;
import mlibs.trace;
import mlibs.hash;

namespace xengine::cellular::rigid {
namespace matter = xengine::matter;
namespace {

// A grid's occupied cells as rectangles of one label each, row-major greedy: the widest run from the first
// free cell, grown down while the whole run below is free and of its label. Exact (every cell in one).
struct Rect { std::uint32_t x = 0, y = 0, width = 0, height = 0, label = 0; };
// Into `out`, with `used` as its scratch: both the caller's, kept between frames (a frame allocates nothing).
template <class Label>
void rectangles(std::uint32_t width, std::uint32_t height, Label label_of, std::vector<Rect>& out, std::vector<std::uint8_t>& used) {
    out.clear();
    used.assign(static_cast<std::size_t>(width) * height, 0);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * width + x;
            const auto label = label_of(x, y);
            if (used[at] || !label) continue;
            std::uint32_t run = 1;
            while (x + run < width && !used[at + run] && label_of(x + run, y) == label) ++run;
            std::uint32_t rows = 1;
            for (; y + rows < height; ++rows) {
                bool whole = true;
                for (std::uint32_t across = 0; across < run && whole; ++across)
                    whole = !used[static_cast<std::size_t>(y + rows) * width + x + across] && label_of(x + across, y + rows) == label;
                if (!whole) break;
            }
            for (std::uint32_t down = 0; down < rows; ++down)
                for (std::uint32_t across = 0; across < run; ++across) used[static_cast<std::size_t>(y + down) * width + x + across] = 1;
            out.push_back({x, y, run, rows, *label});
        }
}

}  // namespace

struct Bodies::Scratch {
    std::vector<Rect> rects;
    std::vector<std::uint8_t> used;
    std::vector<dyn::Shape> shapes;
    std::vector<dyn::Material> materials;
};

bool Bodies::solid(std::uint32_t material) const noexcept {
    return material != matter::none && material < program_->materials().size() && program_->phase(material) == matter::Phase::solid;
}
bool Bodies::gas(std::uint32_t material) const noexcept {
    return material == matter::none || (material < program_->materials().size() && program_->phase(material) == matter::Phase::gas);
}
bool Bodies::liquid(std::uint32_t material) const noexcept {
    return material != matter::none && material < program_->materials().size() && program_->phase(material) == matter::Phase::liquid;
}

std::expected<Bodies, std::string> Bodies::make(const matter::Program& program, Shape field, dyn::World& world, const Options& options) {
    if (!field.width || !field.height) return std::unexpected(std::string("a field of no cells"));
    if (!(options.cell > 0) || !options.terrain_tile) return std::unexpected(std::string("a cell and a terrain tile are larger than 0"));
    const auto density = program.column("density");
    if (!density) return std::unexpected(std::string("the program has no `density` column"));
    Bodies bodies;
    bodies.program_ = &program;
    bodies.world_ = &world;
    bodies.options_ = options;
    bodies.field_ = field;
    bodies.channels_ = static_cast<std::uint32_t>(program.channels().size());
    bodies.density_ = *density;
    bodies.friction_ = program.column("friction").value_or(matter::none);
    bodies.restitution_ = program.column("restitution").value_or(matter::none);
    for (const matter::Law& law : program.laws())
        if (law.kind == matter::LawKind::motion && law.along && law.force > 0)
            bodies.pushes_.push_back({*law.along, static_cast<float>(law.up ? -law.force : law.force), law.scale});
    bodies.owner_.assign(static_cast<std::size_t>(field.width) * field.height, 0);
    const Shape tiles = cellular::tiles(field, {options.terrain_tile, options.terrain_tile});
    bodies.terrain_.assign(static_cast<std::size_t>(tiles.width) * tiles.height, dyn::BodyId{});
    bodies.terrain_hash_.assign(bodies.terrain_.size(), ~std::uint64_t{0});
    return bodies;
}

// The piece's body made (or its shapes replaced) from its cells: a box a rectangle of one material.
std::expected<void, std::string> Bodies::shape(Piece& piece, bool make_body, const dyn::Pose* like) {
    const std::size_t area = static_cast<std::size_t>(piece.width) * piece.height;
    if (!scratch_) scratch_ = std::make_shared<Scratch>();
    Scratch& scratch = *scratch_;
    rectangles(piece.width, piece.height, [&](std::uint32_t x, std::uint32_t y) -> std::optional<std::uint32_t> {
        const std::uint32_t material = piece.cells[static_cast<std::size_t>(y) * piece.width + x];
        if (material == matter::none) return std::nullopt;
        return material;
    }, scratch.rects, scratch.used);
    const std::vector<Rect>& rects = scratch.rects;
    if (rects.empty()) return std::unexpected(std::string("a body of no cells"));
    const float cell = options_.cell;
    std::vector<dyn::Shape>& shapes = scratch.shapes;
    std::vector<dyn::Material>& materials = scratch.materials;
    shapes.clear(), materials.clear();
    for (const Rect& rect : rects) {
        dyn::Shape box = dyn::Shape::box(static_cast<float>(rect.width) * cell * 0.5f, static_cast<float>(rect.height) * cell * 0.5f);
        box.centre = {(static_cast<float>(piece.left) + static_cast<float>(rect.x) + static_cast<float>(rect.width) * 0.5f) * cell,
                      (static_cast<float>(piece.top) + static_cast<float>(rect.y) + static_cast<float>(rect.height) * 0.5f) * cell};
        shapes.push_back(box);
        dyn::Material material{.density = static_cast<float>(program_->number(rect.label, density_))};
        if (friction_ != matter::none) material.friction = static_cast<float>(program_->number(rect.label, friction_));
        if (restitution_ != matter::none) material.restitution = static_cast<float>(program_->number(rect.label, restitution_));
        materials.push_back(material);
    }
    (void)area;
    if (!make_body) {
        auto reshaped = world_->reshape(piece.body, shapes, materials);
        if (!reshaped) return std::unexpected(reshaped.error().text);
        return {};
    }
    dyn::BodyDef def{.kind = dyn::Kind::moving};
    if (like) def.at = like->at, def.angle = like->angle;
    auto made = world_->add(def, shapes, materials);
    if (!made) return std::unexpected(made.error().text);
    piece.body = *made;
    return {};
}

std::expected<std::vector<dyn::BodyId>, std::string> Bodies::carve(std::span<std::uint32_t> field, Tile region, std::span<const std::uint32_t> fill) {
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    if (field.size() != cells * channels_ || fill.size() != channels_) return std::unexpected(std::string("a field and a fill of the program's channels"));
    if (!region.width || !region.height || region.x >= field_.width || region.y >= field_.height || region.width > field_.width - region.x ||
        region.height > field_.height - region.y)
        return std::unexpected(std::string("a region within the field"));
    fill_.assign(fill.begin(), fill.end());
    // The solid cells of the region no body holds, in connected pieces.
    std::vector<std::uint32_t> label(static_cast<std::size_t>(region.width) * region.height, 0);
    const auto field_at = [&](std::uint32_t x, std::uint32_t y) { return static_cast<std::size_t>(region.y + y) * field_.width + region.x + x; };
    std::uint32_t pieces = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> stack;
    for (std::uint32_t y = 0; y < region.height; ++y)
        for (std::uint32_t x = 0; x < region.width; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * region.width + x;
            if (label[at] || owner_[field_at(x, y)] || !solid(field[field_at(x, y)])) continue;
            ++pieces;
            stack.assign(1, {x, y});
            label[at] = pieces;
            while (!stack.empty()) {
                const auto [px, py] = stack.back();
                stack.pop_back();
                const std::array<std::pair<std::int32_t, std::int32_t>, 4> beside{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
                for (const auto [dx, dy] : beside) {
                    const std::int64_t nx = std::int64_t{px} + dx, ny = std::int64_t{py} + dy;
                    if (nx < 0 || ny < 0 || nx >= region.width || ny >= region.height) continue;
                    const std::size_t n = static_cast<std::size_t>(ny) * region.width + static_cast<std::size_t>(nx);
                    const std::size_t f = field_at(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
                    if (label[n] || owner_[f] || !solid(field[f])) continue;
                    label[n] = pieces;
                    stack.push_back({static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny)});
                }
            }
        }
    std::vector<dyn::BodyId> made;
    for (std::uint32_t which = 1; which <= pieces; ++which) {
        // The piece's grid: its bounds within the region; the body's origin at the region's corner.
        std::uint32_t x0 = region.width, y0 = region.height, x1 = 0, y1 = 0;
        for (std::uint32_t y = 0; y < region.height; ++y)
            for (std::uint32_t x = 0; x < region.width; ++x)
                if (label[static_cast<std::size_t>(y) * region.width + x] == which) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        Piece piece;
        piece.left = static_cast<std::int32_t>(x0);
        piece.top = static_cast<std::int32_t>(y0);
        piece.width = x1 - x0 + 1;
        piece.height = y1 - y0 + 1;
        const std::size_t local_cells = static_cast<std::size_t>(piece.width) * piece.height;
        piece.cells.assign(local_cells * channels_, 0);
        for (std::size_t local = 0; local < local_cells; ++local) piece.cells[local] = matter::none;
        for (std::uint32_t y = 0; y < piece.height; ++y)
            for (std::uint32_t x = 0; x < piece.width; ++x) {
                if (label[static_cast<std::size_t>(y0 + y) * region.width + x0 + x] != which) continue;
                const std::size_t local = static_cast<std::size_t>(y) * piece.width + x, f = field_at(x0 + x, y0 + y);
                for (std::uint32_t channel = 0; channel < channels_; ++channel) piece.cells[channel * local_cells + local] = field[channel * cells + f];
                // Stamped where it is: the cell is the body's, and the fill lies under it.
                piece.covered.push_back(static_cast<std::uint32_t>(f));
                piece.from.push_back(static_cast<std::uint32_t>(local));
                piece.under.insert(piece.under.end(), fill.begin(), fill.end());
            }
        const dyn::Pose at{.at = {static_cast<float>(region.x) * options_.cell, static_cast<float>(region.y) * options_.cell}};
        if (auto shaped = shape(piece, true, &at); !shaped) return std::unexpected(shaped.error());
        for (const std::uint32_t f : piece.covered) owner_[f] = static_cast<std::uint32_t>(pieces_.size() + 1);
        made.push_back(piece.body);
        pieces_.push_back(std::move(piece));
    }
    return made;
}

void Bodies::push(std::span<const std::uint32_t> field) {
    last_.wetted = 0;
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    if (field.size() != cells * channels_) return;
    const float cell = options_.cell, step = world_->options().step;
    for (std::size_t index = 0; index < pieces_.size(); ++index) {
        const Piece& piece = pieces_[index];
        const auto pose = world_->pose(piece.body);
        if (!pose) continue;
        const std::uint32_t mine = static_cast<std::uint32_t>(index + 1);
        float force_x = 0, force_y = 0, torque = 0;
        const auto add = [&](float at_x, float at_y, float fx, float fy) {
            force_x += fx, force_y += fy;
            torque += (at_x - pose->centre.x) * fy - (at_y - pose->centre.y) * fx;
        };
        for (const std::uint32_t f : piece.covered) {
            if (owner_[f] != mine) continue;
            const std::uint32_t x = f % field_.width, y = f / field_.width;
            // Buoyancy (Archimedes, a cell at a time): the cell is under a liquid's surface when its row, walked
            // out of the body either way, meets that liquid; it displaces the liquid's weight, up, at its centre.
            double displaced = 0;
            for (const int way : {-1, 1}) {
                std::int64_t walk = x;
                while (walk >= 0 && walk < field_.width && owner_[static_cast<std::size_t>(y) * field_.width + static_cast<std::size_t>(walk)] == mine) walk += way;
                if (walk < 0 || walk >= field_.width) continue;
                const std::uint32_t beside = field[static_cast<std::size_t>(y) * field_.width + static_cast<std::size_t>(walk)];
                if (liquid(beside)) displaced = std::max(displaced, program_->number(beside, density_));
            }
            const float centre_x = (static_cast<float>(x) + 0.5f) * cell, centre_y = (static_cast<float>(y) + 0.5f) * cell;
            if (displaced > 0) add(centre_x, centre_y, 0, -static_cast<float>(displaced) * options_.gravity * cell * cell);
            // Each face the body shows the field: a liquid's drag against the face's velocity, a blast's push in.
            const std::array<std::pair<std::int32_t, std::int32_t>, 4> beside{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
            for (const auto [dx, dy] : beside) {
                const std::int64_t nx = std::int64_t{x} + dx, ny = std::int64_t{y} + dy;
                if (nx < 0 || ny < 0 || nx >= field_.width || ny >= field_.height) continue;
                const std::size_t n = static_cast<std::size_t>(ny) * field_.width + static_cast<std::size_t>(nx);
                if (owner_[n]) continue;
                const std::uint32_t material = field[n];
                const float face_x = centre_x + static_cast<float>(dx) * 0.5f * cell, face_y = centre_y + static_cast<float>(dy) * 0.5f * cell;
                if (liquid(material)) {
                    ++last_.wetted;
                    const float rx = face_x - pose->centre.x, ry = face_y - pose->centre.y;
                    const float vx = pose->velocity.x - pose->spin * ry, vy = pose->velocity.y + pose->spin * rx;
                    const float k = options_.drag * static_cast<float>(program_->number(material, density_)) * cell;
                    add(face_x, face_y, -k * vx, -k * vy);
                }
                // A law of motion down a channel's slope: what the channel is beside the face pushes the face in.
                for (const Push& law : pushes_) {
                    float push = law.force * static_cast<float>(field[static_cast<std::size_t>(law.channel) * cells + n]);
                    if (law.scale) push *= static_cast<float>(program_->number(field[f], *law.scale));
                    if (push != 0) add(face_x, face_y, -static_cast<float>(dx) * push, -static_cast<float>(dy) * push);
                }
            }
        }
        world_->apply(piece.body, {force_x * step, force_y * step}, torque * step);
    }
}

void Bodies::lift(std::span<std::uint32_t> field) {
    last_.lost = last_.split = last_.gone = 0;
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    if (field.size() != cells * channels_) return;
    std::vector<std::size_t> changed;
    for (std::size_t index = 0; index < pieces_.size(); ++index) {
        Piece& piece = pieces_[index];
        const std::uint32_t mine = static_cast<std::uint32_t>(index + 1);
        const std::size_t local_cells = static_cast<std::size_t>(piece.width) * piece.height;
        std::vector<std::uint8_t>& read = read_;  // kept between frames: a frame allocates nothing
        read.assign(local_cells, 0);
        bool lost = false;
        for (std::size_t entry = 0; entry < piece.covered.size(); ++entry) {
            const std::uint32_t f = piece.covered[entry], local = piece.from[entry];
            if (owner_[f] != mine) continue;
            owner_[f] = 0;
            const std::uint32_t material = field[f];
            if (!read[local] && piece.cells[local] != matter::none) {
                read[local] = 1;
                if (solid(material)) {
                    for (std::uint32_t channel = 0; channel < channels_; ++channel) piece.cells[channel * local_cells + local] = field[channel * cells + f];
                } else {
                    // No longer solid: it leaves the body and stays in the field as what it became.
                    piece.cells[local] = matter::none;
                    if (log_) log_->left.push_back({piece.body, f});
                    ++last_.lost;
                    lost = true;
                    continue;
                }
            } else if (piece.cells[local] == matter::none && !solid(material)) {
                continue;  // the cell it came from left the body this lift: what the field made of it stays
            }
            for (std::uint32_t channel = 0; channel < channels_; ++channel) field[channel * cells + f] = piece.under[entry * channels_ + channel];
        }
        piece.covered.clear(), piece.from.clear(), piece.under.clear();
        if (lost) changed.push_back(index);
    }
    // Bodies that lost cells are re-made of what is left — split where it fell apart, gone where nothing is.
    for (auto at = changed.rbegin(); at != changed.rend(); ++at) reshape(*at);
}

void Bodies::reshape(std::size_t index) {
    Piece& piece = pieces_[index];
    const std::size_t local_cells = static_cast<std::size_t>(piece.width) * piece.height;
    std::vector<std::uint32_t> label(local_cells, 0);
    std::uint32_t parts = 0;
    std::vector<std::uint32_t> stack;
    for (std::size_t start = 0; start < local_cells; ++start) {
        if (label[start] || piece.cells[start] == matter::none) continue;
        ++parts;
        label[start] = parts;
        stack.assign(1, static_cast<std::uint32_t>(start));
        while (!stack.empty()) {
            const std::uint32_t at = stack.back();
            stack.pop_back();
            const std::uint32_t x = at % piece.width, y = at / piece.width;
            const std::array<std::pair<std::int32_t, std::int32_t>, 4> beside{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
            for (const auto [dx, dy] : beside) {
                const std::int64_t nx = std::int64_t{x} + dx, ny = std::int64_t{y} + dy;
                if (nx < 0 || ny < 0 || nx >= piece.width || ny >= piece.height) continue;
                const std::size_t n = static_cast<std::size_t>(ny) * piece.width + static_cast<std::size_t>(nx);
                if (label[n] || piece.cells[n] == matter::none) continue;
                label[n] = parts;
                stack.push_back(static_cast<std::uint32_t>(n));
            }
        }
    }
    const auto pose = world_->pose(piece.body);
    if (!parts || !pose) {
        if (log_) log_->gone.push_back(piece.body);
        world_->remove(piece.body);
        pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(index));
        ++last_.gone;
        return;
    }
    // Each part as a grid of its own bounds, the body's origin kept: part 1 stays this body, the rest are new ones
    // moving as the old one moved at their centres.
    std::vector<Piece> made;
    for (std::uint32_t part = 1; part <= parts; ++part) {
        std::uint32_t x0 = piece.width, y0 = piece.height, x1 = 0, y1 = 0;
        for (std::size_t at = 0; at < local_cells; ++at)
            if (label[at] == part) {
                const std::uint32_t x = static_cast<std::uint32_t>(at % piece.width), y = static_cast<std::uint32_t>(at / piece.width);
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            }
        Piece next;
        next.body = piece.body;
        next.left = piece.left + static_cast<std::int32_t>(x0);
        next.top = piece.top + static_cast<std::int32_t>(y0);
        next.width = x1 - x0 + 1;
        next.height = y1 - y0 + 1;
        const std::size_t next_cells = static_cast<std::size_t>(next.width) * next.height;
        next.cells.assign(next_cells * channels_, 0);
        for (std::size_t local = 0; local < next_cells; ++local) next.cells[local] = matter::none;
        for (std::uint32_t y = 0; y < next.height; ++y)
            for (std::uint32_t x = 0; x < next.width; ++x) {
                const std::size_t from = static_cast<std::size_t>(y0 + y) * piece.width + x0 + x, to = static_cast<std::size_t>(y) * next.width + x;
                if (label[from] != part) continue;
                for (std::uint32_t channel = 0; channel < channels_; ++channel) next.cells[channel * next_cells + to] = piece.cells[channel * local_cells + from];
            }
        made.push_back(std::move(next));
    }
    made[0].body = piece.body;
    if (!shape(made[0], false, nullptr)) return;
    pieces_[index] = std::move(made[0]);
    for (std::size_t part = 1; part < made.size(); ++part) {
        if (!shape(made[part], true, &*pose)) continue;
        if (const auto own = world_->pose(made[part].body)) {
            const float rx = own->centre.x - pose->centre.x, ry = own->centre.y - pose->centre.y;
            world_->set_velocity(made[part].body, {pose->velocity.x - pose->spin * ry, pose->velocity.y + pose->spin * rx}, pose->spin);
        }
        if (log_) log_->split.push_back({pieces_[index].body, made[part].body});
        pieces_.push_back(std::move(made[part]));
        ++last_.split;
    }
}

void Bodies::stamp(std::span<std::uint32_t> field) {
    last_.covered = last_.displaced = last_.hidden = 0;
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    if (field.size() != cells * channels_) return;
    const float cell = options_.cell;
    for (std::size_t index = 0; index < pieces_.size(); ++index) {
        Piece& piece = pieces_[index];
        const auto pose = world_->pose(piece.body);
        if (!pose) continue;
        const std::uint32_t mine = static_cast<std::uint32_t>(index + 1);
        const float c = pose->rotation.x, s = pose->rotation.y;
        // The grid's corners in the field, for the cells to look at.
        float min_x = std::numeric_limits<float>::max(), min_y = min_x, max_x = -min_x, max_y = -min_x;
        for (const auto [gx, gy] : std::array<std::pair<float, float>, 4>{{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}}) {
            const float lx = (static_cast<float>(piece.left) + gx * static_cast<float>(piece.width)) * cell;
            const float ly = (static_cast<float>(piece.top) + gy * static_cast<float>(piece.height)) * cell;
            const float wx = pose->at.x + c * lx - s * ly, wy = pose->at.y + s * lx + c * ly;
            min_x = std::min(min_x, wx), max_x = std::max(max_x, wx), min_y = std::min(min_y, wy), max_y = std::max(max_y, wy);
        }
        const auto clamp_cell = [&](float metres, std::uint32_t size) {
            const float at = std::floor(metres / cell);
            return at < 0 ? 0u : at >= static_cast<float>(size) ? size - 1 : static_cast<std::uint32_t>(at);
        };
        if (max_x < 0 || max_y < 0 || min_x >= static_cast<float>(field_.width) * cell || min_y >= static_cast<float>(field_.height) * cell) continue;
        const std::uint32_t x0 = clamp_cell(min_x, field_.width), x1 = clamp_cell(max_x, field_.width);
        const std::uint32_t y0 = clamp_cell(min_y, field_.height), y1 = clamp_cell(max_y, field_.height);
        const std::size_t local_cells = static_cast<std::size_t>(piece.width) * piece.height;
        for (std::uint32_t y = y0; y <= y1; ++y)
            for (std::uint32_t x = x0; x <= x1; ++x) {
                const std::size_t f = static_cast<std::size_t>(y) * field_.width + x;
                if (owner_[f]) continue;  // another body's this frame
                // Where the cell's centre falls in the grid.
                const float dx = (static_cast<float>(x) + 0.5f) * cell - pose->at.x, dy = (static_cast<float>(y) + 0.5f) * cell - pose->at.y;
                const float lx = c * dx + s * dy, ly = -s * dx + c * dy;
                const float gx = std::floor(lx / cell) - static_cast<float>(piece.left), gy = std::floor(ly / cell) - static_cast<float>(piece.top);
                if (gx < 0 || gy < 0 || gx >= static_cast<float>(piece.width) || gy >= static_cast<float>(piece.height)) continue;
                const std::size_t local = static_cast<std::size_t>(gy) * piece.width + static_cast<std::size_t>(gx);
                if (piece.cells[local] == matter::none) continue;
                // What is there goes under the body — or, a liquid or a powder, changes places with the nearest gas
                // no body holds, whose content then lies under.
                std::size_t under_from = f;
                const std::uint32_t there = field[f];
                if (!gas(there) && !solid(there)) {
                    std::optional<std::size_t> found;
                    for (std::uint32_t ring = 1; ring <= options_.room && !found; ++ring)
                        for (std::int64_t oy = -std::int64_t{ring}; oy <= std::int64_t{ring} && !found; ++oy)
                            for (std::int64_t ox = -std::int64_t{ring}; ox <= std::int64_t{ring} && !found; ++ox) {
                                if (std::max(std::abs(ox), std::abs(oy)) != ring) continue;
                                const std::int64_t gx2 = std::int64_t{x} + ox, gy2 = std::int64_t{y} + oy;
                                if (gx2 < 0 || gy2 < 0 || gx2 >= field_.width || gy2 >= field_.height) continue;
                                const std::size_t g = static_cast<std::size_t>(gy2) * field_.width + static_cast<std::size_t>(gx2);
                                if (!owner_[g] && gas(field[g])) found = g;
                            }
                    if (found) {
                        for (std::uint32_t channel = 0; channel < channels_; ++channel) std::swap(field[channel * cells + f], field[channel * cells + *found]);
                        ++last_.displaced;
                    } else {
                        ++last_.hidden;
                    }
                }
                for (std::uint32_t channel = 0; channel < channels_; ++channel) {
                    piece.under.push_back(field[channel * cells + under_from]);
                    field[channel * cells + f] = piece.cells[channel * local_cells + local];
                }
                piece.covered.push_back(static_cast<std::uint32_t>(f));
                piece.from.push_back(static_cast<std::uint32_t>(local));
                owner_[f] = mine;
                ++last_.covered;
            }
    }
}

void Bodies::terrain(std::span<const std::uint32_t> field) {
    last_.terrain_tiles = 0;
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    if (field.size() != cells * channels_) return;
    const std::uint32_t side = options_.terrain_tile;
    const Shape tiles = cellular::tiles(field_, {side, side});
    for (std::uint32_t ty = 0; ty < tiles.height; ++ty)
        for (std::uint32_t tx = 0; tx < tiles.width; ++tx) {
            const std::size_t tile = static_cast<std::size_t>(ty) * tiles.width + tx;
            const std::uint32_t x0 = tx * side, y0 = ty * side, width = std::min(side, field_.width - x0), height = std::min(side, field_.height - y0);
            // The tile's solid cells no body holds, as a hash: rebuilt only where it changed.
            mlibs::hash::Fnv1a hash;
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::size_t f = static_cast<std::size_t>(y0 + y) * field_.width + x0 + x;
                    hash.le(!owner_[f] && solid(field[f]) ? field[f] + 1 : 0, 4);
                }
            if (hash.value() == terrain_hash_[tile]) continue;
            terrain_hash_[tile] = hash.value();
            ++last_.terrain_tiles;
            if (terrain_[tile]) world_->remove(terrain_[tile]), terrain_[tile] = {};
            if (!scratch_) scratch_ = std::make_shared<Scratch>();
            Scratch& scratch = *scratch_;
            rectangles(width, height, [&](std::uint32_t x, std::uint32_t y) -> std::optional<std::uint32_t> {
                const std::size_t f = static_cast<std::size_t>(y0 + y) * field_.width + x0 + x;
                if (owner_[f] || !solid(field[f])) return std::nullopt;
                return 1u;  // one fixed body: what the terrain is made of meets the bodies by their own materials
            }, scratch.rects, scratch.used);
            const std::vector<Rect>& rects = scratch.rects;
            if (rects.empty()) continue;
            std::vector<dyn::Shape>& shapes = scratch.shapes;
            std::vector<dyn::Material>& materials = scratch.materials;
            shapes.clear(), materials.clear();
            const float cell = options_.cell;
            for (const Rect& rect : rects) {
                dyn::Shape box = dyn::Shape::box(static_cast<float>(rect.width) * cell * 0.5f, static_cast<float>(rect.height) * cell * 0.5f);
                box.centre = {(static_cast<float>(rect.x) + static_cast<float>(rect.width) * 0.5f) * cell, (static_cast<float>(rect.y) + static_cast<float>(rect.height) * 0.5f) * cell};
                shapes.push_back(box);
                materials.push_back({.density = 0, .friction = 0.6f});
            }
            if (auto made = world_->add({.kind = dyn::Kind::fixed, .at = {static_cast<float>(x0) * cell, static_cast<float>(y0) * cell}}, shapes, materials)) terrain_[tile] = *made;
        }
}

std::vector<View> Bodies::bodies() const {
    std::vector<View> out;
    for (const Piece& piece : pieces_) {
        const std::size_t local_cells = static_cast<std::size_t>(piece.width) * piece.height;
        View view{piece.body, 0, piece.width, piece.height};
        for (std::size_t local = 0; local < local_cells; ++local) view.cells += piece.cells[local] != matter::none;
        out.push_back(view);
    }
    return out;
}

std::uint32_t Bodies::material_of(dyn::BodyId body) const noexcept {
    for (const Piece& piece : pieces_) {
        if (!(piece.body == body)) continue;
        const std::size_t local_cells = static_cast<std::size_t>(piece.width) * piece.height;
        for (std::size_t local = 0; local < local_cells && local < piece.cells.size(); ++local)
            if (piece.cells[local] != xengine::matter::none) return piece.cells[local];
    }
    return xengine::matter::none;
}

std::optional<View> Bodies::body(dyn::BodyId body) const {
    for (const View& view : bodies())
        if (view.body == body) return view;
    return std::nullopt;
}

std::vector<Tile> Bodies::touched() const {
    std::vector<Tile> out;
    touched(out);
    return out;
}
void Bodies::touched(std::vector<Tile>& out) const {
    out.clear();
    for (const Piece& piece : pieces_) {
        if (piece.covered.empty()) continue;
        std::uint32_t x0 = field_.width, y0 = field_.height, x1 = 0, y1 = 0;
        for (const std::uint32_t f : piece.covered) {
            const std::uint32_t x = f % field_.width, y = f / field_.width;
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
        const std::uint32_t room = options_.room;
        x0 = x0 > room ? x0 - room : 0, y0 = y0 > room ? y0 - room : 0;
        x1 = std::min(field_.width - 1, x1 + room), y1 = std::min(field_.height - 1, y1 + room);
        out.push_back({x0, y0, x1 - x0 + 1, y1 - y0 + 1});
    }
}

std::optional<dyn::BodyId> Bodies::owner(std::uint32_t x, std::uint32_t y) const {
    if (x >= field_.width || y >= field_.height) return std::nullopt;
    const std::uint32_t at = owner_[static_cast<std::size_t>(y) * field_.width + x];
    if (!at) return std::nullopt;
    return pieces_[at - 1].body;
}

}  // namespace xengine::cellular::rigid

// ── the coupling's state as bytes, and a world of matter with bodies ─────
namespace xengine::cellular::rigid {
namespace {
void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void put_list(std::vector<std::uint8_t>& out, std::span<const std::uint32_t> values) {
    put64(out, values.size());
    for (const std::uint32_t value : values) put32(out, value);
}
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t& at;
    bool ok = true;
    std::uint64_t take(int width) {
        if (!ok || bytes.size() - at < static_cast<std::size_t>(width)) { ok = false; return 0; }
        std::uint64_t value = 0;
        for (int index = 0; index < width; ++index) value |= std::uint64_t{bytes[at + static_cast<std::size_t>(index)]} << (8 * index);
        at += static_cast<std::size_t>(width);
        return value;
    }
    std::uint32_t u32() { return static_cast<std::uint32_t>(take(4)); }
    std::uint64_t u64() { return take(8); }
    std::vector<std::uint32_t> list() {
        const std::uint64_t count = u64();
        if (!ok || count > (bytes.size() - at) / 4) { ok = false; return {}; }
        std::vector<std::uint32_t> values(static_cast<std::size_t>(count));
        for (std::uint32_t& value : values) value = u32();
        return values;
    }
};
}  // namespace

void Bodies::save(std::vector<std::uint8_t>& out) const {
    put64(out, pieces_.size());
    for (const Piece& piece : pieces_) {
        put32(out, static_cast<std::uint32_t>(piece.body.index)), put32(out, piece.body.generation);
        put32(out, static_cast<std::uint32_t>(piece.left)), put32(out, static_cast<std::uint32_t>(piece.top)), put32(out, piece.width), put32(out, piece.height);
        put_list(out, piece.cells), put_list(out, piece.covered), put_list(out, piece.from), put_list(out, piece.under);
    }
    put_list(out, owner_);
    put_list(out, fill_);
    put64(out, terrain_.size());
    for (std::size_t tile = 0; tile < terrain_.size(); ++tile)
        put32(out, static_cast<std::uint32_t>(terrain_[tile].index)), put32(out, terrain_[tile].generation), put64(out, terrain_hash_[tile]);
}

std::expected<void, std::string> Bodies::restore(std::span<const std::uint8_t> saved, std::size_t& at) {
    Reader read{saved, at};
    std::vector<Piece> pieces(static_cast<std::size_t>(std::min<std::uint64_t>(read.u64(), saved.size())));
    for (Piece& piece : pieces) {
        const std::uint32_t index = read.u32(), generation = read.u32();
        piece.body = dyn::BodyId{static_cast<std::int32_t>(index), static_cast<std::uint16_t>(generation)};
        piece.left = static_cast<std::int32_t>(read.u32()), piece.top = static_cast<std::int32_t>(read.u32());
        piece.width = read.u32(), piece.height = read.u32();
        piece.cells = read.list(), piece.covered = read.list(), piece.from = read.list(), piece.under = read.list();
        if (read.ok && piece.cells.size() != static_cast<std::size_t>(piece.width) * piece.height * channels_) read.ok = false;
    }
    auto owner = read.list();
    auto fill = read.list();
    const std::uint64_t tiles = read.u64();
    std::vector<dyn::BodyId> terrain;
    std::vector<std::uint64_t> hashes;
    for (std::uint64_t tile = 0; read.ok && tile < tiles; ++tile) {
        const std::uint32_t index = read.u32(), generation = read.u32();
        terrain.push_back(dyn::BodyId{static_cast<std::int32_t>(index), static_cast<std::uint16_t>(generation)});
        hashes.push_back(read.u64());
    }
    if (!read.ok || owner.size() != owner_.size() || terrain.size() != terrain_.size()) return std::unexpected(std::string("not a saved coupling of this field"));
    pieces_ = std::move(pieces), owner_ = std::move(owner), fill_ = std::move(fill), terrain_ = std::move(terrain), terrain_hash_ = std::move(hashes);
    return {};
}

Bodies Bodies::onto(dyn::World& world) const {
    Bodies copy = *this;
    copy.world_ = &world;
    copy.scratch_.reset();  // its own, made when it first needs it
    return copy;
}

std::expected<std::unique_ptr<MatterWorld>, std::string> MatterWorld::make(const matter::Program& program, const WorldOptions& options) {
    const auto fill = program.material(options.fill);
    if (!fill) return std::unexpected(std::format("no material `{}` to fill the world with", options.fill));
    auto lattice = cellular::lower(program, {.seed = options.seed});
    if (!lattice) return std::unexpected(std::format("the program does not lower: {}", lattice.error().text()));
    // Gravity is the definition's: its law of motion toward a direction. A lattice's cells fall along +y.
    float gravity = options.gravity.value_or(0.0f);
    for (const matter::Law& law : program.laws()) {
        if (law.kind != matter::LawKind::motion || law.along || options.gravity) continue;
        if (law.toward_x != 0 || !(law.toward_y > 0)) return std::unexpected(std::format("the law `{}` moves things toward ({}, {}): this world's cells fall along +y", law.name, law.toward_x, law.toward_y));
        gravity += static_cast<float>(law.accel);
    }
    auto rigid = dyn::World::make({.gravity = {0, gravity}});
    if (!rigid) return std::unexpected(rigid.error().text);
    std::unique_ptr<MatterWorld> world(new MatterWorld());
    world->program_ = &program;
    world->options_ = options;
    world->lattice_ = std::move(*lattice);
    const std::size_t area = static_cast<std::size_t>(options.shape.width) * options.shape.height;
    world->cells_ = program.fresh(area, *fill);
    world->next_.assign(world->cells_.size(), 0);
    world->outside_ = program.fresh(1, *fill);
    const Shape tiles = cellular::tiles(options.shape, options.tile);
    world->activity_.assign(static_cast<std::size_t>(tiles.width) * tiles.height, 0);
    world->listen();
    world->rigid_ = std::make_unique<dyn::World>(std::move(*rigid));
    Options of_bodies = options.bodies;
    of_bodies.gravity = gravity;
    auto bodies = Bodies::make(program, options.shape, *world->rigid_, of_bodies);
    if (!bodies) return std::unexpected(bodies.error());
    world->bodies_ = std::make_unique<Bodies>(std::move(*bodies));
    particles::Options flying = options.flying;
    flying.cell = options.bodies.cell, flying.gravity = gravity;
    auto made = particles::Particles::make(program, options.shape, flying);
    if (!made) return std::unexpected(made.error());
    world->flying_ = std::make_unique<particles::Particles>(std::move(*made));
    return world;
}

void Journal::clear() {
    thrown.clear(), landed.clear(), left.clear(), split.clear(), gone.clear(), edits.clear(), carried.clear(), lattice.clear(), activity.clear();
}

void MatterWorld::watch(bool on) {
    watched_ = on;
    journal_.clear(), pending_.clear();
    // Between steps what is thrown (a blast) is the next frame's.
    flying_->log(on ? &pending_ : nullptr);
    bodies_->log(nullptr);
}

std::uint32_t MatterWorld::blast(particles::Vec centre, float radius, float speed, bool breaks) {
    flying_->now(tick_ + 1);  // thrown between frames: of the frame the next step makes
    const std::uint32_t thrown = flying_->blast(cells_, outside_, centre, radius, speed, breaks);
    const std::uint32_t r = static_cast<std::uint32_t>(std::ceil(radius));
    const std::uint32_t x = static_cast<std::uint32_t>(std::max(0.0f, centre.x - static_cast<float>(r))), y = static_cast<std::uint32_t>(std::max(0.0f, centre.y - static_cast<float>(r)));
    lattice_.wake(options_.shape, options_.tile, {x, y, 2 * r + 1, 2 * r + 1}, activity_);
    if (watched_) pending_.edits.push_back({.kind = Journal::EditKind::blast, .region = {x, y, 2 * r + 1, 2 * r + 1}, .centre = centre, .radius = radius, .speed = speed, .breaks = breaks, .by = by_});
    return thrown;
}

void MatterWorld::place(Tile region, std::uint32_t material) {
    const std::size_t area = static_cast<std::size_t>(options_.shape.width) * options_.shape.height;
    if (material >= program_->materials().size()) return;
    for (std::uint32_t y = region.y; y < std::min(region.y + region.height, options_.shape.height); ++y)
        for (std::uint32_t x = region.x; x < std::min(region.x + region.width, options_.shape.width); ++x) {
            const std::size_t cell = static_cast<std::size_t>(y) * options_.shape.width + x;
            // What was there and was not gas is no longer: a work that does this turns one thing into another.
            const std::uint32_t was = cells_[cell];
            if (was != material && was < program_->materials().size() && program_->phase(was) != matter::Phase::gas) ++converted_;
            program_->place(cells_, area, cell, material);
        }
    lattice_.wake(options_.shape, options_.tile, region, activity_);
    if (watched_) pending_.edits.push_back({.kind = Journal::EditKind::place, .region = region, .value = material, .by = by_});
}

void MatterWorld::set(std::uint32_t channel, Tile region, std::uint32_t value) {
    const std::size_t area = static_cast<std::size_t>(options_.shape.width) * options_.shape.height;
    if (channel >= program_->channels().size()) return;
    for (std::uint32_t y = region.y; y < std::min(region.y + region.height, options_.shape.height); ++y)
        for (std::uint32_t x = region.x; x < std::min(region.x + region.width, options_.shape.width); ++x)
            cells_[channel * area + static_cast<std::size_t>(y) * options_.shape.width + x] = value;
    lattice_.wake(options_.shape, options_.tile, region, activity_);
    if (watched_) pending_.edits.push_back({.kind = Journal::EditKind::set, .region = region, .channel = channel, .value = value, .by = by_});
}

std::expected<std::vector<dyn::BodyId>, std::string> MatterWorld::carve(Tile region) {
    auto made = bodies_->carve(cells_, region, outside_);
    if (watched_ && made) pending_.edits.push_back({.kind = Journal::EditKind::carve, .region = region, .value = static_cast<std::uint32_t>(made->size()), .by = by_});
    return made;
}

void MatterWorld::listen() {
    heard_counts_.assign(activity_.size(), 0);
    heard_.assign(activity_.size() * happenings_a_tile, {});
    happened_.clear(), happened_.reserve(heard_.size());
    laws_ = laws_of_reactions(*program_);
}

void MatterWorld::events(std::vector<xengine::matter::Event>& out) const {
    for (const Happening& happening : happened_) out.push_back(event_of(*program_, laws_, happening));
}

void MatterWorld::step() {
    // Each part of a frame a span (simulation.world.*): where a frame's time goes is read from a trace.
    mlibs::trace::Span frame("simulation.world.step", "simulation");
    if (watched_) {
        // This frame's journal: what was done since the last step (and what that threw), then what the step does.
        journal_.clear();
        journal_.frame = tick_ + 1;
        journal_.edits.swap(pending_.edits), journal_.thrown.swap(pending_.thrown);
        pending_.clear();
        journal_.activity.assign(activity_.begin(), activity_.end());
        flying_->log(&journal_), bodies_->log(&journal_);
    }
    {
        Execution execution{.tile = options_.tile, .boundary = options_.open ? open_to(outside_) : nothing_beyond, .tick = tick_++};
        if (options_.sparse) execution.activity = activity_;
        execution.happened = heard_, execution.happened_counts = heard_counts_;
        (void)lattice_.step(options_.shape, cells_, next_, execution);
        happened_.clear();  // room for every place was made with the world: a frame allocates nothing
        for (std::size_t tile = 0; tile < heard_counts_.size(); ++tile)
            happened_.insert(happened_.end(), heard_.begin() + static_cast<std::ptrdiff_t>(tile * happenings_a_tile),
                             heard_.begin() + static_cast<std::ptrdiff_t>(tile * happenings_a_tile + heard_counts_[tile]));
        cells_.swap(next_);
        if (watched_) journal_.lattice.assign(cells_.begin(), cells_.end());
        flying_->now(tick_);
    }
    {
        // Cells in flight, among the bodies (stamped: what flies hits them): thrown by the pressure, falling, landing.
        mlibs::trace::Span span("simulation.world.flying", "simulation");
        (void)flying_->scatter(cells_, outside_);
        const bool flew = flying_->flying() > 0;
        flying_->step(cells_);
        // What flies writes the field from outside the lattice's step: a sparse world wakes it whole while anything
        // is in flight or has just landed (simple, and the same bits).
        if (options_.sparse && flew) std::ranges::fill(activity_, std::uint8_t{0});
    }
    // The bodies write the field from outside the lattice's step: where they were and where they are wake.
    const auto wake = [&] {
        if (!options_.sparse) return;
        bodies_->touched(touched_);
        for (const Tile& region : touched_) lattice_.wake(options_.shape, options_.tile, region, activity_);
    };
    {
        mlibs::trace::Span span("simulation.world.push", "simulation");
        bodies_->push(cells_);
        wake();
    }
    {
        mlibs::trace::Span span("simulation.world.lift", "simulation");
        bodies_->lift(cells_);
    }
    {
        mlibs::trace::Span span("simulation.world.terrain", "simulation");
        bodies_->terrain(cells_);
    }
    {
        mlibs::trace::Span span("simulation.world.rigid", "simulation");
        if (watched_) {
            poses_.clear();
            for (const View& view : bodies_->bodies())
                if (const auto pose = std::as_const(*rigid_).pose(view.body)) poses_.emplace_back(view.body, *pose);
        }
        rigid_->step();
        // Bodies that met something this step, for whoever plays a sound: of each contact that began with a
        // speed, the bodies of ours in it.
        struck_count_ = 0;
        for (const dyn::Contact& contact : std::as_const(*rigid_).contacts()) {
            if (!contact.began || !(contact.approach > 0)) continue;
            const float cell = options_.bodies.cell;
            const auto column = static_cast<std::int64_t>(std::floor(contact.point.x / cell)), row = static_cast<std::int64_t>(std::floor(contact.point.y / cell));
            const std::uint32_t at = static_cast<std::uint32_t>(std::clamp<std::int64_t>(row, 0, options_.shape.height - 1)) * options_.shape.width +
                                     static_cast<std::uint32_t>(std::clamp<std::int64_t>(column, 0, options_.shape.width - 1));
            for (const dyn::BodyId body : {contact.one, contact.other}) {
                const std::uint32_t material = bodies_->material_of(body);
                if (material == xengine::matter::none || struck_count_ == struck_.size()) continue;
                struck_[struck_count_++] = {at, material, contact.approach / cell};
            }
        }
        if (watched_)
            for (const auto& [body, before] : poses_)
                if (const auto after = std::as_const(*rigid_).pose(body); after && (after->at.x != before.at.x || after->at.y != before.at.y || after->angle != before.angle))
                    journal_.carried.push_back({body, before.at, after->at, after->angle - before.angle});
    }
    {
        mlibs::trace::Span span("simulation.world.stamp", "simulation");
        bodies_->stamp(cells_);
        wake();
    }
    if (watched_) flying_->log(&pending_), bodies_->log(nullptr);
}

std::vector<std::uint8_t> MatterWorld::save() const {
    std::vector<std::uint8_t> out{'X', 'M', 'W', '2'};
    put64(out, tick_);
    put_list(out, cells_);
    put64(out, activity_.size());
    out.insert(out.end(), activity_.begin(), activity_.end());
    bodies_->save(out);
    flying_->save(out);
    const auto rigid = rigid_->save();
    put64(out, rigid.size());
    out.insert(out.end(), rigid.begin(), rigid.end());
    return out;
}

std::expected<void, std::string> MatterWorld::restore(std::span<const std::uint8_t> saved) {
    if (saved.size() < 4 || saved[0] != 'X' || saved[1] != 'M' || saved[2] != 'W' || saved[3] != '2') return std::unexpected(std::string("not a saved world of matter"));
    std::size_t at = 4;
    Reader read{saved, at};
    const std::uint64_t tick = read.u64();
    auto cells = read.list();
    const std::uint64_t activity = read.u64();
    if (!read.ok || cells.size() != cells_.size() || activity != activity_.size() || saved.size() - at < activity) return std::unexpected(std::string("a saved world of another shape"));
    std::vector<std::uint8_t> bytes(saved.begin() + static_cast<std::ptrdiff_t>(at), saved.begin() + static_cast<std::ptrdiff_t>(at + activity));
    at += static_cast<std::size_t>(activity);
    // The coupling and the rigid world after it; nothing replaced until both read.
    Bodies coupled = bodies_->onto(*rigid_);
    if (auto restored = coupled.restore(saved, at); !restored) return std::unexpected(restored.error());
    particles::Particles flying = *flying_;
    if (auto restored = flying.restore(saved, at); !restored) return std::unexpected(restored.error());
    const std::uint64_t rigid_size = read.u64();
    if (!read.ok || saved.size() - at < rigid_size) return std::unexpected(std::string("a saved world cut short"));
    if (auto restored = rigid_->restore(saved.subspan(at, static_cast<std::size_t>(rigid_size))); !restored) return std::unexpected(restored.error().text);
    tick_ = tick, cells_ = std::move(cells), activity_ = std::move(bytes);
    happened_.clear();
    *bodies_ = std::move(coupled);
    *flying_ = std::move(flying);
    // Another state: what was being said of the old one is dropped.
    journal_.clear(), pending_.clear();
    flying_->log(watched_ ? &pending_ : nullptr), bodies_->log(nullptr);
    return {};
}

std::expected<std::unique_ptr<MatterWorld>, std::string> MatterWorld::branch() const {
    std::unique_ptr<MatterWorld> world(new MatterWorld());
    world->program_ = program_;
    world->options_ = options_;
    world->lattice_ = lattice_;
    world->cells_ = cells_;
    world->next_.assign(cells_.size(), 0);
    world->outside_ = outside_;
    world->activity_ = activity_;
    world->listen();
    world->tick_ = tick_;
    auto rigid = rigid_->branch();
    if (!rigid) return std::unexpected(rigid.error().text);
    world->rigid_ = std::make_unique<dyn::World>(std::move(*rigid));
    world->bodies_ = std::make_unique<Bodies>(bodies_->onto(*world->rigid_));
    world->flying_ = std::make_unique<particles::Particles>(*flying_);
    world->flying_->log(nullptr), world->bodies_->log(nullptr);  // a branch is not watched
    return world;
}

std::uint64_t MatterWorld::state_hash() const noexcept {
    mlibs::hash::Fnv1a hash(rigid_->state_hash());
    for (const std::uint32_t value : cells_) hash.le(value, 4);
    std::vector<std::uint8_t> flying;
    flying_->save(flying, false);  // what a step reads: where a particle came from is no part of the state's hash
    hash.bytes(flying);
    return hash.value();
}

WorldValue::WorldValue() noexcept = default;
WorldValue::WorldValue(std::unique_ptr<MatterWorld> world) noexcept : world_(std::move(world)) {}
WorldValue::WorldValue(const WorldValue& other) {
    if (!other.world_) return;
    if (auto branch = other.world_->branch()) world_ = std::move(*branch);
}
WorldValue& WorldValue::operator=(const WorldValue& other) {
    if (this != &other) *this = WorldValue(other);
    return *this;
}
WorldValue::WorldValue(WorldValue&&) noexcept = default;
WorldValue& WorldValue::operator=(WorldValue&&) noexcept = default;
WorldValue::~WorldValue() = default;
WorldValue::operator bool() const noexcept { return world_ != nullptr; }
MatterWorld& WorldValue::operator*() const noexcept { return *world_; }
MatterWorld* WorldValue::operator->() const noexcept { return world_.get(); }

}  // namespace xengine::cellular::rigid
