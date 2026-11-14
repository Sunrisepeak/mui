module xengine.field;

import std;
import mlibs.hash;

namespace xengine::field {

namespace {

std::int64_t floor_div(std::int64_t value, std::int64_t by) noexcept { return value >= 0 ? value / by : -((-value + by - 1) / by); }
std::int64_t floor_to(float value) noexcept { return static_cast<std::int64_t>(std::floor(value)); }

}  // namespace

std::expected<Field, std::string> Field::make(const matter::Program& program, FieldOptions options) {
    if (options.air == matter::none || options.air >= program.materials().size()) return std::unexpected(std::string("the air: a material of the program"));
    const auto temperature = program.channel("temperature");
    if (!temperature) return std::unexpected(std::string("a program with a `temperature` channel (the wind rises with it)"));
    if (!(options.cell > 0) || !(options.step > 0)) return std::unexpected(std::string("a cell and a step above 0"));
    Field made;
    made.program_ = &program;
    made.options_ = options;
    made.temperature_ = *temperature;
    made.air_stored_ = static_cast<std::uint32_t>(std::lround(std::ldexp(static_cast<double>(options.air_kelvin), static_cast<int>(program.channels()[*temperature].fraction))));
    return made;
}

std::optional<std::uint32_t> Field::cell_of(std::int64_t x, std::int64_t y, std::int64_t z) const noexcept {
    const Key key{static_cast<std::int32_t>(floor_div(x, block_side)), static_cast<std::int32_t>(floor_div(y, block_side)), static_cast<std::int32_t>(floor_div(z, block_side))};
    const auto found = index_.find(key);
    if (found == index_.end()) return std::nullopt;
    const std::int64_t local_x = x - std::int64_t{key[0]} * block_side, local_y = y - std::int64_t{key[1]} * block_side, local_z = z - std::int64_t{key[2]} * block_side;
    return static_cast<std::uint32_t>(found->second * block_cells + static_cast<std::size_t>((local_z * block_side + local_y) * block_side + local_x));
}

std::array<std::int64_t, 3> Field::place_of(std::uint32_t cell) const noexcept {
    const Key& key = blocks_[cell / block_cells];
    const auto local = static_cast<std::int64_t>(cell % block_cells);
    return {std::int64_t{key[0]} * block_side + local % block_side, std::int64_t{key[1]} * block_side + (local / block_side) % block_side,
            std::int64_t{key[2]} * block_side + local / (block_side * block_side)};
}

std::optional<std::uint32_t> Field::cell_at(Vec3 at) const noexcept {
    const float cell = options_.cell;
    return cell_of(floor_to(at.x / cell), floor_to(at.y / cell), floor_to(at.z / cell));
}

Vec3 Field::centre(std::uint32_t cell) const noexcept {
    const auto [x, y, z] = place_of(cell);
    const float size = options_.cell;
    return {(static_cast<float>(x) + 0.5f) * size, (static_cast<float>(y) + 0.5f) * size, (static_cast<float>(z) + 0.5f) * size};
}

void Field::cover(Vec3 at, float radius) {
    const float block = options_.cell * block_side;
    const auto low = [&](float value) { return static_cast<std::int32_t>(std::floor((value - radius) / block)); };
    const auto high = [&](float value) { return static_cast<std::int32_t>(std::floor((value + radius) / block)); };
    std::vector<Key> added;
    for (std::int32_t z = low(at.z); z <= high(at.z); ++z)
        for (std::int32_t y = low(at.y); y <= high(at.y); ++y)
            for (std::int32_t x = low(at.x); x <= high(at.x); ++x)
                if (!index_.contains(Key{x, y, z})) added.push_back(Key{x, y, z});
    if (added.empty()) return;
    // Every channel widened by the new blocks' cells: the air, still, at the air's temperature.
    const std::size_t channels = program_->channels().size(), before = cells(), after = before + added.size() * block_cells;
    const std::vector<std::uint32_t> fresh = program_->fresh(1, options_.air);
    std::vector<std::uint32_t> widened(channels * after);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        std::copy_n(state_.begin() + static_cast<std::ptrdiff_t>(channel * before), before, widened.begin() + static_cast<std::ptrdiff_t>(channel * after));
        const std::uint32_t value = channel == temperature_ ? air_stored_ : fresh[channel];
        std::fill_n(widened.begin() + static_cast<std::ptrdiff_t>(channel * after + before), after - before, value);
    }
    state_ = std::move(widened);
    for (const Key& key : added) index_.emplace(key, static_cast<std::uint32_t>(blocks_.size())), blocks_.push_back(key);
    wind_x_.resize(after, options_.wind.x), wind_y_.resize(after, options_.wind.y), wind_z_.resize(after, options_.wind.z);
    link();
}

