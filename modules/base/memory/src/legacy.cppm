// Source compatibility: the same types and implementation, within this package.
export module openmui.memory;
export import mlibs.memory.api;
export namespace omui {
using mlibs::memory::Arena;
using mlibs::memory::SlotKey;
using mlibs::memory::slot_generation_t;
using mlibs::memory::SlotStorageError;
using mlibs::memory::SlotStorage;
using mlibs::memory::Handle;
using mlibs::memory::SlotMap;
using mlibs::memory::FunctionRef;
}
