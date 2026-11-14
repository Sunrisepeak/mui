module xrender.mesh;

import std;

namespace xrender::mesh {

namespace math = mlibs::math;

namespace {

using Square = Surroundings::Square;

constexpr float sign_of(float value) noexcept { return value < 0 ? -1.0f : 1.0f; }

// A way (of any length but none) to a place in the unit square: the eight faces of an octahedron unfolded, the
// upper four the diamond in the middle and the lower four folded out to the corners. No angle is taken.
constexpr Vec2 square_of(Vec3 way) noexcept {
    const float total = (way.x < 0 ? -way.x : way.x) + (way.y < 0 ? -way.y : way.y) + (way.z < 0 ? -way.z : way.z);
    if (!(total > 0)) return {0.5f, 0.5f};
    float across = way.x / total, down = way.z / total;
    if (way.y < 0) {
        const float kept = across;
        across = (1.0f - (down < 0 ? -down : down)) * sign_of(kept);
        down = (1.0f - (kept < 0 ? -kept : kept)) * sign_of(down);
    }
    return {across * 0.5f + 0.5f, down * 0.5f + 0.5f};
}
// The way a place in the square stands for, not of length 1: its length says how small the place's share of
// all the ways is (a share ∝ 1 / length³).
constexpr Vec3 way_of(Vec2 place) noexcept {
    const float across = place.x * 2.0f - 1.0f, down = place.y * 2.0f - 1.0f;
    const float up = 1.0f - (across < 0 ? -across : across) - (down < 0 ? -down : down);
    if (up >= 0) return {across, up, down};
    return {(1.0f - (down < 0 ? -down : down)) * sign_of(across), up, (1.0f - (across < 0 ? -across : across)) * sign_of(down)};
}
// The texel beyond an edge is the one beside the edge, as far from the edge's middle on its other side: the
// square's rim is the way straight down's neighbourhood, each edge folded shut about its middle.
constexpr std::size_t folded(int side, int column, int row) noexcept {
    if (column < 0) column = 0, row = side - 1 - row;
    else if (column >= side) column = side - 1, row = side - 1 - row;
    if (row < 0) row = 0, column = side - 1 - column;
    else if (row >= side) row = side - 1, column = side - 1 - column;
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(side) + static_cast<std::size_t>(column);
}
// Between the four texels about a way.
Vec3 between(const Square& square, Vec3 way) noexcept {
    if (square.side <= 0) return {0, 0, 0};
    const Vec2 place = square_of(way);
    const float column = place.x * static_cast<float>(square.side) - 0.5f, row = place.y * static_cast<float>(square.side) - 0.5f;
    const float left = std::floor(column), top = std::floor(row);
    const float along = column - left, below = row - top;
    const int first_column = static_cast<int>(left), first_row = static_cast<int>(top);
    const Vec3& top_left = square.light[folded(square.side, first_column, first_row)];
    const Vec3& top_right = square.light[folded(square.side, first_column + 1, first_row)];
    const Vec3& bottom_left = square.light[folded(square.side, first_column, first_row + 1)];
    const Vec3& bottom_right = square.light[folded(square.side, first_column + 1, first_row + 1)];
    const Vec3 upper = top_left + (top_right - top_left) * along, lower = bottom_left + (bottom_right - bottom_left) * along;
    return upper + (lower - upper) * below;
}
// Between two of a row of squares, each half the one before it across.
Vec3 between(std::span<const Square> squares, Vec3 way, float which) noexcept {
    if (squares.empty()) return {0, 0, 0};
    const float last = static_cast<float>(squares.size() - 1);
    const float held = which > 0 ? (which < last ? which : last) : 0.0f;
    const std::size_t first = static_cast<std::size_t>(held);
    const float share = held - static_cast<float>(first);
    const Vec3 nearer = between(squares[first], way);
    if (!(share > 0) || first + 1 >= squares.size()) return nearer;
    return nearer + (between(squares[first + 1], way) - nearer) * share;
}

// The picture as it came — a row an angle down from straight up, a column an angle about — at the size asked:
// each texel the mean of what it covers of the picture where the picture is finer, the picture itself where not.
struct Rows {
    int width = 0, height = 0;
    std::vector<Vec3> light;
};
Rows rows_of(std::span<const float> rgb, int width, int height, int most_height, float strength, float most, std::uint64_t& clamped) {
    const auto read = [&](int column, int row) {
        const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)) * 3;
        Vec3 light{rgb[at], rgb[at + 1], rgb[at + 2]};
        bool cut = false;
        for (float* channel : {&light.x, &light.y, &light.z}) {
            float value = *channel * strength;
            if (!(value > 0)) value = 0;  // less than none, or not a number
            if (value > most) value = most, cut = true;
            *channel = value;
        }
        if (cut) ++clamped;
        return light;
    };
    Rows rows;
    rows.height = std::min(height, most_height), rows.width = std::min(width, most_height * 2);
    rows.light.resize(static_cast<std::size_t>(rows.width) * static_cast<std::size_t>(rows.height));
    if (rows.width == width && rows.height == height) {
        for (int row = 0; row < height; ++row)
            for (int column = 0; column < width; ++column) rows.light[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)] = read(column, row);
        return rows;
    }
    // What each texel covers of the picture, across and down: whole texels and the parts of two at its ends.
    struct Cover {
        int   from = 0, until = 0;
        float first = 1, last = 1;  // how much of the first and of the last counts
    };
    const auto covers = [](int coarse, int fine) {
        std::vector<Cover> all(static_cast<std::size_t>(coarse));
        const double each = static_cast<double>(fine) / static_cast<double>(coarse);
        for (int index = 0; index < coarse; ++index) {
            const double begin = each * index, end = each * (index + 1);
            Cover& cover = all[static_cast<std::size_t>(index)];
            cover.from = static_cast<int>(std::floor(begin)), cover.until = std::min(static_cast<int>(std::ceil(end)), fine);
            cover.first = static_cast<float>(std::min(static_cast<double>(cover.from + 1), end) - begin);
            cover.last = static_cast<float>(end - std::max(static_cast<double>(cover.until - 1), begin));
        }
        return all;
    };
    const std::vector<Cover> across = covers(rows.width, width), down = covers(rows.height, height);
    for (int row = 0; row < rows.height; ++row) {
        const Cover& rows_covered = down[static_cast<std::size_t>(row)];
        for (int column = 0; column < rows.width; ++column) {
            const Cover& columns_covered = across[static_cast<std::size_t>(column)];
            Vec3 sum{0, 0, 0};
            float weight = 0;
            for (int source_row = rows_covered.from; source_row < rows_covered.until; ++source_row) {
                const float tall = source_row == rows_covered.from ? rows_covered.first : (source_row == rows_covered.until - 1 ? rows_covered.last : 1.0f);
                for (int source_column = columns_covered.from; source_column < columns_covered.until; ++source_column) {
                    const float wide = source_column == columns_covered.from ? columns_covered.first : (source_column == columns_covered.until - 1 ? columns_covered.last : 1.0f);
                    sum = sum + read(source_column, source_row) * (tall * wide);
                    weight += tall * wide;
                }
            }
            rows.light[static_cast<std::size_t>(row) * static_cast<std::size_t>(rows.width) + static_cast<std::size_t>(column)] = weight > 0 ? sum * (1.0f / weight) : Vec3{0, 0, 0};
        }
    }
    return rows;
}
// The picture along a way: the angle about (from +x toward +z, the picture's middle column at +x before it is
// turned) and the angle down from straight up; between four texels, round the picture across and held at its
// top and bottom rows.
Vec3 along(const Rows& rows, Vec3 way, double turn) noexcept {
    const double about = (math::det::atan2(static_cast<double>(way.z), static_cast<double>(way.x)) - turn) / math::det::two_pi + 0.5;
    const double level = std::sqrt(static_cast<double>(way.x) * way.x + static_cast<double>(way.z) * way.z);
    const double from_top = math::det::atan2(level, static_cast<double>(way.y)) / math::det::pi;
    const double column = (about - std::floor(about)) * rows.width - 0.5, row = from_top * rows.height - 0.5;
    const double left = std::floor(column), top = std::floor(row);
    const float along_row = static_cast<float>(column - left), below = static_cast<float>(row - top);
    const auto wrapped = [&](int column_at) { return ((column_at % rows.width) + rows.width) % rows.width; };
    const auto held = [&](int row_at) { return std::clamp(row_at, 0, rows.height - 1); };
    const auto at = [&](int column_at, int row_at) -> const Vec3& {
        return rows.light[static_cast<std::size_t>(held(row_at)) * static_cast<std::size_t>(rows.width) + static_cast<std::size_t>(wrapped(column_at))];
    };
    const int first_column = static_cast<int>(left), first_row = static_cast<int>(top);
    const Vec3 upper = at(first_column, first_row) + (at(first_column + 1, first_row) - at(first_column, first_row)) * along_row;
    const Vec3 lower = at(first_column, first_row + 1) + (at(first_column + 1, first_row + 1) - at(first_column, first_row + 1)) * along_row;
    return upper + (lower - upper) * below;
}

