// gongfu — the fighting framework's core (D-85; structure-render-web-game §8).
//
//   :fixed   the numbers: Q16.16 fixed point, the same bits everywhere
//   :input   pads, the input log, motions, charges, commands, SOCD
//   :data    fighters, moves and the system's rules as data; compile() checks them
//   :state   a match at one frame as one block of plain data, and its hash
//   :match   the simulation: tick(p1, p2), rollback by state()/restore(), events
//
// The standard library only: what draws (2D now, 3D later), sounds, reads
// devices and talks over the network is outside, reading the state and the
// events. The same inputs give the same bytes on every machine, so a
// rollback, a replay and a verification are each a re-simulation.
export module gongfu;

export import :fixed;
export import :input;
export import :data;
export import :state;
export import :match;

export namespace gongfu {

inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;

}  // namespace gongfu
