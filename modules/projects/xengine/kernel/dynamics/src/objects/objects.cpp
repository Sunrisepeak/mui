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
    return stepped;
}

std::optional<matter::Why> Objects::why(BodyId body) const {
    const auto found = std::ranges::lower_bound(bodies_before_, body);
    if (found == bodies_before_.end() || *found != body || before_.empty()) return std::nullopt;
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
