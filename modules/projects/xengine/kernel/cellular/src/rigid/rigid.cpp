module xengine.cellular.rigid;
import std;
import mlibs.hash;

namespace xengine::cellular::rigid {
namespace matter = xengine::matter;
namespace {

// A grid's occupied cells as rectangles of one label each, row-major greedy: the widest run from the first
// free cell, grown down while the whole run below is free and of its label. Exact (every cell in one).
struct Rect { std::uint32_t x = 0, y = 0, width = 0, height = 0, label = 0; };
template <class Label>
std::vector<Rect> rectangles(std::uint32_t width, std::uint32_t height, Label label_of) {
    std::vector<Rect> out;
    std::vector<std::uint8_t> used(static_cast<std::size_t>(width) * height, 0);
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
    return out;
}

}  // namespace

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
    bodies.pressure_ = program.channel("pressure");
    bodies.owner_.assign(static_cast<std::size_t>(field.width) * field.height, 0);
    const Shape tiles = cellular::tiles(field, {options.terrain_tile, options.terrain_tile});
    bodies.terrain_.assign(static_cast<std::size_t>(tiles.width) * tiles.height, dyn::BodyId{});
    bodies.terrain_hash_.assign(bodies.terrain_.size(), ~std::uint64_t{0});
    return bodies;
}

// The piece's body made (or its shapes replaced) from its cells: a box a rectangle of one material.
std::expected<void, std::string> Bodies::shape(Piece& piece, bool make_body, const dyn::Pose* like) {
    const std::size_t area = static_cast<std::size_t>(piece.width) * piece.height;
    const auto rects = rectangles(piece.width, piece.height, [&](std::uint32_t x, std::uint32_t y) -> std::optional<std::uint32_t> {
        const std::uint32_t material = piece.cells[static_cast<std::size_t>(y) * piece.width + x];
        if (material == matter::none) return std::nullopt;
        return material;
    });
    if (rects.empty()) return std::unexpected(std::string("a body of no cells"));
    const float cell = options_.cell;
    std::vector<dyn::Shape> shapes;
    std::vector<dyn::Material> materials;
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
                if (options_.blast > 0 && pressure_) {
                    const float push = options_.blast * static_cast<float>(field[static_cast<std::size_t>(*pressure_) * cells + n]);
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
        std::vector<std::uint8_t> read(local_cells, 0);
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
            const auto rects = rectangles(width, height, [&](std::uint32_t x, std::uint32_t y) -> std::optional<std::uint32_t> {
                const std::size_t f = static_cast<std::size_t>(y0 + y) * field_.width + x0 + x;
                if (owner_[f] || !solid(field[f])) return std::nullopt;
                return 1u;  // one fixed body: what the terrain is made of meets the bodies by their own materials
            });
            if (rects.empty()) continue;
            std::vector<dyn::Shape> shapes;
            std::vector<dyn::Material> materials;
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

std::optional<View> Bodies::body(dyn::BodyId body) const {
    for (const View& view : bodies())
        if (view.body == body) return view;
    return std::nullopt;
}

std::optional<dyn::BodyId> Bodies::owner(std::uint32_t x, std::uint32_t y) const {
    if (x >= field_.width || y >= field_.height) return std::nullopt;
    const std::uint32_t at = owner_[static_cast<std::size_t>(y) * field_.width + x];
    if (!at) return std::nullopt;
    return pieces_[at - 1].body;
}

}  // namespace xengine::cellular::rigid
