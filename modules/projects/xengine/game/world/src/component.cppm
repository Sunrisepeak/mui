// xengine.world:component — what a component is: plain data whose bytes are its value (both forms of world).
export module xengine.world:component;

import std;

export namespace xengine {

// Plain data with no padding: its bytes are its value, so a checksum of them
// is the same for the same value. A type with doubles (whose representations
// are not unique to the standard: ±0) says so itself with
// `static constexpr bool no_padding = true;` — its author vouches for it.
template <class C>
concept Component = std::is_trivially_copyable_v<C> && std::is_default_constructible_v<C> &&
                    (std::has_unique_object_representations_v<C> || requires { requires C::no_padding; });

}  // namespace xengine
