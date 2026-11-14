// Pictures of square pixels as objects: a part a pixel, or a part an ink —
// its region's outline traced along the cells' edges, staircase or smoothed
// with the same segments, so the one morphs into the other.
module hanim.object;
import std;

namespace hanim {
namespace {

constexpr std::size_t none = std::numeric_limits<std::size_t>::max();

bool finite_positive(double value) { return std::isfinite(value) && value > 0 && value < 1e9; }

// The cells' inks as a grid (none where empty), width × height.
struct Mask {
    int width = 0, height = 0;
    std::vector<std::size_t> ink;
    [[nodiscard]] std::size_t at(int x, int y) const {
        return x < 0 || y < 0 || x >= width || y >= height ? none : ink[static_cast<std::size_t>(y) * width + x];
    }
};

// The corners of every loop bounding the cells `inside` takes, in cell
// units: clockwise on screen around what is inside, holes the other way (a
// fill's nonzero rule leaves them empty); runs along one line are one edge.
std::vector<std::vector<Vec2>> trace(const Mask& mask, const std::function<bool(std::size_t)>& inside) {
    struct Edge {
        int from_x, from_y, to_x, to_y;
        bool used = false;
    };
    const auto in = [&](int x, int y) {
        const std::size_t ink = mask.at(x, y);
        return ink != none && inside(ink);
    };
    std::vector<Edge> edges;
    for (int y = 0; y < mask.height; ++y)
        for (int x = 0; x < mask.width; ++x) {
            if (!in(x, y)) continue;
            if (!in(x, y - 1)) edges.push_back({x, y, x + 1, y});
            if (!in(x + 1, y)) edges.push_back({x + 1, y, x + 1, y + 1});
            if (!in(x, y + 1)) edges.push_back({x + 1, y + 1, x, y + 1});
            if (!in(x - 1, y)) edges.push_back({x, y + 1, x, y});
        }
    const int stride = mask.width + 1;
    std::vector<std::vector<std::size_t>> leaving(static_cast<std::size_t>(stride) * (mask.height + 1));
    for (std::size_t i = 0; i < edges.size(); ++i) leaving[static_cast<std::size_t>(edges[i].from_y) * stride + edges[i].from_x].push_back(i);

    std::vector<std::vector<Vec2>> loops;
    for (std::size_t first = 0; first < edges.size(); ++first) {
        if (edges[first].used) continue;
        std::vector<std::pair<int, int>> points;
        std::size_t current = first;
        while (current != none && !edges[current].used) {
            Edge& edge = edges[current];
            edge.used = true;
            points.emplace_back(edge.from_x, edge.from_y);
            const int dx = edge.to_x - edge.from_x, dy = edge.to_y - edge.from_y;
            // Where two loops touch at a corner, keep turning right: each
            // region's own loop, never a figure eight.
            std::size_t next = none;
            int best = 3;
            for (const std::size_t candidate : leaving[static_cast<std::size_t>(edge.to_y) * stride + edge.to_x]) {
                if (edges[candidate].used && candidate != first) continue;
                const int cx = edges[candidate].to_x - edges[candidate].from_x, cy = edges[candidate].to_y - edges[candidate].from_y;
                const int rank = cx == -dy && cy == dx ? 0 : cx == dx && cy == dy ? 1 : 2;  // right, straight, left
                if (rank < best) best = rank, next = candidate;
            }
            current = next;
        }
        // Only the corners: where the direction turns.
        std::vector<Vec2> corners;
        const std::size_t count = points.size();
        for (std::size_t i = 0; i < count; ++i) {
            const auto [px, py] = points[(i + count - 1) % count];
            const auto [x, y] = points[i];
            const auto [nx, ny] = points[(i + 1) % count];
            if ((x - px) != (nx - x) || (y - py) != (ny - y)) corners.push_back({static_cast<double>(x), static_cast<double>(y)});
        }
        if (corners.size() >= 4) loops.push_back(std::move(corners));
    }
    return loops;
}

Vec2 lerp(Vec2 a, Vec2 b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
Vec2 scaled(Vec2 point, double by) { return {point.x * by, point.y * by}; }

// A loop of corners as two cubics a corner — from the middle of the edge
// before it, through (staircase) or round (smooth) the corner, to the middle
// of the edge after — so every form of a loop has the same segments.
std::expected<Contour, ObjectError> contour_of(const std::vector<Vec2>& corners, double cell, double smoothing) {
    const std::size_t count = corners.size();
    const auto middle = [&](std::size_t i) { return lerp(corners[i % count], corners[(i + 1) % count], 0.5); };
    const auto cubic_of = [&](Vec2 from, Vec2 control, Vec2 to) {
        return ShapeSegment::cubic(scaled(lerp(from, control, 2.0 / 3), cell), scaled(lerp(to, control, 2.0 / 3), cell), scaled(to, cell));
    };
    std::vector<ShapeSegment> segments;
    segments.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        const Vec2 before = middle(i + count - 1), corner = corners[i], after = middle(i);
        // The staircase: a line to the corner and on, each a cubic.
        const Vec2 sharp_in_1 = lerp(before, corner, 1.0 / 3), sharp_in_2 = lerp(before, corner, 2.0 / 3);
        const Vec2 sharp_out_1 = lerp(corner, after, 1.0 / 3), sharp_out_2 = lerp(corner, after, 2.0 / 3);
        // The curve: the quadratic before → corner → after, halved.
        const Vec2 halfway = lerp(lerp(before, corner, 0.5), lerp(corner, after, 0.5), 0.5);
        const ShapeSegment round_in = cubic_of(before, lerp(before, corner, 0.5), halfway);
        const ShapeSegment round_out = cubic_of(halfway, lerp(corner, after, 0.5), after);
        const auto blend = [&](Vec2 sharp, Vec2 round) { return lerp(scaled(sharp, cell), round, smoothing); };
        segments.push_back(ShapeSegment::cubic(blend(sharp_in_1, round_in.control1), blend(sharp_in_2, round_in.control2),
                                               blend(corner, round_in.end)));
        segments.push_back(ShapeSegment::cubic(blend(sharp_out_1, round_out.control1), blend(sharp_out_2, round_out.control2),
                                               scaled(after, cell)));
    }
    return Contour::copy({scaled(middle(count - 1), cell), segments, true}, segments.size() + 1);
}

}  // namespace

std::string pixel_id(int x, int y) { return std::format("px_{}_{}", x, y); }
std::string ink_id(char key) { return std::format("ink_{}", key); }

std::expected<std::vector<PixelCell>, ObjectError> pixel_cells(const PixelGrid& grid) {
    std::vector<PixelCell> cells;
    for (std::size_t y = 0; y < grid.rows.size(); ++y)
        for (std::size_t x = 0; x < grid.rows[y].size(); ++x) {
            const char key = grid.rows[y][x];
            if (key == '.' || key == ' ') continue;
            const auto ink = std::ranges::find(grid.inks, key, &PixelInk::key);
            if (ink == grid.inks.end()) return std::unexpected(ObjectError::invalid_color);
            cells.push_back({static_cast<int>(x), static_cast<int>(y), static_cast<std::size_t>(ink - grid.inks.begin())});
        }
    return cells;
}

std::expected<ObjectDescription, ObjectError> pixel_object(std::string root, const PixelGrid& grid, PixelOptions options) {
    if (root.empty()) return std::unexpected(ObjectError::invalid_id);
    if (!finite_positive(grid.cell) || !std::isfinite(options.bleed) || options.bleed < 0 || options.bleed > 0.5 || !std::isfinite(options.smoothing) ||
        options.smoothing < 0 || options.smoothing > 1)
        return std::unexpected(ObjectError::invalid_coordinate);
    for (std::size_t i = 0; i < grid.inks.size(); ++i)
        if (grid.inks[i].key == '.' || grid.inks[i].key == ' ' ||
            std::ranges::count(grid.inks, grid.inks[i].key, &PixelInk::key) != 1)
            return std::unexpected(ObjectError::duplicate_id);
    auto cells = pixel_cells(grid);
    if (!cells) return std::unexpected(cells.error());

    ObjectDescription description;
    description.parts.push_back({.id = root});
    const double cell = grid.cell;
    if (options.form == PixelForm::pixels) {
        const double half = cell * (0.5 + options.bleed);
        const std::array edges{ShapeSegment::line({half, -half}), ShapeSegment::line({half, half}), ShapeSegment::line({-half, half})};
        auto square = Contour::copy({{-half, -half}, edges, true});
        if (!square) return std::unexpected(square.error());
        for (const PixelCell& at : *cells)
            description.parts.push_back({.id = pixel_id(at.x, at.y),
                                         .parent = root,
                                         .local = {.tx = (at.x + 0.5) * cell, .ty = (at.y + 0.5) * cell},
                                         .contours = {*square},
                                         .fill = grid.inks[at.ink].color});
        return description;
    }
    Mask mask;
    for (const auto& row : grid.rows) mask.width = std::max(mask.width, static_cast<int>(row.size()));
    mask.height = static_cast<int>(grid.rows.size());
    mask.ink.assign(static_cast<std::size_t>(mask.width) * mask.height, none);
    for (const PixelCell& at : *cells) mask.ink[static_cast<std::size_t>(at.y) * mask.width + at.x] = at.ink;
    const double smoothing = options.form == PixelForm::smooth ? options.smoothing : 0;
    for (std::size_t ink = 0; ink < grid.inks.size(); ++ink) {
        if (std::ranges::none_of(*cells, [&](const PixelCell& c) { return c.ink == ink; })) continue;
        PartDescription part{.id = ink_id(grid.inks[ink].key), .parent = root, .fill = grid.inks[ink].color};
        for (const auto& loop : trace(mask, [ink](std::size_t other) { return other >= ink; })) {
            auto contour = contour_of(loop, cell, smoothing);
            if (!contour) return std::unexpected(contour.error());
            part.contours.push_back(std::move(*contour));
        }
        description.parts.push_back(std::move(part));
    }
    return description;
}

}  // namespace hanim
