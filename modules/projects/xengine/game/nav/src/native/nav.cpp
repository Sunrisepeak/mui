// xengine.nav's World on Recast & Detour (mcpplibs.mwrap.recastnavigation).
//
// The mesh: Recast's pipeline a tile at a time, as upstream's tiled sample runs it, with the obstacles over a tile
// marked as no ground before the walkable area is eroded by the walker's radius (so a walker keeps that far from
// them) and the links that start in it. Each tile is kept as the bytes Detour was given (Detour writes into the
// copy it holds) in a slot of its own — the tile's place in the grid — under a generation (Detour's salt) that
// moves on when its bytes change. Whenever any tile changes, every tile is given to Detour again in slot order
// under its own reference: the links between polygons are then a function of the tiles alone, not of the order
// they were built in, and a world restored from an image links them the same.
//
// The crowd: Detour's dtCrowd steers, keeps apart, avoids and moves the walkers. What it would keep between steps
// where an image cannot reach is kept out of it: a path is planned here, whole, when a walker is sent and whenever
// dtCrowd would ask its path queue to plan again (the same checks, made before its step), so the queue is never
// used; a walker's local boundary is kept as the polygon and centre dtCrowd found it from (its own test, made
// before its step) and found again from them by a restore; and a link crossed is carried by this package the way
// dtCrowd carries it (the same arithmetic), so that a walker restored half across goes on to the same bits.
module xengine.nav;

import std;
import mlibs.hash;
import mcpplibs.mwrap.recastnavigation;

namespace recast = mcpplibs::mwrap::recastnavigation;

namespace xengine::nav {
namespace {

constexpr unsigned short walkable_flag = 1;
constexpr int most_path = 256;
constexpr int check_lookahead = 10;    // dtCrowd's: the corridor checked this many polygons ahead
constexpr float replan_delay = 1.0f;   // dtCrowd's: a partial path planned again after this long
constexpr std::string_view magic = "xengine.nav image 1\n";

const float* raw(const Vec& value) noexcept { return &value.x; }
Vec vec(const float* value) noexcept { return {value[0], value[1], value[2]}; }

std::uint64_t digest(std::span<const std::uint8_t> bytes) { return mlibs::hash::Fnv1a().bytes(bytes).value(); }

int next_power(int value) {
    int power = 1;
    while (power < value) power <<= 1;
    return power;
}
int log2_of(int value) {
    int bits = 0;
    while ((1 << bits) < value) ++bits;
    return bits;
}

// dtCrowd's own easing over a link (DetourCrowd.cpp: tween).
float tween(float time, float from, float to) { return dtClamp((time - from) / (to - from), 0.0f, 1.0f); }

struct Tile {
    std::vector<std::uint8_t> bytes;  // as built: empty when the tile has no polygon
    std::uint32_t salt = 1;           // its generation: moves on when its bytes change
};

// A link being crossed: as dtCrowd's animation of it, and whether this package carries it (a world restored or
// branched) or dtCrowd does (one that stepped into it).
struct Crossing {
    bool active = false;
    bool carried_here = false;
    Vec from_place, start, end;
    float time = 0, length = 0;
};

struct WalkerRecord {
    WalkerId id;
    int index = 0;  // dtCrowd's
    WalkerDef def;
    Crossing crossing;
    dtPolyRef boundary_from = 0;  // the polygon its local boundary was last found from (with its centre: found again)
};

struct Writer {
    std::vector<std::uint8_t> bytes;
    void put(std::uint64_t value, int count) {
        for (int index = 0; index < count; ++index) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    }
    void u8(std::uint8_t value) { put(value, 1); }
    void u32(std::uint32_t value) { put(value, 4); }
    void f32(float value) { put(std::bit_cast<std::uint32_t>(value), 4); }
    void v3(Vec value) { f32(value.x), f32(value.y), f32(value.z); }
    void v3(const float* value) { f32(value[0]), f32(value[1]), f32(value[2]); }
    void block(std::span<const std::uint8_t> value) {
        put(value.size(), 8);
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
    bool good = true;
    std::uint64_t take(int count) {
        if (!good || bytes.size() - at < static_cast<std::size_t>(count)) return good = false, 0;
        std::uint64_t value = 0;
        for (int index = 0; index < count; ++index) value |= static_cast<std::uint64_t>(bytes[at + static_cast<std::size_t>(index)]) << (8 * index);
        at += static_cast<std::size_t>(count);
        return value;
    }
    std::uint8_t u8() { return static_cast<std::uint8_t>(take(1)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(take(4)); }
    float f32() { return std::bit_cast<float>(u32()); }
    Vec v3() {
        const float x = f32(), y = f32();
        return {x, y, f32()};
    }
    void v3(float* out) { out[0] = f32(), out[1] = f32(), out[2] = f32(); }
    std::span<const std::uint8_t> block() {
        const std::uint64_t size = take(8);
        if (!good || bytes.size() - at < size) return good = false, std::span<const std::uint8_t>{};
        const auto out = bytes.subspan(at, size);
        at += size;
        return out;
    }
};

}  // namespace

struct World::Native {
    Ground ground;
    Walker walker;
    Options options;
    std::map<LinkId, Link> links;
    std::map<ObstacleId, Obstacle> obstacles;
    std::uint32_t next_link = 1, next_obstacle = 1, next_walker = 1;
    std::uint64_t ground_digest = 0;

    // The grid of tiles over the ground's bounds.
    float low[3]{}, high[3]{};
    int across = 0, along = 0;  // tiles in x and z
    std::vector<Tile> tiles;    // slot = along index * across + across index

    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;  // this package's: paths, rays, the nearest place
    dtCrowd* crowd = nullptr;
    dtQueryFilter filter;
    std::vector<WalkerRecord> walkers;  // in the order of their ids

    Native() = default;
    Native(const Native&) = delete;
    ~Native() {
        if (crowd) dtFreeCrowd(crowd);
        if (query) dtFreeNavMeshQuery(query);
        if (mesh) dtFreeNavMesh(mesh);
    }

    [[nodiscard]] float tile_size() const noexcept { return static_cast<float>(options.tile) * options.cell; }
    [[nodiscard]] WalkerRecord* record(WalkerId id) noexcept {
        const auto found = std::ranges::find(walkers, id, &WalkerRecord::id);
        return found == walkers.end() ? nullptr : &*found;
    }
    [[nodiscard]] dtCrowdAgent* agent(const WalkerRecord& walker) noexcept { return crowd->getEditableAgent(walker.index); }
    [[nodiscard]] const float* reach() const noexcept { return crowd->getQueryHalfExtents(); }

