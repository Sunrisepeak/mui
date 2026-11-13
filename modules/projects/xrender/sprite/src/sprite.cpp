module xrender.sprite;

import std;
import mlibs.math;

namespace xrender::sprite {

namespace det = mlibs::math::det;

void skinned(const Region& region, const Grid& grid, const Posed& own, std::span<const Posed> bends, Mesh& out) {
    out.clear();
    // The art's own turn and size: anchor to tip along the bone (the picture's y runs down).
    double line_x = region.tip.x - region.anchor.x, line_y = -(region.tip.y - region.anchor.y);
    double line = det::sqrt(line_x * line_x + line_y * line_y);
    const double scale = grid.scale > 0 ? grid.scale : line > 0 ? grid.length / line : 1.0;
    if (line <= 0) line_x = 1, line_y = 0, line = 1;
    const double line_cos = line_x / line, line_sin = line_y / line;
    const double facing = grid.facing;
    const auto place = [&](const Bone& bone, double along, double across) {
        const double turn_cos = det::cos(bone.angle), turn_sin = det::sin(bone.angle);
        return Point{static_cast<float>(bone.joint.x + along * turn_cos - across * facing * turn_sin),
                     static_cast<float>(bone.joint.y + along * turn_sin + across * facing * turn_cos)};
    };
    const std::int32_t columns = std::max(1, grid.columns), rows = std::max(1, grid.rows);
    for (std::int32_t row = 0; row <= rows; ++row)
        for (std::int32_t column = 0; column <= columns; ++column) {
            const double pixel_x = region.source.x + region.source.width * column / columns, pixel_y = region.source.y + region.source.height * row / rows;
            const double from_x = pixel_x - region.anchor.x, from_y = -(pixel_y - region.anchor.y);
            const double along = (from_x * line_cos + from_y * line_sin) * scale, across = (-from_x * line_sin + from_y * line_cos) * scale * (grid.mirror ? -1 : 1);
            Point at = place(own.now, along, across);
            if (!bends.empty()) {
                const Point rest = place(own.rest, along, across);
                const double own_cos = det::cos(own.rest.angle), own_sin = det::sin(own.rest.angle);
                for (const Posed& bend : bends) {
                    const Bone& drawn = bend.rest;
                    const double bend_cos = det::cos(drawn.angle), bend_sin = det::sin(drawn.angle);
                    const double rest_x = rest.x - drawn.joint.x, rest_y = rest.y - drawn.joint.y;
                    const double along_bend = rest_x * bend_cos + rest_y * bend_sin, across_bend = (-rest_x * bend_sin + rest_y * bend_cos) * facing;
                    // The bend's joint on the part's own bone.
                    const double at_joint = (drawn.joint.x - own.rest.joint.x) * own_cos + (drawn.joint.y - own.rest.joint.y) * own_sin;
                    const double over = std::clamp(0.5 + (along - at_joint) / std::max(1.0, grid.blend * scale), 0.0, 1.0);
                    const Point moved = place(bend.now, along_bend, across_bend);
                    at = {static_cast<float>(at.x + (moved.x - at.x) * over), static_cast<float>(at.y + (moved.y - at.y) * over)};
                }
            }
            out.at.push_back(at);
            out.uv.push_back({static_cast<float>(pixel_x), static_cast<float>(pixel_y)});
        }
    for (std::int32_t row = 0; row < rows; ++row)
        for (std::int32_t column = 0; column < columns; ++column) {
            const auto first = static_cast<std::uint16_t>(row * (columns + 1) + column), stride = static_cast<std::uint16_t>(columns + 1);
            for (const std::uint16_t corner : {first, static_cast<std::uint16_t>(first + 1), static_cast<std::uint16_t>(first + stride + 1), first,
                                               static_cast<std::uint16_t>(first + stride + 1), static_cast<std::uint16_t>(first + stride)})
                out.index.push_back(corner);
        }
}

void cornered(const Rect& source, std::span<const Point, 4> placed, Mesh& out) {
    out.clear();
    const std::array<Point, 4> pixel{Point{source.x, source.y}, {source.x + source.width, source.y}, {source.x + source.width, source.y + source.height}, {source.x, source.y + source.height}};
    for (std::size_t corner = 0; corner < 4; ++corner) out.at.push_back(placed[corner]), out.uv.push_back(pixel[corner]);
    out.index.assign({0, 1, 2, 0, 2, 3});
}

}  // namespace xrender::sprite