void Field::link() {
    links_.clear();
    near_.assign(cells(), {nobody, nobody, nobody, nobody, nobody, nobody});
    const auto fine = static_cast<std::uint32_t>(cells());
    constexpr std::array<std::array<std::int64_t, 3>, 6> ways{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (std::uint32_t cell = 0; cell < cells(); ++cell) {
        const auto [x, y, z] = place_of(cell);
        for (std::size_t way = 0; way < ways.size(); ++way) {
            const std::int64_t beside_x = x + ways[way][0], beside_y = y + ways[way][1], beside_z = z + ways[way][2];
            if (const auto beside = cell_of(beside_x, beside_y, beside_z)) {
                near_[cell][way] = *beside;
                if (way % 2 == 0) links_.push_back({cell, *beside, 1});  // each pair once: from its lower side
                continue;
            }
            // Beside a parked block: the cell and that block's coarse instance.
            const Key key{static_cast<std::int32_t>(floor_div(beside_x, block_side)), static_cast<std::int32_t>(floor_div(beside_y, block_side)), static_cast<std::int32_t>(floor_div(beside_z, block_side))};
            if (const auto found = parked_.find(key); found != parked_.end()) links_.push_back({cell, fine + found->second.coarse, 1});
        }
    }
    // Parked blocks beside each other.
    for (const auto& [key, held] : parked_)
        for (const Key beside : {Key{key[0] + 1, key[1], key[2]}, Key{key[0], key[1] + 1, key[2]}, Key{key[0], key[1], key[2] + 1}})
            if (const auto found = parked_.find(beside); found != parked_.end()) links_.push_back({fine + held.coarse, fine + found->second.coarse, 1});
}

std::optional<std::uint32_t> Field::coarse_at(Vec3 at) const noexcept {
    const float block = options_.cell * block_side;
    const Key key{static_cast<std::int32_t>(std::floor(at.x / block)), static_cast<std::int32_t>(std::floor(at.y / block)), static_cast<std::int32_t>(std::floor(at.z / block))};
    const auto found = parked_.find(key);
    if (found == parked_.end()) return std::nullopt;
    return found->second.coarse;
}

void Field::drop_block(std::uint32_t block) {
    const std::size_t channels = program_->channels().size(), before = cells(), after = before - block_cells;
    const std::uint32_t last = static_cast<std::uint32_t>(blocks_.size() - 1);
    // The last block's cells into this one's place, then every channel to the shorter stride.
    std::vector<std::uint32_t> narrowed(channels * after);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto from = state_.begin() + static_cast<std::ptrdiff_t>(channel * before);
        if (block != last) std::copy_n(from + static_cast<std::ptrdiff_t>(last * block_cells), block_cells, from + static_cast<std::ptrdiff_t>(block * block_cells));
        std::copy_n(from, after, narrowed.begin() + static_cast<std::ptrdiff_t>(channel * after));
    }
    state_ = std::move(narrowed);
    for (std::vector<float>* wind : {&wind_x_, &wind_y_, &wind_z_}) {
        if (block != last) std::copy_n(wind->begin() + static_cast<std::ptrdiff_t>(last * block_cells), block_cells, wind->begin() + static_cast<std::ptrdiff_t>(block * block_cells));
        wind->resize(after);
    }
    index_.erase(blocks_[block]);
    if (block != last) blocks_[block] = blocks_[last], index_[blocks_[block]] = block;
    blocks_.pop_back();
}

