module omui.showcase.kiln.sim;
import std;
import xengine.matter.json;
import mlibs.hash;

namespace kiln {
namespace substance = xengine::matter;
namespace ca = xengine::cellular;

const substance::Program& matter() {
    static const substance::Program program = *substance::standard();
    return program;
}

namespace level {
namespace {
std::vector<std::string> first_grid(bool flawed) {
        // Painted: rectangles of material on a grid of air, then read as rows.
        constexpr int width = 128, height = 64;
        std::vector<std::string> rows(height, std::string(width, '.'));
        const auto paint = [&](int x0, int y0, int x1, int y1, char what) {
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = what;
        };
        paint(0, 0, width - 1, 17, '#');            // the rock above
        paint(0, 62, width - 1, height - 1, '#');   // and below
        paint(0, 0, 1, height - 1, '#');
        paint(width - 2, 0, width - 1, height - 1, '#');
        paint(2, 30, 29, 61, '#');                   // the ledge she starts on
        paint(25, 21, 27, 29, 'w');                  // a barricade of wood across the way: fire
        paint(30, 56, 59, 61, '#');                  // the pool's floor
        paint(30, 40, 59, 55, '~');                  // its water
        paint(41, 34, 46, 39, 'C');                  // a crate afloat
        paint(60, 34, 79, 61, '#');                  // the far ledge
        paint(76, 18, 79, 33, '#');                  // a wall to the ceiling: a bomb
        paint(72, 31, 73, 33, 'g');                  // a keg of gunpowder by it (fire at it: a hazard, not a key)
        paint(80, 34, 85, 61, '#');                  // beyond the wall
        paint(86, 44, 111, 61, '#');                 // the lava pit's floor
        paint(86, 36, 111, 43, 'l');                 // lava: water cools it to stone
        paint(112, 34, 125, 61, '#');                // the last floor
        paint(118, 29, 121, 33, 'E');                // the exit
        if (flawed) paint(113, 18, 116, 33, '#');  // the mistake: a second wall, and one bomb
        rows[30][6] = 'P';
        rows[29][6] = '.';
        return rows;
}
}  // namespace
Level first() {
    static const std::vector<std::string> grid = first_grid(false);
    Level level{.name = "the first cavern", .bombs = 2};
    for (const std::string& row : grid) level.rows.push_back(row);
    return level;
}
Level flawed() {
    static const std::vector<std::string> grid = first_grid(true);
    Level level{.name = "the first cavern, with a second wall", .bombs = 1};
    for (const std::string& row : grid) level.rows.push_back(row);
    return level;
}
}  // namespace level

namespace {
bool blocks(substance::Phase phase) noexcept { return phase == substance::Phase::solid || phase == substance::Phase::granular || phase == substance::Phase::soft; }

struct Look {
    const substance::Program& program;
    std::span<const std::uint32_t> cells;
    ca::Shape shape;
    std::size_t area;
    [[nodiscard]] std::uint32_t at(std::int64_t x, std::int64_t y) const noexcept {
        if (x < 0 || y < 0 || x >= shape.width || y >= shape.height) return substance::none;
        return cells[static_cast<std::size_t>(y) * shape.width + static_cast<std::size_t>(x)];
    }
    [[nodiscard]] std::uint32_t channel(std::uint32_t which, std::int64_t x, std::int64_t y) const noexcept {
        if (x < 0 || y < 0 || x >= shape.width || y >= shape.height) return 0;
        return cells[which * area + static_cast<std::size_t>(y) * shape.width + static_cast<std::size_t>(x)];
    }
    [[nodiscard]] bool solid(std::int64_t x, std::int64_t y) const noexcept {
        const std::uint32_t material = at(x, y);
        if (material == substance::none) return x < 0 || y < 0 || x >= shape.width || y >= shape.height;  // beyond the field: a wall
        return blocks(program.phase(material));
    }
    [[nodiscard]] bool liquid(std::int64_t x, std::int64_t y) const noexcept {
        const std::uint32_t material = at(x, y);
        return material != substance::none && program.phase(material) == substance::Phase::liquid;
    }
    [[nodiscard]] bool gas(std::int64_t x, std::int64_t y) const noexcept {
        const std::uint32_t material = at(x, y);
        return material != substance::none && program.phase(material) == substance::Phase::gas;
    }
};
Look look_of(const State& state) {
    const auto& world = *state.world;
    return {world.program(), world.cells(), world.shape(), static_cast<std::size_t>(world.shape().width) * world.shape().height};
}

// The cells Mui's box covers with her feet at (x, y).
struct Box { std::int64_t left, right, top, bottom; };
Box box_at(float x, float y) noexcept {
    const float half = static_cast<float>(cat_width) * 0.5f;
    return {static_cast<std::int64_t>(std::floor(x - half)), static_cast<std::int64_t>(std::ceil(x + half)) - 1, static_cast<std::int64_t>(std::floor(y - static_cast<float>(cat_height))),
            static_cast<std::int64_t>(std::ceil(y)) - 1};
}
bool free_at(const Look& look, float x, float y) noexcept {
    const Box box = box_at(x, y);
    for (std::int64_t row = box.top; row <= box.bottom; ++row)
        for (std::int64_t column = box.left; column <= box.right; ++column)
            if (look.solid(column, row)) return false;
    return true;
}
}  // namespace

std::expected<State, std::string> start(const Level& level) {
    const auto& program = matter();
    if (level.rows.empty()) return std::unexpected(std::string("a level of no rows"));
    const std::uint32_t height = static_cast<std::uint32_t>(level.rows.size());
    std::uint32_t width = 0;
    for (const auto row : level.rows) width = std::max(width, static_cast<std::uint32_t>(row.size()));
    auto made = rigid::MatterWorld::make(program, {.shape = {width, height}, .tile = {16, 16}, .seed = 17, .fill = "air", .flying = {.throw_at = 300}});
    if (!made) return std::unexpected(made.error());
    State state;
    state.world = rigid::WorldValue(std::move(*made));
    auto& world = *state.world;
    const std::array<std::pair<char, std::string_view>, 10> legend{
        {{'#', "stone"}, {'w', "wood"}, {'~', "water"}, {'s', "sand"}, {'o', "oil"}, {'l', "lava"}, {'g', "gunpowder"}, {'i', "iron"}, {'I', "ice"}, {'C', "wood"}}};
    std::optional<std::pair<std::uint32_t, std::uint32_t>> feet;
    std::uint32_t exit_left = width, exit_top = height, exit_right = 0, exit_bottom = 0;
    std::uint32_t crate_left = width, crate_top = height, crate_right = 0, crate_bottom = 0;
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < level.rows[y].size(); ++x) {
            const char what = level.rows[y][x];
            if (what == 'P') feet = {x, y};
            if (what == 'E') exit_left = std::min(exit_left, x), exit_top = std::min(exit_top, y), exit_right = std::max(exit_right, x), exit_bottom = std::max(exit_bottom, y);
            if (what == 'C') crate_left = std::min(crate_left, x), crate_top = std::min(crate_top, y), crate_right = std::max(crate_right, x), crate_bottom = std::max(crate_bottom, y);
            const auto found = std::ranges::find(legend, what, &std::pair<char, std::string_view>::first);
            if (found == legend.end()) continue;
            const auto material = program.material(found->second);
            if (!material) return std::unexpected(std::format("the definition has no `{}`", found->second));
            world.place({x, y, 1, 1}, *material);
        }
    if (!feet) return std::unexpected(std::string("a level with no `P`: where Mui stands"));
    if (exit_right < exit_left) return std::unexpected(std::string("a level with no `E`: the exit"));
    state.exit = {exit_left, exit_top, exit_right - exit_left + 1, exit_bottom - exit_top + 1};
    if (crate_right >= crate_left) {
        auto crates = world.carve({crate_left, crate_top, crate_right - crate_left + 1, crate_bottom - crate_top + 1});
        if (!crates) return std::unexpected(crates.error());
    }
    // Her feet on the top of the cell the `P` marks.
    state.cat.bombs = level.bombs;
    state.cat.x = static_cast<float>(feet->first) + 0.5f;
    state.cat.y = static_cast<float>(feet->second);
    return state;
}

