// The matter of Kiln's world as a subject beside the game (kiln::Causes; the foundation plan's §6.10.1, T2.13d):
// Mui throws a bomb, and an agent that is shown everything asks of a grain of sand why it is where it is — it
// landed, thrown by pressure, where stone had shattered by a law, under pressure the player applied. The game with the record beside it is the same game; going
// back in the game's time takes the record back; a person's tier has no such subject.
import std;
import mlibs.agent;
import mlibs.json;
import openas;
import omui.showcase.kiln.headless;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;

namespace {

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error, text;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        const auto kind = parsed->root()["kind"].text();
        if (kind == "error") out.error = parsed->root()["code"].text().value_or(""), out.text = record;
        if (kind == "data" && !out.data) out.data.emplace(std::move(*parsed)), out.text = record;
    });
    return out;
}
Answered told(agent::SubjectSession& session, std::string_view line, std::source_location at = std::source_location::current()) {
    Answered out = ask(session, line);
    expect(fatal(out.data.has_value() && out.error.empty()), at) << line << " → " << out.text;
    return out;
}

// The game played by its owner's input (a recording, a test), shown everything or as a person.
xengine::runtime::Options options(bool everything) {
    xengine::runtime::Options out{.stepping = xengine::runtime::Stepping::agent, .seat = xengine::runtime::Seat::owner, .keep = 900};
    if (everything) out.tier = {.name = "assist", .reaction_steps = 0, .actions_per_second = 0, .full_view = true};
    return out;
}
kiln::State cavern() {
    auto state = kiln::start(kiln::level::first());
    expect(fatal(state.has_value())) << (state ? "" : state.error());
    return std::move(*state);
}
// Mui chooses the bomb and throws it at the floor a little ahead of her; then nothing for `after` frames.
void bomb(kiln::Standalone& game, int after) {
    const auto press = [&](int button, std::int16_t x = -1, std::int16_t y = -1) {
        xengine::Actions actions{.held = 1u << button, .pressed = 1u << button};
        actions.pointer_x = x, actions.pointer_y = y;
        game.step({.player = actions});
        game.step({});
    };
    press(kiln::next_spell_button), press(kiln::next_spell_button);
    expect(fatal(game.state().cat.spell == kiln::Spell::bomb));
    const auto shape = game.state().world->shape();
    const auto to = [](float cell, std::uint32_t cells) { return static_cast<std::int16_t>(std::clamp(cell / static_cast<float>(cells), 0.0f, 1.0f) * 32767.0f); };
    press(kiln::cast_button, to(game.state().cat.x + 12.0f, shape.width), to(game.state().cat.y + 1.0f, shape.height));
    for (int frame = 0; frame < after; ++frame) game.step({});
}

}  // namespace