bool Field::park(Vec3 at) {
    const float size = options_.cell * block_side;
    const Key key{static_cast<std::int32_t>(std::floor(at.x / size)), static_cast<std::int32_t>(std::floor(at.y / size)), static_cast<std::int32_t>(std::floor(at.z / size))};
    const auto found = index_.find(key);
    if (found == index_.end()) return false;
    const std::uint32_t block = found->second;
    const std::size_t channels = program_->channels().size(), count = cells(), coarse = coarse_keys_.size();
    Parked held;
    held.coarse = static_cast<std::uint32_t>(coarse);
    held.difference.resize(channels * block_cells);
    std::vector<std::uint32_t> value(channels);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const std::uint32_t* cell = state_.data() + channel * count + static_cast<std::size_t>(block) * block_cells;
        std::uint64_t sum = 0;
        for (std::size_t local = 0; local < block_cells; ++local) sum += cell[local];
        const std::uint64_t mean = sum / block_cells;
        value[channel] = static_cast<std::uint32_t>(mean);
        for (std::size_t local = 0; local < block_cells; ++local) held.difference[channel * block_cells + local] = static_cast<std::int64_t>(cell[local]) - static_cast<std::int64_t>(mean);
    }
    // Material: what most of it is made of is the coarse instance's (a block of air is air; the differences keep the rest).
    double wind_x = 0, wind_y = 0, wind_z = 0;
    for (std::size_t local = 0; local < block_cells; ++local) {
        const std::size_t cell = static_cast<std::size_t>(block) * block_cells + local;
        wind_x += wind_x_[cell], wind_y += wind_y_[cell], wind_z += wind_z_[cell];
    }
    held.wind = {static_cast<float>(wind_x / block_cells), static_cast<float>(wind_y / block_cells), static_cast<float>(wind_z / block_cells)};
    if (!value.empty()) {  // the material channel is a name, not a mean: the block's first cell's (the differences restore each)
        value[0] = state_[static_cast<std::size_t>(block) * block_cells];
        for (std::size_t local = 0; local < block_cells; ++local) held.difference[local] = static_cast<std::int64_t>(state_[static_cast<std::size_t>(block) * block_cells + local]) - value[0];
    }
    std::vector<std::uint32_t> widened(channels * (coarse + 1));
    for (std::size_t channel = 0; channel < channels; ++channel) {
        std::copy_n(coarse_state_.begin() + static_cast<std::ptrdiff_t>(channel * coarse), coarse, widened.begin() + static_cast<std::ptrdiff_t>(channel * (coarse + 1)));
        widened[channel * (coarse + 1) + coarse] = value[channel];
    }
    coarse_state_ = std::move(widened);
    coarse_keys_.push_back(key);
    parked_.emplace(key, std::move(held));
    drop_block(block);
    link();
    return true;
}

bool Field::expand(Vec3 at) {
    const float size = options_.cell * block_side;
    const Key key{static_cast<std::int32_t>(std::floor(at.x / size)), static_cast<std::int32_t>(std::floor(at.y / size)), static_cast<std::int32_t>(std::floor(at.z / size))};
    if (index_.contains(key)) return false;
    const auto found = parked_.find(key);
    if (found == parked_.end()) {
        cover({(static_cast<float>(key[0]) + 0.5f) * size, (static_cast<float>(key[1]) + 0.5f) * size, (static_cast<float>(key[2]) + 0.5f) * size}, 0);
        return true;
    }
    const Parked held = std::move(found->second);
    parked_.erase(found);
    const std::size_t channels = program_->channels().size(), coarse = coarse_keys_.size();
    // Its coarse instance out (the last one into its place), its cells back: the kept difference over the value now.
    std::vector<std::uint32_t> value(channels);
    for (std::size_t channel = 0; channel < channels; ++channel) value[channel] = coarse_state_[channel * coarse + held.coarse];
    const std::uint32_t last = static_cast<std::uint32_t>(coarse - 1);
    std::vector<std::uint32_t> narrowed(channels * (coarse - 1));
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto from = coarse_state_.begin() + static_cast<std::ptrdiff_t>(channel * coarse);
        if (held.coarse != last) *(from + held.coarse) = *(from + last);
        std::copy_n(from, coarse - 1, narrowed.begin() + static_cast<std::ptrdiff_t>(channel * (coarse - 1)));
    }
    coarse_state_ = std::move(narrowed);
    if (held.coarse != last) coarse_keys_[held.coarse] = coarse_keys_[last], parked_.at(coarse_keys_[held.coarse]).coarse = held.coarse;
    coarse_keys_.pop_back();
    const std::size_t before = cells(), after = before + block_cells;
    std::vector<std::uint32_t> widened(channels * after);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        std::copy_n(state_.begin() + static_cast<std::ptrdiff_t>(channel * before), before, widened.begin() + static_cast<std::ptrdiff_t>(channel * after));
        for (std::size_t local = 0; local < block_cells; ++local) {
            const std::int64_t made = static_cast<std::int64_t>(value[channel]) + held.difference[channel * block_cells + local];
            widened[channel * after + before + local] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(made, 0, 0xffffffffLL));
        }
    }
    state_ = std::move(widened);
    index_.emplace(key, static_cast<std::uint32_t>(blocks_.size()));
    blocks_.push_back(key);
    wind_x_.resize(after, held.wind.x), wind_y_.resize(after, held.wind.y), wind_z_.resize(after, held.wind.z);
    link();
    return true;
}

