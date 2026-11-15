module omui.showcase.swarm.window;

import std;

namespace swarm::window {

namespace {

using omui::Key;

xengine::Bindings bindings() {
    xengine::Bindings made;
    for (const Key left : {Key::a, Key::left}) made.key(xengine::ui::code(left), xengine::Axis{0, -1});
    for (const Key right : {Key::d, Key::right}) made.key(xengine::ui::code(right), xengine::Axis{0, 1});
    for (const Key up : {Key::w, Key::up}) made.key(xengine::ui::code(up), xengine::Axis{1, -1});
    for (const Key down : {Key::s, Key::down}) made.key(xengine::ui::code(down), xengine::Axis{1, 1});
    made.key(xengine::ui::code(Key::space), xengine::Button{shoot_button});
    made.mouse(1, xengine::Button{shoot_button});
    // A gamepad: the left stick (or the d-pad) moves, A or the right trigger shoots where the gun points.
    using omui::GamepadAxis, omui::GamepadButton;
    made.pad_axis(xengine::ui::pad(GamepadAxis::left_x), xengine::Axis{0, 1});
    made.pad_axis(xengine::ui::pad(GamepadAxis::left_y), xengine::Axis{1, 1});
    made.pad_button(xengine::ui::pad(GamepadButton::dpad_left), xengine::Axis{0, -1});
    made.pad_button(xengine::ui::pad(GamepadButton::dpad_right), xengine::Axis{0, 1});
    made.pad_button(xengine::ui::pad(GamepadButton::dpad_up), xengine::Axis{1, -1});
    made.pad_button(xengine::ui::pad(GamepadButton::dpad_down), xengine::Axis{1, 1});
    made.pad_button(xengine::ui::pad(GamepadButton::south), xengine::Button{shoot_button});
    made.pad_button(xengine::ui::pad(GamepadButton::right_trigger), xengine::Button{shoot_button});
    return made;
}

}  // namespace

const std::shared_ptr<Hud>& hud() {
    static const std::shared_ptr<Hud> made = std::make_shared<Hud>();
    return made;
}

Options& options() {
    static Options made;
    return made;
}

Arena::Arena() : sampler_(bindings()) {}

Arena::~Arena() {
    pacer_.stop();
    view_.clear();
}

void Arena::mount(omui::Services& services, omui::NodeId node) {
    services_ = &services, node_ = node;
    view_.mount(services, node);
    game_.emplace(start(options().seed), xengine::runtime::Options{.stepping = xengine::runtime::Stepping::owner, .seat = xengine::runtime::Seat::owner, .keep = 600, .record = true});
    frame_ = xrender::raster::Surface(static_cast<int>(arena_width) * scale, static_cast<int>(arena_height) * scale);
    view_.show({.label = "the arena",
                .subject = &*game_,
                .pixels = [this]() -> std::optional<xrender::raster::ImageView> { return frame_.view(); },
                .width = static_cast<float>(frame_.width()),
                .height = static_cast<float>(frame_.height()),
                .pixels_stay = true});
    services.focus(node);
    draw();
    pacer_.start(services, steps_per_second, [this](std::int32_t steps) { run(steps); });
}

bool Arena::on_event(omui::EventContext&, const omui::InputEvent& event) {
    return services_ && xengine::ui::feed(sampler_, event, services_->bounds(node_));
}

void Arena::run(std::int32_t steps) {
    if (!game_) return;
    while (steps-- > 0 && !Play::over(game_->state())) game_->step(Input{sampler_.sample()});
    const State& state = game_->state();
    const auto& shown = hud();
    shown->lives.set(std::format("Lives {}", std::max(0, state.lives)));
    shown->score.set(std::format("Score {}", state.score));
    shown->wave.set(state.wave == 0 ? std::string("The first wave comes from the east") : std::format("Wave {} from the {}", state.wave, swarm::name(state.from)));
    if (over(state)) shown->said.set(std::format("The swarm got through: {} points, wave {}", state.score, state.wave));
    draw();
}

void Arena::draw() {
    if (!game_) return;
    list_.clear();
    picture::draw(game_->state(), list_, {0, 0, static_cast<float>(frame_.width()), static_cast<float>(frame_.height())});
    (void)renderer_.render(list_, frame_, {});
    view_.changed();
}

omui::AppSpec app(Options given) {
    const std::filesystem::path record = given.record;
    options() = std::move(given);
    omui::AppSpec made;
    made.id = "org.openmui.swarm";
    made.title = "Swarm";
    made.size = {980, 600};
    made.theme = omui::Theme::dark_theme();
    made.setup = [](omui::Runtime&) { omui::register_type(omui::component_type<Arena>()); };
    made.view = [] {
        omui::Element arena;
        arena.type = &omui::component_type<Arena>();
        arena.id = "arena";
        const auto& shown = hud();
        const omui::Color light = omui::Color::hex(0xe6ecff);
        // The window's ground is the arena's floor, under the HUD too, whatever the theme.
        return omui::column({.id = "swarm", .gap = 0, .grow = 1, .background = omui::Color::hex(0x0b1020)},
                            {std::move(arena), omui::row({.id = "hud", .padding = 10, .gap = 24.0f},
                                                         {omui::text({.id = "hud.lives", .text = shown->lives, .style = "body", .color = light}),
                                                          omui::text({.id = "hud.score", .text = shown->score, .style = "body", .color = light}),
                                                          omui::text({.id = "hud.wave", .text = shown->wave, .style = "body", .color = light}),
                                                          omui::text({.id = "hud.said", .text = shown->said, .style = "caption", .color = omui::Color::hex(0xaab4d4)})})});
    };
    made.teardown = [record](omui::Runtime& runtime) {
        if (record.empty()) return;
        const auto* arena = runtime.component<Arena>(runtime.find("main/arena"));
        if (!arena || !arena->game()) return;
        std::ofstream file(record, std::ios::binary);
        file << recorded(*arena->game());
    };
    return made;
}

std::string recorded(const Standalone& game) {
    std::vector<xengine::Actions> frames;
    for (const Input& step : game.recorded()) frames.push_back(step.player);
    return std::format("seed {}\n{}", options().seed, xengine::to_text(frames));
}

std::expected<State, std::string> replayed(std::string_view lines) {
    if (!lines.starts_with("seed ")) return std::unexpected(std::string("line 1: `seed N`, the game it was played on"));
    const std::size_t end = lines.find('\n');
    const std::string_view number = lines.substr(5, end == std::string_view::npos ? std::string_view::npos : end - 5);
    std::uint64_t seed = 0;
    if (const auto read = std::from_chars(number.data(), number.data() + number.size(), seed); read.ec != std::errc{} || read.ptr != number.data() + number.size())
        return std::unexpected(std::format("line 1: `{}` is not a seed (a whole number)", number));
    State state = start(seed);
    const auto frames = xengine::from_text(end == std::string_view::npos ? std::string_view{} : lines.substr(end + 1));
    if (!frames) return std::unexpected(frames.error());
    for (const xengine::Actions& actions : *frames) step(state, Input{actions});
    return state;
}

}  // namespace swarm::window