namespace {
constexpr float gravity = 98.0f;     // cells a second², 9.8 m/s² at 0.1 m a cell
constexpr float walk = 25.0f;        // cells a second
constexpr float jump = 45.0f;
constexpr float bolt_speed = 70.0f;

void cast(State& state, const Input& input) {
    Cat& cat = state.cat;
    const auto& world = *state.world;
    const std::int32_t cost = spell_cost[static_cast<std::size_t>(cat.spell)];
    if (cat.mana < cost || (cat.spell == Spell::bomb && cat.bombs == 0)) return;
    cat.mana -= cost;
    if (cat.spell == Spell::bomb) --cat.bombs;
    ++state.casts;
    const float from_x = cat.x, from_y = cat.y - static_cast<float>(cat_height) * 0.6f;
    float dx = cat.facing_right ? 1.0f : -1.0f, dy = 0;
    if (input.player.pointer_x >= 0 && input.player.pointer_y >= 0) {
        const float to_x = static_cast<float>(input.player.pointer_x) / 32767.0f * static_cast<float>(world.shape().width);
        const float to_y = static_cast<float>(input.player.pointer_y) / 32767.0f * static_cast<float>(world.shape().height);
        dx = to_x - from_x, dy = to_y - from_y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 0.001f) dx /= length, dy /= length;
        else dx = cat.facing_right ? 1.0f : -1.0f, dy = 0;
    }
    state.bolts.push_back({from_x + dx * 2.0f, from_y + dy * 2.0f, dx * bolt_speed, dy * bolt_speed, cat.spell});
}