Vec2 middle_of(int side, int column, int row) noexcept {
    return {(static_cast<float>(column) + 0.5f) / static_cast<float>(side), (static_cast<float>(row) + 0.5f) / static_cast<float>(side)};
}
// A texel's share of all the ways, up to a factor that is the same for every texel of a square.
float share_of(Vec3 way) noexcept {
    const float length = math::length(way);
    return 1.0f / (length * length * length);
}
// A square half as far across: each texel the mean of four, each by its share of the ways.
Square halved(const Square& fine) {
    Square coarse;
    coarse.side = fine.side / 2;
    coarse.light.resize(static_cast<std::size_t>(coarse.side) * static_cast<std::size_t>(coarse.side));
    for (int row = 0; row < coarse.side; ++row)
        for (int column = 0; column < coarse.side; ++column) {
            Vec3 sum{0, 0, 0};
            float weight = 0;
            for (int below = 0; below < 2; ++below)
                for (int beside = 0; beside < 2; ++beside) {
                    const int fine_column = column * 2 + beside, fine_row = row * 2 + below;
                    const float share = share_of(way_of(middle_of(fine.side, fine_column, fine_row)));
                    sum = sum + fine.light[static_cast<std::size_t>(fine_row) * static_cast<std::size_t>(fine.side) + static_cast<std::size_t>(fine_column)] * share;
                    weight += share;
                }
            coarse.light[static_cast<std::size_t>(row) * static_cast<std::size_t>(coarse.side) + static_cast<std::size_t>(column)] = sum * (1.0f / weight);
        }
    return coarse;
}

