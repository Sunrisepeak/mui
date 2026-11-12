// xengine.matter on the preparation contract (mlibs.cts:prepare): a definition prepares to the same plan
// twice and steps a world to the same state; a malformed one is refused as invalid-argument, one beyond
// its limits as refused.
import std;
import xengine.matter;
import mlibs.cts;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace cts = mlibs::cts;

namespace {
struct Definitions {
    struct Description {
        matter::Definition definition;
        matter::Limits     limits;
    };
    auto prepare(const Description& description) { return matter::Program::prepare(description.definition, description.limits); }
    std::string fingerprint(const matter::Program& program) {
        mlibs::hash::Fnv1a hash;
        for (const char letter : program.describe()) hash.le(static_cast<std::uint8_t>(letter), 1);
        return std::format("{} materials, {} laws, {:016x}", program.materials().size(), program.laws().size(), hash.value());
    }
    std::string state() { return "pure"; }  // preparation holds nothing of the caller's
    // Forty steps of a row of nine of its first material, the middle one hot: every value hashed.
    std::string execute(const matter::Program& program) noexcept {
        if (program.materials().empty()) return "no material";
        std::vector<matter::Pair> pairs;
        for (std::uint32_t index = 0; index + 1 < 9; ++index) pairs.push_back({index, index + 1, 1});
        std::vector<std::uint32_t> state = program.fresh(9, 0), next(state.size());
        if (const auto temperature = program.channel("temperature")) state[*temperature * 9 + 4] = 900 * 256;
        matter::Workspace workspace;
        for (int step = 0; step < 40; ++step) {
            if (!program.step({9, pairs}, state, next, workspace)) return "refused";
            state.swap(next);
        }
        mlibs::hash::Fnv1a hash;
        for (const std::uint32_t value : state) hash.le(value, 4);
        return std::format("{:016x}", hash.value());
    }
};

using P = matter::Property;
matter::Definition with(std::vector<matter::MaterialSpec> materials, std::vector<matter::LawSpec> laws) {
    matter::Definition out = matter::Definition::standard();
    out.materials = std::move(materials);
    out.laws = std::move(laws);
    return out;
}
}  // namespace

UTest cases{"matter.cts.prepare", [] {
    "definitions prepare the same way twice; malformed ones and ones beyond the limits are refused by kind"_test = [] {
        const std::vector<matter::MaterialSpec> things{{"wood", {P{"ignition", 570.0}, P{"fuel", 400.0}, P{"heat", 80.0}, P{"oxygen", 1.0}, P{"burnt", "ash"}}, {}}, {"ash", {}, {}}};
        const std::vector<matter::LawSpec> burning{{.transfer = "temperature", .by = "thermal"},
                                                   {.when = "temperature >= ignition && burned < fuel && near.oxygen > 0", .add = {{"burned", "burn_rate"}, {"temperature", "heat"}}, .flag = "burning"},
                                                   {.when = "fuel > 0 && burned >= fuel", .become = "burnt", .event = "burn_out"}};
        Definitions definitions;
        cts::against({.subject = "reference"}, [&] {
            cts::prepare::declare(definitions, "xengine.matter@1",
                                  {.valid = {{"burning", {with(things, burning), {}}}, {"columns-only", {matter::Definition::standard(), {}}}, {"no-laws", {with(things, {}), {}}}},
                                   .invalid = {{"names-a-material", {with(things, {{.when = "material == wood", .add = {{"temperature", "1"}}}}), {}}},
                                               {"unknown-property", {with({{"mud", {P{"stickiness", 1.0}}, {}}}, {}), {}}},
                                               {"value-out-of-range", {with({{"mud", {P{"thermal", 99.0}}, {}}}, {}), {}}},
                                               {"law-of-two-kinds", {with(things, {{.transfer = "charge", .by = "conduct", .flag = "burning"}}), {}}},
                                               {"malformed-condition", {with(things, {{.when = "temperature >=", .add = {{"temperature", "1"}}}}), {}}},
                                               {"two-flows-of-one-quantity", {with(things, {{.transfer = "charge", .by = "conduct"}, {.transfer = "charge", .by = "thermal"}}), {}}}},
                                   .over_budget = {{"laws", {with(things, burning), {.max_laws = 2}}}, {"materials", {with(things, {}), {.max_materials = 1}}}, {"terms", {with(things, {{.add = {{"temperature", "heat + heat + heat"}}}}), {.max_terms = 2}}}}});
        });
    };
}};