// Where a bolt lands, what it does: fire heats, water pours, a bomb blasts and heats.
void land(State& state, const Bolt& bolt) {
    auto& world = *state.world;
    const auto& program = world.program();
    const std::uint32_t temperature = *program.channel("temperature");
    const std::int64_t cx = static_cast<std::int64_t>(std::floor(bolt.x)), cy = static_cast<std::int64_t>(std::floor(bolt.y));
    const auto clip = [&](std::int64_t radius) -> ca::Tile {
        const std::int64_t x0 = std::max<std::int64_t>(0, cx - radius), y0 = std::max<std::int64_t>(0, cy - radius);
        const std::int64_t x1 = std::min<std::int64_t>(world.shape().width - 1, cx + radius), y1 = std::min<std::int64_t>(world.shape().height - 1, cy + radius);
        if (x1 < x0 || y1 < y0) return {};
        return {static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0), static_cast<std::uint32_t>(x1 - x0 + 1), static_cast<std::uint32_t>(y1 - y0 + 1)};
    };
    switch (bolt.spell) {
        case Spell::fire: world.set(temperature, clip(2), 1000 * 256); break;
        case Spell::water: {
            const Look look = look_of(state);
            const auto water = *program.material("water");
            for (std::int64_t y = cy - 2; y <= cy + 2; ++y)
                for (std::int64_t x = cx - 2; x <= cx + 2; ++x)
                    if (std::abs(x - cx) + std::abs(y - cy) <= 3 && look.gas(x, y)) world.place({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 1, 1}, water);
            break;
        }
        case Spell::bomb: {
            // The blast shatters the stone about it to sand, then throws what is there: what comes down is a
            // heap that slumps and can be walked over, not a wall rebuilt of its own rubble.
            const Look look = look_of(state);
            const auto stone = *program.material("stone"), sand = *program.material("sand");
            for (std::int64_t y = cy - 6; y <= cy + 6; ++y)
                for (std::int64_t x = cx - 6; x <= cx + 6; ++x)
                    if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= 36 && look.at(x, y) == stone && !world.bodies().owner(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)))
                        world.place({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 1, 1}, sand);
            (void)world.blast({bolt.x, bolt.y}, 7.0f, 30.0f, true);
            world.set(temperature, clip(3), 1100 * 256);
            break;
        }
    }
}

