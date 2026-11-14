module hanim.skeleton;

import std;
import mlibs.hash;

namespace hanim::skeleton {

namespace {

// Where a clip is `seconds` on from `time`: round again from the start when it loops (the laps through its end
// counted), held at its end when it does not.
struct Advanced {
    float         time = 0;
    std::uint32_t laps = 0;
};
Advanced advanced(float time, float seconds, float duration, bool loop) noexcept {
    if (!(duration > 0)) return {0, 0};
    float next = time + seconds;
    if (!loop) return {std::clamp(next, 0.0f, duration), 0};
    std::uint32_t laps = 0;
    if (next >= duration) {
        const float whole = std::floor(next / duration);
        laps = static_cast<std::uint32_t>(whole);
        next = std::fmod(next, duration);
    }
    if (next < 0) next = std::fmod(next, duration) + duration;
    return {next, laps};
}

void put_choice(Image& image, const Choice& choice) {
    image.put(choice.count);
    for (std::uint32_t index = 0; index < choice.count; ++index) image.put(choice.playing[index]);
}
bool take_choice(Reading& reading, Choice& choice) {
    if (!reading.take(choice.count) || choice.count > Choice::most) return false;
    for (std::uint32_t index = 0; index < choice.count; ++index)
        if (!reading.take(choice.playing[index])) return false;
    return true;
}

}  // namespace

// ── the player ───────────────────────────────────────────────────────────

Player::Player(std::shared_ptr<const Rig> rig) : rig_(std::move(rig)) {
    const std::size_t joints = rig_->skeleton.size();
    pose_.assign(rig_->skeleton.rest().begin(), rig_->skeleton.rest().end());
    scratch_.resize(joints), other_.resize(joints);
    weights_.resize(joints);
    model_.resize(joints), skin_.resize(joints);
    evaluate();
}

std::expected<void, Error> Player::layer(std::size_t index, const Layer& layer) {
    if (index >= layers) return std::unexpected(Error{std::format("layer {}: there are {} (0 … {})", index, layers, layers - 1)});
    if (layer.weight > 0 && layer.clip >= rig_->clips.size()) return std::unexpected(Error{std::format("no clip {}: the rig has {}", layer.clip, rig_->clips.size())});
    if (layer.mask && *layer.mask >= rig_->skeleton.size()) return std::unexpected(Error{std::format("no joint {}: the skeleton has {}", *layer.mask, rig_->skeleton.size())});
    if (!std::isfinite(layer.weight) || !std::isfinite(layer.speed) || !std::isfinite(layer.time)) return std::unexpected(Error{"a layer's weight, speed and time are numbers"});
    layers_[index] = layer;
    layers_[index].weight = std::clamp(layer.weight, 0.0f, 1.0f);
    prepare(index);
    evaluate();
    return {};
}

// What a layer needs that does not change while it plays: its mask, and for one that adds, its clip's first moment.
void Player::prepare(std::size_t index) {
    const Layer& layer = layers_[index];
    masks_[index] = layer.weight > 0 && layer.mask ? rig_->skeleton.mask(*layer.mask, 1.0f) : std::vector<float>{};
    references_[index].clear();
    if (layer.weight > 0 && layer.additive) {
        references_[index].resize(rig_->skeleton.size());
        sample(rig_->clips[layer.clip], rig_->skeleton, 0.0f, references_[index]);
    }
}

void Player::apply(const Choice& choice, float seconds) {
    chosen_ = choice;
    for (Layer& layer : layers_) {
        if (layer.weight <= 0) continue;
        const Clip& clip = rig_->clips[layer.clip];
        layer.time = advanced(layer.time, seconds * layer.speed, clip.duration, layer.loop).time;
    }
    // Root motion: each clip playing moves the character by what its root moved over the step, by its weight.
    Motion moved;
    float total = 0;
    for (std::uint32_t index = 0; index < choice.count; ++index) {
        const Playing& playing = choice.playing[index];
        const Clip& clip = rig_->clips[playing.clip];
        if (!clip.root_motion || playing.weight <= 0) continue;
        const Motion step = motion(clip, rig_->skeleton, playing.before, playing.time, playing.laps);
        total += playing.weight;
        const float share = playing.weight / total;
        moved = {.moved = mlibs::math::mix(moved.moved, step.moved, share), .turned = mlibs::math::mix(moved.turned, step.turned, share)};
    }
    if (total > 0) placed_ = placed_.then(moved);
    evaluate();
}

void Player::evaluate() {
    const Skeleton& skeleton = rig_->skeleton;
    // What the stage chose, mixed in order: each by its weight over the weights so far.
    float total = 0;
    bool any = false;
    for (std::uint32_t index = 0; index < chosen_.count; ++index) {
        const Playing& playing = chosen_.playing[index];
        if (playing.weight <= 0 || playing.clip >= rig_->clips.size()) continue;
        total += playing.weight;
        if (!any) {
            sample(rig_->clips[playing.clip], skeleton, playing.time, pose_);
            any = true;
            continue;
        }
        sample(rig_->clips[playing.clip], skeleton, playing.time, scratch_);
        blend(pose_, scratch_, playing.weight / total, pose_);
    }
    if (!any) std::ranges::copy(skeleton.rest(), pose_.begin());
    for (std::size_t index = 0; index < layers; ++index) {
        const Layer& layer = layers_[index];
        if (layer.weight <= 0) continue;
        sample(rig_->clips[layer.clip], skeleton, layer.time, scratch_);
        const std::span<const float> mask = masks_[index];
        if (layer.additive) {
            difference(scratch_, references_[index], other_);
            add(pose_, other_, layer.weight, mask, pose_);
        } else if (!mask.empty()) {
            for (std::size_t joint = 0; joint < weights_.size(); ++joint) weights_[joint] = mask[joint] * layer.weight;
            blend(pose_, scratch_, weights_, pose_);
        } else {
            blend(pose_, scratch_, layer.weight, pose_);
        }
    }
    hanim::skeleton::model(skeleton, pose_, model_);
    hanim::skeleton::skin(skeleton, model_, skin_);
}

Mat4 Player::world(std::uint32_t joint) const noexcept {
    const Mat4 placed = mlibs::math::translation(placed_.moved) * mlibs::math::rotation(placed_.turned);
    return joint < model_.size() ? placed * model_[joint] : placed;
}

void Player::save(Image& image) const {
    for (const Layer& layer : layers_) {
        image.put(layer.clip), image.put(layer.weight), image.put(layer.speed), image.put(layer.loop), image.put(layer.additive);
        image.put(layer.mask.value_or(any_state)), image.put(layer.time);
    }
    put_choice(image, chosen_);
    image.put(placed_);
}

bool Player::restore(Reading& reading) {
    for (Layer& layer : layers_) {
        std::uint32_t mask = any_state;
        if (!reading.take(layer.clip) || !reading.take(layer.weight) || !reading.take(layer.speed) || !reading.take(layer.loop) || !reading.take(layer.additive) ||
            !reading.take(mask) || !reading.take(layer.time))
            return false;
        layer.mask = mask == any_state ? std::nullopt : std::optional<std::uint32_t>(mask);
        if (layer.weight > 0 && (layer.clip >= rig_->clips.size() || (layer.mask && *layer.mask >= rig_->skeleton.size()))) return false;
    }
    if (!take_choice(reading, chosen_) || !reading.take(placed_)) return false;
    for (std::size_t index = 0; index < layers; ++index) prepare(index);
    for (std::uint32_t index = 0; index < chosen_.count; ++index)
        if (chosen_.playing[index].clip >= rig_->clips.size()) return false;
    evaluate();
    return true;
}

// ── the state machine ────────────────────────────────────────────────────

std::expected<void, Error> Machine::check(const Rig& rig) const {
    if (states.empty()) return std::unexpected(Error{"a machine has at least one state"});
    if (initial >= states.size()) return std::unexpected(Error{std::format("initial state {}: there are {}", initial, states.size())});
    for (std::size_t index = 0; index < states.size(); ++index) {
        if (states[index].clip >= rig.clips.size()) return std::unexpected(Error{std::format("state {} ({}) plays clip {}: the rig has {}", index, states[index].name, states[index].clip, rig.clips.size())});
        if (!std::isfinite(states[index].speed)) return std::unexpected(Error{std::format("state {} ({}): its speed is not a number", index, states[index].name)});
    }
    for (std::size_t index = 0; index < transitions.size(); ++index) {
        const Transition& transition = transitions[index];
        if ((transition.from != any_state && transition.from >= states.size()) || transition.to >= states.size())
            return std::unexpected(Error{std::format("transition {} names a state that is not there", index)});
        if (!(transition.fade >= 0)) return std::unexpected(Error{std::format("transition {}: a fade below 0", index)});
        for (const Condition& condition : transition.when)
            if (condition.kind != Condition::Kind::ended && condition.parameter >= parameters.size())
                return std::unexpected(Error{std::format("transition {} asks parameter {}: there are {}", index, condition.parameter, parameters.size())});
    }
    return {};
}

std::optional<std::uint32_t> Machine::state(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < states.size(); ++index)
        if (states[index].name == name) return static_cast<std::uint32_t>(index);
    return std::nullopt;
}
std::optional<std::uint32_t> Machine::parameter(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < parameters.size(); ++index)
        if (parameters[index] == name) return static_cast<std::uint32_t>(index);
    return std::nullopt;
}

