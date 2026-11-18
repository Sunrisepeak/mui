module omui.showcase.kiln.window;

import std;

namespace kiln::window {

namespace {

using omui::Key;
constexpr std::int32_t sound_lead = 2048;  // frames the output is kept ahead (43 ms at 48 kHz)

meng::Bindings bindings() {
    meng::Bindings made;
    for (const Key left : {Key::a, Key::left}) made.key(meng::ui::code(left), meng::Axis{0, -1});
    for (const Key right : {Key::d, Key::right}) made.key(meng::ui::code(right), meng::Axis{0, 1});
    for (const Key up : {Key::w, Key::up, Key::space}) made.key(meng::ui::code(up), meng::Button{jump_button});
    made.key(meng::ui::code(Key::q), meng::Button{next_spell_button});
    made.mouse(1, meng::Button{cast_button});
    made.mouse(2, meng::Button{next_spell_button});
    // A gamepad: the left stick (or the d-pad) walks, A jumps, the right trigger or R1 casts, Y the next spell.
    using omui::GamepadAxis, omui::GamepadButton;
    made.pad_axis(meng::ui::pad(GamepadAxis::left_x), meng::Axis{0, 1});
    made.pad_button(meng::ui::pad(GamepadButton::dpad_left), meng::Axis{0, -1});
    made.pad_button(meng::ui::pad(GamepadButton::dpad_right), meng::Axis{0, 1});
    made.pad_button(meng::ui::pad(GamepadButton::south), meng::Button{jump_button});
    made.pad_button(meng::ui::pad(GamepadButton::right_trigger), meng::Button{cast_button});
    made.pad_button(meng::ui::pad(GamepadButton::right_shoulder), meng::Button{cast_button});
    made.pad_button(meng::ui::pad(GamepadButton::north), meng::Button{next_spell_button});
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

Cavern::Cavern() : sampler_(bindings()) {}

Cavern::~Cavern() {
    pacer_.stop();
    output_.reset();  // the output's thread stops before what it drains goes
    view_.clear();
}

void Cavern::mount(omui::Services& services, omui::NodeId node) {
    services_ = &services, node_ = node;
    view_.mount(services, node);
    const Level level = level::named(options().level).value_or(level::first());
    auto started = start(level);
    if (!started) {
        hud()->said.set(std::format("The cavern could not be made: {}", started.error()));
        return;
    }
    game_.emplace(std::move(*started), meng::runtime::Options{.stepping = meng::runtime::Stepping::owner, .seat = meng::runtime::Seat::owner, .keep = 600, .record = true});
    if (auto made = Picture::make(images_, level)) picture_.emplace(std::move(*made));
    const auto& shape = game_->state().world->shape();
    frame_ = xrender::raster::Surface(static_cast<int>(shape.width) * scale, static_cast<int>(shape.height) * scale);
    view_.show({.label = "the cavern",
                .subject = &*game_,
                .pixels = [this]() -> std::optional<xrender::raster::ImageView> {
                    if (!picture_) return std::nullopt;
                    return frame_.view();
                },
                .width = static_cast<float>(frame_.width()),
                .height = static_cast<float>(frame_.height()),
                .pixels_stay = true});
    if (const Hearing hearing = options().hearing; hearing != Hearing::none) {
        if (auto made = Sound::make()) {
            sound_.emplace(std::move(*made));
            feed_ = std::make_unique<meng::audio::Feed>(sound_->mixer().rate());
            feed_->fill(sound_->mixer(), sound_lead);
            if (auto opened = meng::audio::Device::open(*feed_, {.null = hearing == Hearing::silent})) output_.emplace(std::move(*opened));
            else std::println(std::cerr, "kiln: no sound ({})", opened.error());
        }
    }
    services.focus(node);
    draw();
    pacer_.start(services, steps_per_second, [this](std::int32_t steps) { run(steps); });
}

bool Cavern::on_event(omui::EventContext&, const omui::InputEvent& event) {
    return services_ && meng::ui::feed(sampler_, event, services_->bounds(node_));
}

void Cavern::run(std::int32_t steps) {
    if (!game_) return;
    while (steps-- > 0 && !Play::over(game_->state())) {
        Input input;
        input.player = sampler_.sample();
        game_->step(input);
        if (sound_) sounds_begun_ += sound_->hear(game_->state());
    }
    if (sound_ && output_) feed_->fill(sound_->mixer(), sound_lead);
    const State& state = game_->state();
    const auto& shown = hud();
    shown->health.set(std::format("Health {}", std::max(0, state.cat.health)));
    shown->mana.set(std::format("Mana {}", std::max(0, state.cat.mana)));
    shown->spell.set(state.cat.spell == Spell::bomb ? std::format("Bomb ({} left)", state.cat.bombs) : std::format("{}", spell_names[static_cast<std::size_t>(state.cat.spell)]));
    if (state.outcome != Outcome::playing) shown->said.set(state.outcome == Outcome::won ? "Out of the cavern" : "Mui is hurt too much");
    draw();
}

void Cavern::draw() {
    if (!game_ || !picture_) return;
    picture_->show(game_->state());
    list_.clear();
    picture_->draw(list_, {0, 0, static_cast<float>(frame_.width()), static_cast<float>(frame_.height())});
    (void)renderer_.render(list_, frame_, {&images_, nullptr});
    view_.changed();
}

omui::AppSpec app(Options given) {
    const std::filesystem::path record = given.record;
    options() = std::move(given);
    omui::AppSpec made;
    made.id = "org.openmui.kiln";
    made.title = "Kiln";
    made.size = {1100, 560};
    made.theme = omui::Theme::dark_theme();
    made.setup = [](omui::Runtime&) { omui::register_type(omui::component_type<Cavern>()); };
    made.view = [] {
        omui::Element cave;
        cave.type = &omui::component_type<Cavern>();
        cave.id = "cavern";
        const auto& shown = hud();
        const omui::Color light = omui::Color::hex(0xf2e6d8);
        return omui::column({.id = "kiln", .gap = 0},
                            {std::move(cave), omui::row({.id = "hud", .padding = 10, .gap = 24.0f},
                                                        {omui::text({.id = "hud.health", .text = shown->health, .style = "body", .color = light}),
                                                         omui::text({.id = "hud.mana", .text = shown->mana, .style = "body", .color = light}),
                                                         omui::text({.id = "hud.spell", .text = shown->spell, .style = "body", .color = light}),
                                                         omui::text({.id = "hud.said", .text = shown->said, .style = "caption", .color = omui::Color::hex(0xd9cbbb)})})});
    };
    made.teardown = [record](omui::Runtime& runtime) {
        if (record.empty()) return;
        const auto* cavern = runtime.component<Cavern>(runtime.find("main/cavern"));
        if (!cavern || !cavern->game()) return;
        std::ofstream file(record, std::ios::binary);
        file << recorded(*cavern->game());
    };
    return made;
}

std::string recorded(const Standalone& game) {
    std::vector<meng::Actions> frames;
    for (const Input& step : game.recorded()) frames.push_back(step.player);
    return meng::to_recording(std::format("level {}", options().level), frames);
}

std::expected<State, std::string> replayed(std::string_view lines) {
    const auto recording = meng::from_recording(lines);
    if (!recording) return std::unexpected(recording.error());
    if (!recording->heading.starts_with("level ")) return std::unexpected(std::string("line 1: `level NAME`, the level it was played on"));
    const std::string_view name = std::string_view(recording->heading).substr(6);
    const auto level = level::named(name);
    if (!level) return std::unexpected(std::format("line 1: no level is named `{}` (first, flawed)", name));
    auto state = start(*level);
    if (!state) return std::unexpected(state.error());
    for (const meng::Actions& actions : recording->frames) step(*state, Input{actions});
    return std::move(*state);
}

}  // namespace kiln::window