    // Recast's pipeline for the tile in slot (x, z): the bytes Detour is given, empty when it has no polygon.
    std::expected<std::vector<std::uint8_t>, Error> build_tile(int x, int z) const;
    std::expected<void, Error> make_grid();
    std::expected<void, Error> rebuild(int x, int z);
    std::expected<void, Error> rebuild_over(float min_x, float min_z, float max_x, float max_z);
    std::expected<void, Error> link_all();
    std::expected<void, Error> make_crowd();
    void plan(WalkerRecord& walker);
    void before_step();
    void after_step(const std::vector<Vec>& before);
    void write(Writer& out, bool with_ground) const;
};

std::expected<std::vector<std::uint8_t>, Error> World::Native::build_tile(int x, int z) const {
    const float size = tile_size();
    rcConfig config{};
    config.cs = options.cell;
    config.ch = options.cell_height;
    config.walkableSlopeAngle = walker.slope * 180.0f / recast::RC_PI;
    config.walkableHeight = static_cast<int>(std::ceil(walker.height / config.ch));
    config.walkableClimb = static_cast<int>(std::floor(walker.climb / config.ch));
    config.walkableRadius = static_cast<int>(std::ceil(walker.radius / config.cs));
    config.maxEdgeLen = static_cast<int>(12.0f / config.cs);
    config.maxSimplificationError = 1.3f;
    config.minRegionArea = 8 * 8;
    config.mergeRegionArea = 20 * 20;
    config.maxVertsPerPoly = recast::DT_VERTS_PER_POLYGON;
    config.tileSize = options.tile;
    config.borderSize = config.walkableRadius + 3;
    config.width = config.tileSize + config.borderSize * 2;
    config.height = config.width;
    config.detailSampleDist = config.cs * 6;
    config.detailSampleMaxError = config.ch;
    // The tile's own bounds, and the border around it the build sees too (so that edges meet their neighbours').
    const float tile_low[3] = {low[0] + static_cast<float>(x) * size, low[1], low[2] + static_cast<float>(z) * size};
    const float tile_high[3] = {tile_low[0] + size, high[1], tile_low[2] + size};
    std::copy_n(tile_low, 3, config.bmin);
    std::copy_n(tile_high, 3, config.bmax);
    const float border = static_cast<float>(config.borderSize) * config.cs;
    config.bmin[0] -= border, config.bmin[2] -= border, config.bmax[0] += border, config.bmax[2] += border;

    // The triangles over the tile and its border.
    std::vector<int> corners;
    for (std::size_t triangle = 0; triangle + 2 < ground.corners.size(); triangle += 3) {
        float min_x = std::numeric_limits<float>::max(), min_z = min_x, max_x = -min_x, max_z = -min_x;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const Vec& point = ground.points[ground.corners[triangle + corner]];
            min_x = std::min(min_x, point.x), max_x = std::max(max_x, point.x), min_z = std::min(min_z, point.z), max_z = std::max(max_z, point.z);
        }
        if (max_x < config.bmin[0] || min_x > config.bmax[0] || max_z < config.bmin[2] || min_z > config.bmax[2]) continue;
        for (std::size_t corner = 0; corner < 3; ++corner) corners.push_back(static_cast<int>(ground.corners[triangle + corner]));
    }
    if (corners.empty()) return std::vector<std::uint8_t>{};
    const float* points = &ground.points.front().x;
    const int point_count = static_cast<int>(ground.points.size()), triangle_count = static_cast<int>(corners.size() / 3);

    rcContext context(false);
    struct Owned {
        rcHeightfield* solid = rcAllocHeightfield();
        rcCompactHeightfield* compact = rcAllocCompactHeightfield();
        rcContourSet* contours = rcAllocContourSet();
        rcPolyMesh* polygons = rcAllocPolyMesh();
        rcPolyMeshDetail* detail = rcAllocPolyMeshDetail();
        ~Owned() { rcFreeHeightField(solid), rcFreeCompactHeightfield(compact), rcFreeContourSet(contours), rcFreePolyMesh(polygons), rcFreePolyMeshDetail(detail); }
    } owned;
    const auto failed = [](std::string_view stage) { return std::unexpected(Error{Errc::limit, std::format("Recast could not {}", stage)}); };
    if (!rcCreateHeightfield(&context, *owned.solid, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch)) return failed("make a heightfield");
    std::vector<unsigned char> areas(static_cast<std::size_t>(triangle_count), 0);
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, points, point_count, corners.data(), triangle_count, areas.data());
    if (!rcRasterizeTriangles(&context, points, point_count, corners.data(), areas.data(), triangle_count, *owned.solid, config.walkableClimb)) return failed("rasterise");
    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *owned.solid);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *owned.solid);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *owned.solid);
    if (!rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *owned.solid, *owned.compact)) return failed("compact the heightfield");
    // The obstacles over it, as no ground — before the erosion, so that a walker keeps its radius from them.
    for (const auto& [id, obstacle] : obstacles) {
        if (obstacle.form == Obstacle::Form::cylinder) {
            rcMarkCylinderArea(&context, raw(obstacle.at), obstacle.radius, obstacle.height, recast::RC_NULL_AREA, *owned.compact);
        } else {
            const Vec min = obstacle.at - obstacle.half, max = obstacle.at + obstacle.half;
            rcMarkBoxArea(&context, raw(min), raw(max), recast::RC_NULL_AREA, *owned.compact);
        }
    }
    if (!rcErodeWalkableArea(&context, config.walkableRadius, *owned.compact)) return failed("erode by the walker's radius");
    if (!rcBuildDistanceField(&context, *owned.compact)) return failed("build the distance field");
    if (!rcBuildRegions(&context, *owned.compact, config.borderSize, config.minRegionArea, config.mergeRegionArea)) return failed("build regions");
    if (!rcBuildContours(&context, *owned.compact, config.maxSimplificationError, config.maxEdgeLen, *owned.contours)) return failed("build contours");
    if (!rcBuildPolyMesh(&context, *owned.contours, config.maxVertsPerPoly, *owned.polygons)) return failed("build polygons");
    if (!rcBuildPolyMeshDetail(&context, *owned.polygons, *owned.compact, config.detailSampleDist, config.detailSampleMaxError, *owned.detail))
        return failed("build the detail mesh");
    rcPolyMesh& mesh = *owned.polygons;
    if (mesh.npolys == 0) return std::vector<std::uint8_t>{};
    for (int polygon = 0; polygon < mesh.npolys; ++polygon) mesh.flags[polygon] = mesh.areas[polygon] == recast::RC_WALKABLE_AREA ? walkable_flag : 0;

    // The links that start in this tile (its own bounds, not its border).
    std::vector<float> link_points, link_radii;
    std::vector<unsigned short> link_flags;
    std::vector<unsigned char> link_areas, link_ways;
    std::vector<unsigned int> link_ids;
    for (const auto& [id, link] : links) {
        if (link.from.x < tile_low[0] || link.from.x >= tile_high[0] || link.from.z < tile_low[2] || link.from.z >= tile_high[2]) continue;
        link_points.insert(link_points.end(), {link.from.x, link.from.y, link.from.z, link.to.x, link.to.y, link.to.z});
        link_radii.push_back(link.radius);
        link_flags.push_back(walkable_flag);
        link_areas.push_back(recast::RC_WALKABLE_AREA);
        link_ways.push_back(link.both_ways ? static_cast<unsigned char>(recast::DT_OFFMESH_CON_BIDIR) : 0);
        link_ids.push_back(id.value);
    }

    dtNavMeshCreateParams params{};
    params.verts = mesh.verts;
    params.vertCount = mesh.nverts;
    params.polys = mesh.polys;
    params.polyAreas = mesh.areas;
    params.polyFlags = mesh.flags;
    params.polyCount = mesh.npolys;
    params.nvp = mesh.nvp;
    params.detailMeshes = owned.detail->meshes;
    params.detailVerts = owned.detail->verts;
    params.detailVertsCount = owned.detail->nverts;
    params.detailTris = owned.detail->tris;
    params.detailTriCount = owned.detail->ntris;
    params.offMeshConVerts = link_points.data();
    params.offMeshConRad = link_radii.data();
    params.offMeshConFlags = link_flags.data();
    params.offMeshConAreas = link_areas.data();
    params.offMeshConDir = link_ways.data();
    params.offMeshConUserID = link_ids.data();
    params.offMeshConCount = static_cast<int>(link_ids.size());
    params.walkableHeight = walker.height;
    params.walkableRadius = walker.radius;
    params.walkableClimb = walker.climb;
    params.tileX = x;
    params.tileY = z;
    params.tileLayer = 0;
    std::copy_n(mesh.bmin, 3, params.bmin);
    std::copy_n(mesh.bmax, 3, params.bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;
    unsigned char* data = nullptr;
    int data_size = 0;
    if (!dtCreateNavMeshData(&params, &data, &data_size)) return failed("make Detour's tile (too many vertices for a tile?)");
    std::vector<std::uint8_t> out(data, data + data_size);
    dtFree(data);
    return out;
}