UTest cases{"kiln.causes", [] {
    "the game says what its controls mean and what to begin with; its tasks, sent as written, do what they say (D-186)"_test = [] {
        kiln::Standalone game(cavern(), {.keep = 600});  // an agent in the seat, on a person's tier
        agent::SubjectSession session({&game});
        const auto state = told(session, R"({"op":"world.state"})");
        expect(eq(state["controls"].size(), std::size_t{6}) && state["controls"][2].dump().contains(R"("name":"jump")") &&
               state["controls"][2]["act"].dump() == R"({"buttons":[0]})")
            << state["controls"].dump();
        const auto found = told(session, R"({"op":"find","q":"swim up in water"})");
        expect(found["found"][0]["kind"].text() == "control" && found["found"][0]["name"].text() == "jump") << found.text;

        const auto described = told(session, R"({"op":"describe","subject":"world"})");
        const auto walk = described["tasks"][0], cast = described["tasks"][1];
        expect(walk["name"].text() == "walk-right-and-jump" && cast["name"].text() == "cast-a-spell") << described["tasks"].dump();
        const float from = game.state().cat.x;
        for (std::size_t index = 0; index < walk["steps"].size(); ++index) told(session, walk["steps"][index].dump());
        expect(game.state().cat.x > from + 5.0f) << "she walked right: " << from << " → " << game.state().cat.x;
        expect(cast["steps"][0].dump() == R"({"op":"world.act","buttons":[1],"pointer":[0.9,0.5]})") << cast["steps"][0].dump();
        const std::int32_t mana = game.state().cat.mana;
        for (std::size_t index = 0; index < cast["steps"].size(); ++index) told(session, cast["steps"][index].dump());
        expect(eq(game.state().casts, 1u) && game.state().cat.mana < mana) << "one spell, paid for";
    };

    "Mui throws a bomb; of a grain of the sand it left, an agent shown everything is told: it landed, thrown by pressure, where stone had shattered by the law — under pressure the player applied"_test = [] {
        kiln::Standalone game(cavern(), options(true));
        kiln::Causes causes(game);
        agent::SubjectSession session(kiln::subjects(game, &causes), {.actor = {openas::Kind::agent, "the one who asks", {}}});
        for (int frame = 0; frame < 30; ++frame) game.step({});
        const std::int64_t before = game.frame();
        bomb(game, 400);
        // A bomb is pressure and heat: both on record as applied by the player; stone shattered by the law.
        const Answered applied = told(session, R"({"op":"matter.events","name":"applied"})");
        expect(fatal(applied["events"].size() >= 2u)) << applied.text;
        const std::int64_t bomb_at = applied["events"][0]["frame"].integer().value_or(-1);
        expect(applied["events"][0]["by"]["name"].text() == "player" && applied["events"][0]["by"]["kind"].text() == "person" && bomb_at >= before);
        expect(told(session, R"({"op":"matter.events","name":"shatter"})")["events"].size() > 10u) << "the wall's stone broke by the law, not by the game";
        expect(eq(told(session, R"({"op":"matter.events","name":"blast"})")["events"].size(), std::size_t{0})) << "the game made no blast of its own";
        // A grain of sand that the blast threw, where it now rests.
        const auto& world = *game.state().world;
        const auto shape = world.shape();
        const std::uint32_t sand = *world.program().material("sand");
        std::optional<Answered> why;
        std::size_t asked = 0;
        for (std::uint32_t cell = 0; cell < shape.width * shape.height && !why; ++cell) {
            if (world.cells()[cell] != sand) continue;
            Answered answer = told(session, std::format(R"({{"op":"matter.explain","cell":[{},{}],"depth":3}})", cell % shape.width, cell / shape.width));
            ++asked;
            if (answer["records"].size() && answer["records"][0]["what"].text() == "landed") why = std::move(answer);
        }
        expect(fatal(why.has_value())) << std::format("of {} cells of sand asked, none says it landed", asked);
        std::println("{}", why->text);
        const auto landing = (*why)["records"][0];
        expect(landing["material"].text() == "sand" && landing["thrown_at"].integer().value_or(-1) > bomb_at);
        const auto throwing = landing["because"];
        expect(throwing["what"].text() == "thrown" && throwing["by"].text() == "pressure");
        // Why there was sand under pressure there: it shattered — the law, what it read, and whose pressure it was.
        const auto cause = throwing["because"];
        expect(cause["what"].text() == "shatter" && cause["from"].text() == "stone" && cause["to"].text() == "sand" && cause["law"]["name"].text() == "shatters") << why->text;
        expect(cause["applied"]["channel"].text() == "pressure" && cause["applied"]["by"]["name"].text() == "player") << "and the pressure was the player's";

        // Back in the game's time to before the bomb: the record goes back with it, and the game goes on from there.
        expect(fatal(game.seek(before)));
        expect(eq(told(session, R"({"op":"matter.events","name":"shatter"})")["events"].size(), std::size_t{0})) << "what happened after that frame is another branch's";
        for (int frame = 0; frame < 60; ++frame) game.step({});
        expect(eq(told(session, R"({"op":"matter.events","name":"shatter"})")["events"].size(), std::size_t{0})) << "and in this branch no bomb was thrown";
    };

    "the game with its causes on record is the same game: the same checksum each frame as one with no record beside it"_test = [] {
        kiln::Standalone plain(cavern(), options(true)), watched(cavern(), options(true));
        kiln::Causes causes(watched);
        agent::SubjectSession session(kiln::subjects(watched, &causes));
        for (int frame = 0; frame < 20; ++frame) plain.step({}), watched.step({});
        bomb(plain, 0), bomb(watched, 0);
        for (int frame = 0; frame < 200; ++frame) {
            plain.step({}), watched.step({});
            if (frame == 100) told(session, R"({"op":"matter.observe"})"), told(session, R"({"op":"matter.branch","steps":30})");
            if (plain.state_hash() != watched.state_hash()) {
                expect(false) << std::format("frame {}: the game asked about parts from the other", watched.frame());
                break;
            }
        }
        expect(eq(plain.frame(), watched.frame()));
    };

    "a person's tier is shown what a person is: its session has the game and no matter subject"_test = [] {
        kiln::Standalone game(cavern(), options(false));
        kiln::Causes causes(game);
        expect(eq(kiln::subjects(game, &causes).size(), std::size_t{1}));
        agent::SubjectSession session(kiln::subjects(game, &causes));
        const Answered refused = ask(session, R"({"op":"matter.explain","cell":[10,10]})");
        expect(!refused.data.has_value() && !refused.error.empty()) << refused.text;
        kiln::Standalone open(cavern(), options(true));
        kiln::Causes open_causes(open);
        expect(eq(kiln::subjects(open, &open_causes).size(), std::size_t{2}));
    };
}};
