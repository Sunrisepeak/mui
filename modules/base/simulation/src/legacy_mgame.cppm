// Source-compatible game-facing names for the shared simulation foundation.
// There is one clock/checkpoint implementation, owned by xengine.simulation.
export module mgame.sim;
import std;
import xengine.simulation.api;
export namespace mgame {
using Clock = xengine::simulation::Clock;
template<class G>
concept Game = std::is_trivially_copyable_v<typename G::State> && xengine::simulation::Model<G>;
template<Game G>
using Timeline = xengine::simulation::Timeline<G>;
}
