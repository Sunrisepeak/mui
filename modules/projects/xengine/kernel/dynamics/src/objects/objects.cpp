module xengine.dynamics.objects;

import std;
import mlibs.hash;

namespace xengine::dynamics::rigid3 {

std::expected<Objects, std::string> Objects::make(const matter::Program& program, ObjectsOptions options) {
    if (program.channels().empty()) return std::unexpected(std::string("a program with no channels"));
    if (!(options.unit > 0) || options.most == 0) return std::unexpected(std::string("a weight's unit above 0 and a largest weight of 1 or more"));
    Objects made;
    made.program_ = &program;
    made.options_ = options;
    return made;
}

std::optional<std::size_t> Objects::index(BodyId body) const noexcept {
    const auto found = std::ranges::lower_bound(bodies_, body);
    if (found == bodies_.end() || *found != body) return std::nullopt;
    return static_cast<std::size_t>(found - bodies_.begin());
}

bool Objects::add(BodyId body, matter::MaterialId material) {
    if (!body || index(body) || material >= program_->materials().size()) return false;
    const std::size_t channels = program_->channels().size(), count = bodies_.size();
    const auto at = static_cast<std::size_t>(std::ranges::lower_bound(bodies_, body) - bodies_.begin());
    const std::vector<std::uint32_t> fresh = program_->fresh(1, material);
    std::vector<std::uint32_t> widened(channels * (count + 1));
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto from = state_.begin() + static_cast<std::ptrdiff_t>(channel * count);
        auto to = std::copy(from, from + static_cast<std::ptrdiff_t>(at), widened.begin() + static_cast<std::ptrdiff_t>(channel * (count + 1)));
        *to++ = fresh[channel];
        std::copy(from + static_cast<std::ptrdiff_t>(at), from + static_cast<std::ptrdiff_t>(count), to);
    }
    state_ = std::move(widened);
    bodies_.insert(bodies_.begin() + static_cast<std::ptrdiff_t>(at), body);
    return true;
}

bool Objects::remove(BodyId body) {
    const auto at = index(body);
    if (!at) return false;
    const std::size_t channels = program_->channels().size(), count = bodies_.size();
    std::vector<std::uint32_t> narrowed;
    narrowed.reserve(channels * (count - 1));
    for (std::size_t channel = 0; channel < channels; ++channel)
        for (std::size_t instance = 0; instance < count; ++instance)
            if (instance != *at) narrowed.push_back(state_[channel * count + instance]);
    state_ = std::move(narrowed);
    bodies_.erase(bodies_.begin() + static_cast<std::ptrdiff_t>(*at));
    return true;
}

std::optional<std::uint32_t> Objects::get(BodyId body, std::uint32_t channel) const noexcept {
    const auto at = index(body);
    if (!at || channel >= program_->channels().size()) return std::nullopt;
    return state_[channel * bodies_.size() + *at];
}

bool Objects::set(BodyId body, std::uint32_t channel, std::uint32_t value) noexcept {
    const auto at = index(body);
    if (!at || channel >= program_->channels().size()) return false;
    state_[channel * bodies_.size() + *at] = value;
    return true;
}

std::optional<matter::MaterialId> Objects::material(BodyId body) const noexcept {
    const auto value = get(body, 0);
    if (!value) return std::nullopt;
    return matter::MaterialId{*value};
}

