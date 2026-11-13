module xengine.cellular.particles;
import std;

namespace xengine::cellular::particles {
namespace matter = xengine::matter;

std::expected<Particles, std::string> Particles::make(const matter::Program& program, Shape field, const Options& options) {
    if (!field.width || !field.height) return std::unexpected(std::string("a field of no cells"));
    if (!(options.cell > 0) || !(options.step > 0)) return std::unexpected(std::string("a cell and a step are longer than 0"));
    Particles particles;
    particles.program_ = &program;
    particles.field_ = field;
    particles.options_ = options;
    particles.channels_ = static_cast<std::uint32_t>(program.channels().size());
    particles.pressure_ = program.channel("pressure");
    return particles;
}

bool Particles::gas(std::uint32_t material) const noexcept {
    return material == matter::none || (material < program_->materials().size() && program_->phase(material) == matter::Phase::gas);
}
bool Particles::solid(std::uint32_t material) const noexcept {
    return material != matter::none && material < program_->materials().size() &&
           (program_->phase(material) == matter::Phase::solid || program_->phase(material) == matter::Phase::soft);
}

bool Particles::fling(std::span<std::uint32_t> field, std::uint32_t x, std::uint32_t y, Vec velocity, std::span<const std::uint32_t> fill) {
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    if (field.size() != cells * channels_ || fill.size() != channels_ || x >= field_.width || y >= field_.height) return false;
    const std::size_t at = static_cast<std::size_t>(y) * field_.width + x;
    if (gas(field[at])) return false;
    if (position_.size() >= options_.most) {
        ++pending_.refused;
        return false;
    }
    const float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    if (speed > options_.fastest) velocity = {velocity.x * options_.fastest / speed, velocity.y * options_.fastest / speed};
    position_.push_back({static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f});
    velocity_.push_back({velocity.x / options_.cell, velocity.y / options_.cell});
    for (std::uint32_t channel = 0; channel < channels_; ++channel) {
        cells_.push_back(field[channel * cells + at]);
        field[channel * cells + at] = fill[channel];
    }
    ++pending_.thrown;
    return true;
}

std::uint32_t Particles::scatter(std::span<std::uint32_t> field, std::span<const std::uint32_t> fill) {
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    if (!pressure_ || !(options_.throw_at > 0) || field.size() != cells * channels_) return 0;
    const std::span<const std::uint32_t> pressure = field.subspan(static_cast<std::size_t>(*pressure_) * cells, cells);
    const auto at = [&](std::int64_t x, std::int64_t y) -> float {
        if (x < 0 || y < 0 || x >= field_.width || y >= field_.height) return 0;
        return static_cast<float>(pressure[static_cast<std::size_t>(y) * field_.width + static_cast<std::size_t>(x)]);
    };
    // Decided from the field as it is, then thrown: the order of the throws changes nothing.
    struct Throw { std::uint32_t x, y; Vec velocity; };
    std::vector<Throw> throws;
    for (std::uint32_t y = 0; y < field_.height; ++y)
        for (std::uint32_t x = 0; x < field_.width; ++x) {
            const std::size_t cell = static_cast<std::size_t>(y) * field_.width + x;
            if (static_cast<float>(pressure[cell]) < options_.throw_at) continue;
            const std::uint32_t material = field[cell];
            if (gas(material) || solid(material)) continue;
            const float across = at(x - 1, y) - at(std::int64_t{x} + 1, y), down = at(x, std::int64_t{y} - 1) - at(x, std::int64_t{y} + 1);
            if (across == 0 && down == 0) continue;
            throws.push_back({x, y, {across * options_.push, down * options_.push}});
        }
    std::uint32_t thrown = 0;
    for (const Throw& one : throws) thrown += fling(field, one.x, one.y, one.velocity, fill) ? 1 : 0;
    return thrown;
}

std::uint32_t Particles::blast(std::span<std::uint32_t> field, std::span<const std::uint32_t> fill, Vec centre, float radius, float speed, bool breaks) {
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    if (field.size() != cells * channels_ || !(radius > 0)) return 0;
    const auto first = [&](float at, std::uint32_t size) { return static_cast<std::uint32_t>(std::clamp(std::floor(at - radius), 0.0f, static_cast<float>(size - 1))); };
    const auto last = [&](float at, std::uint32_t size) { return static_cast<std::uint32_t>(std::clamp(std::floor(at + radius), 0.0f, static_cast<float>(size - 1))); };
    std::uint32_t thrown = 0;
    for (std::uint32_t y = first(centre.y, field_.height); y <= last(centre.y, field_.height); ++y)
        for (std::uint32_t x = first(centre.x, field_.width); x <= last(centre.x, field_.width); ++x) {
            const float dx = static_cast<float>(x) + 0.5f - centre.x, dy = static_cast<float>(y) + 0.5f - centre.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius) continue;
            const std::uint32_t material = field[static_cast<std::size_t>(y) * field_.width + x];
            if (gas(material) || (solid(material) && !breaks)) continue;
            const float strength = speed * (1.0f - distance / radius);
            const Vec away = distance > 0 ? Vec{dx / distance, dy / distance} : Vec{0, -1};
            thrown += fling(field, x, y, {away.x * strength, away.y * strength}, fill) ? 1 : 0;
        }
    return thrown;
}

