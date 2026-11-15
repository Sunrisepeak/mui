module omui.showcase.quay.window;

import std;
import mlibs.rhi;
import xrender.raster;

namespace quay::window {

namespace {

using omui::Key;

constexpr double turn_rate = 2.6, look_rate = 1.8;    // radians a second at a full axis (the sim's)
constexpr double look_per_pixel = 0.0022;              // radians a logical pixel of the hand's movement
constexpr int    cpu_width = 320, cpu_height = 180;    // the reference's picture where the host has no device
constexpr std::int32_t sound_lead = 2048;              // frames the output is kept ahead (43 ms at 48 kHz): a frame's wait and some

xengine::Bindings bindings() {
    xengine::Bindings made;
    made.key(xengine::ui::code(Key::w), xengine::Axis{1, 1});
    made.key(xengine::ui::code(Key::s), xengine::Axis{1, -1});
    made.key(xengine::ui::code(Key::d), xengine::Axis{0, 1});
    made.key(xengine::ui::code(Key::a), xengine::Axis{0, -1});
    made.key(xengine::ui::code(Key::space), xengine::Button{jump_button});
    made.key(xengine::ui::code(Key::e), xengine::Button{use_button});
    made.key(xengine::ui::code(Key::shift), xengine::Button{run_button});
    made.mouse(1, xengine::Button{fire_button});
    // A gamepad: the left stick walks (up is forward), the right stick looks — given with the mouse's look in
    // `input` — the right trigger or R1 fires, A jumps, X works the beacon or a fuse box, the left stick pressed runs.
    using omui::GamepadAxis, omui::GamepadButton;
    made.pad_axis(xengine::ui::pad(GamepadAxis::left_x), xengine::Axis{0, 1});
    made.pad_axis(xengine::ui::pad(GamepadAxis::left_y), xengine::Axis{1, -1});
    made.pad_axis(xengine::ui::pad(GamepadAxis::right_x), xengine::Axis{2, 1});
    made.pad_axis(xengine::ui::pad(GamepadAxis::right_y), xengine::Axis{3, -1});
    made.pad_button(xengine::ui::pad(GamepadButton::right_trigger), xengine::Button{fire_button});
    made.pad_button(xengine::ui::pad(GamepadButton::right_shoulder), xengine::Button{fire_button});
    made.pad_button(xengine::ui::pad(GamepadButton::south), xengine::Button{jump_button});
    made.pad_button(xengine::ui::pad(GamepadButton::west), xengine::Button{use_button});
    made.pad_button(xengine::ui::pad(GamepadButton::left_stick), xengine::Button{run_button});
    return made;
}

std::int16_t axis_of(double value) { return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0, 1.0) * 32767)); }

}  // namespace

const std::shared_ptr<Hud>& hud() {
    static const std::shared_ptr<Hud> made = std::make_shared<Hud>();
    return made;
}

Options& options() {
    static Options made;
    return made;
}

Harbour::Harbour() : sampler_(bindings()) {}

Harbour::~Harbour() {
    pacer_.stop();
    output_.reset();  // the output's thread stops before what it drains goes
    view_.clear();
    on_device_.reset();  // before the host's device goes
}