StateMachine::StateMachine(std::shared_ptr<const Machine> machine) : machine_(std::move(machine)) {
    values_.assign(machine_->parameters.size(), 0.0f);
    sources_[0] = {.state = machine_->initial, .share = 0};
    count_ = 1, target_ = 0;
    fade_ = 0, elapsed_ = 0;
}

float StateMachine::faded() const noexcept { return fade_ > 0 ? std::min(1.0f, elapsed_ / fade_) : 1.0f; }

std::expected<void, Error> StateMachine::set(std::string_view parameter, float value) {
    const auto index = machine_->parameter(parameter);
    if (!index) return std::unexpected(Error{std::format("no parameter {}", parameter)});
    if (!std::isfinite(value)) return std::unexpected(Error{"a parameter's value is a number"});
    values_[*index] = value;
    return {};
}

std::expected<void, Error> StateMachine::go(std::string_view state, float fade) {
    const auto index = machine_->state(state);
    if (!index) return std::unexpected(Error{std::format("no state {}", state)});
    if (!(fade >= 0) || !std::isfinite(fade)) return std::unexpected(Error{"a fade is 0 seconds or more"});
    last_ = {.transition = any_state, .from = current(), .to = *index, .step = steps_};
    start(*index, fade, ease::smoothstep);
    return {};
}

