// openmui.core — the foundation of the openmui runtime.
//
//   :base      the spec's values (Value, Role, Effect …), the shared geometry
//              (openmui.geom), memory utilities (openmui.memory), anim's time
//   :diag      logging and invariants with source locations
//   :json      JSON text to Values (nlohmann.json) with located errors
//   :signal    signals, computed values, effects, batches
//   :verb      the application's named operations, with effect checks
//   :keys      keys, modifiers, chords
//   :event     input events and their JSON form
//   :task      the work-stealing pool and the queue to the UI thread
export module openmui.core;

export import :base;
export import :diag;
export import :json;
export import :signal;
export import :verb;
export import :keys;
export import :event;
export import :task;