void Harbour::mount(omui::Services& services, omui::NodeId node) {
    services_ = &services, node_ = node;
    view_.mount(services, node);
    const Level played = level::named(options().level).value_or(level::harbour());
    game_.emplace(start(played), xengine::runtime::Options{.stepping = xengine::runtime::Stepping::owner, .seat = xengine::runtime::Seat::owner, .keep = 600, .record = true});
    if (auto made = picture::Picture::make(played)) picture_.emplace(std::move(*made));
    xengine::ui::Viewport::Shown shown{.label = "the harbour", .subject = &*game_, .width = 960, .height = 540, .look = true};
    if (mlibs::rhi::Device* device = services.device(); device && picture_) {
        if (auto made = picture::OnDevice::make(*device, *picture_, 960, 540)) {  // sized to the node at each frame
            on_device_.emplace(std::move(*made));
            shown.texture = [this]() -> std::optional<mlibs::rhi::Texture> { return on_device_ ? std::optional(on_device_->colour()) : std::nullopt; };
        }
    }
    if (!on_device_)
        shown.pixels = [this]() -> std::optional<xrender::raster::ImageView> {
            if (!picture_ || picture_->shown().empty()) return std::nullopt;
            return xrender::raster::ImageView{reinterpret_cast<const xrender::raster::Rgba8*>(picture_->shown().data()), cpu_width, cpu_height, cpu_width};
        };
    view_.show(std::move(shown));
    if (const Hearing hearing = options().hearing; hearing != Hearing::none) {
        if (auto made = Sound::make(game_->state())) {
            sound_.emplace(std::move(*made));
            feed_ = std::make_unique<xengine::audio::Feed>(sound_->mixer().rate());
            feed_->fill(sound_->mixer(), sound_lead);  // ahead before the output starts
            if (auto opened = xengine::audio::Device::open(*feed_, {.null = hearing == Hearing::silent})) output_.emplace(std::move(*opened));
            else std::println(std::cerr, "quay: no sound ({})", opened.error());
        }
    }
    services.focus(node);
    draw();
    pacer_.start(services, steps_per_second, [this](std::int32_t steps) { run(steps); });
}

bool Harbour::on_event(omui::EventContext& ctx, const omui::InputEvent& event) {
    const bool looked = view_.on_event(ctx, event);
    const bool played = services_ && xengine::ui::feed(sampler_, event, services_->bounds(node_));
    return looked || played;
}

// A step's input: the keys, buttons and sticks as sampled; the mouse's look given out a step at a time, at most
// what a full axis turns in a step with the right stick's, the rest kept for the next.
Input Harbour::input() {
    Input made;
    made.player = sampler_.sample();
    const double stick_turn = static_cast<double>(made.player.axes[2]) / 32767.0, stick_look = static_cast<double>(made.player.axes[3]) / 32767.0;
    const omui::Point moved = view_.take_look();
    turning_ += static_cast<double>(moved.x) * look_per_pixel, looking_ -= static_cast<double>(moved.y) * look_per_pixel;
    const double most_turn = turn_rate / steps_per_second, most_look = look_rate / steps_per_second;
    made.player.axes[2] = axis_of(turning_ / most_turn + stick_turn), made.player.axes[3] = axis_of(looking_ / most_look + stick_look);
    // What the axes give the mouse's look, as the sim reads them back: so nothing is lost or given twice to rounding.
    turning_ -= (static_cast<double>(made.player.axes[2]) / 32767.0 - stick_turn) * most_turn, looking_ -= (static_cast<double>(made.player.axes[3]) / 32767.0 - stick_look) * most_look;
    return made;
}

void Harbour::run(std::int32_t steps) {
    if (!game_) return;
    while (steps-- > 0 && !Play::over(game_->state())) {
        game_->step(input());
        if (sound_) sounds_begun_ += sound_->hear(game_->state());
    }
    if (sound_ && output_) feed_->fill(sound_->mixer(), sound_lead);
    const State& state = game_->state();
    const auto& shown = hud();
    shown->health.set(std::format("Health {}", std::max(0, state.keeper.health)));
    shown->rounds.set(std::format("Rounds {}", state.keeper.rounds));
    shown->tide.set(std::format("Tide {:.0f} s", std::max(0.0f, state.tide)));
    if (state.outcome != Outcome::playing) shown->said.set(std::format("{} — {}", state.outcome == Outcome::won ? "Lit" : "Lost", state.why));
    draw();
}

void Harbour::draw() {
    if (!game_ || !picture_) return;
    const State& state = game_->state();
    if (on_device_) {
        // The node's own pixels: no more drawn than are shown, none stretched.
        const omui::Rect box = services_->bounds(node_);
        const float scale = services_->pixel_scale();
        const auto across = static_cast<std::uint32_t>(std::max(1L, std::lround(box.width * scale))), down = static_cast<std::uint32_t>(std::max(1L, std::lround(box.height * scale)));
        if (box.width > 0 && box.height > 0 && (across != on_device_->width() || down != on_device_->height())) (void)on_device_->resize(across, down);
        picture_->prepare(state);
        list_.clear();
        if (on_device_->record(list_, *picture_)) (void)services_->device()->submit(list_);  // ahead of the host's frame on the same device
    } else {
        picture_->draw(state, {.width = cpu_width, .height = cpu_height, .shadows = false});
    }
    view_.changed();
}