void Particles::land(std::span<std::uint32_t> field, std::size_t particle, std::size_t cell) {
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    for (std::uint32_t channel = 0; channel < channels_; ++channel) field[channel * cells + cell] = cells_[particle * channels_ + channel];
    ++last_.landed;
}

void Particles::step(std::span<std::uint32_t> field) {
    last_ = {.thrown = pending_.thrown, .refused = pending_.refused};
    pending_ = {};
    const std::size_t cells = static_cast<std::size_t>(field_.width) * field_.height;
    if (field.size() != cells * channels_) return;
    const float dt = options_.step, fall = options_.gravity / options_.cell * dt, keep = 1.0f - options_.drag;
    const auto inside = [&](std::int64_t x, std::int64_t y) { return x >= 0 && y >= 0 && x < field_.width && y < field_.height; };
    const auto index = [&](std::int64_t x, std::int64_t y) { return static_cast<std::size_t>(y) * field_.width + static_cast<std::size_t>(x); };
    std::size_t kept = 0;
    for (std::size_t particle = 0; particle < position_.size(); ++particle) {
        Vec& velocity = velocity_[particle];
        Vec& position = position_[particle];
        velocity.y += fall;
        velocity.x *= keep, velocity.y *= keep;
        const Vec from = position;
        // Where it is now, if that is still a gas — something may have moved there: else the nearest gas around
        // it (rings of cells, in a fixed order); with none near, it waits in flight this frame.
        std::int64_t last_x = static_cast<std::int64_t>(std::floor(from.x)), last_y = static_cast<std::int64_t>(std::floor(from.y));
        if (!inside(last_x, last_y) || !gas(field[index(last_x, last_y)])) {
            bool found = false;
            for (std::int64_t ring = 1; ring <= 4 && !found; ++ring)
                for (std::int64_t oy = -ring; oy <= ring && !found; ++oy)
                    for (std::int64_t ox = -ring; ox <= ring && !found; ++ox) {
                        if (std::max(std::abs(ox), std::abs(oy)) != ring) continue;
                        const std::int64_t x = last_x + ox, y = last_y + oy;
                        if (inside(x, y) && gas(field[index(x, y)])) last_x = x, last_y = y, found = true;
                    }
            if (!found) {
                if (kept != particle) {
                    position_[kept] = position_[particle], velocity_[kept] = velocity_[particle];
                    std::copy_n(cells_.begin() + static_cast<std::ptrdiff_t>(particle * channels_), channels_, cells_.begin() + static_cast<std::ptrdiff_t>(kept * channels_));
                }
                ++kept;
                continue;
            }
            position = {static_cast<float>(last_x) + 0.5f, static_cast<float>(last_y) + 0.5f};
        }
        bool landed = false;
        {
            const Vec start = position;
            const Vec end{start.x + velocity.x * dt, start.y + velocity.y * dt};
            // Walk the cells between, a sample at most half a cell apart.
            const float span = std::max(std::abs(end.x - start.x), std::abs(end.y - start.y));
            const std::uint32_t samples = static_cast<std::uint32_t>(std::ceil(span * 2.0f)) + 1;
            for (std::uint32_t sample = 1; sample <= samples && !landed; ++sample) {
                const float share = static_cast<float>(sample) / static_cast<float>(samples);
                const std::int64_t x = static_cast<std::int64_t>(std::floor(start.x + (end.x - start.x) * share));
                const std::int64_t y = static_cast<std::int64_t>(std::floor(start.y + (end.y - start.y) * share));
                if (x == last_x && y == last_y) continue;
                if (!inside(x, y) || !gas(field[index(x, y)])) landed = true;
                else last_x = x, last_y = y;
            }
            // Slowed to nothing on something: it stays.
            if (!landed && std::abs(velocity.x) + std::abs(velocity.y) < fall * 2 && (!inside(last_x, last_y + 1) || !gas(field[index(last_x, last_y + 1)]))) landed = true;
            if (!landed) position = end;
        }
        if (landed) {
            if (inside(last_x, last_y)) land(field, particle, index(last_x, last_y));
            continue;
        }
        if (kept != particle) {
            position_[kept] = position_[particle], velocity_[kept] = velocity_[particle];
            std::copy_n(cells_.begin() + static_cast<std::ptrdiff_t>(particle * channels_), channels_, cells_.begin() + static_cast<std::ptrdiff_t>(kept * channels_));
        }
        ++kept;
    }
    position_.resize(kept), velocity_.resize(kept), cells_.resize(kept * channels_);
    last_.flying = kept;
}

