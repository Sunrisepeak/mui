module omui.showcase.kiln.headless;

import std;
import openas;
import mlibs.agent;
import xengine.cellular.matter.agent;
import xengine.cellular.rigid.agent;

namespace kiln {

namespace {
// The standard definition lives as long as the program: shared without owning.
std::shared_ptr<const xengine::matter::Program> definition() { return {std::shared_ptr<void>(), &kiln::matter()}; }
}  // namespace

Causes::Causes(Standalone& game, openas::Actor player)
    : game_(&game), lattice_(*game.state().world), matter_(definition(), lattice_, {.seed = game.state().world->seed()}) {
    lattice_.by(player);
    game.tell([this](const State& state) { lattice_.stepped(*state.world); }, [this](const State& state) { lattice_.rewound(*state.world); });
}

Causes::~Causes() { game_->tell({}, {}); }

std::vector<mlibs::agent::Subject*> subjects(Standalone& game, Causes* causes) {
    std::vector<mlibs::agent::Subject*> out{&game};
    if (causes && game.tier().full_view) out.push_back(&causes->matter());
    return out;
}

}  // namespace kiln
