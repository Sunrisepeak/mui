// hanim.skeleton — skeletal animation as hanim's channels (the foundation plan's M6.4 and §9.4; D-93: hanim owns
// general animation): a skeleton (joints, their parents, the rest pose); clips whose channels are each joint's
// translation, rotation and scale, sampled at any time as a pure function of it, and compressed by ACL within an
// error said and measured; blending (a mix, a mix by joint, an added difference); root motion as a channel; the
// step that chooses the next pose as a stage that plugs in (one built in: a state machine with cross-fades); and
// what a renderer consumes — the joints in the model's space and the skin's matrices.
//
//     auto skeleton = Skeleton::make({{"hips", -1, rest_hips}, {"spine", 0, rest_spine}});
//     auto rig = std::make_shared<Rig>(Rig{*skeleton, {walk, run}});
//     rig->clips[0].compress(rig->skeleton);                     // ACL, 0.1 mm at 3 cm from each joint
//     Character hero(rig, StateMachine(machine));
//     hero.stage.set("speed", 3.2f);                             // the machine fades from walk to run
//     hero.step(1.0f / 60);
//     draw(hero.player.skin());                                  // joint world × inverse bind, one a joint
//
// Determinism: no contraction of a*b+c and no fast math here and in ACL (scalar, no SIMD intrinsics); a crowd
// steps its characters on any number of workers to the same bits; a character's state saves, restores and
// branches to the same bits.
export module hanim.skeleton;

import std;
export import mlibs.math;
export import hanim.core;
import mcpplibs.os;

export namespace hanim::skeleton {

using mlibs::math::Mat4;
using mlibs::math::Quat;
using mlibs::math::Vec3;

struct Error {
    std::string text;
};

// A joint's place relative to its parent: scaled, then turned, then moved.
struct Transform {
    Vec3 translation{0, 0, 0};
    Quat rotation{};
    Vec3 scale{1, 1, 1};
    // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it (as hanim.core's Ease).
    friend constexpr bool operator==(const Transform& one, const Transform& other) noexcept {
        return one.translation == other.translation && one.rotation == other.rotation && one.scale == other.scale;
    }
};
[[nodiscard]] Mat4 matrix(const Transform& transform) noexcept;
// `inner` placed in `outer`'s space (outer × inner, for a scale that is the same each way).
[[nodiscard]] Transform compose(const Transform& outer, const Transform& inner) noexcept;

// The rotation about the way up (y) that `rotation` makes (its twist about y), of length 1; the identity where it
// has none (a half turn about a level axis).
[[nodiscard]] Quat yaw(const Quat& rotation) noexcept;

// ── the skeleton ─────────────────────────────────────────────────────────

struct JointDef {
    std::string  name;
    std::int32_t parent = -1;  // an earlier joint's index; −1: a root
    Transform    rest;
};

class Skeleton {
public:
    // Refused: a parent that is not an earlier joint, two joints of one name, a rest that is not a number.
    // `inverse_binds`: the skin's, one a joint; none: the inverse of each joint at rest in the model.
    [[nodiscard]] static std::expected<Skeleton, Error> make(std::vector<JointDef> joints, std::vector<Mat4> inverse_binds = {}, Mat4 base = {});

