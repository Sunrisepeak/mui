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
    constexpr std::array<std::array<std::int64_t, 3>, 6> ways{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (std::uint32_t cell = 0; cell < cells(); ++cell) {
        const auto [x, y, z] = place_of(cell);
        for (std::size_t way = 0; way < ways.size(); ++way)
            if (const auto beside = cell_of(x + ways[way][0], y + ways[way][1], z + ways[way][2])) {
                near_[cell][way] = *beside;
                if (way % 2 == 0) links_.push_back({cell, *beside, 1});  // each pair once: from its lower side
            }
    }
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
    const std::size_t channels = program_->channels().size(), count = cells();
    std::size_t instances = count;
    firsts_.clear();
    for (const Guests& group : guests) firsts_.push_back(static_cast<std::uint32_t>(instances)), instances += group.at.size();
    // The input: the cells, then each group's guests; the pairs: between cells, among each group, each guest and its cell.
    input_.assign(channels * instances, 0);
    for (std::size_t channel = 0; channel < channels; ++channel)
        std::copy_n(state_.begin() + static_cast<std::ptrdiff_t>(channel * count), count, input_.begin() + static_cast<std::ptrdiff_t>(channel * instances));
    pairs_.assign(links_.begin(), links_.end());
    capacity_.assign(instances, 1);
    for (std::size_t group = 0; group < guests.size(); ++group)
        if (!guests[group].capacity.empty()) std::copy_n(guests[group].capacity.begin(), guests[group].at.size(), capacity_.begin() + firsts_[group]);
    for (std::size_t group = 0; group < guests.size(); ++group) {
        const Guests& these = guests[group];
        const std::size_t many = these.at.size(), first = firsts_[group];
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::copy_n(these.state.begin() + static_cast<std::ptrdiff_t>(channel * many), many, input_.begin() + static_cast<std::ptrdiff_t>(channel * instances + first));
        for (const matter::Pair& pair : these.pairs) pairs_.push_back({static_cast<std::uint32_t>(first + pair.first), static_cast<std::uint32_t>(first + pair.second), pair.weight});
        for (std::size_t guest = 0; guest < many; ++guest)
            if (const auto cell = cell_at(these.at[guest])) pairs_.push_back({*cell, static_cast<std::uint32_t>(first + guest), these.weight});
    }
    output_.resize(input_.size());
    events_.clear();
    auto stepped = program_->step({instances, pairs_, true, capacity_}, input_, output_, workspace_, &events_);  // a cell and all in it: crowded
    if (!stepped) return stepped;
    for (std::size_t channel = 0; channel < channels; ++channel)
        std::copy_n(output_.begin() + static_cast<std::ptrdiff_t>(channel * instances), count, state_.begin() + static_cast<std::ptrdiff_t>(channel * count));
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
    return program_->why({before_.size() / channels, pairs_, true, capacity_}, before_, instance);
}

std::uint64_t Field::state_hash() const noexcept {
    mlibs::hash::Fnv1a hash;
    for (const Key& key : blocks_) hash.le(static_cast<std::uint32_t>(key[0]), 4), hash.le(static_cast<std::uint32_t>(key[1]), 4), hash.le(static_cast<std::uint32_t>(key[2]), 4);
    for (const std::uint32_t value : state_) hash.le(value, 4);
    for (const std::vector<float>* wind : {&wind_x_, &wind_y_, &wind_z_})
        for (const float value : *wind) hash.le(std::bit_cast<std::uint32_t>(value), 4);
    return hash.value();
}

}  // namespace xengine::field
