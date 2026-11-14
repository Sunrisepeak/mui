// Packs laid over a definition (xengine.matter's `overlay`; the foundation plan's §6.15, D-182): a pack says only
// what it adds, and a world made of the standard definition with packs over it is the world a whole definition
// written by hand would make — the same bits. A pack adds and never replaces: every clash is said.
import std;
import xengine.matter.json;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;

namespace {

matter::Definition standard_definition() {
    auto read = matter::read(matter::standard_snapshot, "standard.matter.omui.json");
    expect(fatal(read.has_value()));
    return *read;
}
matter::Definition pack_of(std::string_view text) {
    auto read = matter::read_pack(text, "a.pack.matter.omui.json");
    expect(fatal(read.has_value())) << (read ? "" : read.error().front().text());
    return *read;
}
// Three fuels no law knows of: each says its properties and nothing else.
constexpr std::string_view fuels = R"({"matter": {"materials": {
    "coal": {"density": 1400, "thermal": 1, "ignition": 700, "fuel": 600, "heat": 120, "burnt": "ash"},
    "peat": {"density": 700, "thermal": 1, "porous": 6, "ignition": 520, "fuel": 250, "heat": 60, "burnt": "ash", "tags": ["plant"]},
    "tar": {"phase": "liquid", "density": 1100, "thermal": 1, "ignition": 540, "fuel": 500, "heat": 110, "burnt": "smoke"}}}})";
// A law the world did not have: a glow that passes through what conducts it and wakes what can be woken.
constexpr std::string_view glow = R"({"matter": {
    "columns": {"lucent": {"unit": "64ths a step", "doc": "how well it passes glow", "most": 16},
                "wakes_at": {"doc": "the glow at which it wakes", "most": 100000},
                "woken": {"kind": "material", "doc": "what it wakes into"}},
    "channels": {"glow": {"doc": "what a lamp gives"}},
    "materials": {"crystal": {"density": 2600, "lucent": 16, "wakes_at": 40, "woken": "lamp"},
                  "lamp": {"density": 2600, "lucent": 16},
                  "iron": {"lucent": 8}},
    "laws": [{"name": "glow passes", "transfer": "glow", "by": "lucent"},
             {"name": "wakes", "when": "wakes_at > 0 && glow >= wakes_at", "become": "woken", "event": "wake"}]}})";

// A row of instances, each beside the next: what a definition makes of it in `steps` steps.
std::vector<std::uint32_t> run(const matter::Program& program, std::span<const std::string_view> row, int steps, std::string_view heated = {}, double kelvin = 0, std::size_t at = 0) {
    const std::size_t instances = row.size();
    std::vector<matter::Pair> pairs;
    for (std::uint32_t index = 0; index + 1 < instances; ++index) pairs.push_back({index, index + 1, 1});
    std::vector<std::uint32_t> state = program.fresh(instances, *program.material("air")), next = state;
    for (std::size_t index = 0; index < instances; ++index) program.place(state, instances, index, *program.material(row[index]));
    if (!heated.empty()) {
        const std::uint32_t channel = *program.channel(heated);
        state[channel * instances + at] = static_cast<std::uint32_t>(kelvin * static_cast<double>(1u << program.channels()[channel].fraction));
    }
    matter::Workspace workspace;
    for (int step = 0; step < steps; ++step) {
        const auto stepped = program.step({instances, pairs}, state, next, workspace);
        expect(fatal(stepped.has_value())) << (stepped ? "" : stepped.error().where + ": " + stepped.error().message);
        state.swap(next);
    }
    return state;
}

}  // namespace