void fly(State& state) {
    const Look look = look_of(state);
    std::vector<Bolt> kept;
    for (Bolt bolt : state.bolts) {
        if (bolt.spell == Spell::bomb) bolt.vy += gravity * step_seconds;
        const float span = std::max(std::abs(bolt.vx), std::abs(bolt.vy)) * step_seconds;
        const int samples = static_cast<int>(std::ceil(span * 2.0f)) + 1;
        bool landed = false;
        float last_x = bolt.x, last_y = bolt.y;
        for (int sample = 1; sample <= samples && !landed; ++sample) {
            const float share = static_cast<float>(sample) / static_cast<float>(samples);
            const float x = bolt.x + bolt.vx * step_seconds * share, y = bolt.y + bolt.vy * step_seconds * share;
            const std::int64_t column = static_cast<std::int64_t>(std::floor(x)), row = static_cast<std::int64_t>(std::floor(y));
            if (look.solid(column, row) || look.liquid(column, row) || look.at(column, row) == substance::none) landed = true;
            else last_x = x, last_y = y;
        }
        if (landed) {
            land(state, {last_x, last_y, bolt.vx, bolt.vy, bolt.spell});
            continue;
        }
        bolt.x += bolt.vx * step_seconds, bolt.y += bolt.vy * step_seconds;
        kept.push_back(bolt);
    }
    state.bolts = std::move(kept);
}