// Two ways across a normal, the three at right angles (as the tracer takes them).
struct Frame {
    Vec3 aside, other, normal;
    explicit Frame(Vec3 about) noexcept : normal(about) {
        aside = math::normalized(math::cross((about.y < 0 ? -about.y : about.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, about));
        other = math::cross(about, aside);
    }
    [[nodiscard]] Vec3 out(Vec3 within) const noexcept { return aside * within.x + other * within.y + normal * within.z; }
};
// The second of a pair of numbers that fill the unit square evenly: a count's bits, turned end for end.
constexpr float turned_bits(std::uint32_t count) noexcept {
    std::uint32_t bits = count;
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
    bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
    return static_cast<float>(bits >> 8) / 16777216.0f;
}
// One of a surface's small faces, chosen as a highlight of this roughness spreads them (GGX): its normal about
// (0, 0, 1).
Vec3 small_face(float roughness, std::uint32_t count, std::uint32_t of) noexcept {
    const float spread = roughness * roughness * roughness * roughness;
    const float first = (static_cast<float>(count) + 0.5f) / static_cast<float>(of), turn = turned_bits(count) * 6.2831853f;
    const float up = std::sqrt((1.0f - first) / (1.0f + (spread - 1.0f) * first));
    const float out = std::sqrt(std::max(1.0f - up * up, 0.0f));
    return {out * static_cast<float>(math::det::cos(static_cast<double>(turn))), out * static_cast<float>(math::det::sin(static_cast<double>(turn))), up};
}

}  // namespace