void Field::follow(Vec3 centre, float near, float far) {
    const float size = options_.cell * block_side;
    // Beyond `far`: the nearest point of the block's box farther than that.
    const auto beyond = [&](const Key& key) {
        double sum = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double low = static_cast<double>(key[axis]) * size, high = low + size, point = axis == 0 ? centre.x : axis == 1 ? centre.y : centre.z;
            const double gap = point < low ? low - point : point > high ? point - high : 0.0;
            sum += gap * gap;
        }
        return sum > static_cast<double>(far) * far;
    };
    std::vector<Key> leaving;
    for (const Key& key : blocks_)
        if (beyond(key)) leaving.push_back(key);
    std::ranges::sort(leaving);
    for (const Key& key : leaving) (void)park({(static_cast<float>(key[0]) + 0.5f) * size, (static_cast<float>(key[1]) + 0.5f) * size, (static_cast<float>(key[2]) + 0.5f) * size});
    const auto low = [&](float value) { return static_cast<std::int32_t>(std::floor((value - near) / size)); };
    const auto high = [&](float value) { return static_cast<std::int32_t>(std::floor((value + near) / size)); };
    for (std::int32_t z = low(centre.z); z <= high(centre.z); ++z)
        for (std::int32_t y = low(centre.y); y <= high(centre.y); ++y)
            for (std::int32_t x = low(centre.x); x <= high(centre.x); ++x)
                (void)expand({(static_cast<float>(x) + 0.5f) * size, (static_cast<float>(y) + 0.5f) * size, (static_cast<float>(z) + 0.5f) * size});
}

std::optional<Region> Field::take(Vec3 at) {
    const float size = options_.cell * block_side;
    const Key key{static_cast<std::int32_t>(std::floor(at.x / size)), static_cast<std::int32_t>(std::floor(at.y / size)), static_cast<std::int32_t>(std::floor(at.z / size))};
    if (index_.contains(key)) (void)park(at);
    const auto found = parked_.find(key);
    if (found == parked_.end()) return std::nullopt;
    const std::size_t channels = program_->channels().size(), coarse = coarse_keys_.size();
    const std::uint32_t one = found->second.coarse, last = static_cast<std::uint32_t>(coarse - 1);
    Region out{.block = key, .coarse = std::vector<std::uint32_t>(channels), .difference = std::move(found->second.difference), .wind = found->second.wind};
    for (std::size_t channel = 0; channel < channels; ++channel) out.coarse[channel] = coarse_state_[channel * coarse + one];
    parked_.erase(found);
    // Its coarse instance out, the last one into its place.
    std::vector<std::uint32_t> narrowed(channels * (coarse - 1));
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto from = coarse_state_.begin() + static_cast<std::ptrdiff_t>(channel * coarse);
        if (one != last) *(from + one) = *(from + last);
        std::copy_n(from, coarse - 1, narrowed.begin() + static_cast<std::ptrdiff_t>(channel * (coarse - 1)));
    }
    coarse_state_ = std::move(narrowed);
    if (one != last) coarse_keys_[one] = coarse_keys_[last], parked_.at(coarse_keys_[one]).coarse = one;
    coarse_keys_.pop_back();
    link();
    return out;
}

bool Field::put(Region region) {
    const std::size_t channels = program_->channels().size(), coarse = coarse_keys_.size();
    if (index_.contains(region.block) || parked_.contains(region.block) || region.coarse.size() != channels || region.difference.size() != channels * block_cells) return false;
    std::vector<std::uint32_t> widened(channels * (coarse + 1));
    for (std::size_t channel = 0; channel < channels; ++channel) {
        std::copy_n(coarse_state_.begin() + static_cast<std::ptrdiff_t>(channel * coarse), coarse, widened.begin() + static_cast<std::ptrdiff_t>(channel * (coarse + 1)));
        widened[channel * (coarse + 1) + coarse] = region.coarse[channel];
    }
    coarse_state_ = std::move(widened);
    coarse_keys_.push_back(region.block);
    parked_.emplace(region.block, Parked{.coarse = static_cast<std::uint32_t>(coarse), .difference = std::move(region.difference), .wind = region.wind});
    link();
    return true;
}