void StateMachine::start(std::uint32_t state, float fade, Ease ease) {
    // Each source keeps its share of what is not the incoming state's: a fade interrupted carries on without a jump.
    const float target_weight = count_ > 1 ? static_cast<float>(ease_(faded())) : 1.0f;
    float sum = 0;
    for (std::uint32_t index = 0; index < count_; ++index) {
        Source& source = sources_[index];
        source.share = index == target_ ? target_weight : source.share * (1.0f - target_weight);
        sum += source.share;
    }
    for (std::uint32_t index = 0; index < count_; ++index) sources_[index].share = sum > 0 ? sources_[index].share / sum : 1.0f / static_cast<float>(count_);
    // Full: the source with the least share goes, its share to the others.
    if (count_ == Choice::most) {
        std::uint32_t least = 0;
        for (std::uint32_t index = 1; index < count_; ++index)
            if (sources_[index].share < sources_[least].share) least = index;
        const float gone = sources_[least].share;
        for (std::uint32_t index = least; index + 1 < count_; ++index) sources_[index] = sources_[index + 1];
        --count_;
        for (std::uint32_t index = 0; index < count_; ++index) sources_[index].share /= (1.0f - gone > 0 ? 1.0f - gone : 1.0f);
    }
    sources_[count_] = {.state = state};
    target_ = count_++;
    fade_ = fade, elapsed_ = 0, ease_ = ease;
    if (fade <= 0) {
        sources_[0] = sources_[target_];
        count_ = 1, target_ = 0;
    }
}

void StateMachine::advance(const Rig& rig, float seconds, Choice& choice) {
    ++steps_;
    const Machine& machine = *machine_;
    // A transition first (on what the target state has played so far), then time moves on.
    const Source& now = sources_[target_];
    for (std::size_t index = 0; index < machine.transitions.size(); ++index) {
        const Transition& transition = machine.transitions[index];
        if (transition.from == any_state ? transition.to == now.state : transition.from != now.state) continue;
        bool holds = true;
        for (const Condition& condition : transition.when) {
            switch (condition.kind) {
                case Condition::Kind::above: holds = values_[condition.parameter] > condition.value; break;
                case Condition::Kind::below: holds = values_[condition.parameter] < condition.value; break;
                case Condition::Kind::trigger: holds = values_[condition.parameter] != 0; break;
                case Condition::Kind::ended: holds = now.played >= rig.clips[machine.states[now.state].clip].duration / std::max(1e-6f, std::abs(machine.states[now.state].speed)); break;
            }
            if (!holds) break;
        }
        if (!holds) continue;
        for (const Condition& condition : transition.when)
            if (condition.kind == Condition::Kind::trigger) values_[condition.parameter] = 0;  // taken
        last_ = {.transition = static_cast<std::uint32_t>(index), .from = now.state, .to = transition.to, .step = steps_};
        start(transition.to, transition.fade, transition.ease);
        break;
    }
    elapsed_ += seconds;
    const float target_weight = static_cast<float>(ease_(faded()));
    if (faded() >= 1.0f && count_ > 1) {
        sources_[0] = sources_[target_];
        count_ = 1, target_ = 0;
    }
    for (std::uint32_t index = 0; index < count_; ++index) {
        Source& source = sources_[index];
        const State& state = machine.states[source.state];
        const Clip& clip = rig.clips[state.clip];
        const Advanced next = advanced(source.time, seconds * state.speed, clip.duration, state.loop);
        source.before = source.time, source.time = next.time, source.laps = next.laps;
        source.played += seconds;
        const float weight = count_ == 1 ? 1.0f : index == target_ ? target_weight : source.share * (1.0f - target_weight);
        choice.push({.clip = state.clip, .time = source.time, .before = source.before, .laps = source.laps, .weight = weight});
    }
}