    [[nodiscard]] std::size_t size() const noexcept { return names_.size(); }
    [[nodiscard]] std::span<const std::string> names() const noexcept { return names_; }
    [[nodiscard]] std::span<const std::int32_t> parents() const noexcept { return parents_; }
    [[nodiscard]] std::span<const Transform> rest() const noexcept { return rest_; }
    [[nodiscard]] std::span<const Mat4> inverse_binds() const noexcept { return inverse_binds_; }
    // Where the roots' parent space is in the model (a node above the skeleton in a file, as it is at rest).
    [[nodiscard]] const Mat4& base() const noexcept { return base_; }
    [[nodiscard]] std::optional<std::uint32_t> find(std::string_view name) const noexcept;
    // The joints under `joint`, with it: one weight a joint, `weight` there and 0 elsewhere.
    [[nodiscard]] std::vector<float> mask(std::uint32_t joint, float weight = 1.0f) const;

private:
    std::vector<std::string>  names_;
    std::vector<std::int32_t> parents_;
    std::vector<Transform>    rest_;
    std::vector<Mat4>         inverse_binds_;
    Mat4                      base_;
};

// A pose: each joint relative to its parent, in the skeleton's order.
using Pose = std::vector<Transform>;

// Each joint in the model's space: its parent's, then its own (the base above the roots).
void model(const Skeleton& skeleton, std::span<const Transform> pose, std::span<Mat4> out) noexcept;
// What skins a mesh: each joint in the model's space times its inverse bind — what a vertex at rest is moved by.
void skin(const Skeleton& skeleton, std::span<const Mat4> model_space, std::span<Mat4> out) noexcept;

// ── channels and clips ──────────────────────────────────────────────────

enum class Interpolation : std::uint8_t { linear, step, cubic };

// A channel: values at rising times. Before the first time, the first value; after the last, the last. `cubic`:
// three values a key, the tangent in, the value, the tangent out (glTF's cubic spline, per second). A rotation is
// mixed along the shorter arc and made of length 1 (a normalised mix: the path of a spherical one at a speed not
// quite even; keys are close enough that the difference is below what a clip is compressed to — the glTF reading
// adds keys where they are not).
template <typename Value>
struct Keys {
    std::vector<float> times;
    std::vector<Value> values;
    Interpolation      interpolation = Interpolation::linear;
    [[nodiscard]] bool empty() const noexcept { return times.empty(); }
};
[[nodiscard]] Vec3 sample(const Keys<Vec3>& keys, float time) noexcept;
[[nodiscard]] Quat sample(const Keys<Quat>& keys, float time) noexcept;

// A joint's three channels; an empty one is the joint's rest.
struct JointChannels {
    Keys<Vec3> translation;
    Keys<Quat> rotation;
    Keys<Vec3> scale;
};

// What a clip's root moves by between two of its times, taken out of its pose and given as a channel: across the
// ground (x, z) and about the way up, in the space of the character as it was at the first.
struct RootMotion {
    std::uint32_t joint = 0;  // the joint that carries it (a root)
};
struct Motion {
    Vec3 moved{0, 0, 0};
    Quat turned{};
    // This, then `after` (each in the space the one before left the character in).
    [[nodiscard]] Motion then(const Motion& after) const noexcept;
};

// A named moment of a clip (a foot down, a hit), and the clip's tags: what a stage that chooses poses reads.
struct Marker {
    std::string name;
    float       time = 0;
};

// Within what a compressed clip is played back: ACL's measure — the distance between where points `shell` from
// each joint (on its three axes) are in the model's space, raw and compressed.
struct Compression {
    float precision = 0.0001f;  // metres: 0.1 mm
    float shell = 0.03f;        // metres: 3 cm, a finger's joint to its tip
    // Samples a second the raw channels are read at; 0: the clip's own (`Clip::key_rate`, else its keys' finest
    // spacing; at most 240), so a clip baked at a rate is read where its keys are and nothing is lost between them.
    float rate = 0;
};

// A clip compressed by ACL: its bytes, played back at any time.
class Compressed {
public:
    [[nodiscard]] std::size_t bytes() const noexcept { return size_; }
    [[nodiscard]] float duration() const noexcept { return duration_; }
    // ACL's own measure of the error, at its samples, against the channels as read at `Compression::rate`.
    [[nodiscard]] float error() const noexcept { return error_; }
    [[nodiscard]] const Compression& settings() const noexcept { return settings_; }
    // Every joint at `time` (clamped to the clip): the same bits for the same time, on every platform.
    void sample(float time, std::span<Transform> out) const noexcept;
    // The bytes' fingerprint.
    [[nodiscard]] std::uint64_t hash() const noexcept;

private:
    friend struct Compressing;
    std::shared_ptr<const std::byte> data_;  // ACL's compressed tracks, aligned to 16 bytes
    std::size_t                      size_ = 0, joints_ = 0;
    float                            duration_ = 0, error_ = 0;
    Compression                      settings_;
};

struct Clip {
    std::string                name;
    float                      duration = 0;
    std::vector<JointChannels> joints;  // one a joint of its skeleton
    float                      key_rate = 0;  // keys a second its source was made at (0: not known)
    std::optional<RootMotion>  root_motion;
    std::vector<Marker>        markers;
    std::vector<std::string>   tags;
    std::optional<Compressed>  compressed;  // played from when there