omui::AppSpec app(Options given) {
    options() = given;
    omui::AppSpec made;
    made.id = "org.openmui.quay";
    made.title = "Quay";
    made.size = {1280, 760};
    made.theme = omui::Theme::dark_theme();
    made.setup = [](omui::Runtime&) { omui::register_type(omui::component_type<Harbour>()); };
    made.view = [] {
        omui::Element world;
        world.type = &omui::component_type<Harbour>();
        world.id = "harbour";
        const auto& shown = hud();
        const omui::Color light = omui::Color::hex(0xdfe4f2);
        return omui::column({.width = omui::Dim(mlibs::layout::pct(100)), .height = omui::Dim(mlibs::layout::pct(100)), .gap = 6.0f, .background = omui::Color::hex(0x05070d)},
                            {std::move(world),
                             omui::row({.id = "hud", .padding = 10, .gap = 24.0f},
                                       {omui::text({.id = "hud.health", .text = shown->health, .style = "body", .color = light}),
                                        omui::text({.id = "hud.rounds", .text = shown->rounds, .style = "body", .color = light}),
                                        omui::text({.id = "hud.tide", .text = shown->tide, .style = "body", .color = light}),
                                        omui::text({.id = "hud.said", .text = shown->said, .style = "caption", .color = omui::Color::hex(0xc7cde0)})})});
    };
    // Each step's input, when the window closes: what plays the same game with no window.
    made.teardown = [record = std::move(given.record)](omui::Runtime& runtime) {
        if (record.empty()) return;
        const auto* harbour = runtime.component<Harbour>(runtime.find("main/harbour"));
        if (!harbour || !harbour->game()) return;
        std::ofstream file(record, std::ios::binary);
        file << recorded(*harbour->game());
    };
    return made;
}

std::string recorded(const Standalone& game) {
    std::string out;
    if (game.state().level != "harbour" && !game.state().level.empty()) out = std::format("level {}\n", game.state().level);
    for (const Input& step : game.recorded()) {
        const xengine::Actions& actions = step.player;
        out += std::format("{} {} {} {} {} {} {} {}\n", actions.held, actions.pressed, actions.released, actions.axes[0], actions.axes[1], actions.axes[2], actions.axes[3], actions.wheel);
    }
    return out;
}

std::expected<State, std::string> replayed(std::string_view lines) {
    // The level a first line names; else the harbour.
    std::optional<Level> level = level::harbour();
    std::size_t number = 0;  // lines read
    if (lines.starts_with("level ")) {
        number = 1;
        const std::size_t end = lines.find('\n');
        const std::string_view name = lines.substr(6, end == std::string_view::npos ? std::string_view::npos : end - 6);
        level = level::named(name);
        if (!level) return std::unexpected(std::format("line 1: no level is named `{}` (harbour, flawed, second-night)", name));
        lines = end == std::string_view::npos ? std::string_view{} : lines.substr(end + 1);
    }
    State state = start(*level);
    for (const auto line : std::views::split(lines, '\n')) {
        const std::string_view text(line.begin(), line.end());
        if (text.empty()) continue;
        ++number;
        std::array<std::int64_t, 8> values{};
        const char* at = text.data();
        const char* const end = text.data() + text.size();
        for (std::int64_t& value : values) {
            while (at < end && *at == ' ') ++at;
            const auto [next, failed] = std::from_chars(at, end, value);
            if (failed != std::errc{}) return std::unexpected(std::format("line {}: eight whole numbers (held, pressed, released, four axes, wheel)", number));
            at = next;
        }
        Input input;
        input.player.held = static_cast<std::uint32_t>(values[0]), input.player.pressed = static_cast<std::uint32_t>(values[1]), input.player.released = static_cast<std::uint32_t>(values[2]);
        for (std::size_t axis = 0; axis < 4; ++axis) input.player.axes[axis] = static_cast<std::int16_t>(values[3 + axis]);
        input.player.wheel = static_cast<std::int16_t>(values[7]);
        step(state, input);
    }
    return state;
}

}  // namespace quay::window
