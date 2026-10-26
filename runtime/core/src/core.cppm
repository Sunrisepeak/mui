// openmui.core — the foundation of the openmui runtime.
//
//   :base      the spec's value types (Value, Rect, Role …) and anim's time
//   :diag      logging and invariants with source locations
//   :function  FunctionRef, a non-owning callable reference
//   :handle    generational handles and slot maps
//   :arena     the per-frame allocator
//   :json      JSON text to Values (nlohmann.json) with located errors
//   :signal    signals, computed values, effects, batches
//   :verb      the application's named operations, with effect checks
//   :keys      keys, modifiers, chords
//   :event     input events and their JSON form
//   :display   the display list (contract ⑤): paths, paints, layers, glyphs
//   :task      the work-stealing pool and the queue to the UI thread
export module openmui.core;

export import :base;
export import :diag;
export import :function;
export import :handle;
export import :arena;
export import :json;
export import :signal;
export import :verb;
export import :keys;
export import :event;
export import :display;
export import :task;
