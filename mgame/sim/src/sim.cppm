// Source-compatible game-facing names for the shared simulation foundation.
// There is one clock/checkpoint implementation, owned by openmui.simulation.
export module mgame.sim;
import std;
import openmui.simulation;
export namespace mgame {
using Clock = omui::simulation::Clock;
template<class G>
concept Game = std::is_trivially_copyable_v<typename G::State> && omui::simulation::Model<G>;
template<Game G>
using Timeline = omui::simulation::Timeline<G>;
}