    // Compressed by ACL (`compressed` set); refused: a clip that does not fit the skeleton, or ACL's reason.
    std::expected<void, Error> compress(const Skeleton& skeleton, Compression settings = {});
    // Raw bytes: what the channels hold.
    [[nodiscard]] std::size_t bytes() const noexcept;
};

// Every joint of `clip` at `time` (clamped to it), from its compressed bytes when it has them: a pure function of
// the time. With root motion, its root is in place (what it moves by is `motion`).
void sample(const Clip& clip, const Skeleton& skeleton, float time, std::span<Transform> out) noexcept;
// The same from the channels themselves, compressed or not.
void sample_raw(const Clip& clip, const Skeleton& skeleton, float time, std::span<Transform> out) noexcept;
// What the root moves by from `from` to `to` (a later time; past the end, round again from the start: `laps`
// times through the end between them). None moved for a clip without root motion.
[[nodiscard]] Motion motion(const Clip& clip, const Skeleton& skeleton, float from, float to, std::uint32_t laps = 0) noexcept;

// How far the compressed clip is from its channels where it matters: the largest distance between points `shell`
// from each joint, raw and compressed, at `per_second` times a second — between ACL's samples too.
struct Fidelity {
    float         worst = 0;  // metres
    std::uint32_t joint = 0;
    float         time = 0;
};
[[nodiscard]] Fidelity measure(const Clip& clip, const Skeleton& skeleton, float per_second = 120) noexcept;

// ── blending ─────────────────────────────────────────────────────────────

// `from` toward `to` by `weight`, each joint.
void blend(std::span<const Transform> from, std::span<const Transform> to, float weight, std::span<Transform> out) noexcept;
// … by a weight a joint (`Skeleton::mask`).
void blend(std::span<const Transform> from, std::span<const Transform> to, std::span<const float> weights, std::span<Transform> out) noexcept;
// What takes `reference` to `pose`, each joint (to be added to another pose).
void difference(std::span<const Transform> pose, std::span<const Transform> reference, std::span<Transform> out) noexcept;
// `base` with `added` added by a weight a joint (`weight` × `weights`, or `weight` where none are given).
void add(std::span<const Transform> base, std::span<const Transform> added, float weight, std::span<const float> weights, std::span<Transform> out) noexcept;

// ── a rig, a player, a stage ─────────────────────────────────────────────

// A skeleton and its clips, shared by every character that plays them.
struct Rig {
    Skeleton          skeleton;
    std::vector<Clip> clips;
    [[nodiscard]] std::optional<std::uint32_t> clip(std::string_view name) const noexcept;
};

// A state's bytes, written and read back.
struct Image {
    std::vector<std::uint8_t> bytes;
    template <typename Value>
        requires std::is_trivially_copyable_v<Value>
    void put(const Value& value) {
        const auto* at = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), at, at + sizeof(Value));
    }
};
struct Reading {
    std::span<const std::uint8_t> bytes;
    std::size_t                   at = 0;
    template <typename Value>
        requires std::is_trivially_copyable_v<Value>
    [[nodiscard]] bool take(Value& value) {
        if (bytes.size() - at < sizeof(Value)) return false;
        std::memcpy(&value, bytes.data() + at, sizeof(Value));
        at += sizeof(Value);
        return true;
    }
};

// What plays under a character, chosen by its stage each step: clips at their times, each with a weight.
struct Playing {
    std::uint32_t clip = 0;
    float         time = 0;      // where in the clip, now
    float         before = 0;    // where it was a step ago (what root motion is taken between)
    std::uint32_t laps = 0;      // times it went through its end since
    float         weight = 0;
};
struct Choice {
    static constexpr std::size_t most = 8;
    std::array<Playing, most> playing{};
    std::uint32_t             count = 0;
    void push(const Playing& item) noexcept {
        if (count < most) playing[count++] = item;
    }
};

// The step that chooses the next pose: given the rig and how long a step is, it moves on and says what plays. The
// state machine below is the first; motion matching (a pose found nearest to where the character is going) and
// learned controllers implement the same, and any clip at any time is theirs to choose. A stage's state is part of
// the character's: it saves, restores and fingerprints with it.
template <typename Stage>
concept PoseStage = requires(Stage& stage, const Stage& held, const Rig& rig, float seconds, Choice& choice, Image& image, Reading& reading) {
    stage.advance(rig, seconds, choice);
    held.save(image);
    { stage.restore(reading) } -> std::same_as<bool>;
};