void move(State& state, const Input& input) {
    Cat& cat = state.cat;
    const Look look = look_of(state);
    const auto& program = look.program;
    const Box box = box_at(cat.x, cat.y);
    // In a liquid when a good part of her is.
    std::uint32_t wet = 0;
    for (std::int64_t row = box.top; row <= box.bottom; ++row)
        for (std::int64_t column = box.left; column <= box.right; ++column) wet += look.liquid(column, row);
    cat.swimming = wet >= 6;
    const float axis = static_cast<float>(input.player.axis(0));
    if (axis > 0.2f) cat.facing_right = true;
    if (axis < -0.2f) cat.facing_right = false;
    const float target = axis * (cat.swimming ? walk * 0.5f : walk);
    cat.vx += (target - cat.vx) * (cat.on_ground || cat.swimming ? 0.35f : 0.12f);
    if (cat.swimming) {
        // Holding jump she swims up; at the surface, her head out, she leaps out as she would from the ground.
        bool head_out = false;
        for (std::int64_t column = box.left; column <= box.right; ++column) head_out = head_out || !look.liquid(column, box.top);
        if (input.player.down(jump_button) && head_out && cat.vy > -5.0f) cat.vy = -jump;
        else {
            cat.vy += gravity * 0.3f * step_seconds;
            if (input.player.down(jump_button)) cat.vy -= 200.0f * step_seconds;
            cat.vy *= 0.92f;
        }
    } else {
        cat.vy += gravity * step_seconds;
        if (cat.on_ground && input.player.went_down(jump_button)) cat.vy = -jump;
    }
    cat.vy = std::clamp(cat.vy, -80.0f, 80.0f);
    // Across, half a cell at a time; a step of one cell is climbed.
    const float across = cat.vx * step_seconds;
    const int parts = static_cast<int>(std::ceil(std::abs(across) * 2.0f));
    for (int part = 0; part < parts; ++part) {
        const float next = cat.x + across / static_cast<float>(parts);
        if (free_at(look, next, cat.y)) cat.x = next;
        else if ((cat.on_ground || cat.swimming) && free_at(look, next, std::floor(cat.y) - 1.0f)) cat.x = next, cat.y = std::floor(cat.y) - 1.0f;
        else { cat.vx = 0; break; }
    }
    // Down or up, half a cell at a time.
    const float down = cat.vy * step_seconds;
    const int rows = static_cast<int>(std::ceil(std::abs(down) * 2.0f));
    cat.on_ground = false;
    for (int part = 0; part < rows; ++part) {
        const float next = cat.y + down / static_cast<float>(rows);
        if (free_at(look, cat.x, next)) { cat.y = next; continue; }
        if (cat.vy > 0) cat.y = std::floor(next), cat.on_ground = free_at(look, cat.x, cat.y);
        cat.vy = 0;
        break;
    }
    if (!cat.on_ground && !free_at(look, cat.x, std::floor(cat.y) + 1.0f) && free_at(look, cat.x, std::floor(cat.y)) && cat.vy >= 0) cat.on_ground = true, cat.y = std::floor(cat.y);
    // Pushed out of what moved into her (a body), up to three cells.
    for (int lift = 0; lift < 3 && !free_at(look, cat.x, cat.y); ++lift) cat.y = std::floor(cat.y) - 1.0f;
    // What hurts: heat around her, burning, lava; water over her head takes her breath.
    const std::uint32_t temperature = *program.channel("temperature"), burning = *program.channel("burning");
    const Box now = box_at(cat.x, cat.y);
    // Heat by what holds it: a hot solid or liquid at 420 K, a gas only near flames (700 K) — the warm air a fire
    // leaves in a low passage is not a trap for half a minute.
    std::uint32_t hottest = 0;
    bool aflame = false, under = true;
    for (std::int64_t row = now.top - 1; row <= now.bottom + 1; ++row)
        for (std::int64_t column = now.left - 1; column <= now.right + 1; ++column) {
            const std::uint32_t heat = look.channel(temperature, column, row);
            const bool airy = look.gas(column, row);
            if (!airy || heat >= 700 * 256) hottest = std::max(hottest, heat);
            aflame = aflame || look.channel(burning, column, row) != 0;
        }
    for (std::int64_t column = now.left; column <= now.right; ++column) under = under && look.liquid(column, now.top);
    const bool hurt = hottest >= 420 * 256 || aflame;
    if (hurt) cat.health -= hottest >= 900 * 256 ? 3 : 1, cat.hurt = 0;
    else ++cat.hurt;
    // Two seconds out of harm, and she mends: a point every ten frames.
    if (cat.hurt > 120 && cat.health < 100 && state.frame % 10 == 0) ++cat.health;
    if (under) {
        if (cat.breath > 0) --cat.breath;
        else if (state.frame % 10 == 0) --cat.health, cat.hurt = 0;
    } else {
        cat.breath = std::min(180, cat.breath + 3);
    }
    if (state.frame % 3 == 0) cat.mana = std::min(100, cat.mana + 1);
}
}  // namespace

void step(State& state, const Input& input) {
    if (state.outcome != Outcome::playing || !state.world) {
        ++state.frame;
        return;
    }
    Cat& cat = state.cat;
    if (input.player.went_down(next_spell_button)) cat.spell = static_cast<Spell>((static_cast<int>(cat.spell) + 1) % 3);
    if (input.player.went_down(cast_button)) cast(state, input);
    fly(state);
    state.world->step();
    move(state, input);
    const Box box = box_at(cat.x, cat.y);
    const auto& exit = state.exit;
    if (box.right >= exit.x && box.left < static_cast<std::int64_t>(exit.x + exit.width) && box.bottom >= exit.y && box.top < static_cast<std::int64_t>(exit.y + exit.height))
        state.outcome = Outcome::won;
    if (cat.health <= 0 || cat.y > static_cast<float>(state.world->shape().height) + 4.0f) cat.health = std::max(0, cat.health), state.outcome = Outcome::lost;
    ++state.frame;
}