std::expected<void, Error> World::Native::make_grid() {
    if (ground.points.empty() || ground.corners.size() < 3 || ground.corners.size() % 3 != 0)
        return std::unexpected(Error{Errc::ground, "the ground is triangles: points, and three corners a triangle"});
    for (const std::uint32_t corner : ground.corners)
        if (corner >= ground.points.size()) return std::unexpected(Error{Errc::ground, std::format("corner {} names no point ({} points)", corner, ground.points.size())});
    for (const Vec& point : ground.points)
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return std::unexpected(Error{Errc::ground, "a point is not finite"});
    if (!(walker.radius > 0) || !(walker.height > 0) || !(walker.climb >= 0) || !(walker.slope > 0 && walker.slope < 1.5708f))
        return std::unexpected(Error{Errc::walker, "a walker's radius and height are above 0, its climb not below 0, its slope 0 … 90°"});
    if (!(options.cell > 0) || !(options.cell_height > 0) || options.tile < 8 || options.tile > 1024 || !(options.step > 0) || options.most_walkers < 1)
        return std::unexpected(Error{Errc::ground, "cells above 0, a tile 8 … 1024 cells, a step above 0, at least one walker"});
    rcCalcBounds(&ground.points.front().x, static_cast<int>(ground.points.size()), low, high);
    const float size = tile_size();
    across = std::max(1, static_cast<int>(std::ceil((high[0] - low[0]) / size)));
    along = std::max(1, static_cast<int>(std::ceil((high[2] - low[2]) / size)));
    if (across * along > (1 << 14)) return std::unexpected(Error{Errc::limit, std::format("{} × {} tiles: at most 16384 (larger tiles)", across, along)});
    tiles.assign(static_cast<std::size_t>(across * along), {});
    ground_digest = mlibs::hash::Fnv1a().bytes(std::span(reinterpret_cast<const std::uint8_t*>(ground.points.data()), ground.points.size() * sizeof(Vec)))
                        .bytes(std::span(reinterpret_cast<const std::uint8_t*>(ground.corners.data()), ground.corners.size() * sizeof(std::uint32_t)))
                        .value();
    mesh = dtAllocNavMesh();
    dtNavMeshParams params{};
    std::copy_n(low, 3, params.orig);
    params.tileWidth = size;
    params.tileHeight = size;
    const int tile_bits = log2_of(next_power(across * along));
    params.maxTiles = 1 << tile_bits;
    params.maxPolys = 1 << (22 - tile_bits);
    if (!dtStatusSucceed(mesh->init(&params))) return std::unexpected(Error{Errc::limit, "Detour could not make the mesh"});
    query = dtAllocNavMeshQuery();
    if (!dtStatusSucceed(query->init(mesh, 8192))) return std::unexpected(Error{Errc::limit, "Detour could not make a query"});
    filter.setIncludeFlags(walkable_flag);
    return {};
}

std::expected<void, Error> World::Native::rebuild(int x, int z) {
    auto bytes = build_tile(x, z);
    if (!bytes) return std::unexpected(bytes.error());
    Tile& tile = tiles[static_cast<std::size_t>(z * across + x)];
    if (*bytes == tile.bytes) return {};
    tile.bytes = std::move(*bytes);
    tile.salt = tile.salt + 1 >= (1u << 10) ? 1 : tile.salt + 1;  // 32-bit references: ten bits of salt
    return {};
}

std::expected<void, Error> World::Native::rebuild_over(float min_x, float min_z, float max_x, float max_z) {
    // A tile's build sees its border too: a change that far outside it reaches it.
    const float size = tile_size(), border = (std::ceil(walker.radius / options.cell) + 3) * options.cell;
    const int x0 = std::max(0, static_cast<int>(std::floor((min_x - border - low[0]) / size))), x1 = std::min(across - 1, static_cast<int>(std::floor((max_x + border - low[0]) / size)));
    const int z0 = std::max(0, static_cast<int>(std::floor((min_z - border - low[2]) / size))), z1 = std::min(along - 1, static_cast<int>(std::floor((max_z + border - low[2]) / size)));
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            if (auto built = rebuild(x, z); !built) return built;
    return link_all();
}

std::expected<void, Error> World::Native::link_all() {
    for (int slot = 0; slot < mesh->getMaxTiles(); ++slot) {
        const dtMeshTile* tile = static_cast<const dtNavMesh*>(mesh)->getTile(slot);
        if (tile && tile->header) (void)mesh->removeTile(mesh->getTileRef(tile), nullptr, nullptr);
    }
    for (std::size_t slot = 0; slot < tiles.size(); ++slot) {
        const Tile& tile = tiles[slot];
        if (tile.bytes.empty()) continue;
        auto* data = static_cast<unsigned char*>(dtAlloc(static_cast<int>(tile.bytes.size()), DT_ALLOC_PERM));
        std::ranges::copy(tile.bytes, data);
        const dtTileRef reference = mesh->encodePolyId(tile.salt, static_cast<unsigned int>(slot), 0);
        if (!dtStatusSucceed(mesh->addTile(data, static_cast<int>(tile.bytes.size()), DT_TILE_FREE_DATA, reference, nullptr))) {
            dtFree(data);
            return std::unexpected(Error{Errc::state, std::format("Detour refused tile {}", slot)});
        }
    }
    return {};
}

std::expected<void, Error> World::Native::make_crowd() {
    if (crowd) dtFreeCrowd(crowd);
    crowd = dtAllocCrowd();
    if (!crowd->init(options.most_walkers, walker.radius * 4, mesh)) return std::unexpected(Error{Errc::limit, "Detour could not make the crowd"});
    crowd->getEditableFilter(0)->setIncludeFlags(walkable_flag);
    return {};
}