namespace {
constexpr std::array<std::uint8_t, 4> region_magic{'O', 'M', 'F', 'R'};
constexpr std::uint32_t region_version = 1;
void put_le(std::vector<std::uint8_t>& out, std::uint64_t value, std::size_t width) {
    for (std::size_t byte = 0; byte < width; ++byte) out.push_back(static_cast<std::uint8_t>(value >> (8 * byte)));
}
}  // namespace

std::vector<std::uint8_t> Region::bytes() const {
    std::vector<std::uint8_t> out(region_magic.begin(), region_magic.end());
    put_le(out, region_version, 4);
    for (const std::int32_t axis : block) put_le(out, static_cast<std::uint32_t>(axis), 4);
    put_le(out, coarse.size(), 4);
    for (const std::uint32_t value : coarse) put_le(out, value, 4);
    for (const std::int64_t value : difference) put_le(out, static_cast<std::uint64_t>(value), 8);
    for (const float value : {wind.x, wind.y, wind.z}) put_le(out, std::bit_cast<std::uint32_t>(value), 4);
    return out;
}

std::expected<Region, std::string> Region::read(std::span<const std::uint8_t> bytes) {
    std::size_t at = 0;
    const auto take = [&](std::size_t width) -> std::optional<std::uint64_t> {
        if (bytes.size() - at < width) return std::nullopt;
        std::uint64_t value = 0;
        for (std::size_t byte = 0; byte < width; ++byte) value |= std::uint64_t{bytes[at + byte]} << (8 * byte);
        at += width;
        return value;
    };
    if (bytes.size() < 12 || !std::ranges::equal(bytes.first(4), region_magic)) return std::unexpected(std::string("not a region: it begins with \"OMFR\""));
    at = 4;
    if (const auto version = take(4); *version != region_version) return std::unexpected(std::format("a region of version {}: this reads version {}", *version, region_version));
    Region out;
    for (std::int32_t& axis : out.block) {
        const auto value = take(4);
        if (!value) return std::unexpected(std::string("a region cut short in its place"));
        axis = static_cast<std::int32_t>(static_cast<std::uint32_t>(*value));
    }
    const auto channels = take(4);
    if (!channels || *channels > 4096 || bytes.size() - at != *channels * 4 + *channels * block_cells * 8 + 12)
        return std::unexpected(std::format("a region's size is not {} channels' (4 + 8 × 512 bytes each, and 12 of wind)", channels.value_or(0)));
    for (std::uint64_t channel = 0; channel < *channels; ++channel) out.coarse.push_back(static_cast<std::uint32_t>(*take(4)));
    out.difference.reserve(*channels * block_cells);
    for (std::uint64_t value = 0; value < *channels * block_cells; ++value) out.difference.push_back(static_cast<std::int64_t>(*take(8)));
    out.wind = {std::bit_cast<float>(static_cast<std::uint32_t>(*take(4))), std::bit_cast<float>(static_cast<std::uint32_t>(*take(4))), std::bit_cast<float>(static_cast<std::uint32_t>(*take(4)))};
    return out;
}

std::int64_t Field::total(std::uint32_t channel) const noexcept {
    if (channel >= program_->channels().size()) return 0;
    std::int64_t sum = 0;
    const std::size_t count = cells(), coarse = coarse_keys_.size();
    for (std::size_t cell = 0; cell < count; ++cell) sum += state_[channel * count + cell];
    for (std::size_t one = 0; one < coarse; ++one) {
        sum += static_cast<std::int64_t>(block_cells) * coarse_state_[channel * coarse + one];
        const Parked& held = parked_.at(coarse_keys_[one]);
        for (std::size_t local = 0; local < block_cells; ++local) sum += held.difference[channel * block_cells + local];
    }
    return sum;
}

std::optional<std::uint32_t> Field::get(std::uint32_t cell, std::uint32_t channel) const noexcept {
    if (cell >= cells() || channel >= program_->channels().size()) return std::nullopt;
    return state_[channel * cells() + cell];
}

bool Field::set(std::uint32_t cell, std::uint32_t channel, std::uint32_t value) noexcept {
    if (cell >= cells() || channel >= program_->channels().size()) return false;
    state_[channel * cells() + cell] = value;
    return true;
}

