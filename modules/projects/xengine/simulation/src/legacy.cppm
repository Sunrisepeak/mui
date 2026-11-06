// Source compatibility without importing an unrelated optional parent engine.
export module openmui.simulation;
export import xengine.simulation.api;
export namespace omui::simulation {
using xengine::simulation::Clock;
using xengine::simulation::Model;
using xengine::simulation::Timeline;
}