UTest cases{"matter.pack", [] {
    "a pack of three materials over the standard definition is the definition with three rows more: the same world, bit for bit"_test = [] {
        const matter::Definition pack = pack_of(fuels);
        expect(pack.columns.empty() && pack.channels.empty() && pack.laws.empty() && eq(pack.materials.size(), std::size_t{3})) << "a pack says only what it adds";
        // By hand: the whole definition, with the rows written at its end.
        matter::Definition whole = standard_definition();
        for (const matter::MaterialSpec& material : pack.materials) whole.materials.push_back(material);
        const auto laid = matter::overlay(standard_definition(), pack);
        expect(fatal(laid.has_value()));
        const auto by_hand = matter::Program::prepare(whole), by_pack = matter::Program::prepare(*laid);
        expect(fatal(by_hand.has_value() && by_pack.has_value()));
        expect(eq(by_pack->materials().size(), by_hand->materials().size()) && eq(by_pack->laws().size(), by_hand->laws().size()));
        const std::array<std::string_view, 9> row{"air", "wood", "coal", "air", "peat", "tar", "air", "water", "stone"};
        expect(run(*by_pack, row, 600, "temperature", 1200, 1) == run(*by_hand, row, 600, "temperature", 1200, 1));
        // And it takes part in laws that never heard of it: the coal beside the lit wood has burned.
        const auto with = matter::standard_with(std::array{pack});
        expect(fatal(with.has_value())) << (with ? "" : with.error());
        const auto after = run(*with, row, 3000, "temperature", 1200, 1);
        expect(after[2] == *with->material("ash") || after[*with->channel("burned") * row.size() + 2] > 0u) << "coal caught from the wood";
    };

    "a pack may bring a law the world did not have, its channel and columns, and say more of a material that is there"_test = [] {
        const auto program = matter::standard_with(std::array{pack_of(glow)});
        expect(fatal(program.has_value())) << (program ? "" : program.error());
        expect(program->channel("glow").has_value() && program->material("crystal").has_value());
        const auto lucent = std::ranges::find(program->columns(), "lucent", &matter::Column::name);
        expect(fatal(lucent != program->columns().end()));
        expect(eq(program->number(*program->material("iron"), static_cast<std::uint32_t>(lucent - program->columns().begin())), 8.0)) << "iron, said more of";
        expect(eq(program->recipes(), std::size_t{0})) << "and the new laws name no material";
        // A lamp's glow goes along iron to a crystal, which wakes; wood passes none, and the crystal behind it sleeps.
        const std::array<std::string_view, 7> row{"lamp", "iron", "iron", "crystal", "wood", "crystal", "air"};
        const auto after = run(*program, row, 4000, "glow", 4000);
        expect(after[3] == *program->material("lamp")) << "the crystal the glow reached woke";
        expect(after[5] == *program->material("crystal")) << "the one behind the wood did not";
    };

    "a pack adds and never replaces: a law, a column, a channel of a name that is there, a material said otherwise — each clash said"_test = [] {
        const matter::Definition bad = pack_of(R"({"matter": {
            "columns": {"density": {"most": 10}},
            "channels": {"temperature": {}},
            "materials": {"wood": {"ignition": 300}, "iron": {"density": 7800}},
            "laws": [{"name": "burns", "when": "temperature > 0", "add": {"burned": "1"}}]}})");
        const auto laid = matter::overlay(standard_definition(), bad);
        expect(fatal(!laid.has_value()));
        std::vector<std::string> said;
        for (const matter::Clash& clash : laid.error()) said.push_back(clash.part + " " + clash.name);
        expect(said == std::vector<std::string>{"column density", "channel temperature", "law burns", "material wood"}) << matter::said(laid.error());
        expect(matter::said(laid.error()).contains("`wood` already says its `ignition`: a pack may say more of a material, not otherwise"));
        // Iron's density said again as it is: no clash. Two packs with the same new material: the second clashes.
        const matter::Definition coal = pack_of(fuels);
        const auto twice = matter::standard_with(std::array{coal, coal});
        expect(twice.has_value()) << "the same rows said twice the same way are not said otherwise";
        const matter::Definition other = pack_of(R"({"matter": {"materials": {"coal": {"density": 900}}}})");
        const auto clashed = matter::standard_with(std::array{coal, other});
        expect(!clashed.has_value() && clashed.error().contains("`coal` already says its `density`")) << (clashed ? "" : clashed.error());
    };
}};