// A walker's path, planned whole from where it is to its target (or as near as it can get), as dtCrowd plans one
// when it is asked for: the corridor set, the target held within the last polygon when it does not reach.
void World::Native::plan(WalkerRecord& walker) {
    dtCrowdAgent& agent = *this->agent(walker);
    std::array<dtPolyRef, most_path> polygons{};
    int count = 0;
    const dtStatus status = query->findPath(agent.corridor.getFirstPoly(), agent.targetRef, agent.npos, agent.targetPos, &filter, polygons.data(), &count, most_path);
    float aim[3];
    dtVcopy(aim, agent.targetPos);
    if (dtStatusFailed(status) || count == 0) {
        polygons[0] = agent.corridor.getFirstPoly(), count = 1;
        dtVcopy(aim, agent.npos);
    } else if (polygons[static_cast<std::size_t>(count - 1)] != agent.targetRef) {
        (void)query->closestPointOnPoly(polygons[static_cast<std::size_t>(count - 1)], agent.targetPos, aim, nullptr);
    }
    agent.corridor.setCorridor(aim, polygons.data(), count);
    agent.boundary.reset();
    agent.partial = polygons[static_cast<std::size_t>(count - 1)] != agent.targetRef;
    agent.targetState = DT_CROWDAGENT_TARGET_VALID;
    agent.targetReplan = false;
    agent.targetReplanTime = 0;
}

// dtCrowd's checks of a path (DetourCrowd.cpp: checkPathValidity), made before its step: where it would ask its
// queue to plan again, the path is planned here instead, whole; it then finds nothing to ask.
void World::Native::before_step() {
    const float dt = options.step;
    const dtNavMeshQuery& crowd_query = *crowd->getNavMeshQuery();
    for (WalkerRecord& walker : walkers) {
        dtCrowdAgent& agent = *this->agent(walker);
        if (agent.state != DT_CROWDAGENT_STATE_WALKING) continue;
        bool replan = false;
        dtPolyRef here = agent.corridor.getFirstPoly();
        if (!crowd_query.isValidPolyRef(here, &filter)) {
            float nearest[3];
            dtVcopy(nearest, agent.npos);
            here = 0;
            (void)query->findNearestPoly(agent.npos, reach(), &filter, &here, nearest);
            if (!here) {
                agent.corridor.reset(0, nearest);
                agent.partial = false;
                agent.state = DT_CROWDAGENT_STATE_INVALID;
                continue;
            }
            agent.corridor.fixPathStart(here, nearest);
            dtVcopy(agent.npos, nearest);
            replan = true;
        }
        if (agent.targetState == DT_CROWDAGENT_TARGET_NONE || agent.targetState == DT_CROWDAGENT_TARGET_VELOCITY) continue;
        if (!crowd_query.isValidPolyRef(agent.targetRef, &filter)) {
            float nearest[3];
            dtVcopy(nearest, agent.targetPos);
            agent.targetRef = 0;
            (void)query->findNearestPoly(agent.targetPos, reach(), &filter, &agent.targetRef, nearest);
            dtVcopy(agent.targetPos, nearest);
            replan = true;
        }
        if (!agent.targetRef) {
            agent.corridor.reset(here, agent.npos);
            agent.partial = false;
            agent.targetState = DT_CROWDAGENT_TARGET_NONE;
            continue;
        }
        if (!agent.corridor.isValid(check_lookahead, const_cast<dtNavMeshQuery*>(&crowd_query), &filter)) replan = true;
        if (agent.targetReplanTime + dt > replan_delay && agent.corridor.getPathCount() < check_lookahead && agent.corridor.getLastPoly() != agent.targetRef)
            replan = true;
        if (replan) plan(walker);
    }
    // Whether dtCrowd finds a walker's local boundary again this step (its test, DetourCrowd.cpp: update), and from
    // which polygon: an image keeps the boundary as that polygon and its centre, and finds it again from them.
    for (WalkerRecord& walker : walkers) {
        dtCrowdAgent& agent = *this->agent(walker);
        if (agent.state != DT_CROWDAGENT_STATE_WALKING) continue;
        const float threshold = agent.params.collisionQueryRange * 0.25f;
        if (dtVdist2DSqr(agent.npos, agent.boundary.getCenter()) > dtSqr(threshold) || !agent.boundary.isValid(const_cast<dtNavMeshQuery*>(&crowd_query), &filter))
            walker.boundary_from = agent.corridor.getFirstPoly();
    }
}

// After dtCrowd's step: a walker that stepped onto a link has its crossing recorded as dtCrowd made it; one this
// package carries (restored or branched half across) is carried on, with dtCrowd's arithmetic.
void World::Native::after_step(const std::vector<Vec>& before) {
    const float dt = options.step;
    for (std::size_t index = 0; index < walkers.size(); ++index) {
        WalkerRecord& walker = walkers[index];
        dtCrowdAgent& agent = *this->agent(walker);
        Crossing& crossing = walker.crossing;
        if (crossing.active && crossing.carried_here) {
            crossing.time += dt;
            if (crossing.time > crossing.length) {
                crossing.active = false;
                agent.state = DT_CROWDAGENT_STATE_WALKING;
                continue;
            }
            const float start = crossing.length * 0.15f, end = crossing.length;
            if (crossing.time < start)
                dtVlerp(agent.npos, raw(crossing.from_place), raw(crossing.start), tween(crossing.time, 0.0f, start));
            else
                dtVlerp(agent.npos, raw(crossing.start), raw(crossing.end), tween(crossing.time, start, end));
            dtVset(agent.vel, 0, 0, 0);
            dtVset(agent.dvel, 0, 0, 0);
            continue;
        }
        if (crossing.active) {  // dtCrowd carries it: the record follows
            crossing.time += dt;
            if (agent.state != DT_CROWDAGENT_STATE_OFFMESH) crossing.active = false;
            continue;
        }
        if (agent.state != DT_CROWDAGENT_STATE_OFFMESH) continue;
        // Onto a link this step: its far end is where the corridor now stands; its near end the link's other
        // point, as Detour placed both (the nearest such link to where the walker was).
        const Vec end = vec(agent.corridor.getPos());
        std::optional<Vec> start;
        float closest = std::numeric_limits<float>::max();
        for (int slot = 0; slot < mesh->getMaxTiles(); ++slot) {
            const dtMeshTile* tile = static_cast<const dtNavMesh*>(mesh)->getTile(slot);
            if (!tile || !tile->header) continue;
            for (int polygon = 0; polygon < tile->header->polyCount; ++polygon) {
                const dtPoly& poly = tile->polys[polygon];
                if (poly.getType() != DT_POLYTYPE_OFFMESH_CONNECTION) continue;
                const Vec ends[2] = {vec(&tile->verts[poly.verts[0] * 3]), vec(&tile->verts[poly.verts[1] * 3])};
                for (int side = 0; side < 2; ++side) {
                    if (!(ends[side] == end)) continue;
                    const float distance = dtVdist2DSqr(raw(ends[1 - side]), raw(before[index]));
                    if (distance < closest) closest = distance, start = ends[1 - side];
                }
            }
        }
        if (!start) continue;
        crossing = {.active = true, .carried_here = false, .from_place = before[index], .start = *start, .end = end, .time = 0.0f + dt,
                    .length = (dtVdist2D(raw(*start), raw(end)) / agent.params.maxSpeed) * 0.5f};
    }
}