template <class Read>
double Field::sample(Vec3 at, double outside, Read read) const noexcept {
    // The eight cell centres about the place, weighed by how near (trilinear); a missing cell is the outside.
    const double size = options_.cell;
    const double grid_x = static_cast<double>(at.x) / size - 0.5, grid_y = static_cast<double>(at.y) / size - 0.5, grid_z = static_cast<double>(at.z) / size - 0.5;
    const double base_x = std::floor(grid_x), base_y = std::floor(grid_y), base_z = std::floor(grid_z);
    const double part_x = grid_x - base_x, part_y = grid_y - base_y, part_z = grid_z - base_z;
    // The corners from the base cell by the neighbour table (one lookup); at a missing base, each looked up.
    std::array<std::uint32_t, 8> corners;
    const auto base = cell_of(static_cast<std::int64_t>(base_x), static_cast<std::int64_t>(base_y), static_cast<std::int64_t>(base_z));
    if (base) {
        const auto step = [&](std::uint32_t cell, std::size_t way) { return cell == nobody ? nobody : near_[cell][way]; };
        corners[0] = *base, corners[1] = step(*base, 0);
        corners[2] = step(corners[0], 2), corners[3] = step(corners[1], 2);
        for (int corner = 0; corner < 4; ++corner) corners[4 + corner] = step(corners[corner], 4);
    } else {
        for (int corner = 0; corner < 8; ++corner) {
            const auto cell = cell_of(static_cast<std::int64_t>(base_x) + (corner & 1), static_cast<std::int64_t>(base_y) + ((corner >> 1) & 1), static_cast<std::int64_t>(base_z) + ((corner >> 2) & 1));
            corners[static_cast<std::size_t>(corner)] = cell ? *cell : nobody;
        }
    }
    double sum = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const int along_x = corner & 1, along_y = (corner >> 1) & 1, along_z = (corner >> 2) & 1;
        const double weight = (along_x ? part_x : 1 - part_x) * (along_y ? part_y : 1 - part_y) * (along_z ? part_z : 1 - part_z);
        const std::uint32_t cell = corners[static_cast<std::size_t>(corner)];
        sum += weight * (cell != nobody ? read(cell) : outside);
    }
    return sum;
}

Vec3 Field::wind_at(Vec3 at) const noexcept {
    return {static_cast<float>(sample(at, options_.wind.x, [&](std::uint32_t cell) { return static_cast<double>(wind_x_[cell]); })),
            static_cast<float>(sample(at, options_.wind.y, [&](std::uint32_t cell) { return static_cast<double>(wind_y_[cell]); })),
            static_cast<float>(sample(at, options_.wind.z, [&](std::uint32_t cell) { return static_cast<double>(wind_z_[cell]); }))};
}

float Field::kelvin_at(Vec3 at) const noexcept {
    const double scale = std::ldexp(1.0, -static_cast<int>(program_->channels()[temperature_].fraction));
    return static_cast<float>(sample(at, options_.air_kelvin, [&](std::uint32_t cell) { return static_cast<double>(state_[temperature_ * cells() + cell]) * scale; }));
}

