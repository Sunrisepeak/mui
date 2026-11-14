// What matter looks like, as data (xengine.matter.appearance, D-168): the standard pack against the standard
// program; a material the pack has never heard of; a pack written for another world; what is refused, by its
// code, pointer and message; JSON problems where they are written.
import std;
import xengine.matter.json;
import xengine.matter.appearance;
import mlibs.error;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;

namespace {

std::string file_text(std::string_view name) {
    const auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / name;
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
matter::Definition standard_definition() {
    auto read = matter::read(file_text("standard.matter.omui.json"), "standard.matter.omui.json");
    expect(fatal(read.has_value())) << (read ? "" : read.error().front().text());
    return *read;
}
matter::Program prepared(const matter::Definition& definition) {
    auto program = matter::Program::prepare(definition);
    expect(fatal(program.has_value())) << (program ? "" : program.error().where + ": " + program.error().message);
    return *program;
}
const matter::StateLook& state(const matter::Look& look, std::string_view name) {
    const auto found = std::ranges::find(look.states(), name, &matter::StateLook::name);
    expect(fatal(found != look.states().end())) << name;
    return *found;
}
constexpr std::uint32_t kelvin(std::uint32_t value) { return value << 8; }  // temperature is stored in 256ths

}  // namespace

UTest cases{"simulation.matter.appearance", [] {

    "the standard pack against the standard program: a colour a material, states along their channels in the channels' units"_test = [] {
        const auto program = matter::standard();
        expect(fatal(program.has_value()));
        expect(matter::standard_appearance_snapshot == file_text("standard.appearance.omui.json")) << "the module carries the committed snapshot";
        const auto look = matter::standard_look(*program);
        expect(fatal(look.has_value())) << (look ? "" : look.error().front().text());
        expect(eq(look->base().size(), program->materials().size()) && eq(look->grain().size(), program->materials().size()));
        expect(look->unused().empty());
        for (matter::MaterialId material = 0; material < program->materials().size(); ++material)
            expect(look->named(material)) << std::format("`{}` has no look of its own in the standard pack", program->materials()[material].name);
        const auto id = [&](std::string_view name) { return *program->material(name); };
        // "#8a5a2b" is 0xAABBGGRR 0xff2b5a8a; a grain of 0.12 is 31 of 255; water is see-through.
        expect(eq(look->base()[id("wood")], 0xff2b5a8au) && eq(look->grain()[id("wood")], std::uint8_t{31}));
        expect(eq(look->base()[id("water")], 0xb4e07f2fu));
        expect(eq(look->base()[id("air")], 0u) && eq(look->nothing(), 0u));
        expect(eq(look->states().size(), std::size_t{9}));

        // Burned, against a thing's own fuel — on what is solid, soft or granular and has fuel.
        const auto& charred = state(*look, "charred");
        expect(eq(charred.channel, *program->channel("burned")) && eq(charred.from, 0u) && !charred.emits);
        expect(eq(charred.to[id("wood")], 400u) && eq(charred.to[id("cloth")], 60u) && eq(charred.to[id("gunpowder")], 3u));
        expect(eq(charred.to[id("stone")], 0u)) << "nothing to burn: the state does not show";
        expect(eq(charred.to[id("oil")], 0u)) << "a liquid: not of the phases the state shows on";
        expect(eq(charred.color, 0xd90c1015u)) << "a mix of 0.85 is the colour's alpha, 217";
        // Temperature in kelvin, stored in 256ths; hot is hot whatever it is.
        const auto& glows = state(*look, "glows with heat");
        expect(eq(glows.channel, *program->channel("temperature")) && eq(glows.from, kelvin(800)) && glows.emits);
        expect(std::ranges::all_of(glows.to, [](std::uint32_t to) { return to == kelvin(1500); }));
        // The less, the more — and not on what flows.
        const auto& frosted = state(*look, "frosted");
        expect(eq(frosted.from, kelvin(273)) && eq(frosted.to[id("stone")], kelvin(213)) && eq(frosted.to[id("air")], kelvin(273)) &&
               eq(frosted.to[id("water")], kelvin(273)));
        expect(state(*look, "aflame").emits && eq(state(*look, "aflame").to[id("air")], 1u));
        // The air over a fire shows its heat from 420 K — and only what is a gas does.
        const auto& hot_air = state(*look, "hot air");
        expect(eq(hot_air.from, kelvin(420)) && eq(hot_air.to[id("air")], kelvin(900)) && eq(hot_air.to[id("smoke")], kelvin(900)) && eq(hot_air.to[id("wood")], kelvin(420)));

        // What an agent reads.
        const auto described = mlibs::json::Document::parse(look->describe(*program), "look");
        expect(fatal(described.has_value())) << look->describe(*program);
        const auto root = described->root();
        expect(eq(root["materials"].size(), program->materials().size()) && eq(root["states"].size(), std::size_t{9}));
        expect(root["materials"][id("wood")]["color"].text() == "#8a5a2bff" && root["materials"][id("wood")]["from"].text() == "material");
        expect(root["states"][4]["name"].text() == "glows with heat" && root["states"][4]["unit"].text() == "K" && root["states"][4]["from"].number() == 800.0 &&
               root["states"][4]["to"]["lava"].number() == 1500.0);
        expect(root["states"][0]["to"]["wood"].number() == 400.0 && !root["states"][0]["to"]["stone"]) << "where a state does not show, it is not said";
    };

    "a material the pack has never heard of looks like its phase, and is charred, frosted and aglow like the rest"_test = [] {
        using P = matter::Property;
        auto definition = standard_definition();
        definition.materials.push_back({"alcohol", {P{"phase", "liquid"}, P{"density", 790.0}, P{"boiling", 351.0}}, {}});
        definition.materials.push_back({"peat", {P{"phase", "granular"}, P{"density", 500.0}, P{"ignition", 520.0}, P{"fuel", 900.0}, P{"heat", 60.0}, P{"burnt", "ash"}}, {}});
        const auto program = prepared(definition);
        const auto look = matter::standard_look(program);
        expect(fatal(look.has_value())) << (look ? "" : look.error().front().text());
        const auto alcohol = *program.material("alcohol"), peat = *program.material("peat");
        expect(!look->named(alcohol) && !look->named(peat));
        expect(eq(look->base()[alcohol], 0xc8d97f3bu)) << "the pack's liquid";
        expect(eq(look->base()[peat], 0xff77a3b9u) && eq(look->grain()[peat], std::uint8_t{41})) << "the pack's granular";
        expect(eq(state(*look, "charred").to[peat], 900u)) << "charred against its own fuel: no one wrote how peat burns";
        expect(eq(state(*look, "frosted").to[peat], kelvin(213)) && eq(state(*look, "glows with heat").to[alcohol], kelvin(1500)));
        const auto described = mlibs::json::Document::parse(look->describe(program), "look");
        expect(fatal(described.has_value()));
        expect(described->root()["materials"][peat]["from"].text() == "phase granular");
    };

    "a pack serves many worlds: what it colours and a world has not is unused, not wrong; no pack at all is the phases' own looks"_test = [] {
        matter::Definition definition = matter::Definition::standard();
        using P = matter::Property;
        definition.materials.push_back({"void", {P{"phase", "gas"}}, {}});
        definition.materials.push_back({"wood", {P{"fuel", 10.0}}, {}});
        const auto program = prepared(definition);
        const auto look = matter::standard_look(program);
        expect(fatal(look.has_value()));
        expect(eq(look->unused().size(), std::size_t{16})) << "the standard pack's other materials";
        expect(std::ranges::contains(look->unused(), "lava") && !std::ranges::contains(look->unused(), "wood"));
        expect(look->named(*program.material("wood")) && !look->named(*program.material("void")));
        expect(eq(state(*look, "charred").to[*program.material("wood")], 10u));

        const auto plain = matter::Look::prepare(program, {});
        expect(fatal(plain.has_value()));
        expect(plain->states().empty() && plain->unused().empty());
        expect(eq(plain->base()[*program.material("void")], 0x18e0d2c8u) && eq(plain->base()[*program.material("wood")], 0xff85807du));
    };

    "an appearance is refused where it is wrong: the code, the pointer, and what to write instead"_test = [] {
        const auto program = matter::standard();
        expect(fatal(program.has_value()));
        using Errc = matter::Errc;
        const matter::StateSpec good{.name = "hot", .by = "temperature", .from = 800, .to = 1500.0, .color = "#ff4a0c"};
        const auto refused = [&](const matter::Appearance& appearance, Errc code, std::string_view where, std::string_view part) {
            const auto look = matter::Look::prepare(*program, appearance);
            expect(!look.has_value()) << std::format("accepted: expected {} at {}", describe(code).name, where);
            if (look) return;
            expect(look.error().code == code && look.error().where == where && look.error().message.contains(part))
                << std::format("{} at {}: {} — expected {} at {} saying `{}`", describe(look.error().code).name, look.error().where, look.error().message,
                               describe(code).name, where, part);
        };
        const auto with_state = [&](auto change) {
            matter::Appearance appearance;
            appearance.states.push_back(good);
            change(appearance.states.back());
            return appearance;
        };
        expect(matter::Look::prepare(*program, with_state([](matter::StateSpec&) {})).has_value()) << "the state the others are changed from";

        refused({.phases = {{"plasma", "#ffffff", {}}}}, Errc::name, "/phases/plasma", "`plasma` is no phase");
        refused({.phases = {{"solid", "red", {}}}}, Errc::value, "/phases/solid/color", "`red` is no colour: \"#rrggbb\" or \"#rrggbbaa\"");
        refused({.phases = {{"solid", "#12345", {}}}}, Errc::value, "/phases/solid/color", "is no colour");
        refused({.phases = {{"solid", "#12345g", {}}}}, Errc::value, "/phases/solid/color", "is no colour");
        refused({.phases = {{"gas", "", -0.1}}}, Errc::value, "/phases/gas/grain", "a grain is 0 … 1");
        refused({.materials = {{"wood", "#8a5a2b", 2.0}}}, Errc::value, "/materials/wood/grain", "a grain is 0 … 1; this is 2");
        refused({.materials = {{"unobtainium", "blue", {}}}}, Errc::value, "/materials/unobtainium/color", "is no colour");  // wrong whether or not the world has it
        refused({.nothing = "none"}, Errc::value, "/nothing", "is no colour");

        refused(with_state([](matter::StateSpec& state) { state.by = ""; }), Errc::law, "/states/0", "a state reads a channel: `by`");
        refused(with_state([](matter::StateSpec& state) { state.by = "temprature"; }), Errc::name, "/states/0/by", "did you mean `temperature`?");
        refused(with_state([](matter::StateSpec& state) { state.by = "material"; }), Errc::names_material, "/states/0/by", "a material's own look is under `materials`");
        refused(with_state([](matter::StateSpec& state) { state.by = "wood"; }), Errc::name, "/states/0/by", "`wood` is no channel: a state reads what a thing carries");
        refused(with_state([](matter::StateSpec& state) { state.by = "fuel"; }), Errc::name, "/states/0/by", "`fuel` is no channel: a state reads what a thing carries");
        refused(with_state([](matter::StateSpec& state) { state.from = -5; }), Errc::value, "/states/0/from", "-5 is not a value `temperature` holds");
        refused(with_state([](matter::StateSpec& state) { state.to = 1e12; }), Errc::value, "/states/0/to", "is not a value `temperature` holds (0 … 16777215.99609375)");
        refused(with_state([](matter::StateSpec& state) { state.to = 800.0; }), Errc::value, "/states/0/to", "`from` and `to` are both 800");
        refused(with_state([](matter::StateSpec& state) { state.to = std::string("fule"); }), Errc::name, "/states/0/to", "did you mean `fuel`?");
        refused(with_state([](matter::StateSpec& state) { state.to = std::string("burnt"); }), Errc::value, "/states/0/to", "`burnt` is not a number");
        refused(with_state([](matter::StateSpec& state) { state.color = ""; }), Errc::value, "/states/0/color", "is no colour");
        refused(with_state([](matter::StateSpec& state) { state.mix = 1.5; }), Errc::value, "/states/0/mix", "a mix is 0 … 1; this is 1.5");
        refused(with_state([](matter::StateSpec& state) { state.of = {"solid", "plasma"}; }), Errc::name, "/states/0/of/1", "`plasma` is no phase");
    };

    "JSON problems are said where they are written; a problem of preparing is placed in the text too"_test = [] {
        const auto program = matter::standard();
        expect(fatal(program.has_value()));
        const std::string text = R"({"appearance": {
  "materials": {"wood": {"colour": "#8a5a2b"}, "ash": 3},
  "states": [
    {"by": "temperature", "to": 900, "color": "#ff0000", "glows": true},
    {"by": "burned", "color": "#000000"},
    {"by": "burned", "to": true, "color": "#000000", "emits": "yes", "of": "solid"}
  ],
  "sounds": {}
}})";
        const auto read = matter::read_appearance(text, "bad.appearance.omui.json");
        expect(fatal(!read.has_value()));
        std::string all;
        for (const auto& problem : read.error()) all += problem.text() + "\n";
        const auto says = [&](std::string_view part) { expect(all.contains(part)) << std::format("no `{}` in:\n{}", part, all); };
        says("bad.appearance.omui.json:2:");
        says("the look of `wood` has no key `colour`");
        says("did you mean `color`?");
        says("the look of `ash` is an object");
        says("a state has no key `glows`");
        says("a state shows along a range: `to` says where it is all there");
        says("`to` is a number in the channel's unit, or a property of the thing itself");
        says("`emits` is true or false");
        says("`of` is a list of phases");
        says("an appearance has no key `sounds`");
        expect(eq(read.error().size(), std::size_t{8})) << all;