std::expected<matter::Report, matter::Failure> Objects::step(const World& world) {
    const std::size_t count = bodies_.size(), channels = program_->channels().size();
    const bool bath = options_.ambient != matter::none;
    const std::size_t instances = bath ? count * 2 : count;
    world.touching(touches_, options_.margin);
    pairs_.clear();
    for (const Touch& touch : touches_) {
        const auto one = index(touch.one), other = index(touch.other);
        if (!one || !other) continue;
        const auto weight = static_cast<std::uint32_t>(std::clamp<long>(std::lround(touch.area / options_.unit), 1, static_cast<long>(options_.most)));
        pairs_.push_back({static_cast<std::uint32_t>(*one), static_cast<std::uint32_t>(*other), weight});
    }
    // The input: the bodies, then (with a bath) a sample of the air beside each, made anew at its temperature.
    input_.assign(channels * instances, 0);
    for (std::size_t channel = 0; channel < channels; ++channel)
        std::copy_n(state_.begin() + static_cast<std::ptrdiff_t>(channel * count), count, input_.begin() + static_cast<std::ptrdiff_t>(channel * instances));
    if (bath) {
        const auto temperature = program_->channel("temperature");
        for (std::size_t body = 0; body < count; ++body) {
            const std::size_t sample = count + body;
            program_->place(input_, instances, sample, options_.ambient);
            if (temperature)
                input_[*temperature * instances + sample] =
                    static_cast<std::uint32_t>(std::lround(std::ldexp(static_cast<double>(options_.ambient_kelvin), static_cast<int>(program_->channels()[*temperature].fraction))));
            pairs_.push_back({static_cast<std::uint32_t>(body), static_cast<std::uint32_t>(sample), options_.ambient_weight});
        }
    }
    output_.resize(input_.size());
    events_.clear();
    auto stepped = program_->step({instances, pairs_}, input_, output_, workspace_, &events_);
    if (!stepped) return stepped;
    // What became of the air is not kept: it is a bath.
    std::erase_if(events_, [&](const matter::Event& event) { return event.instance >= count; });
    for (std::size_t channel = 0; channel < channels; ++channel)
        std::copy_n(output_.begin() + static_cast<std::ptrdiff_t>(channel * instances), count, state_.begin() + static_cast<std::ptrdiff_t>(channel * count));
    before_.assign(input_.begin(), input_.end());
    bodies_before_.assign(bodies_.begin(), bodies_.end());
    joined_ = nullptr;
    return stepped;
}

std::expected<matter::Report, matter::Failure> Objects::step(const World& world, xengine::field::Field& air) {
    world.touching(touches_, options_.margin);
    pairs_.clear();
    for (const Touch& touch : touches_) {
        const auto one = index(touch.one), other = index(touch.other);
        if (!one || !other) continue;
        const auto weight = static_cast<std::uint32_t>(std::clamp<long>(std::lround(touch.area / options_.unit), 1, static_cast<long>(options_.most)));
        pairs_.push_back({static_cast<std::uint32_t>(*one), static_cast<std::uint32_t>(*other), weight});
    }
    at_.clear();
    for (const BodyId body : bodies_) {
        const auto pose = world.pose(body);
        const xengine::field::Vec3 centre = pose ? pose->centre : xengine::field::Vec3{};
        at_.push_back(centre);
        air.cover(centre, 2);
    }
    const std::array<xengine::field::Guests, 1> guests{xengine::field::Guests{.state = state_, .pairs = pairs_, .at = at_}};
    auto stepped = air.step(guests);
    if (!stepped) return stepped;
    // What happened to the bodies (the field's events are by instance: cells first, then these).
    const std::uint32_t first = air.first_guest(0);
    events_.clear();
    for (matter::Event event : air.events())
        if (event.instance >= first && event.instance < first + bodies_.size()) event.instance -= first, events_.push_back(event);
    bodies_before_.assign(bodies_.begin(), bodies_.end());
    before_.clear();
    joined_ = &air;
    return stepped;
}

void Objects::blow(World& world, const xengine::field::Field& air) const {
    const float dt = world.options().step;
    for (const BodyId body : bodies_) {
        const auto pose = world.pose(body);
        const auto box = world.bounds(body);
        if (!pose || !box || !(pose->mass > 0)) continue;
        const xengine::field::Vec3 wind = air.wind_at(pose->centre);
        const xengine::field::Vec3 relative{wind.x - pose->velocity.x, wind.y - pose->velocity.y, wind.z - pose->velocity.z};
        const float speed = std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
        const float across_x = box->high.x - box->low.x, across_y = box->high.y - box->low.y, across_z = box->high.z - box->low.z;
        if (!(speed > 0)) continue;
        // The area the box shows to the wind: each face's area by how squarely the wind meets it.
        const float area = (std::abs(relative.x) * across_y * across_z + std::abs(relative.y) * across_x * across_z + std::abs(relative.z) * across_x * across_y) / speed;
        const float push = 0.5f * options_.air_density * options_.drag * area * speed * dt;
        if (push > 0) world.push(body, {relative.x * push, relative.y * push, relative.z * push});
    }
}