std::optional<Surroundings> Surroundings::made(std::span<const float> rgb, int width, int height, const Wrapping& how) {
    if (width <= 0 || height <= 0 || rgb.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3) return std::nullopt;
    Surroundings made;
    // How far across: what is asked, or as fine as the picture (the power of two not above its height), 16 … 1024.
    int side = 16;
    const int asked = how.side > 0 ? how.side : std::min(height, 1024);
    while (side * 2 <= asked && side < 4096) side *= 2;
    const Rows rows = rows_of(rgb, width, height, side, how.strength, how.most, made.clamped);

    // The picture itself, and it at every size down to 4 across: what the blurred ones are gathered from.
    std::vector<Square> chain(1);
    chain[0].side = side;
    chain[0].light.resize(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    for (int row = 0; row < side; ++row)
        for (int column = 0; column < side; ++column)
            chain[0].light[static_cast<std::size_t>(row) * static_cast<std::size_t>(side) + static_cast<std::size_t>(column)] = along(rows, way_of(middle_of(side, column, row)), static_cast<double>(how.turn));
    while (chain.back().side > 4) chain.push_back(halved(chain.back()));

    // A size of the picture, each texel with its way and its light by its share of the ways: what the sums over
    // every way are taken from.
    struct From {
        Vec3  way;
        Vec3  light;  // by its share of the ways
        float share = 0;
    };
    const auto all_of = [&](int at_most) {
        const Square* gathered = &chain.back();
        for (const Square& square : chain)
            if (square.side <= at_most) {
                gathered = &square;
                break;
            }
        std::vector<From> from;
        from.reserve(gathered->light.size());
        for (int row = 0; row < gathered->side; ++row)
            for (int column = 0; column < gathered->side; ++column) {
                const Vec3 way = way_of(middle_of(gathered->side, column, row));
                const float share = share_of(way);
                from.push_back({math::normalized(way), gathered->light[static_cast<std::size_t>(row) * static_cast<std::size_t>(gathered->side) + static_cast<std::size_t>(column)] * share, share});
            }
        return from;
    };
    const std::vector<From> from = all_of(64), finer = all_of(128);

    // As a surface of each roughness mirrors it, looked at straight on: about each way, every way by how many
    // of the surface's small faces throw the eye's way there (GGX) and by how it faces. Each is as far across as
    // its highlight is narrow (a texel a sixth of the highlight's width, a power of two, 16 … `side`): a smooth
    // one fine, a rough one a few texels — not half the one before, which leaves the smooth ones too coarse.
    //   · A wide highlight (roughness 0.35 and up) is summed over every texel of a size finer than itself: no
    //     grain at all.
    //   · A narrow one is too fine for that. The ways the highlight throws are chosen as it spreads them, and
    //     each is taken from the size whose texels are as large as the share of the ways it stands for
    //     (Křivánek and Colbert's filtered sampling: few ways, no grain); at most 512 across.
    const int sizes = std::clamp(how.sizes, 2, 16);
    made.sizes.resize(static_cast<std::size_t>(sizes));
    made.sizes[0] = chain[0];
    struct Thrown {
        Vec3  way;  // about (0, 0, 1)
        float facing = 0, size = 0;
    };
    std::vector<Thrown> thrown;
    for (int which = 1; which < sizes; ++which) {
        const float roughness = static_cast<float>(which) / static_cast<float>(sizes - 1);
        const float spread = roughness * roughness * roughness * roughness;
        const bool wide = roughness >= 0.35f;
        Square& blurred = made.sizes[static_cast<std::size_t>(which)];
        blurred.side = 16;
        while (static_cast<float>(blurred.side) * roughness * roughness < 6.2831853f && blurred.side < std::min(side, wide ? 64 : 512)) blurred.side *= 2;
        blurred.side = std::min(blurred.side, side);
        blurred.light.resize(static_cast<std::size_t>(blurred.side) * static_cast<std::size_t>(blurred.side));
        if (wide) {
            const std::vector<From>& summed = blurred.side > 32 ? finer : from;
            for (int row = 0; row < blurred.side; ++row)
                for (int column = 0; column < blurred.side; ++column) {
                    const Vec3 about = math::normalized(way_of(middle_of(blurred.side, column, row)));
                    Vec3 sum{0, 0, 0};
                    float weight = 0;
                    for (const From& one : summed) {
                        const float facing = math::dot(about, one.way);
                        if (!(facing > 0)) continue;
                        const float under = (1.0f + facing) * 0.5f * (spread - 1.0f) + 1.0f;  // the half way's cosine, squared, is (1 + facing) / 2
                        const float counts = facing / (under * under);
                        sum = sum + one.light * counts;
                        weight += counts * one.share;
                    }
                    blurred.light[static_cast<std::size_t>(row) * static_cast<std::size_t>(blurred.side) + static_cast<std::size_t>(column)] = weight > 0 ? sum * (1.0f / weight) : Vec3{0, 0, 0};
                }
            continue;
        }
        // As many ways a texel as keep a size's work about that of 256 across at `samples`.
        const std::uint32_t samples = static_cast<std::uint32_t>(std::clamp(how.samples, 1, 4096)) * static_cast<std::uint32_t>(blurred.side >= 256 ? 1 : (blurred.side >= 128 ? 4 : 16));
        thrown.clear();
        for (std::uint32_t count = 0; count < samples; ++count) {
            const Vec3 face = small_face(roughness, count, samples);
            const float facing = 2.0f * face.z * face.z - 1.0f;
            if (!(facing > 0)) continue;
            const float under = face.z * face.z * (spread - 1.0f) + 1.0f;
            const double likelihood = static_cast<double>(spread) / (math::det::pi * under * under) / 4.0;  // a share of the ways, a way
            const double stands_for = 1.0 / (static_cast<double>(samples) * likelihood), texel = 4.0 * math::det::pi / (static_cast<double>(side) * side);
            const double size = 0.5 * math::det::log(stands_for / texel) / math::det::ln2;
            thrown.push_back({Vec3{2.0f * face.z * face.x, 2.0f * face.z * face.y, facing}, facing, static_cast<float>(std::max(size, 0.0))});
        }
        for (int row = 0; row < blurred.side; ++row)
            for (int column = 0; column < blurred.side; ++column) {
                const Frame frame{math::normalized(way_of(middle_of(blurred.side, column, row)))};
                Vec3 sum{0, 0, 0};
                float weight = 0;
                for (const Thrown& one : thrown) {
                    sum = sum + between(std::span<const Square>{chain}, frame.out(one.way), one.size) * one.facing;
                    weight += one.facing;
                }
                blurred.light[static_cast<std::size_t>(row) * static_cast<std::size_t>(blurred.side) + static_cast<std::size_t>(column)] = weight > 0 ? sum * (1.0f / weight) : Vec3{0, 0, 0};
            }
    }

    // What falls on a matt surface: about each normal, every way's light by how it faces the surface — summed
    // over the small size. No grain, and no ringing about a sun.
    made.falls.side = std::min(side, 32);
    made.falls.light.resize(static_cast<std::size_t>(made.falls.side) * static_cast<std::size_t>(made.falls.side));
    // All the ways are 4π; a surface faces half of them, by π in all: the sum is over 4π · share / shares, ÷ π.
    float shares = 0;
    for (const From& one : from) shares += one.share;
    const float each = 4.0f / shares;
    for (int row = 0; row < made.falls.side; ++row)
        for (int column = 0; column < made.falls.side; ++column) {
            const Vec3 normal = math::normalized(way_of(middle_of(made.falls.side, column, row)));
            Vec3 sum{0, 0, 0};
            for (const From& one : from) {
                const float facing = math::dot(normal, one.way);
                if (facing > 0) sum = sum + one.light * facing;
            }
            made.falls.light[static_cast<std::size_t>(row) * static_cast<std::size_t>(made.falls.side) + static_cast<std::size_t>(column)] = sum * each;
        }

    // Where the light is: a running sum over a size's texels, each its light by its share of the ways.
    const Square* counted = &chain.back();
    for (const Square& square : chain)
        if (square.side <= 256) {
            counted = &square;
            break;
        }
    made.chosen_side = counted->side;
    made.chosen_up_to.reserve(counted->light.size());
    double running = 0;
    for (int row = 0; row < counted->side; ++row)
        for (int column = 0; column < counted->side; ++column) {
            const Vec3& light = counted->light[static_cast<std::size_t>(row) * static_cast<std::size_t>(counted->side) + static_cast<std::size_t>(column)];
            running += static_cast<double>(light.x + light.y + light.z) * static_cast<double>(share_of(way_of(middle_of(counted->side, column, row))));
            made.chosen_up_to.push_back(running);
        }

    // How much a surface mirrors of an even light from all about, by how it is looked at and how rough it is:
    // (the share of what it mirrors head on, and what is added whatever that is) — the tracer's own highlight
    // (GGX, Smith for light from all about, Schlick), summed over 256 of its small faces.
    made.shares_side = 32;
    made.shares.resize(static_cast<std::size_t>(made.shares_side) * static_cast<std::size_t>(made.shares_side));
    constexpr std::uint32_t faces = 256;
    for (int row = 0; row < made.shares_side; ++row) {
        const float roughness = static_cast<float>(row) / static_cast<float>(made.shares_side - 1);
        const float hides = roughness * roughness * 0.5f;
        for (int column = 0; column < made.shares_side; ++column) {
            const float seen = std::max(static_cast<float>(column) / static_cast<float>(made.shares_side - 1), 1.0f / 1024.0f);
            const Vec3 toward_eye{std::sqrt(std::max(1.0f - seen * seen, 0.0f)), 0, seen};
            float scaled = 0, added = 0;
            for (std::uint32_t count = 0; count < faces; ++count) {
                const Vec3 face = small_face(roughness, count, faces);
                const float eye_half = math::dot(toward_eye, face);
                const float facing = 2.0f * eye_half * face.z - toward_eye.z;
                if (!(eye_half > 0) || !(facing > 0) || !(face.z > 0)) continue;
                const float unhidden = 1.0f / ((facing * (1.0f - hides) + hides) * (seen * (1.0f - hides) + hides));
                const float weight = unhidden * facing * eye_half / face.z;
                const float glance = 1.0f - eye_half, glance5 = glance * glance * glance * glance * glance;
                scaled += (1.0f - glance5) * weight, added += glance5 * weight;
            }
            made.shares[static_cast<std::size_t>(row) * static_cast<std::size_t>(made.shares_side) + static_cast<std::size_t>(column)] = {scaled / static_cast<float>(faces), added / static_cast<float>(faces)};
        }
    }
    return made;
}

Vec3 Surroundings::toward(Vec3 way) const noexcept { return sizes.empty() ? Vec3{0, 0, 0} : between(sizes[0], way); }
Vec3 Surroundings::blurred(Vec3 way, float roughness) const noexcept {
    return between(std::span<const Square>{sizes}, way, std::clamp(roughness, 0.0f, 1.0f) * static_cast<float>(sizes.empty() ? 0 : sizes.size() - 1));
}
// A place in the square is 4 / length³ of the ways a unit of the square's area (`way_of`'s length there).
Surroundings::Chosen Surroundings::chosen(float first, float second) const noexcept {
    if (chosen_up_to.empty() || !(chosen_up_to.back() > 0)) return {};
    const double whole = chosen_up_to.back(), sought = std::clamp(static_cast<double>(first), 0.0, 1.0) * whole;
    const std::size_t found = std::min(static_cast<std::size_t>(std::ranges::upper_bound(chosen_up_to, sought) - chosen_up_to.begin()), chosen_up_to.size() - 1);
    const double before = found > 0 ? chosen_up_to[found - 1] : 0.0, here = chosen_up_to[found] - before;
    if (!(here > 0)) return {};
    const float within = static_cast<float>(std::clamp((sought - before) / here, 0.0, 1.0));
    const std::size_t side = static_cast<std::size_t>(chosen_side);
    const Vec2 place{(static_cast<float>(found % side) + within) / static_cast<float>(chosen_side), (static_cast<float>(found / side) + std::clamp(second, 0.0f, 1.0f)) / static_cast<float>(chosen_side)};
    const Vec3 way = way_of(place);
    const float length = math::length(way);
    return {way * (1.0f / length), static_cast<float>(here / whole * static_cast<double>(chosen_side) * chosen_side * static_cast<double>(length * length * length) / 4.0)};
}
float Surroundings::likelihood(Vec3 way) const noexcept {
    if (chosen_up_to.empty() || !(chosen_up_to.back() > 0)) return 0;
    const Vec2 place = square_of(way);
    const int column = std::clamp(static_cast<int>(place.x * static_cast<float>(chosen_side)), 0, chosen_side - 1), row = std::clamp(static_cast<int>(place.y * static_cast<float>(chosen_side)), 0, chosen_side - 1);
    const std::size_t found = static_cast<std::size_t>(row) * static_cast<std::size_t>(chosen_side) + static_cast<std::size_t>(column);
    const double here = chosen_up_to[found] - (found > 0 ? chosen_up_to[found - 1] : 0.0);
    const float length = math::length(way_of(place));
    return static_cast<float>(here / chosen_up_to.back() * static_cast<double>(chosen_side) * chosen_side * static_cast<double>(length * length * length) / 4.0);
}
Vec3 Surroundings::falling(Vec3 normal) const noexcept { return between(falls, normal); }
Vec2 Surroundings::share(float seen, float roughness) const noexcept {
    if (shares_side <= 0) return {1, 0};
    const float last = static_cast<float>(shares_side - 1);
    const float column = std::clamp(seen, 0.0f, 1.0f) * last, row = std::clamp(roughness, 0.0f, 1.0f) * last;
    const int first_column = std::min(static_cast<int>(column), shares_side - 2), first_row = std::min(static_cast<int>(row), shares_side - 2);
    const float along_row = column - static_cast<float>(first_column), below = row - static_cast<float>(first_row);
    const auto at = [&](int column_at, int row_at) { return shares[static_cast<std::size_t>(row_at) * static_cast<std::size_t>(shares_side) + static_cast<std::size_t>(column_at)]; };
    const Vec2 upper = at(first_column, first_row) + (at(first_column + 1, first_row) - at(first_column, first_row)) * along_row;
    const Vec2 lower = at(first_column, first_row + 1) + (at(first_column + 1, first_row + 1) - at(first_column, first_row + 1)) * along_row;
    return upper + (lower - upper) * below;
}

}  // namespace xrender::mesh