// A layer on top of what the stage chose: a clip at its own time, mixed in (by a weight a joint) or added (its
// difference from its own first moment).
struct Layer {
    std::uint32_t                clip = 0;
    float                        weight = 0;  // 0: no layer
    float                        speed = 1;
    bool                         loop = true;
    bool                         additive = false;
    std::optional<std::uint32_t> mask;  // the joint whose subtree it plays on; none: every joint
    float                        time = 0;
};

// The pose a character is in, made each step from what its stage chose and its layers, and where root motion has
// taken it.
class Player {
public:
    static constexpr std::size_t layers = 4;

    explicit Player(std::shared_ptr<const Rig> rig);

    // Move on by `seconds`: the layers' times; the pose made of `choice` and the layers; root motion applied to
    // where the character is.
    void apply(const Choice& choice, float seconds);
    // A layer set (its weight 0: taken away); refused: a clip or joint the rig has not, a layer past the last.
    std::expected<void, Error> layer(std::size_t index, const Layer& layer);
    [[nodiscard]] const std::array<Layer, layers>& layered() const noexcept { return layers_; }

    [[nodiscard]] const Rig& rig() const noexcept { return *rig_; }
    [[nodiscard]] std::span<const Transform> pose() const noexcept { return pose_; }
    [[nodiscard]] std::span<const Mat4> model() const noexcept { return model_; }
    [[nodiscard]] std::span<const Mat4> skin() const noexcept { return skin_; }
    // Where the character stands: moved and turned by root motion (and by its owner).
    [[nodiscard]] const Motion& placed() const noexcept { return placed_; }
    void place(const Motion& at) noexcept { placed_ = at; }
    // A joint in the world: the character's place, then the joint in the model's space.
    [[nodiscard]] Mat4 world(std::uint32_t joint) const noexcept;
    [[nodiscard]] const Choice& chosen() const noexcept { return chosen_; }

    void save(Image& image) const;
    // The pose made again from what was restored: the same bits as when it was saved.
    [[nodiscard]] bool restore(Reading& reading);

private:
    void evaluate();
    void prepare(std::size_t layer);

    std::shared_ptr<const Rig> rig_;
    std::array<Layer, layers>  layers_{};
    std::array<std::vector<float>, layers> masks_;  // a weight a joint, or none: every joint
    std::array<Pose, layers>   references_;         // an added layer's clip at its start
    Choice                     chosen_;
    Motion                     placed_;
    Pose                       pose_, scratch_, other_;
    std::vector<float>         weights_;
    std::vector<Mat4>          model_, skin_;
};

// ── the state machine ────────────────────────────────────────────────────

struct State {
    std::string   name;
    std::uint32_t clip = 0;
    float         speed = 1;
    bool          loop = true;
};
// What lets a transition happen: a parameter above or below a value, a trigger set, the state's clip played through.
struct Condition {
    enum class Kind : std::uint8_t { above, below, trigger, ended } kind = Kind::above;
    std::uint32_t parameter = 0;
    float         value = 0;
};
inline constexpr std::uint32_t any_state = 0xffffffffu;
struct Transition {
    std::uint32_t          from = any_state;  // a state, or any (not into itself)
    std::uint32_t          to = 0;
    float                  fade = 0.2f;  // seconds the next state fades in over
    Ease                   ease = ease::smoothstep;
    std::vector<Condition> when;  // every one
};
struct Machine {
    std::vector<State>       states;
    std::vector<Transition>  transitions;  // the first whose conditions hold, in order
    std::vector<std::string> parameters;   // a trigger is a parameter set to 1 and cleared when a transition takes it
    std::uint32_t            initial = 0;
    // Refused: a clip the rig has not, a state, parameter or transition that names what is not there.
    [[nodiscard]] std::expected<void, Error> check(const Rig& rig) const;
    [[nodiscard]] std::optional<std::uint32_t> state(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> parameter(std::string_view name) const noexcept;
};

// The state machine as a stage: one state plays; a transition fades the next one in over its time and the ones
// before out, keeping their shares (a fade interrupted carries on from where it was, no jump).
class StateMachine {
public:
    explicit StateMachine(std::shared_ptr<const Machine> machine);

    void advance(const Rig& rig, float seconds, Choice& choice);
    void save(Image& image) const;
    [[nodiscard]] bool restore(Reading& reading);

    // A parameter's value (a trigger: 1 sets it); refused: a name the machine has not.
    std::expected<void, Error> set(std::string_view parameter, float value);
    // To a state now, fading over `fade` seconds (an agent's or a game's own choice: no condition asked).
    std::expected<void, Error> go(std::string_view state, float fade = 0.2f);