std::uint64_t checksum(const State& state) noexcept {
    mlibs::hash::Fnv1a hash(state.world ? state.world->state_hash() : 0);
    const Cat& cat = state.cat;
    for (const float value : {cat.x, cat.y, cat.vx, cat.vy}) hash.le(std::bit_cast<std::uint32_t>(value), 4);
    for (const std::int32_t value : {cat.health, cat.mana, cat.breath}) hash.le(static_cast<std::uint32_t>(value), 4);
    hash.le(cat.bombs, 4);
    hash.le(static_cast<std::uint8_t>(cat.spell), 1).le(cat.facing_right, 1).le(cat.on_ground, 1).le(cat.swimming, 1).le(cat.hurt, 4);
    for (const Bolt& bolt : state.bolts)
        for (const float value : {bolt.x, bolt.y, bolt.vx, bolt.vy}) hash.le(std::bit_cast<std::uint32_t>(value), 4);
    hash.u64(state.frame).le(static_cast<std::uint8_t>(state.outcome), 1).le(state.casts, 4);
    return hash.value();
}

std::string observe(const State& state, bool full) {
    if (!state.world) return "{}";
    const Look look = look_of(state);
    const auto& program = look.program;
    const Cat& cat = state.cat;
    const auto outcome = state.outcome == Outcome::won ? "won" : state.outcome == Outcome::lost ? "lost" : "playing";
    std::string out = std::format(R"({{"frame":{},"outcome":"{}","cat":{{"x":{:.2f},"y":{:.2f},"vx":{:.2f},"vy":{:.2f},"health":{},"mana":{},"breath":{},"bombs":{},"spell":"{}","on_ground":{},"swimming":{}}},"exit":[{},{},{},{}],)",
                                  state.frame, outcome, cat.x, cat.y, cat.vx, cat.vy, cat.health, cat.mana, cat.breath, cat.bombs, spell_names[static_cast<std::size_t>(cat.spell)], cat.on_ground,
                                  cat.swimming, state.exit.x, state.exit.y, state.exit.width, state.exit.height);
    // What is around her: a screen's worth, 80 × 48 cells, a letter a cell by what it is (what a person playing sees).
    const std::int64_t left = static_cast<std::int64_t>(cat.x) - 40, top = static_cast<std::int64_t>(cat.y) - 30;
    out += std::format(R"("around":{{"left":{},"top":{},"rows":[)", left, top);
    for (std::int64_t row = 0; row < 48; ++row) {
        out += row ? ",\"" : "\"";
        for (std::int64_t column = 0; column < 80; ++column) {
            const std::uint32_t material = look.at(left + column, top + row);
            char letter = '#';
            if (material != substance::none) {
                const std::string_view name = program.materials()[material].name;
                switch (program.phase(material)) {
                    case substance::Phase::gas: letter = name == "air" ? '.' : '^'; break;
                    case substance::Phase::liquid: letter = name == "lava" ? 'l' : name == "water" ? '~' : 'o'; break;
                    case substance::Phase::granular: letter = 's'; break;
                    default: letter = name == "wood" ? 'w' : name == "gunpowder" ? 'g' : '#'; break;
                }
                if (look.channel(*program.channel("burning"), left + column, top + row)) letter = '*';
            }
            out += letter;
        }
        out += '"';
    }
    out += "]},";
    out += std::format(R"("bolts":{},"flying":{},"bodies":{})", state.bolts.size(), state.world->flying().flying(), state.world->bodies().bodies().size());
    if (full) out += std::format(R"(,"casts":{},"hash":"{:016x}")", state.casts, checksum(state));
    out += "}";
    return out;
}

}  // namespace kiln