std::expected<matter::Report, matter::Failure> Field::step(std::span<const Guests> guests) {
    const std::size_t channels = program_->channels().size(), count = cells(), coarse = coarse_keys_.size();
    std::size_t instances = count + coarse;
    firsts_.clear();
    for (const Guests& group : guests) firsts_.push_back(static_cast<std::uint32_t>(instances)), instances += group.at.size();
    // The input: the cells, then each group's guests; the pairs: between cells, among each group, each guest and its cell.
    input_.assign(channels * instances, 0);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        std::copy_n(state_.begin() + static_cast<std::ptrdiff_t>(channel * count), count, input_.begin() + static_cast<std::ptrdiff_t>(channel * instances));
        std::copy_n(coarse_state_.begin() + static_cast<std::ptrdiff_t>(channel * coarse), coarse, input_.begin() + static_cast<std::ptrdiff_t>(channel * instances + count));
    }
    pairs_.assign(links_.begin(), links_.end());
    capacity_.assign(instances, 1);
    std::fill_n(capacity_.begin() + static_cast<std::ptrdiff_t>(count), coarse, static_cast<std::uint32_t>(block_cells));  // a coarse instance holds a block
    for (std::size_t group = 0; group < guests.size(); ++group)
        if (!guests[group].capacity.empty()) std::copy_n(guests[group].capacity.begin(), guests[group].at.size(), capacity_.begin() + firsts_[group]);
    exposed_.assign(capacity_.begin(), capacity_.end());  // all of each, unless a group says
    for (std::size_t group = 0; group < guests.size(); ++group)
        if (!guests[group].exposed.empty()) std::copy_n(guests[group].exposed.begin(), guests[group].at.size(), exposed_.begin() + firsts_[group]);
    for (std::size_t group = 0; group < guests.size(); ++group) {
        const Guests& these = guests[group];
        const std::size_t many = these.at.size(), first = firsts_[group];
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::copy_n(these.state.begin() + static_cast<std::ptrdiff_t>(channel * many), many, input_.begin() + static_cast<std::ptrdiff_t>(channel * instances + first));
        for (const matter::Pair& pair : these.pairs) pairs_.push_back({static_cast<std::uint32_t>(first + pair.first), static_cast<std::uint32_t>(first + pair.second), pair.weight});
        for (std::size_t guest = 0; guest < many; ++guest) {
            if (const auto cell = cell_at(these.at[guest])) pairs_.push_back({*cell, static_cast<std::uint32_t>(first + guest), these.weight});
            else if (const auto parked_in = coarse_at(these.at[guest])) pairs_.push_back({static_cast<std::uint32_t>(count + *parked_in), static_cast<std::uint32_t>(first + guest), these.weight});
        }
    }
    output_.resize(input_.size());
    events_.clear();
    auto stepped = program_->step({instances, pairs_, true, capacity_, exposed_}, input_, output_, workspace_, &events_);  // a cell and all in it: crowded
    if (!stepped) return stepped;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        std::copy_n(output_.begin() + static_cast<std::ptrdiff_t>(channel * instances), count, state_.begin() + static_cast<std::ptrdiff_t>(channel * count));
        std::copy_n(output_.begin() + static_cast<std::ptrdiff_t>(channel * instances + count), coarse, coarse_state_.begin() + static_cast<std::ptrdiff_t>(channel * coarse));
    }
    for (std::size_t group = 0; group < guests.size(); ++group) {
        const Guests& these = guests[group];
        const std::size_t many = these.at.size(), first = firsts_[group];
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::copy_n(output_.begin() + static_cast<std::ptrdiff_t>(channel * instances + first), many, these.state.begin() + static_cast<std::ptrdiff_t>(channel * many));
    }
    before_.assign(input_.begin(), input_.end());
    move_wind();
    return stepped;
}