void World::Native::write(Writer& out, bool with_ground) const {
    if (with_ground) {
        out.put(ground.points.size(), 8);
        for (const Vec& point : ground.points) out.v3(point);
        out.put(ground.corners.size(), 8);
        for (const std::uint32_t corner : ground.corners) out.u32(corner);
    } else {
        out.put(ground_digest, 8);
    }
    out.f32(walker.radius), out.f32(walker.height), out.f32(walker.climb), out.f32(walker.slope);
    out.f32(options.cell), out.f32(options.cell_height), out.u32(static_cast<std::uint32_t>(options.tile)), out.f32(options.step), out.u32(static_cast<std::uint32_t>(options.most_walkers));
    out.u32(next_link), out.u32(next_obstacle), out.u32(next_walker);
    out.u32(static_cast<std::uint32_t>(links.size()));
    for (const auto& [id, link] : links) out.u32(id.value), out.v3(link.from), out.v3(link.to), out.f32(link.radius), out.u8(link.both_ways);
    out.u32(static_cast<std::uint32_t>(obstacles.size()));
    for (const auto& [id, obstacle] : obstacles)
        out.u32(id.value), out.u8(static_cast<std::uint8_t>(obstacle.form)), out.v3(obstacle.at), out.f32(obstacle.radius), out.f32(obstacle.height), out.v3(obstacle.half);
    out.u32(static_cast<std::uint32_t>(tiles.size()));
    for (const Tile& tile : tiles) {
        out.u32(tile.salt);
        if (with_ground) out.block(tile.bytes);
        else out.put(digest(tile.bytes), 8);
    }
    out.u32(static_cast<std::uint32_t>(walkers.size()));
    for (const WalkerRecord& walker : walkers) {
        const dtCrowdAgent& agent = *crowd->getAgent(walker.index);
        out.u32(walker.id.value), out.u32(static_cast<std::uint32_t>(walker.index));
        out.v3(walker.def.at), out.f32(walker.def.radius), out.f32(walker.def.height), out.f32(walker.def.speed), out.f32(walker.def.acceleration), out.f32(walker.def.separation);
        out.u8(agent.state), out.u8(agent.partial), out.f32(agent.topologyOptTime), out.f32(agent.desiredSpeed);
        out.v3(agent.npos), out.v3(agent.disp), out.v3(agent.dvel), out.v3(agent.nvel), out.v3(agent.vel);
        out.u8(agent.targetState), out.u32(agent.targetRef), out.v3(agent.targetPos), out.u8(agent.targetReplan), out.f32(agent.targetReplanTime);
        out.v3(agent.corridor.getPos()), out.v3(agent.corridor.getTarget());
        out.u32(static_cast<std::uint32_t>(agent.corridor.getPathCount()));
        for (int polygon = 0; polygon < agent.corridor.getPathCount(); ++polygon) out.u32(agent.corridor.getPath()[polygon]);
        out.v3(agent.boundary.getCenter()), out.u32(walker.boundary_from);
        const Crossing& crossing = walker.crossing;
        out.u8(crossing.active);
        if (crossing.active) out.v3(crossing.from_place), out.v3(crossing.start), out.v3(crossing.end), out.f32(crossing.time), out.f32(crossing.length);
    }
}

World::World() : native_(std::make_unique<Native>()) {}
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;
World::~World() = default;

std::expected<World, Error> World::build(const Ground& ground, const Walker& walker, const Options& options, std::span<const Link> links) {
    World world;
    Native& native = *world.native_;
    native.ground = ground;
    native.walker = walker;
    native.options = options;
    if (auto made = native.make_grid(); !made) return std::unexpected(made.error());
    for (const Link& link : links) {
        const LinkId id{native.next_link++};
        native.links.emplace(id, link);
        world.links_.push_back(id);
    }
    for (int z = 0; z < native.along; ++z)
        for (int x = 0; x < native.across; ++x)
            if (auto built = native.rebuild(x, z); !built) return std::unexpected(built.error());
    if (auto linked = native.link_all(); !linked) return std::unexpected(linked.error());
    if (auto made = native.make_crowd(); !made) return std::unexpected(made.error());
    return world;
}

std::expected<void, Error> World::rebuild(const Ground& ground, const Walker& walker) {
    auto built = build(ground, walker, native_->options);
    if (!built) return std::unexpected(built.error());
    built->steps_ = steps_;
    *this = std::move(*built);
    return {};
}

std::optional<Place> World::nearest(Vec at, Vec reach) const {
    dtPolyRef polygon = 0;
    float found[3];
    if (dtStatusFailed(native_->query->findNearestPoly(raw(at), raw(reach), &native_->filter, &polygon, found)) || !polygon) return std::nullopt;
    return Place{vec(found), polygon};
}