void Objects::impacts(const World& world, std::vector<matter::Impact>& out) const {
    out.clear();
    for (const Contact& contact : world.contacts()) {
        if (!contact.began || contact.sensor || !(contact.approach > 0)) continue;
        for (const BodyId body : {contact.one, contact.other})
            if (const auto at = index(body))
                out.push_back({static_cast<std::uint32_t>(*at), state_[*at], contact.approach});
    }
}

bool Objects::open_to_sky(const World& world, BodyId body, float sky) const noexcept {
    const auto box = world.bounds(body);
    if (!box) return false;
    const Vec top{(box->low.x + box->high.x) / 2, box->high.y + 0.01f, (box->low.z + box->high.z) / 2};
    return !world.ray(top, {top.x, top.y + sky, top.z});
}

void Objects::rain(const World& world, std::uint32_t wetness, std::optional<std::uint32_t> most) {
    const auto channel = program_->channel("wetness");
    if (!channel) return;
    if (!most) {  // as wet as the wettest material holds
        most = 0;
        if (const auto moist = program_->column("moist"))
            for (std::uint32_t material = 0; material < program_->materials().size(); ++material) most = std::max(*most, program_->value(material, *moist));
    }
    for (const BodyId body : bodies_)
        if (open_to_sky(world, body, 60)) {
            std::uint32_t& value = state_[*channel * bodies_.size() + *index(body)];
            if (value < *most) value = std::min(*most, value + std::min(wetness, *most - value));
        }
}

std::optional<BodyId> Objects::lightning(const World& world, Strike strike) {
    const auto conduct = program_->column("conduct");
    const auto charge = program_->channel("charge"), temperature = program_->channel("temperature");
    if (!conduct || !charge || !temperature) return std::nullopt;
    std::optional<BodyId> struck;
    float highest = -std::numeric_limits<float>::infinity();
    for (const BodyId body : bodies_) {  // in the order of their ids: a tie goes to the first
        const auto material = this->material(body);
        if (!material || *material >= program_->materials().size() || program_->value(*material, *conduct) == 0) continue;
        const auto box = world.bounds(body);
        if (!box || !(box->high.y > highest) || !open_to_sky(world, body, strike.sky)) continue;
        highest = box->high.y, struck = body;
    }
    if (!struck) return std::nullopt;
    const std::size_t at = *index(*struck), count = bodies_.size();
    std::uint32_t& charged = state_[*charge * count + at];
    charged = charged > std::numeric_limits<std::uint32_t>::max() - strike.charge ? std::numeric_limits<std::uint32_t>::max() : charged + strike.charge;
    const auto heat = static_cast<std::uint32_t>(std::lround(std::ldexp(static_cast<double>(strike.kelvin), static_cast<int>(program_->channels()[*temperature].fraction))));
    std::uint32_t& warm = state_[*temperature * count + at];
    warm = warm > std::numeric_limits<std::uint32_t>::max() - heat ? std::numeric_limits<std::uint32_t>::max() : warm + heat;
    return struck;
}

std::optional<matter::Why> Objects::why(BodyId body) const {
    const auto found = std::ranges::lower_bound(bodies_before_, body);
    if (found == bodies_before_.end() || *found != body) return std::nullopt;
    if (joined_) return joined_->why(joined_->first_guest(0) + static_cast<std::uint32_t>(found - bodies_before_.begin()));
    if (before_.empty()) return std::nullopt;
    const std::size_t instances = before_.size() / program_->channels().size();
    return program_->why({instances, pairs_}, before_, static_cast<std::size_t>(found - bodies_before_.begin()));
}

std::uint64_t Objects::state_hash() const noexcept {
    mlibs::hash::Fnv1a hash;
    for (const BodyId body : bodies_) hash.le(body.value, 4);
    for (const std::uint32_t value : state_) hash.le(value, 4);
    return hash.value();
}

}  // namespace xengine::dynamics::rigid3