void Particles::save(std::vector<std::uint8_t>& out) const {
    const auto put = [&](std::uint32_t value) { for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift)); };
    put(static_cast<std::uint32_t>(position_.size()));
    for (std::size_t particle = 0; particle < position_.size(); ++particle) {
        put(std::bit_cast<std::uint32_t>(position_[particle].x)), put(std::bit_cast<std::uint32_t>(position_[particle].y));
        put(std::bit_cast<std::uint32_t>(velocity_[particle].x)), put(std::bit_cast<std::uint32_t>(velocity_[particle].y));
        for (std::uint32_t channel = 0; channel < channels_; ++channel) put(cells_[particle * channels_ + channel]);
    }
}

std::expected<void, std::string> Particles::restore(std::span<const std::uint8_t> saved, std::size_t& at) {
    const auto take = [&](std::uint32_t& value) {
        if (saved.size() - at < 4) return false;
        value = 0;
        for (int index = 0; index < 4; ++index) value |= std::uint32_t{saved[at + static_cast<std::size_t>(index)]} << (8 * index);
        at += 4;
        return true;
    };
    std::uint32_t count = 0;
    if (!take(count) || count > (saved.size() - at) / (16 + 4 * std::size_t{channels_})) return std::unexpected(std::string("not saved particles of this program"));
    std::vector<Vec> position(count), velocity(count);
    std::vector<std::uint32_t> cells(static_cast<std::size_t>(count) * channels_);
    for (std::uint32_t particle = 0; particle < count; ++particle) {
        std::uint32_t px = 0, py = 0, vx = 0, vy = 0;
        (void)take(px), (void)take(py), (void)take(vx), (void)take(vy);
        position[particle] = {std::bit_cast<float>(px), std::bit_cast<float>(py)};
        velocity[particle] = {std::bit_cast<float>(vx), std::bit_cast<float>(vy)};
        for (std::uint32_t channel = 0; channel < channels_; ++channel) (void)take(cells[static_cast<std::size_t>(particle) * channels_ + channel]);
    }
    position_ = std::move(position), velocity_ = std::move(velocity), cells_ = std::move(cells);
    return {};
}

}  // namespace xengine::cellular::particles