void StateMachine::save(Image& image) const {
    image.put(static_cast<std::uint32_t>(values_.size()));
    for (const float value : values_) image.put(value);
    // Field by field: no padding in what is fingerprinted.
    image.put(count_), image.put(target_), image.put(fade_), image.put(elapsed_), image.put(ease_.kind), image.put(ease_.p), image.put(steps_);
    image.put(last_.transition), image.put(last_.from), image.put(last_.to), image.put(last_.step);
    for (std::uint32_t index = 0; index < count_; ++index) image.put(sources_[index]);
}

bool StateMachine::restore(Reading& reading) {
    std::uint32_t parameters = 0;
    if (!reading.take(parameters) || parameters != machine_->parameters.size()) return false;
    for (float& value : values_)
        if (!reading.take(value)) return false;
    if (!reading.take(count_) || !reading.take(target_) || !reading.take(fade_) || !reading.take(elapsed_) || !reading.take(ease_.kind) || !reading.take(ease_.p) ||
        !reading.take(steps_) || !reading.take(last_.transition) || !reading.take(last_.from) || !reading.take(last_.to) || !reading.take(last_.step))
        return false;
    if (count_ == 0 || count_ > Choice::most || target_ >= count_) return false;
    for (std::uint32_t index = 0; index < count_; ++index)
        if (!reading.take(sources_[index]) || sources_[index].state >= machine_->states.size()) return false;
    return true;
}

// ── a crowd ──────────────────────────────────────────────────────────────

Crowd::Crowd(std::shared_ptr<const Rig> rig, std::shared_ptr<const Machine> machine, float step, unsigned workers)
    : rig_(std::move(rig)), machine_(std::move(machine)), step_(step), workers_(workers) {}

std::uint32_t Crowd::add(const Motion& at) {
    characters_.emplace_back(rig_, StateMachine(machine_));
    characters_.back().player.place(at);
    return static_cast<std::uint32_t>(characters_.size() - 1);
}

void Crowd::step() {
    step_all<StateMachine>(characters_, step_, workers_);
    ++steps_;
}

std::vector<std::uint8_t> Crowd::save() const {
    Image image;
    image.put(steps_);
    image.put(static_cast<std::uint32_t>(characters_.size()));
    for (const Character& character : characters_) character.save(image);
    return std::move(image.bytes);
}

std::expected<void, Error> Crowd::restore(std::span<const std::uint8_t> bytes) {
    Reading reading{bytes};
    std::uint64_t steps = 0;
    std::uint32_t count = 0;
    if (!reading.take(steps) || !reading.take(count) || count > (1u << 20)) return std::unexpected(Error{"not a crowd's image"});
    std::vector<Character> characters;
    characters.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        characters.emplace_back(rig_, StateMachine(machine_));
        if (!characters.back().restore(reading)) return std::unexpected(Error{std::format("character {}: not a character's image of this rig and machine", index)});
    }
    if (reading.at != bytes.size()) return std::unexpected(Error{"bytes past the crowd's image"});
    steps_ = steps;
    characters_ = std::move(characters);
    return {};
}

std::uint64_t Crowd::state_hash() const {
    mlibs::hash::Fnv1a hash;
    hash.bytes(std::span<const std::uint8_t>(save()));
    // What a renderer is given, too: every joint's skin matrix.
    for (const Character& character : characters_) hash.bytes(std::as_bytes(character.player.skin()));
    return hash.value();
}

}  // namespace hanim::skeleton