    [[nodiscard]] const Machine& machine() const noexcept { return *machine_; }
    [[nodiscard]] std::uint32_t current() const noexcept { return sources_[target_].state; }
    [[nodiscard]] std::span<const float> parameters() const noexcept { return values_; }
    // Seconds the current state has played, and how far its fade in is (1: done).
    [[nodiscard]] float played() const noexcept { return sources_[target_].played; }
    [[nodiscard]] float faded() const noexcept;
    // The last transition taken (its index; any_state: none yet, or `go`), and the step count when.
    struct Taken {
        std::uint32_t transition = any_state, from = any_state, to = any_state;
        std::uint64_t step = 0;
    };
    [[nodiscard]] const Taken& last() const noexcept { return last_; }

private:
    struct Source {
        std::uint32_t state = 0;
        float         time = 0, before = 0, played = 0;
        std::uint32_t laps = 0;
        float         share = 0;  // of the weight that is not the target's, when the fade began
    };
    void start(std::uint32_t state, float fade, Ease ease);

    std::shared_ptr<const Machine> machine_;
    std::vector<float>             values_;
    std::array<Source, Choice::most> sources_{};
    std::uint32_t                  count_ = 0, target_ = 0;
    float                          fade_ = 0, elapsed_ = 0;
    Ease                           ease_ = ease::smoothstep;
    std::uint64_t                  steps_ = 0;
    Taken                          last_;
};

// A character: a stage and a player.
template <PoseStage Stage>
struct Actor {
    Stage  stage;
    Player player;
    Actor(std::shared_ptr<const Rig> rig, Stage chooser) : stage(std::move(chooser)), player(std::move(rig)) {}
    void step(float seconds) {
        Choice choice;
        stage.advance(player.rig(), seconds, choice);
        player.apply(choice, seconds);
    }
    void save(Image& image) const {
        stage.save(image);
        player.save(image);
    }
    [[nodiscard]] bool restore(Reading& reading) { return stage.restore(reading) && player.restore(reading); }
};
using Character = Actor<StateMachine>;

// Every actor stepped, on `workers` threads (0 or 1, or the Web, built without threads: this one): each actor is
// its own, so the bits are the same for any number.
template <PoseStage Stage>
void step_all(std::span<Actor<Stage>> actors, float seconds, unsigned workers = 0) {
    if (workers <= 1 || actors.size() < 2 || mcpplibs::os::is_emscripten) {
        for (auto& actor : actors) actor.step(seconds);
        return;
    }
    const std::size_t share = (actors.size() + workers - 1) / workers;
    std::vector<std::jthread> threads;
    threads.reserve(workers);
    for (std::size_t first = 0; first < actors.size(); first += share)
        threads.emplace_back([=] {
            for (std::size_t index = first; index < std::min(actors.size(), first + share); ++index) actors[index].step(seconds);
        });
}

// Characters of one rig stepped together at a fixed step: what the world contract and an agent's subject hold.
class Crowd {
public:
    Crowd(std::shared_ptr<const Rig> rig, std::shared_ptr<const Machine> machine, float step = 1.0f / 60, unsigned workers = 0);
    // A character added in the machine's first state, standing at `at`; its index.
    std::uint32_t add(const Motion& at = {});
    void step();
    [[nodiscard]] std::uint64_t steps() const noexcept { return steps_; }
    [[nodiscard]] float seconds() const noexcept { return step_; }
    void workers(unsigned count) noexcept { workers_ = count; }
    [[nodiscard]] std::span<Character> characters() noexcept { return characters_; }
    [[nodiscard]] std::span<const Character> characters() const noexcept { return characters_; }
    [[nodiscard]] const std::shared_ptr<const Rig>& rig() const noexcept { return rig_; }
    [[nodiscard]] const std::shared_ptr<const Machine>& machine() const noexcept { return machine_; }

    [[nodiscard]] std::vector<std::uint8_t> save() const;
    [[nodiscard]] std::expected<void, Error> restore(std::span<const std::uint8_t> image);
    [[nodiscard]] std::uint64_t state_hash() const;

private:
    std::shared_ptr<const Rig>     rig_;
    std::shared_ptr<const Machine> machine_;
    float                          step_;
    unsigned                       workers_;
    std::uint64_t                  steps_ = 0;
    std::vector<Character>         characters_;
};

}  // namespace hanim::skeleton