std::expected<Path, Error> World::path(Vec from, Vec to) const {
    const auto start = nearest(from), end = nearest(to);
    if (!start) return std::unexpected(Error{Errc::place, std::format("[{}, {}, {}] is not near the mesh", from.x, from.y, from.z)});
    if (!end) return std::unexpected(Error{Errc::place, std::format("[{}, {}, {}] is not near the mesh", to.x, to.y, to.z)});
    std::array<dtPolyRef, most_path> polygons{};
    int count = 0;
    if (dtStatusFailed(native_->query->findPath(start->polygon, end->polygon, raw(start->at), raw(end->at), &native_->filter, polygons.data(), &count, most_path)) || count == 0)
        return std::unexpected(Error{Errc::place, "Detour found no path"});
    float aim[3];
    dtVcopy(aim, raw(end->at));
    Path out;
    out.reaches = polygons[static_cast<std::size_t>(count - 1)] == end->polygon;
    if (!out.reaches) (void)native_->query->closestPointOnPoly(polygons[static_cast<std::size_t>(count - 1)], raw(end->at), aim, nullptr);
    std::array<float, 3 * most_path> corners{};
    std::array<unsigned char, most_path> flags{};
    int corner_count = 0;
    (void)native_->query->findStraightPath(raw(start->at), aim, polygons.data(), count, corners.data(), flags.data(), nullptr, &corner_count, most_path);
    for (int corner = 0; corner < corner_count; ++corner) {
        out.corners.push_back(vec(&corners[static_cast<std::size_t>(corner * 3)]));
        out.onto_link.push_back((flags[static_cast<std::size_t>(corner)] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0);
        if (corner > 0) out.length += dtVdist(&corners[static_cast<std::size_t>(corner * 3)], &corners[static_cast<std::size_t>(corner * 3 - 3)]);
    }
    out.polygons.assign(polygons.begin(), polygons.begin() + count);
    return out;
}

std::expected<RayHit, Error> World::ray(Vec from, Vec to) const {
    const auto start = nearest(from);
    if (!start) return std::unexpected(Error{Errc::place, std::format("[{}, {}, {}] is not near the mesh", from.x, from.y, from.z)});
    float fraction = 0, normal[3]{};
    std::array<dtPolyRef, most_path> polygons{};
    int count = 0;
    if (dtStatusFailed(native_->query->raycast(start->polygon, raw(start->at), raw(to), &native_->filter, &fraction, normal, polygons.data(), &count, most_path)))
        return std::unexpected(Error{Errc::place, "Detour could not walk the ray"});
    RayHit hit;
    hit.clear = fraction > 1;
    hit.fraction = hit.clear ? 1.0f : fraction;
    hit.at = start->at + (to - start->at) * hit.fraction;
    hit.normal = vec(normal);
    return hit;
}

int World::tiles() const noexcept {
    return static_cast<int>(std::ranges::count_if(native_->tiles, [](const Tile& tile) { return !tile.bytes.empty(); }));
}
int World::polygons() const noexcept {
    int count = 0;
    for (int slot = 0; slot < native_->mesh->getMaxTiles(); ++slot) {
        const dtMeshTile* tile = static_cast<const dtNavMesh*>(native_->mesh)->getTile(slot);
        if (tile && tile->header) count += tile->header->polyCount;
    }
    return count;
}
const Walker& World::walker_size() const noexcept { return native_->walker; }

std::expected<ObstacleId, Error> World::place(const Obstacle& obstacle) {
    const bool finite = std::isfinite(obstacle.at.x) && std::isfinite(obstacle.at.y) && std::isfinite(obstacle.at.z);
    if (!finite || (obstacle.form == Obstacle::Form::cylinder && (!(obstacle.radius > 0) || !(obstacle.height > 0))) ||
        (obstacle.form == Obstacle::Form::box && (!(obstacle.half.x > 0) || !(obstacle.half.y > 0) || !(obstacle.half.z > 0))))
        return std::unexpected(Error{Errc::place, "an obstacle is at a finite place, its sizes above 0"});
    const ObstacleId id{native_->next_obstacle++};
    native_->obstacles.emplace(id, obstacle);
    obstacles_.push_back(id);
    const Vec reach = obstacle.form == Obstacle::Form::cylinder ? Vec{obstacle.radius, 0, obstacle.radius} : obstacle.half;
    if (auto built = native_->rebuild_over(obstacle.at.x - reach.x, obstacle.at.z - reach.z, obstacle.at.x + reach.x, obstacle.at.z + reach.z); !built)
        return std::unexpected(built.error());
    return id;
}

void World::take(ObstacleId id) {
    const auto found = native_->obstacles.find(id);
    if (found == native_->obstacles.end()) return;
    const Obstacle obstacle = found->second;
    native_->obstacles.erase(found);
    std::erase(obstacles_, id);
    const Vec reach = obstacle.form == Obstacle::Form::cylinder ? Vec{obstacle.radius, 0, obstacle.radius} : obstacle.half;
    (void)native_->rebuild_over(obstacle.at.x - reach.x, obstacle.at.z - reach.z, obstacle.at.x + reach.x, obstacle.at.z + reach.z);
}

std::span<const ObstacleId> World::obstacles() const noexcept { return obstacles_; }
std::optional<Obstacle> World::obstacle(ObstacleId id) const noexcept {
    const auto found = native_->obstacles.find(id);
    return found == native_->obstacles.end() ? std::nullopt : std::optional(found->second);
}

std::expected<LinkId, Error> World::join(const Link& link) {
    const auto finite = [](Vec value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };
    if (!finite(link.from) || !finite(link.to) || !(link.radius > 0)) return std::unexpected(Error{Errc::place, "a link's ends are finite places, its radius above 0"});
    if (link.from.x < native_->low[0] || link.from.z < native_->low[2] || link.from.x >= native_->high[0] || link.from.z >= native_->high[2])
        return std::unexpected(Error{Errc::place, "a link starts over the ground"});
    const LinkId id{native_->next_link++};
    native_->links.emplace(id, link);
    links_.push_back(id);
    if (auto built = native_->rebuild_over(link.from.x, link.from.z, link.from.x, link.from.z); !built) return std::unexpected(built.error());
    return id;
}

void World::unjoin(LinkId id) {
    const auto found = native_->links.find(id);
    if (found == native_->links.end()) return;
    const Link link = found->second;
    native_->links.erase(found);
    std::erase(links_, id);
    (void)native_->rebuild_over(link.from.x, link.from.z, link.from.x, link.from.z);
}

std::span<const LinkId> World::links() const noexcept { return links_; }
std::optional<Link> World::link(LinkId id) const noexcept {
    const auto found = native_->links.find(id);
    return found == native_->links.end() ? std::nullopt : std::optional(found->second);
}

namespace {
dtCrowdAgentParams params_of(const WalkerDef& walker) {
    dtCrowdAgentParams params{};
    params.radius = walker.radius;
    params.height = walker.height;
    params.maxAcceleration = walker.acceleration;
    params.maxSpeed = walker.speed;
    params.collisionQueryRange = walker.radius * 12;
    params.pathOptimizationRange = walker.radius * 30;
    params.separationWeight = walker.separation;
    params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
    params.obstacleAvoidanceType = 0;
    params.queryFilterType = 0;
    return params;
}
}  // namespace

std::expected<WalkerId, Error> World::add(const WalkerDef& walker) {
    const bool finite = std::isfinite(walker.at.x) && std::isfinite(walker.at.y) && std::isfinite(walker.at.z);
    if (!finite || !(walker.radius > 0) || walker.radius > native_->walker.radius * 4 || !(walker.height > 0) || !(walker.speed >= 0) || !(walker.acceleration >= 0) ||
        !(walker.separation >= 0))
        return std::unexpected(Error{Errc::walker, std::format("a walker is at a finite place; its radius above 0 and at most {} m; its height above 0; its speed, "
                                                               "acceleration and separation not below 0",
                                                               native_->walker.radius * 4)});
    const dtCrowdAgentParams params = params_of(walker);
    const int index = native_->crowd->addAgent(raw(walker.at), &params);
    if (index < 0) return std::unexpected(Error{Errc::limit, std::format("the crowd is full ({} walkers)", native_->options.most_walkers)});
    const WalkerId id{native_->next_walker++};
    native_->walkers.push_back({id, index, walker, {}});
    walkers_.push_back(id);
    return id;
}

void World::remove(WalkerId id) {
    WalkerRecord* walker = native_->record(id);
    if (!walker) return;
    native_->crowd->removeAgent(walker->index);
    std::erase_if(native_->walkers, [&](const WalkerRecord& other) { return other.id == id; });
    std::erase(walkers_, id);
}

std::expected<void, Error> World::target(WalkerId id, Vec to) {
    WalkerRecord* walker = native_->record(id);
    if (!walker) return std::unexpected(Error{Errc::walker, std::format("no walker {}", id.value)});
    dtCrowdAgent& agent = *native_->agent(*walker);
    if (agent.state == DT_CROWDAGENT_STATE_INVALID) return std::unexpected(Error{Errc::walker, std::format("walker {} is not on the mesh", id.value)});
    dtPolyRef polygon = 0;
    float found[3];
    (void)native_->query->findNearestPoly(raw(to), native_->reach(), &native_->filter, &polygon, found);
    if (!polygon) return std::unexpected(Error{Errc::place, std::format("[{}, {}, {}] is not near the mesh", to.x, to.y, to.z)});
    agent.targetRef = polygon;
    dtVcopy(agent.targetPos, found);
    if (agent.state == DT_CROWDAGENT_STATE_WALKING) native_->plan(*walker);
    else agent.targetState = DT_CROWDAGENT_TARGET_VALID, agent.targetReplanTime = 0;  // across a link: planned when it lands
    return {};
}

void World::stop(WalkerId id) {
    if (WalkerRecord* walker = native_->record(id)) (void)native_->crowd->resetMoveTarget(walker->index);
}

std::optional<WalkerState> World::walker(WalkerId id) const {
    WalkerRecord* walker = native_->record(id);
    if (!walker) return std::nullopt;
    const dtCrowdAgent& agent = *native_->crowd->getAgent(walker->index);
    WalkerState state{id, vec(agent.npos), vec(agent.vel)};
    state.def = walker->def;
    const bool targeted = agent.targetState == DT_CROWDAGENT_TARGET_VALID;
    if (targeted) state.target = vec(agent.targetPos);
    if (agent.state == DT_CROWDAGENT_STATE_INVALID) state.going = Going::off_mesh;
    else if (agent.state == DT_CROWDAGENT_STATE_OFFMESH) state.going = Going::crossing;
    else if (!targeted) state.going = Going::idle;
    else if (agent.partial) state.going = Going::unreachable;
    else if (dtVdist2D(agent.npos, agent.targetPos) <= agent.params.radius) state.going = Going::arrived;
    else state.going = Going::walking;
    for (int corner = 0; corner < agent.ncorners; ++corner) state.ahead.push_back(vec(&agent.cornerVerts[corner * 3]));
    return state;
}

std::span<const WalkerId> World::walkers() const noexcept { return walkers_; }

void World::step() {
    Native& native = *native_;
    std::vector<Vec> before;
    before.reserve(native.walkers.size());
    for (const WalkerRecord& walker : native.walkers) before.push_back(vec(native.crowd->getAgent(walker.index)->npos));
    native.before_step();
    native.crowd->update(native.options.step, nullptr);
    native.after_step(before);
    ++steps_;
}

std::vector<std::uint8_t> World::save() const {
    Writer out;
    out.bytes.assign(magic.begin(), magic.end());
    out.put(steps_, 8);
    out.put(0, 8);  // the checksum of the rest, below
    native_->write(out, true);
    const std::uint64_t sum = digest(std::span(out.bytes).subspan(magic.size() + 16));
    for (int index = 0; index < 8; ++index) out.bytes[magic.size() + 8 + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(sum >> (8 * index));
    return out.bytes;
}

std::uint64_t World::state_hash() const {
    Writer out;
    out.put(steps_, 8);
    native_->write(out, false);
    return digest(out.bytes);
}

std::expected<World, Error> World::from(std::span<const std::uint8_t> image) {
    const auto refused = [](std::string_view why) { return std::unexpected(Error{Errc::state, std::string(why)}); };
    if (image.size() < magic.size() + 16 || !std::ranges::equal(image.first(magic.size()), std::span(reinterpret_cast<const std::uint8_t*>(magic.data()), magic.size())))
        return refused("not an image of a navigation world (or another version's)");
    Reader in{image, magic.size()};
    const std::uint64_t steps = in.take(8), sum = in.take(8);
    if (digest(image.subspan(magic.size() + 16)) != sum) return refused("the image is damaged or cut short");
    World world;
    Native& native = *world.native_;
    const std::uint64_t point_count = in.take(8);
    if (point_count > image.size()) return refused("the image is damaged");
    native.ground.points.resize(point_count);
    for (Vec& point : native.ground.points) point = in.v3();
    const std::uint64_t corner_count = in.take(8);
    if (corner_count > image.size()) return refused("the image is damaged");
    native.ground.corners.resize(corner_count);
    for (std::uint32_t& corner : native.ground.corners) corner = in.u32();
    native.walker = {in.f32(), in.f32(), in.f32(), in.f32()};
    native.options.cell = in.f32(), native.options.cell_height = in.f32(), native.options.tile = static_cast<int>(in.u32()), native.options.step = in.f32();
    native.options.most_walkers = static_cast<int>(in.u32());
    native.next_link = in.u32(), native.next_obstacle = in.u32(), native.next_walker = in.u32();
    if (!in.good) return refused("the image is cut short");
    if (auto made = native.make_grid(); !made) return std::unexpected(made.error());
    for (std::uint32_t count = in.u32(), index = 0; in.good && index < count; ++index) {
        const LinkId id{in.u32()};
        Link link{.from = in.v3(), .to = in.v3()};
        link.radius = in.f32(), link.both_ways = in.u8() != 0;
        native.links.emplace(id, link), world.links_.push_back(id);
    }
    for (std::uint32_t count = in.u32(), index = 0; in.good && index < count; ++index) {
        const ObstacleId id{in.u32()};
        Obstacle obstacle{.form = static_cast<Obstacle::Form>(in.u8())};
        obstacle.at = in.v3(), obstacle.radius = in.f32(), obstacle.height = in.f32(), obstacle.half = in.v3();
        native.obstacles.emplace(id, obstacle), world.obstacles_.push_back(id);
    }
    if (in.u32() != native.tiles.size()) return refused("the image's tiles are not its grid's");
    for (Tile& tile : native.tiles) {
        tile.salt = in.u32();
        const auto bytes = in.block();
        tile.bytes.assign(bytes.begin(), bytes.end());
    }
    if (!in.good) return refused("the image is cut short");
    if (auto linked = native.link_all(); !linked) return std::unexpected(linked.error());
    if (auto made = native.make_crowd(); !made) return std::unexpected(made.error());
    // The walkers, each at its index of dtCrowd's: the places between them held while they are added.
    const std::uint32_t walker_count = in.u32();
    // dtCrowdAgent's fields an image carries (the agent itself owns its corridor: not a value to hold here).
    struct Fields {
        unsigned char state = 0;
        bool partial = false;
        float topologyOptTime = 0, desiredSpeed = 0;
        float npos[3]{}, disp[3]{}, dvel[3]{}, nvel[3]{}, vel[3]{};
        unsigned char targetState = 0;
        dtPolyRef targetRef = 0;
        float targetPos[3]{};
        bool targetReplan = false;
        float targetReplanTime = 0;
    };
    struct Saved {
        WalkerRecord record;
        Fields fields;
        float corridor_at[3], corridor_target[3], boundary_at[3];
        std::vector<dtPolyRef> path;
    };
    std::vector<Saved> saved(walker_count);
    for (Saved& walker : saved) {
        walker.record.id = WalkerId{in.u32()};
        walker.record.index = static_cast<int>(in.u32());
        WalkerDef& def = walker.record.def;
        def.at = in.v3(), def.radius = in.f32(), def.height = in.f32(), def.speed = in.f32(), def.acceleration = in.f32(), def.separation = in.f32();
        Fields& agent = walker.fields;
        agent.state = in.u8(), agent.partial = in.u8() != 0, agent.topologyOptTime = in.f32(), agent.desiredSpeed = in.f32();
        in.v3(agent.npos), in.v3(agent.disp), in.v3(agent.dvel), in.v3(agent.nvel), in.v3(agent.vel);
        agent.targetState = in.u8(), agent.targetRef = in.u32();
        in.v3(agent.targetPos);
        agent.targetReplan = in.u8() != 0, agent.targetReplanTime = in.f32();
        in.v3(walker.corridor_at), in.v3(walker.corridor_target);
        const std::uint32_t path_count = in.u32();
        if (path_count == 0 || path_count > most_path) return refused("a walker's corridor is not 1 … 256 polygons");
        walker.path.resize(path_count);
        for (dtPolyRef& polygon : walker.path) polygon = in.u32();
        in.v3(walker.boundary_at);
        walker.record.boundary_from = in.u32();
        Crossing& crossing = walker.record.crossing;
        crossing.active = in.u8() != 0;
        if (crossing.active) crossing.carried_here = true, crossing.from_place = in.v3(), crossing.start = in.v3(), crossing.end = in.v3(), crossing.time = in.f32(), crossing.length = in.f32();
        if (!in.good) return refused("the image is cut short");
        if (walker.record.index < 0 || walker.record.index >= native.options.most_walkers) return refused("a walker's place in the crowd is past its size");
    }
    if (!in.good || in.at != image.size()) return refused("the image is cut short or has more than a world");
    const int highest = saved.empty() ? -1 : std::ranges::max(saved, {}, [](const Saved& walker) { return walker.record.index; }).record.index;
    std::vector<int> holders;
    for (int index = 0; index <= highest; ++index) {
        const auto found = std::ranges::find(saved, index, [](const Saved& walker) { return walker.record.index; });
        const dtCrowdAgentParams params = found == saved.end() ? params_of({}) : params_of(found->record.def);
        const float* at = found == saved.end() ? native.low : found->fields.npos;
        if (native.crowd->addAgent(at, &params) != index) return refused("the crowd could not take the walkers back");
        if (found == saved.end()) holders.push_back(index);
    }
    for (const int index : holders) native.crowd->removeAgent(index);
    for (Saved& walker : saved) {
        dtCrowdAgent& agent = *native.crowd->getEditableAgent(walker.record.index);
        const Fields& fields = walker.fields;
        agent.state = fields.state, agent.partial = fields.partial, agent.topologyOptTime = fields.topologyOptTime, agent.desiredSpeed = fields.desiredSpeed;
        dtVcopy(agent.npos, fields.npos), dtVcopy(agent.disp, fields.disp), dtVcopy(agent.dvel, fields.dvel), dtVcopy(agent.nvel, fields.nvel), dtVcopy(agent.vel, fields.vel);
        agent.targetState = fields.targetState, agent.targetRef = fields.targetRef, agent.targetReplan = fields.targetReplan, agent.targetReplanTime = fields.targetReplanTime;
        dtVcopy(agent.targetPos, fields.targetPos);
        agent.targetPathqRef = recast::DT_PATHQ_INVALID;
        agent.corridor.reset(walker.path.front(), walker.corridor_at);
        agent.corridor.setCorridor(walker.corridor_target, walker.path.data(), static_cast<int>(walker.path.size()));
        agent.boundary.reset();
        if (walker.boundary_at[0] != std::numeric_limits<float>::max())
            agent.boundary.update(walker.record.boundary_from, walker.boundary_at, agent.params.collisionQueryRange, const_cast<dtNavMeshQuery*>(native.crowd->getNavMeshQuery()),
                                  native.crowd->getFilter(0));
        agent.ncorners = 0, agent.nneis = 0;
    }
    std::ranges::sort(saved, {}, [](const Saved& walker) { return walker.record.id; });
    for (Saved& walker : saved) native.walkers.push_back(walker.record), world.walkers_.push_back(walker.record.id);
    world.steps_ = steps;
    return world;
}

std::expected<void, Error> World::restore(std::span<const std::uint8_t> image) {
    auto restored = from(image);
    if (!restored) return std::unexpected(restored.error());
    *this = std::move(*restored);
    return {};
}

struct Direct::Native {
    dtNavMesh* mesh = nullptr;
    dtCrowd* crowd = nullptr;
    float step = 1.0f / 60.0f;
    std::vector<int> indices;
    Native() = default;
    Native(const Native&) = delete;
    ~Native() {
        if (crowd) dtFreeCrowd(crowd);
        if (mesh) dtFreeNavMesh(mesh);
    }
};

Direct::Direct(Direct&&) noexcept = default;
Direct& Direct::operator=(Direct&&) noexcept = default;
Direct::~Direct() = default;

std::expected<Direct, Error> Direct::of(const World& world) {
    const World::Native& source = *world.native_;
    Direct direct;
    direct.native_ = std::make_unique<Native>();
    Native& native = *direct.native_;
    native.step = source.options.step;
    native.mesh = dtAllocNavMesh();
    if (!dtStatusSucceed(native.mesh->init(source.mesh->getParams()))) return std::unexpected(Error{Errc::limit, "Detour could not make the mesh"});
    for (std::size_t slot = 0; slot < source.tiles.size(); ++slot) {
        const Tile& tile = source.tiles[slot];
        if (tile.bytes.empty()) continue;
        auto* data = static_cast<unsigned char*>(dtAlloc(static_cast<int>(tile.bytes.size()), DT_ALLOC_PERM));
        std::ranges::copy(tile.bytes, data);
        if (!dtStatusSucceed(native.mesh->addTile(data, static_cast<int>(tile.bytes.size()), DT_TILE_FREE_DATA, 0, nullptr))) {
            dtFree(data);
            return std::unexpected(Error{Errc::state, "Detour refused a tile"});
        }
    }
    native.crowd = dtAllocCrowd();
    if (!native.crowd->init(source.options.most_walkers, source.walker.radius * 4, native.mesh)) return std::unexpected(Error{Errc::limit, "Detour could not make the crowd"});
    native.crowd->getEditableFilter(0)->setIncludeFlags(walkable_flag);
    for (const WalkerRecord& walker : source.walkers) {
        const dtCrowdAgent& agent = *source.crowd->getAgent(walker.index);
        const dtCrowdAgentParams params = params_of(walker.def);
        const int index = native.crowd->addAgent(agent.npos, &params);
        if (index < 0) return std::unexpected(Error{Errc::limit, "the crowd is full"});
        native.indices.push_back(index);
        if (agent.targetState == DT_CROWDAGENT_TARGET_VALID) {
            dtPolyRef target = 0;
            float found[3];
            (void)native.crowd->getNavMeshQuery()->findNearestPoly(agent.targetPos, native.crowd->getQueryHalfExtents(), native.crowd->getFilter(0), &target, found);
            if (target) (void)native.crowd->requestMoveTarget(index, target, found);
        }
    }
    return direct;
}

void Direct::step() noexcept { native_->crowd->update(native_->step, nullptr); }
std::size_t Direct::walkers() const noexcept { return native_->indices.size(); }
Vec Direct::at(std::size_t walker) const noexcept { return vec(native_->crowd->getAgent(native_->indices[walker])->npos); }

}  // namespace xengine::nav