// The wind: warm air rises, the wind returns toward the far wind, it carries itself, the pressure keeps it from
// piling up, and it carries the heat. Each a pass over the cells in their order.
void Field::move_wind() {
    const std::size_t count = cells();
    if (count == 0) return;
    const float dt = options_.step, size = options_.cell;
    const double scale = std::ldexp(1.0, -static_cast<int>(program_->channels()[temperature_].fraction));
    const float share = std::min(1.0f, options_.relax * dt);
    const Vec3 far = options_.wind;
    for (std::size_t cell = 0; cell < count; ++cell) {
        const auto kelvin = static_cast<float>(static_cast<double>(state_[temperature_ * count + cell]) * scale);
        wind_y_[cell] += options_.buoyancy * (kelvin - options_.air_kelvin) * dt;
        wind_x_[cell] += (far.x - wind_x_[cell]) * share, wind_y_[cell] += (far.y - wind_y_[cell]) * share, wind_z_[cell] += (far.z - wind_z_[cell]) * share;
    }
    // Carried by itself: each cell takes the wind from where its air was a step ago.
    scratch_x_.resize(count), scratch_y_.resize(count), scratch_z_.resize(count);
    for (std::uint32_t cell = 0; cell < count; ++cell) {
        const Vec3 at = centre(cell), from{at.x - wind_x_[cell] * dt, at.y - wind_y_[cell] * dt, at.z - wind_z_[cell] * dt};
        scratch_x_[cell] = static_cast<float>(sample(from, far.x, [&](std::uint32_t one) { return static_cast<double>(wind_x_[one]); }));
        scratch_y_[cell] = static_cast<float>(sample(from, far.y, [&](std::uint32_t one) { return static_cast<double>(wind_y_[one]); }));
        scratch_z_[cell] = static_cast<float>(sample(from, far.z, [&](std::uint32_t one) { return static_cast<double>(wind_z_[one]); }));
    }
    wind_x_.swap(scratch_x_), wind_y_.swap(scratch_y_), wind_z_.swap(scratch_z_);
    // Kept from piling up: the divergence, a fixed number of pressure iterations, the pressure's gradient taken away.
    const auto value = [&](const std::vector<float>& of, std::uint32_t cell, std::size_t way, float outside) {
        const std::uint32_t beside = near_[cell][way];
        return beside != nobody ? of[beside] : outside;
    };
    divergence_.assign(count, 0);
    for (std::uint32_t cell = 0; cell < count; ++cell)
        divergence_[cell] = ((value(wind_x_, cell, 0, far.x) - value(wind_x_, cell, 1, far.x)) + (value(wind_y_, cell, 2, far.y) - value(wind_y_, cell, 3, far.y)) +
                             (value(wind_z_, cell, 4, far.z) - value(wind_z_, cell, 5, far.z))) /
                            (2 * size);
    pressure_.assign(count, 0);
    pressure_next_.assign(count, 0);
    for (std::uint32_t iteration = 0; iteration < options_.projection; ++iteration) {
        for (std::uint32_t cell = 0; cell < count; ++cell) {
            float sum = 0;
            for (std::size_t way = 0; way < 6; ++way) sum += value(pressure_, cell, way, 0);
            pressure_next_[cell] = (sum - divergence_[cell] * size * size) / 6;
        }
        pressure_.swap(pressure_next_);
    }
    for (std::uint32_t cell = 0; cell < count; ++cell) {
        wind_x_[cell] -= (value(pressure_, cell, 0, 0) - value(pressure_, cell, 1, 0)) / (2 * size);
        wind_y_[cell] -= (value(pressure_, cell, 2, 0) - value(pressure_, cell, 3, 0)) / (2 * size);
        wind_z_[cell] -= (value(pressure_, cell, 4, 0) - value(pressure_, cell, 5, 0)) / (2 * size);
    }
    // The heat carried: each cell takes the temperature from where its air was a step ago.
    heat_.resize(count);
    const double stored_scale = std::ldexp(1.0, static_cast<int>(program_->channels()[temperature_].fraction));
    for (std::uint32_t cell = 0; cell < count; ++cell) {
        const Vec3 at = centre(cell), from{at.x - wind_x_[cell] * dt, at.y - wind_y_[cell] * dt, at.z - wind_z_[cell] * dt};
        const double kelvin = sample(from, options_.air_kelvin, [&](std::uint32_t one) { return static_cast<double>(state_[temperature_ * count + one]) * scale; });
        heat_[cell] = static_cast<std::uint32_t>(std::lround(kelvin * stored_scale));
    }
    std::copy(heat_.begin(), heat_.end(), state_.begin() + static_cast<std::ptrdiff_t>(temperature_ * count));
}

std::optional<matter::Why> Field::why(std::uint32_t instance) const {
    const std::size_t channels = program_->channels().size();
    if (before_.empty() || instance >= before_.size() / channels) return std::nullopt;
    return program_->why({before_.size() / channels, pairs_, true, capacity_, exposed_}, before_, instance);
}

std::uint64_t Field::state_hash() const noexcept {
    mlibs::hash::Fnv1a hash;
    for (const Key& key : blocks_) hash.le(static_cast<std::uint32_t>(key[0]), 4), hash.le(static_cast<std::uint32_t>(key[1]), 4), hash.le(static_cast<std::uint32_t>(key[2]), 4);
    for (const std::uint32_t value : state_) hash.le(value, 4);
    for (const Key& key : coarse_keys_) hash.le(static_cast<std::uint32_t>(key[0]), 4), hash.le(static_cast<std::uint32_t>(key[1]), 4), hash.le(static_cast<std::uint32_t>(key[2]), 4);
    for (const std::uint32_t value : coarse_state_) hash.le(value, 4);
    for (const Key& key : coarse_keys_) {
        const Parked& held = parked_.at(key);
        for (const std::int64_t value : held.difference) hash.le(static_cast<std::uint64_t>(value), 8);
        for (const float value : {held.wind.x, held.wind.y, held.wind.z}) hash.le(std::bit_cast<std::uint32_t>(value), 4);
    }
    for (const std::vector<float>* wind : {&wind_x_, &wind_y_, &wind_z_})
        for (const float value : *wind) hash.le(std::bit_cast<std::uint32_t>(value), 4);
    return hash.value();
}

}  // namespace xengine::field