        expect(!matter::read_appearance("{\"look\": {}}", "x.json").has_value());
        expect(!matter::read_appearance("{\"appearance\": [", "x.json").has_value());

        const std::string late = R"({"appearance": {
  "states": [
    {"by": "temperature", "to": 900, "color": "#ff0000"},
    {"by": "wetnes", "to": 10, "color": "#000000"}
  ]
}})";
        const auto look = matter::look_document(*program, late, "late.appearance.omui.json");
        expect(fatal(!look.has_value()));
        const auto& problem = look.error().front();
        expect(problem.at == "/appearance/states/1/by" && problem.text().contains("late.appearance.omui.json:4:") && problem.text().contains("did you mean `wetness`?"))
            << problem.at << " " << problem.text();

        const std::string good = R"({"appearance": {"nothing": "#102030", "phases": {"gas": {"color": "#00000000"}},
            "states": [{"name": "wet", "by": "wetness", "to": "moist", "color": "#001040", "mix": 0.5, "of": ["soft"]}]}})";
        const auto own = matter::look_document(*program, good, "own.appearance.omui.json");
        expect(fatal(own.has_value())) << (own ? "" : own.error().front().text());
        expect(eq(own->nothing(), 0xff302010u) && eq(own->states().size(), std::size_t{1}));
        expect(eq(own->states()[0].to[*program->material("jelly")], 32u) && eq(own->states()[0].to[*program->material("water")], 0u))
            << "against its own `moist`, and only on what is soft";
        expect(eq(own->states()[0].color, 0x80401000u));
    };
}};
